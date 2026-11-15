#include "client.h"
#include "index.h"
#include "protocol.h"
#include "ui_client.h"

#include <QFile>
#include<QDebug>
#include <QHostAddress>
#include <QMessageBox>
#include <cstring>

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
    case ENUM_TYPE_DOWNLOAD_FILE_RESPOND:
    {
        m_prh->downloadFile();
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
