#include "mytcpserver.h"
#include "mytcpsocket.h"
#include "operatedb.h"
#include "protocol.h"

#include <QDebug>
#include <cstring>

MyTcpSocket::MyTcpSocket()
{
    m_pmh = new MsgHandler;
    connect(this, &QTcpSocket::readyRead, this, &MyTcpSocket::recvMsg);
    connect(this, &QTcpSocket::disconnected, this, &MyTcpSocket::clineOffliine);
}

MyTcpSocket::~MyTcpSocket()
{
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
    // 未登录的连接没有登录名，不能拿空串去置 online=0
    if (!m_strLoginName.isEmpty())
    {
        operateDB::getInstance().handleOffline(m_strLoginName.toStdString().c_str());
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
