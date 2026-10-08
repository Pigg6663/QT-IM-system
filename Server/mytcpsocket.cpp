#include "mytcpserver.h"
#include "mytcpsocket.h"
#include "operatedb.h"
#include "protocol.h"

MyTcpSocket::MyTcpSocket()
{
    m_pmh=new MsgHandler;
connect(this,&QTcpSocket::readyRead,this,&MyTcpSocket::recvMsg);
connect(this,&QTcpSocket::disconnected,this,&MyTcpSocket::clineOffliine);
}

MyTcpSocket::~MyTcpSocket()
{
    delete m_pmh;
}

void MyTcpSocket::recvMsg()
{
   PDU* pdu=readMsg();
   PDU* respdu=handleMsg(pdu);
   sendMsg(respdu);
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
    operateDB::getInstance().handleOffline(m_strLoginName.toStdString().c_str());
    MyTcpServer::getInstance().removeSocket(this);
}

PDU *MyTcpSocket::readMsg()
{

    qDebug()<<"readMsg接收消息长度"<<this->bytesAvailable();
    uint uiPDULen=0;
    this->read((char*)&uiPDULen,sizeof(uint));
    uint uiMsgLen=uiPDULen-sizeof(PDU);
    PDU*pdu=mkPDU(uiMsgLen);
    this->read((char*)pdu+sizeof(uint),uiPDULen-sizeof(uint));
    return pdu;
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

