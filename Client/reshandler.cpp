#include "client.h"
#include "index.h"
#include "reshandler.h"
#include"chat.h"
#include "file.h"
#include<QDebug>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
ResHandler::ResHandler()
{

}

void ResHandler::login()
{
    bool ret;
           memcpy(&ret,pdu->caData,sizeof(bool));
           qDebug()<<"regist ret"<<ret;
           if(ret){
              Index::getInstance().show();
              Client::getInstance().hide();
           }else{
               QMessageBox::information(&Client::getInstance(),"提示","登录失败");
           }
}

void ResHandler::regist()
{
    bool ret;
    memcpy(&ret,pdu->caData,sizeof(bool));
    qDebug()<<"regist ret"<<ret;
    if(ret){
        QMessageBox::information(&Client::getInstance(),"提示","注册成功");
    }else{
        QMessageBox::information(&Client::getInstance(),"提示","注册失败");
    }
}

void ResHandler::findUser()
{
    int ret;
    memcpy(&ret,pdu->caData,sizeof(int));
    qDebug()<<"find user ret"<<ret;
    if(ret==0){
      QMessageBox::information(&Client::getInstance(),"提示","该用户不在线");
    }
    if(ret==1){
      QMessageBox::information(&Client::getInstance(),"提示","该用户在线");
    }
    if(ret==2){
      QMessageBox::information(&Client::getInstance(),"提示","该用户不存在");
    }
    if(ret==-1){
      QMessageBox::information(&Client::getInstance(),"提示","查找错误");
    }
}

void ResHandler::onlineUser()
{
   uint uiSize=pdu->uiMsgLen/32;
   char caTmp[32]={'\0'};
   QStringList userList;
   for(uint i=0;i<uiSize;i++)
   {
       memcpy(caTmp,pdu->caMsg+i*32,32);
       userList.append(caTmp);
   }
   Index::getInstance().getFriend()->m_pOnlineUser->updateLw(userList);

}

void ResHandler::addFriend()
{
    int ret;
    memcpy(&ret,pdu->caData,sizeof(int));
    qDebug()<<"addFriend ret"<<ret;
    if(ret==0){
      QMessageBox::information(&Index::getInstance(),"提示","该用户不在线");
    }
    if(ret==-2){
      QMessageBox::information(&Index::getInstance(),"提示","该用户已经是好友");
    }
    else if(ret==-1){
      QMessageBox::information(&Index::getInstance(),"提示","服务器错误：联系开发人员");
    }
}

void ResHandler::addFriendResend()
{
    char caName[32]={'\0'};
    memcpy(caName,pdu->caData,32);
    int ret = QMessageBox::question(&Index::getInstance(),"添加好友",QString("是否同意%1的添加好友请求？").arg(caName));
    if(ret !=QMessageBox::Yes)
    {
        return ;
    }
    PDU*respdu = mkPDU();
    memcpy(respdu->caData,pdu->caData,64);
    respdu->uiType=ENUM_TYPE_ADD_FRIEND_AGREE_REQUEST;
    Client::getInstance().sendMsg(respdu);
}

void ResHandler::addFriendAgree()
{
    bool ret;
    memcpy(&ret,pdu->caData,sizeof (bool));
    qDebug()<<"addFriendAgree ret"<<ret;
    if(ret){
      QMessageBox::information(&Index::getInstance(),"提示","添加用户成功");
    }
    else{
      QMessageBox::information(&Index::getInstance(),"提示","添加用户失败");
    }
}

void ResHandler::flushFriend()
{

        QStringList friendList;
        int iSize = pdu->uiMsgLen/32;
        char caTmp[32] = {'\0'};
        for (int i=0; i<iSize; i++) {
            memcpy(caTmp, pdu->caMsg+i*32, 32);
            friendList.append(caTmp);
        }
        Index::getInstance().getFriend()->update_LW(friendList);

}

void ResHandler::delFriend()
{
        bool ret;
        memcpy(&ret, pdu->caData, sizeof(bool));
        qDebug() << "delFriend ret" << ret;
        if (ret) {
            Index::getInstance().getFriend()->flushFriend();
        } else {
            QMessageBox::information(&Index::getInstance(), "提示", "删除好友失败");
        }
}

void ResHandler::chat()
{
    Chat* c = Index::getInstance().getFriend()->m_pChat;
        if (c->isHidden()) {
            c->show();
        }

        char caChatName[32] = {'\0'};
        memcpy(caChatName, pdu->caData, 32);
        c->updateShow_TE(QString("%1 : %2").arg(caChatName).arg(pdu->caMsg));
        c->m_strChatName = caChatName;
}

void ResHandler::mkdir()
{
    bool ret;
       memcpy(&ret, pdu->caData, sizeof(bool));
       qDebug() << "mkdir ret" << ret;
       if (ret) {
           QMessageBox::information(&Index::getInstance(), "提示", "创建文件夹成功");
       } else {
           QMessageBox::information(&Index::getInstance(), "提示", "创建文件夹失败");
       }
}

void ResHandler::flushFile()
{
    int iCount = pdu->uiMsgLen/sizeof (File::FileInfo);

       QList<File::FileInfo*> pFileInfoList;
       for (int i=0; i<iCount; i++) {
           File::FileInfo* pFileInfo = new File::FileInfo;
           memcpy(pFileInfo, pdu->caMsg+i*sizeof (File::FileInfo), sizeof(File::FileInfo));
           pFileInfoList.append(pFileInfo);
       }
       Index::getInstance().getFile()->updateFileList(pFileInfoList);
}

void ResHandler::delDir()
{
    bool ret;
    memcpy(&ret, pdu->caData, sizeof(bool));
    qDebug() << "delDir ret" << ret;
    if (ret) {
        QMessageBox::information(&Index::getInstance(), "提示", "删除成功");
        Index::getInstance().getFile()->flushFile();
    } else {
        QMessageBox::information(&Index::getInstance(), "提示", "删除失败");
    }
}

void ResHandler::renameFile()
{
    bool ret;
    memcpy(&ret, pdu->caData, sizeof(bool));
    qDebug() << "renameFile ret" << ret;
    if (ret) {
        QMessageBox::information(&Index::getInstance(), "提示", "重命名成功");
        Index::getInstance().getFile()->flushFile();
    } else {
        QMessageBox::information(&Index::getInstance(), "提示", "重命名失败");
    }
}

void ResHandler::moveFile()
{
    bool ret;
    memcpy(&ret, pdu->caData, sizeof(bool));
    qDebug() << "moveFile ret" << ret;
    if (ret) {
        QMessageBox::information(&Index::getInstance(), "提示", "移动成功");
        Index::getInstance().getFile()->flushFile();
    } else {
        QMessageBox::information(&Index::getInstance(), "提示", "移动失败");
    }
}

void ResHandler::uploadFile()
{
    bool ret;
    memcpy(&ret, pdu->caData, sizeof(bool));
    qDebug() << "uploadFile ret" << ret;
    if (ret) {
        QMessageBox::information(&Index::getInstance(), "提示", "上传成功");
        Index::getInstance().getFile()->flushFile();
    } else {
        QMessageBox::information(&Index::getInstance(), "提示", "上传失败");
    }
}

void ResHandler::downloadFile()
{
    // caData contains the file name, caMsg contains path + file data
    char caFileName[32] = {'\0'};
    memcpy(caFileName, pdu->caData, 32);
    qDebug() << "downloadFile fileName:" << caFileName;

    // Check if download succeeded (caData has file info)
    if (pdu->uiMsgLen > 0) {
        // Extract path info and file data from caMsg
        QString strPathInfo = QString(pdu->caMsg);
        int pathLen = strPathInfo.toUtf8().size() + 1;
        QByteArray fileData(pdu->caMsg + pathLen, pdu->uiMsgLen - pathLen);

        QString strSavePath = QString("%1/%2").arg(File::getInstance().m_strUserPath).arg(caFileName);
        QString strSelectedPath = QFileDialog::getSaveFileName(&Index::getInstance(), "保存文件", caFileName);
        if (strSelectedPath.isEmpty()) {
            return;
        }

        QFile file(strSelectedPath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(fileData);
            file.close();
            QMessageBox::information(&Index::getInstance(), "提示", "下载成功");
        } else {
            QMessageBox::information(&Index::getInstance(), "提示", "下载失败：无法保存文件");
        }
    } else {
        QMessageBox::information(&Index::getInstance(), "提示", "下载失败：文件不存在");
    }
}

void ResHandler::shareFile()
{
    bool ret;
    memcpy(&ret, pdu->caData, sizeof(bool));
    qDebug() << "shareFile ret" << ret;
    if (ret) {
        QMessageBox::information(&Index::getInstance(), "提示", "分享成功");
    } else {
        QMessageBox::information(&Index::getInstance(), "提示", "分享失败");
    }
    // Also handle receiving shared files
    if (pdu->uiMsgLen > 0) {
        char caFileName[32] = {'\0'};
        memcpy(caFileName, pdu->caData, 32);
        QString strPathInfo = QString(pdu->caMsg);
        int pathLen = strPathInfo.toUtf8().size() + 1;
        QByteArray fileData(pdu->caMsg + pathLen, pdu->uiMsgLen - pathLen);

        QString strSavePath = QString("%1/%2").arg(Index::getInstance().getFile()->m_strUserPath).arg(caFileName);
        QFile file(strSavePath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(fileData);
            file.close();
            QMessageBox::information(&Index::getInstance(), "分享文件", QString("收到来自好友的分享文件：%1，已保存到你的目录").arg(caFileName));
        }
    }
}
