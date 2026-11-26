#include "client.h"
#include "index.h"
#include "protocol.h"
#include "ui_client.h"

#include <QFile>
#include<QDebug>
#include <QHostAddress>
#include <QMessageBox>
#include <cstring>

namespace {
const int kHeartbeatIntervalMs = 15000;   // 15 秒发一次心跳，小于服务端 35 秒超时
}

Client::Client(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Client)
{
    ui->setupUi(this);
    loadConfig();
    m_strRootPath = "./userdata";
    m_socket.connectToHost(QHostAddress(m_strIP),m_usPort);

    connect(&m_socket,&QTcpSocket::connected,this,&Client::showConnect);
    connect(&m_socket,&QTcpSocket::readyRead,this,&Client::recvMsg);
    connect(&m_socket,&QTcpSocket::disconnected,this,&Client::onSocketDisconnected);

    // 第 7 项：上传靠"写缓冲低于水位线"驱动，服务端同理
    connect(&m_socket, &QTcpSocket::bytesWritten, this, &Client::pumpUpload);

    // 心跳：定时发 HEARTBEAT_REQUEST，让服务端知道本连接仍然存活
    m_heartbeatTimer.setInterval(kHeartbeatIntervalMs);
    connect(&m_heartbeatTimer, &QTimer::timeout, this, [this]() {
        PDU* pdu = mkPDU();
        pdu->uiType = ENUM_TYPE_HEARTBEAT_REQUEST;
        sendMsg(pdu);          // sendMsg 内部负责 write + free
    });

    m_prh = new ResHandler();
}
Client &Client::getInstance()
{

    static Client instance;
    return instance;
}
Client::~Client()
{
    delete ui;
    delete m_prh;
}

void Client::loadConfig()
{
    QFile file(":/new/prefix1/connect.config.txt");
    if(!file.open(QIODevice::ReadOnly))
    {
        qDebug() <<"打开文件失败";
        return;
    }
    // 逐行读取，不硬编码 "\r\n"：配置文件若为 LF 换行，split("\r\n") 会取不到端口
    const QByteArray baData = file.readAll();
    file.close();

    QStringList strList;
    for (const QByteArray& line : baData.split('\n'))
    {
        const QString one = QString::fromUtf8(line).trimmed();
        if (!one.isEmpty())
        {
            strList.append(one);
        }
    }

    if (strList.size() < 2)
    {
        qWarning() << "配置文件格式非法，期望两行(IP/端口)，实际行数:" << strList.size();
        return;
    }

    m_strIP  = strList.at(0);
    m_usPort = strList.at(1).toUShort();
    if (m_usPort == 0)
    {
        qWarning() << "端口解析失败，原始值:" << strList.at(1);
    }
    qDebug()<<"ip"<<m_strIP<<"port"<<m_usPort;
}
void Client::showConnect()
{
    qDebug()<<"链接成功";
    m_heartbeatTimer.start();
}

void Client::onSocketDisconnected()
{
    m_heartbeatTimer.stop();
    m_recvBuf.clear();
    qDebug()<<"与服务端断开连接";
}

// 收包状态机：处理半包(缓存等待)、粘包(循环切分)、非法长度(断开)
void Client::recvMsg()
{
    m_recvBuf.append(m_socket.readAll());

    while (true)
    {
        if (m_recvBuf.size() < int(sizeof(uint)))
            break;

        uint uiPDULen = 0;
        memcpy(&uiPDULen, m_recvBuf.constData(), sizeof(uint));

        if (uiPDULen < PDU_HEADER_LEN || uiPDULen > PDU_MAX_TOTAL_LEN)
        {
            qWarning() << "客户端收到非法包长:" << uiPDULen << "，断开连接";
            m_recvBuf.clear();
            m_socket.disconnectFromHost();
            return;
        }

        if (m_recvBuf.size() < int(uiPDULen))
            break;

        PDU* pdu = mkPDU(uiPDULen - PDU_HEADER_LEN);
        memcpy(pdu, m_recvBuf.constData(), uiPDULen);
        m_recvBuf.remove(0, int(uiPDULen));

        handleMsg(pdu);        // 内部会 free(pdu)
    }
}

void Client::sendMsg(PDU *pdu)
{
    m_socket.write((char*)pdu,pdu->uiTotalLen);
    qDebug()<<"sendMsg pdu->uiTotalLen"<<pdu->uiTotalLen
            <<"pdu->uiMsgLen"<<pdu->uiMsgLen
            <<"pdu->uiType"<<pdu->uiType
            <<"pdu->caData"<<pdu->caData
           <<"pdu->caData+32"<<pdu->caData+32
            <<"pdu->caMsg"<<pdu->caMsg;
    free(pdu);
    pdu=NULL;
}

void Client::handleMsg(PDU *pdu)
{
    qDebug()<<"recvMsg pdu->uiTotalLen"<<pdu->uiTotalLen
           <<"pdu->uiMsgLen"<<pdu->uiMsgLen
          <<"pdu->uiType"<<pdu->uiType
         <<"pdu->caData"<<pdu->caData
        <<"pdu->caDAta+32"<<pdu->caData+32
        <<"pdu->caMsg"<<pdu->caMsg;
    m_prh->pdu=pdu;
    switch(pdu->uiType)
    {
    case ENUM_TYPE_HEARTBEAT_RESPOND:
    {
        // 心跳应答，无需处理
        break;
    }
    case ENUM_TYPE_KICKED_RESEND:
    {
        // 账号在别处登录，被服务端踢下线
        m_heartbeatTimer.stop();
        QMessageBox::warning(&Client::getInstance(), "下线通知",
                             "您的账号已在其他地方登录，当前连接已断开");
        m_socket.disconnectFromHost();
        break;
    }
    case ENUM_TYPE_REGIST_RESPOND:
    {
        m_prh->regist();
       break;
    }
    case ENUM_TYPE_LOGIN_RESPOND:
    {
       m_prh->login();
       break;
    }
    case ENUM_TYPE_FIND_USER_RESPOND:
    {
        m_prh->findUser();
       break;
    }
    case ENUM_TYPE_ONLINEUSER_RESPOND:
    {
        m_prh->onlineUser();
         break;
    }
    case ENUM_TYPE_ADD_FRIEND_RESPOND:
    {
        m_prh->addFriend();
     break;
    }
     case ENUM_TYPE_ADD_FRIEND_RESEND:
    {
        m_prh->addFriendResend();
        break;
    }
    case ENUM_TYPE_ADD_FRIEND_AGREE_RESPOND:
    {
        m_prh->addFriendAgree();
     break;
    }
    case ENUM_TYPE_FLUSH_FRIEND_RESPOND:
    {
        m_prh->flushFriend();
    break;
    }
    case ENUM_TYPE_DEL_FRIEND_RESPOND:
    {
        m_prh->delFriend();
        break;
    }
    case ENUM_TYPE_CHAT_RESPOND:
    {
        m_prh->chat();
        break;
    }
    case ENUM_TYPE_CHAT_RESEND:
    {
        m_prh->chat();
        break;
    }
    case ENUM_TYPE_OFFLINE_MSG_RESEND:
    {
        // 登录成功后服务端补发的离线消息
        m_prh->offlineMsg();
        break;
    }
    case ENUM_TYPE_MKDIR_RESPOND:
    {
        m_prh->mkdir();
        break;
    }
    case ENUM_TYPE_FLUSH_FILE_RESPOND:
    {
        m_prh->flushFile();
        break;
    }
    case ENUM_TYPE_DEL_DIR_RESPOND:
    {
        m_prh->delDir();
        break;
    }
    case ENUM_TYPE_RENAME_FILE_RESPOND:
    {
        m_prh->renameFile();
        break;
    }
    case ENUM_TYPE_MOVE_FILE_RESPOND:
    {
        m_prh->moveFile();
        break;
    }
    case ENUM_TYPE_UPLOAD_FILE_RESPOND:
    {
        m_prh->uploadFile();
        break;
    }
    case ENUM_TYPE_UPLOAD_FILE_FINISH_RESPOND:
    {
        bool ret = false;
        memcpy(&ret, pdu->caData, sizeof(bool));
        qDebug() << "upload finish ret" << ret;
        if (ret)
        {
            QMessageBox::information(this, "提示", "上传成功");
            Index::getInstance().getFile()->flushFile();
        }
        else
        {
            QMessageBox::information(this, "提示", "上传失败：文件校验不通过（长度或 MD5 不一致）");
        }
        break;
    }
    case ENUM_TYPE_DOWNLOAD_FILE_RESPOND:
    {
        m_prh->downloadFile();
        break;
    }
    case ENUM_TYPE_DOWNLOAD_FILE_CHUNK_RESPOND:
    {
        m_prh->downloadChunk();
        break;
    }
    case ENUM_TYPE_DOWNLOAD_FILE_FINISH_RESPOND:
    {
        m_prh->downloadFinish();
        break;
    }
    case ENUM_TYPE_SHARE_FILE_RESPOND:
    {
        m_prh->shareFile();
        break;
    }
    default:
        break;

    }

    free(pdu);
    pdu=NULL;
}

/*void Client::on_sendPB_clicked()
{
    QString strMsg =ui->inputEdit->text();
    PDU*pdu=mkPDU(strMsg.toStdString().size());
    pdu->uiType=ENUM_TYPE_REGIST_REQUEST;
    memcpy(pdu->caMsg,strMsg.toStdString().c_str(),strMsg.toStdString().size());
    m_socket.write((char*)pdu,pdu->uiTotalLen);
    qDebug()<<"send msg pdu->uiTotalLen"<<pdu->uiTotalLen
            <<"pdu->uiMsgLen"<<pdu->uiMsgLen
            <<"pdu->uiType"<<pdu->uiType
            <<"pdu->caData"<<pdu->caData
            <<"pdu->caMsg"<<pdu->caMsg;
    free(pdu);
    pdu=NULL;

}*/

void Client::on_login_PB_clicked()
{
   QString strName = ui->name_LE->text();
   QString strPwd = ui->pwd_LE->text();
   if(strName.isEmpty()||strPwd.isEmpty()||strName.toStdString().size()>32||strPwd.toStdString().size()>32)
   {
       QMessageBox::critical(this,"注册","用户名或密码长度非法");
       return;
   }
   PDU *pdu=mkPDU();
   memcpy(pdu->caData,strName.toStdString().c_str(),32);
   memcpy(pdu->caData+32,strPwd.toStdString().c_str(),32);
   pdu->uiType=ENUM_TYPE_REGIST_REQUEST;
   m_socket.write((char*)pdu,pdu->uiTotalLen);
   qDebug()<<"recvMsg pdu->uiTotalLen"<<pdu->uiTotalLen
           <<"pdu->uiMsgLen"<<pdu->uiMsgLen
           <<"pdu->uiType"<<pdu->uiType
           <<"pdu->caData"<<pdu->caData
             <<"pdu->caData+32"<<pdu->caData+32
           <<"pdu->caMsg"<<pdu->caMsg;
   free(pdu);
   pdu=NULL;
}

void Client::on_pushButton_2_clicked()
{
    QString strName = ui->name_LE->text();
    QString strPwd = ui->pwd_LE->text();
    if(strName.isEmpty()||strPwd.isEmpty()||strName.toStdString().size()>32||strPwd.toStdString().size()>32)
    {
        QMessageBox::critical(this,"登录","用户名或密码长度非法");
        return;
    }
    m_strLoginName=strName;
    PDU *pdu=mkPDU();
    memcpy(pdu->caData,strName.toStdString().c_str(),32);
    memcpy(pdu->caData+32,strPwd.toStdString().c_str(),32);
    pdu->uiType=ENUM_TYPE_LOGIN_REQUEST;
    m_socket.write((char*)pdu,pdu->uiTotalLen);
    qDebug()<<"recvMsg pdu->uiTotalLen"<<pdu->uiTotalLen
            <<"pdu->uiMsgLen"<<pdu->uiMsgLen
            <<"pdu->uiType"<<pdu->uiType
            <<"pdu->caData"<<pdu->caData
              <<"pdu->caData+32"<<pdu->caData+32
            <<"pdu->caMsg"<<pdu->caMsg;
    free(pdu);
    pdu=NULL;
}

// ---------------------------------------------------------------
// 第 7 项：分片上传状态机
// 流程：File 页选好文件算完 MD5 → beginUpload 接管 QFile 句柄 → File 页发握手
//      → UPLOAD_FILE_RESPOND 带回断点 → continueUpload → pumpUpload 持续推分片
//      → 全部推完发 UPLOAD_FILE_FINISH_REQUEST（带整文件 MD5 十六进制）
// ---------------------------------------------------------------
void Client::beginUpload(QFile* pFile, qint64 llTotal, const QByteArray& md5Hex)
{
    abortUpload();
    m_uploadFile.reset(pFile);
    m_uploadTotal  = llTotal;
    m_uploadOffset = 0;
    m_uploadMd5Hex = md5Hex;
}

void Client::continueUpload(qint64 llResume)
{
    if (!m_uploadFile || !m_uploadFile->isOpen())
    {
        qWarning() << "continueUpload：没有待上传的文件";
        return;
    }
    if (llResume < 0 || llResume > m_uploadTotal)
    {
        llResume = 0;
    }
    if (!m_uploadFile->seek(llResume))
    {
        qWarning() << "continueUpload：seek 失败" << llResume;
        abortUpload();
        return;
    }
    m_uploadOffset = llResume;
    pumpUpload();
}

void Client::abortUpload()
{
    if (m_uploadFile)
    {
        if (m_uploadFile->isOpen())
        {
            m_uploadFile->close();
        }
        m_uploadFile.reset();
    }
    m_uploadTotal  = 0;
    m_uploadOffset = 0;
    m_uploadMd5Hex.clear();
}

void Client::pumpUpload()
{
    if (!m_uploadFile || !m_uploadFile->isOpen())
    {
        return;
    }

    // 写缓冲到水位线就停，等 bytesWritten 再来：这样无论文件多大，内存占用都是两三个分片
    while (m_socket.bytesToWrite() < SOCKET_WRITE_HIGH_WATER)
    {
        if (m_uploadOffset >= m_uploadTotal)
        {
            PDU* pdu = mkPDU();
            pdu->uiType = ENUM_TYPE_UPLOAD_FILE_FINISH_REQUEST;
            memcpy(pdu->caData, m_uploadMd5Hex.constData(),
                   size_t(qMin(int(MD5_HEX_LEN), m_uploadMd5Hex.size())));
            sendMsg(pdu);                    // sendMsg 内部负责 write + free

            if (m_uploadFile->isOpen())
            {
                m_uploadFile->close();
            }
            m_uploadFile.reset();
            return;
        }

        const QByteArray block = m_uploadFile->read(FILE_CHUNK_SIZE);
        if (block.isEmpty())
        {
            qWarning() << "上传文件读取不足，还差" << (m_uploadTotal - m_uploadOffset) << "字节";
            abortUpload();
            return;
        }

        PDU* pdu = mkPDU(block.size());
        pdu->uiType = ENUM_TYPE_UPLOAD_FILE_CHUNK_REQUEST;
        memcpy(pdu->caData + CHUNK_OFFSET_POS, &m_uploadOffset, sizeof(qint64));
        memcpy(pdu->caMsg, block.constData(), size_t(block.size()));
        sendMsg(pdu);

        m_uploadOffset += block.size();
    }
}