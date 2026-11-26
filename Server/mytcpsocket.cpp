#include "mytcpserver.h"
#include "mytcpsocket.h"
#include "operatedb.h"
#include "protocol.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <cstring>

MyTcpSocket::MyTcpSocket()
{
    m_pmh = new MsgHandler;
    connect(this, &QTcpSocket::readyRead, this, &MyTcpSocket::recvMsg);
    connect(this, &QTcpSocket::disconnected, this, &MyTcpSocket::clineOffliine);
    // 写缓冲腾空后接着推下一批分片：没有这个信号，要么一次全写进内存，
    // 要么就靠 sleep 轮询，两种都不对
    connect(this, &QTcpSocket::bytesWritten, this, &MyTcpSocket::pumpDownload);
}

MyTcpSocket::~MyTcpSocket()
{
    stopDownload();
    delete m_pmh;
}

// 收包状态机：处理半包(缓存等待)、粘包(循环切分)、非法长度(断开)
void MyTcpSocket::recvMsg()
{
    m_lastActiveMs = QDateTime::currentMSecsSinceEpoch();
    m_recvBuf.append(this->readAll());

    while (true)
    {
        // 1) 至少要够读到 4 字节的总长字段，才可能切出一个包
        if (m_recvBuf.size() < int(sizeof(uint)))
            break;

        uint uiPDULen = 0;
        memcpy(&uiPDULen, m_recvBuf.constData(), sizeof(uint));

        // 2) 长度合法性校验：防下溢(uiPDULen < 76 时减法会溢出成约 42 亿)、防巨量分配
        if (uiPDULen < PDU_HEADER_LEN || uiPDULen > PDU_MAX_TOTAL_LEN)
        {
            qWarning() << "非法包长:" << uiPDULen << "，断开连接";
            m_recvBuf.clear();
            this->disconnectFromHost();
            return;
        }

        // 3) 半包：还没收齐，保留在缓冲区，等下一次 readyRead
        if (m_recvBuf.size() < int(uiPDULen))
            break;

        // 4) 切出一个完整 PDU（mkPDU 内部会按 uiMsgLen 补足总长并清零）
        PDU* pdu = mkPDU(uiPDULen - PDU_HEADER_LEN);
        memcpy(pdu, m_recvBuf.constData(), uiPDULen);
        m_recvBuf.remove(0, int(uiPDULen));

        // 5) 业务处理（handleMsg 内部会 free(pdu)），再回包（sendMsg 内部也会 free）
        PDU* respdu = handleMsg(pdu);
        sendMsg(respdu);
        // 6) 下载握手应答已经写出，此时才开始推文件数据，
        //    否则首个分片可能比应答先到，客户端拿到无主的数据包
        tryStartDownload();
    }
}

void MyTcpSocket::sendMsg(PDU *pdu)
{
    if(pdu==NULL)
    {
        return ;
    }
    this->write((char*)pdu,pdu->uiTotalLen);
    qDebug()<<"send Msg pdu->uiTotalLen"<<pdu->uiTotalLen
            <<"pdu->uiMsgLen"<<pdu->uiMsgLen
            <<"pdu->uiType"<<pdu->uiType
            <<"pdu->caData"<<pdu->caData
            <<"pdu->caData+32"<<pdu->caData+32
            <<"pdu->caMsg"<<pdu->caMsg;
    free(pdu);
    pdu=NULL;

}

void MyTcpSocket::clineOffliine()
{
    // 连接断了，正在推的下载立刻停掉并关闭文件句柄
    stopDownload();

    // 未登录的连接没有登录名，不能拿空串去置 online=0
    if (!m_strLoginName.isEmpty())
    {
        // 关键：只有当该账号确实没有其它有效在线连接时才置 offline。
        // 否则"重复登录互踢"会出问题：新连接登录时已把 online 置 1，
        // 紧接着旧连接被 disconnectFromHost 触发本函数，会把新连接的状态覆盖回 0。
        if (MyTcpServer::getInstance().hasOtherActiveSession(m_strLoginName, this))
        {
            qDebug()<<"账号"<<m_strLoginName<<"仍有其它在线连接，保持 online=1";
        }
        else
        {
            operateDB::getInstance().handleOffline(m_strLoginName.toStdString().c_str());
        }
    }
    MyTcpServer::getInstance().removeSocket(this);
}

PDU *MyTcpSocket::handleMsg(PDU *pdu)
{
    qDebug()<<"send Msg pdu->uiTotalLen"<<pdu->uiTotalLen
            <<"pdu->uiMsgLen"<<pdu->uiMsgLen
            <<"pdu->uiType"<<pdu->uiType
            <<"pdu->caData"<<pdu->caData
            <<"pdu->caData+32"<<pdu->caData+32
            <<"pdu->caMsg"<<pdu->caMsg;
    PDU* respdu=NULL;
    m_pmh->pdu=pdu;
    // 每次分发前把当前登录名同步给业务层，供网盘路径越权校验使用
    m_pmh->m_loginName = m_strLoginName;

    // 身份校验（第一道）：除注册、登录、心跳外，其余请求都必须是已登录连接。
    // 否则未登录的 TCP 连接可以直接调用网盘操作、查好友列表等业务。
    if (m_strLoginName.isEmpty()
        && pdu->uiType != ENUM_TYPE_REGIST_REQUEST
        && pdu->uiType != ENUM_TYPE_LOGIN_REQUEST
        && pdu->uiType != ENUM_TYPE_HEARTBEAT_REQUEST)
    {
        qWarning() << "未登录连接尝试调用业务接口, uiType=" << pdu->uiType << "，已拒绝";
        free(pdu);
        return NULL;
    }

    switch(pdu->uiType)
    {
    case ENUM_TYPE_REGIST_REQUEST:
    {
      respdu=m_pmh->regist();
        break;
    }
    case ENUM_TYPE_LOGIN_REQUEST:
    {
      respdu=m_pmh->login(m_strLoginName);
      // 登录成功则踢掉该账号的其它在线连接（except=this，避免把自己踢掉）
      bool ok = (respdu != NULL) && (*(bool*)respdu->caData);
      if (ok)
      {
          MyTcpServer::getInstance().kickDuplicate(m_strLoginName, this);

          // 离线消息必须排在"登录成功应答"之后下发。
          // 此刻应答还没发出去（它在 recvMsg 里 sendMsg），所以排队到下一轮事件循环，
          // 保证客户端先收到 LOGIN_RESPOND 完成跳转、再收到补发的消息。
          QPointer<MyTcpSocket> guard(this);
          QMetaObject::invokeMethod(this, [guard]() {
              if (guard)
              {
                  guard->pushOfflineMessages();
              }
          }, Qt::QueuedConnection);
      }
      break;
    }
    case ENUM_TYPE_HEARTBEAT_REQUEST:
    {
        // 心跳只回包，不查库
        respdu = mkPDU();
        respdu->uiType = ENUM_TYPE_HEARTBEAT_RESPOND;
        break;
    }
    case ENUM_TYPE_FIND_USER_REQUEST:
    {
        respdu=m_pmh->findUser();
        break;
    }
    case ENUM_TYPE_ONLINEUSER_REQUEST:
    {
        respdu=m_pmh->onlineUser();
        qDebug()<<"m_strLoginName"<<m_strLoginName;
        break;
    }
    case ENUM_TYPE_ADD_FRIEND_REQUEST:
    {
        respdu=m_pmh->addFriend();
        break;
    }
    case ENUM_TYPE_ADD_FRIEND_AGREE_REQUEST:
    {
        respdu=m_pmh->addFriendAgree();
        break;
    }
    case ENUM_TYPE_FLUSH_FRIEND_REQUEST:
    {
        respdu=m_pmh->flushFriend();
        break;
    }
    case ENUM_TYPE_DEL_FRIEND_REQUEST:
    {
       respdu=m_pmh->delFriend();
    break;
    }
    case ENUM_TYPE_CHAT_REQUEST:
    {
        respdu=m_pmh->chat();
        break;
    }
    case ENUM_TYPE_MKDIR_REQUEST:
    {
        respdu=m_pmh->mkdir();
        break;
    }
    case ENUM_TYPE_FLUSH_FILE_REQUEST:
    {
        respdu=m_pmh->flushFile();
        break;
    }
    case ENUM_TYPE_DEL_DIR_REQUEST:
    {
        respdu=m_pmh->delDir();
        break;
    }
    case ENUM_TYPE_RENAME_FILE_REQUEST:
    {
        respdu=m_pmh->renameFile();
        break;
    }
    case ENUM_TYPE_MOVE_FILE_REQUEST:
    {
        respdu=m_pmh->moveFile();
        break;
    }
    case ENUM_TYPE_UPLOAD_FILE_REQUEST:
    {
        respdu=m_pmh->uploadFile();
        break;
    }
    case ENUM_TYPE_UPLOAD_FILE_CHUNK_REQUEST:
    {
        // 分片成功时没有应答包，返回 NULL
        respdu=m_pmh->uploadChunk();
        break;
    }
    case ENUM_TYPE_UPLOAD_FILE_FINISH_REQUEST:
    {
        respdu=m_pmh->uploadFinish();
        break;
    }
    case ENUM_TYPE_DOWNLOAD_FILE_REQUEST:
    {
        respdu=m_pmh->downloadFile();
        break;
    }
    case ENUM_TYPE_SHARE_FILE_REQUEST:
    {
        respdu=m_pmh->shareFile();
        break;
    }
    default:
        break;
    }

    free(pdu);
    pdu=NULL;
    return respdu;
}

void MyTcpSocket::pushOfflineMessages()
{
    if (m_strLoginName.isEmpty())
    {
        return;
    }

    const QList<operateDB::OfflineMsg> msgs =
            operateDB::getInstance().handleTakeOfflineMsgs(m_strLoginName);
    if (msgs.isEmpty())
    {
        return;
    }

    const QByteArray me = m_strLoginName.toUtf8();
    for (const operateDB::OfflineMsg& msg : msgs)
    {
        const QByteArray content = msg.strContent.toUtf8();
        const QByteArray sender  = msg.strSender.toUtf8();

        PDU* pdu = mkPDU(uint(content.size()));
        pdu->uiType = ENUM_TYPE_OFFLINE_MSG_RESEND;
        // caData 两个定长槽位，按实际长度拷贝，剩余部分由 mkPDU 归零
        memcpy(pdu->caData, sender.constData(), size_t(qMin(sender.size(), 32)));
        memcpy(pdu->caData + 32, me.constData(), size_t(qMin(me.size(), 32)));
        memcpy(pdu->caMsg, content.constData(), size_t(content.size()));

        this->write((char*)pdu, pdu->uiTotalLen);
        free(pdu);
    }

    qDebug() << "已补发离线消息" << msgs.size() << "条 ->" << m_strLoginName;
}

// ---------------------------------------------------------------
// 第 7 项：下载推流
// 握手应答发出去之后才开始推；每次只写一个分片，写完看写缓冲水位，
// 高水位以上就停下等 bytesWritten，避免把整个文件读进内存或堆满写缓冲。
// ---------------------------------------------------------------
void MyTcpSocket::tryStartDownload()
{
    if (!m_pmh || !m_pmh->hasPendingDownload())
    {
        return;
    }

    const QString strPath = m_pmh->takeDownloadPath();
    const qint64 llStart  = m_pmh->takeDownloadOffset();

    stopDownload();   // 上一条没推完就来了新请求，作废

    m_downloadFile.reset(new QFile(strPath));
    if (!m_downloadFile->open(QIODevice::ReadOnly))
    {
        qWarning() << "下载打开文件失败:" << strPath;
        return;
    }
    m_downloadHash.reset(new QCryptographicHash(QCryptographicHash::Md5));
    m_downloadSize   = m_downloadFile->size();
    m_downloadOffset = llStart;

    // 续传：先把跳过的前缀读一遍喂进哈希，否则算不出整文件 MD5
    if (llStart > 0 && m_downloadFile->seek(0))
    {
        qint64 llRemain = llStart;
        while (llRemain > 0)
        {
            const QByteArray block = m_downloadFile->read(qMin<qint64>(FILE_CHUNK_SIZE, llRemain));
            if (block.isEmpty())
            {
                break;
            }
            m_downloadHash->addData(block);
            llRemain -= block.size();
        }
    }
    m_downloadFile->seek(m_downloadOffset);

    qDebug() << "开始推送文件" << strPath << "从" << llStart << "起，总长" << m_downloadSize;
    pumpDownload();
}

void MyTcpSocket::pumpDownload()
{
    if (!m_downloadFile || !m_downloadFile->isOpen())
    {
        return;
    }

    // 写缓冲水位以上就先不写：等 bytesWritten 回来继续
    while (bytesToWrite() < SOCKET_WRITE_HIGH_WATER)
    {
        if (m_downloadOffset >= m_downloadSize)
        {
            // 收尾：整文件 MD5 一起下发，客户端据此判断拼出来的文件对不对
            const QByteArray md5Raw = m_downloadHash->result();
            PDU* respdu = mkPDU();
            respdu->uiType = ENUM_TYPE_DOWNLOAD_FILE_FINISH_RESPOND;
            bool ok = true;
            memcpy(respdu->caData + DL_RESP_OK_POS, &ok, sizeof(bool));
            memcpy(respdu->caData + DL_FINISH_MD5_POS, md5Raw.constData(), size_t(md5Raw.size()));
            this->write((char*)respdu, respdu->uiTotalLen);
            free(respdu);

            qDebug() << "推送完成" << m_downloadFile->fileName() << "MD5" << md5Raw.toHex();
            stopDownload();
            return;
        }

        const qint64 llRemain = m_downloadSize - m_downloadOffset;
        const QByteArray block = m_downloadFile->read(qMin<qint64>(FILE_CHUNK_SIZE, llRemain));
        if (block.isEmpty())
        {
            qWarning() << "读取文件意外中断:" << m_downloadFile->fileName();
            stopDownload();
            return;
        }

        PDU* pdu = mkPDU(block.size());
        pdu->uiType = ENUM_TYPE_DOWNLOAD_FILE_CHUNK_RESPOND;
        memcpy(pdu->caData + CHUNK_OFFSET_POS, &m_downloadOffset, sizeof(qint64));
        memcpy(pdu->caMsg, block.constData(), size_t(block.size()));
        this->write((char*)pdu, pdu->uiTotalLen);
        free(pdu);

        m_downloadHash->addData(block);
        m_downloadOffset += block.size();
    }
}

void MyTcpSocket::stopDownload()
{
    if (m_downloadFile)
    {
        if (m_downloadFile->isOpen())
        {
            m_downloadFile->close();
        }
        m_downloadFile.reset();
    }
    m_downloadHash.reset();
    m_downloadOffset = 0;
    m_downloadSize   = 0;
}
