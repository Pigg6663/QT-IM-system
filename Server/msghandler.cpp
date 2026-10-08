#include "msghandler.h"
#include "mytcpserver.h"
#include "operatedb.h"
#include "server.h"
#include"stdlib.h"
#include"string.h"
#include<QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include<QStringList>
MsgHandler::MsgHandler()
{

}

PDU *MsgHandler::regist()
{
    char caName[32]={'\0'};
    memcpy(caName,pdu->caData,32);
    char caPWD[32]={'\0'};
    memcpy(caPWD,pdu->caData+32,32);
    qDebug()<<"regist caName"<<caName<<"caPWD"<<caPWD;
    bool ret =operateDB::getInstance().handleRegist(caName,caPWD);
    qDebug()<<"regist ret"<<ret;
    if (ret) {
            QDir dir;
            bool res = dir.mkpath(QString("%1/%2").arg(Server::getInstance().m_strRootPath).arg(caName));
            qDebug() << "创建用户文件夹 res" << res;
        }
    PDU* respdu=mkPDU();
    memcpy(respdu->caData,&ret,sizeof(bool));
    respdu->uiType=ENUM_TYPE_REGIST_RESPOND;
    return respdu;
}

PDU *MsgHandler::findUser()
{
    char caName[32]={'\0'};
    memcpy(caName,pdu->caData,32);
    qDebug()<<"find user caName"<<caName;
    int ret =operateDB::getInstance().handleFindUser(caName);
    qDebug()<<"find user ret"<<ret;
    PDU* respdu=mkPDU();
    memcpy(respdu->caData,&ret,sizeof(int));
    respdu->uiType=ENUM_TYPE_FIND_USER_RESPOND;
    return respdu;
}

PDU *MsgHandler::onlineUser()
{
    QStringList res=operateDB::getInstance().handleOnlineUser();
    PDU* respdu=mkPDU(res.size()*32);
    respdu->uiType=ENUM_TYPE_ONLINEUSER_RESPOND;
    for(int i=0;i<res.size();i++)
    {
        memcpy(respdu->caMsg+i*32,res[i].toStdString().c_str(),32);
    }
    return respdu;
}

PDU *MsgHandler::login(QString& strLoginName)
{
    char caName[32]={'\0'};
    memcpy(caName,pdu->caData,32);
    char caPwd[32]={'\0'};
    memcpy(caPwd,pdu->caData+32,32);
    qDebug()<<"login caName"<<caName<<"caPwd"<<caPwd;
    bool ret =operateDB::getInstance().handleLogin(caName,caPwd);
    qDebug()<<"login ret"<<ret;
    if(ret)
    {
        strLoginName=caName;
    }
    PDU* respdu=mkPDU();
    memcpy(respdu->caData,&ret,sizeof(bool));

    respdu->uiType=ENUM_TYPE_LOGIN_RESPOND;
    return respdu;
}

PDU *MsgHandler::addFriend()
{
    char caCurName[32]={'\0'};
    char caTarName[32]={'\0'};
    memcpy(caCurName,pdu->caData,32);
    memcpy(caTarName,pdu->caData+32,32);
    int ret =operateDB::getInstance().handleAddFriend(caCurName,caTarName);
    if(ret==1)
    {
        pdu->uiType =ENUM_TYPE_ADD_FRIEND_RESEND;
        MyTcpServer::getInstance().resend(caTarName,pdu);
        return NULL;
    }
    PDU* respdu=mkPDU();
    memcpy(respdu->caData,&ret,sizeof(int));

    respdu->uiType=ENUM_TYPE_ADD_FRIEND_RESPOND;
    return respdu;
}

PDU *MsgHandler::addFriendAgree()
{
    char caCurName[32]={'\0'};
    char caTarName[32]={'\0'};
    memcpy(caCurName,pdu->caData,32);
    memcpy(caTarName,pdu->caData+32,32);
    bool ret =operateDB::getInstance().handleAddFriendAGree(caCurName,caTarName);
    qDebug()<<"addFriendAgree ret"<<ret;
    PDU*respdu=mkPDU();

    respdu->uiType=ENUM_TYPE_ADD_FRIEND_AGREE_RESPOND;
    memcpy(respdu->caData,&ret,sizeof(bool));
    MyTcpServer::getInstance().resend(caCurName,respdu);
    return respdu;
}

PDU *MsgHandler::flushFriend()
{
    QStringList res =
    operateDB::getInstance().handleFlushFriend(pdu->caData);
       PDU* respdu = mkPDU(res.size()*32);
       respdu->uiType = ENUM_TYPE_FLUSH_FRIEND_RESPOND;
       for (int i=0; i<res.size(); i++) {
           memcpy(respdu->caMsg+i*32, res[i].toStdString().c_str(), 32);
       }
       return respdu;
}

PDU *MsgHandler::delFriend()
{
    char curName[32] = {'\0'};
        char tarName[32] = {'\0'};
        memcpy(curName, pdu->caData, 32);
        memcpy(tarName, pdu->caData+32, 32);

        bool ret = operateDB::getInstance().handleDelFriend(curName, tarName);
        qDebug() << "delFriend ret: " << ret;

        PDU* respdu = mkPDU(0);
        respdu->uiType = ENUM_TYPE_DEL_FRIEND_RESPOND;
        memcpy(respdu->caData, &ret, sizeof(bool));

        return respdu;
}

PDU *MsgHandler::chat()
{
    char tarName[32] = {'\0'};
    memcpy(tarName, pdu->caData+32, 32);
    pdu->uiType = ENUM_TYPE_CHAT_RESEND;
    MyTcpServer::getInstance().resend(tarName, pdu);
    return NULL;
}

PDU *MsgHandler::mkdir()
{
    QString strPath = QString("%1/%2").arg(pdu->caMsg).arg(pdu->caData);
       qDebug() << "mkdir strPath" << strPath;
       QDir dir;
       bool ret = dir.mkpath(strPath);
       qDebug() << "mkdir ret: " << ret;
       PDU* respdu = mkPDU();
       respdu->uiType = ENUM_TYPE_MKDIR_RESPOND;
       memcpy(respdu->caData, &ret, sizeof(bool));
       return respdu;
}


PDU *MsgHandler::flushFile()
{
    QDir dir(pdu->caMsg);
        QFileInfoList fileInfoList = dir.entryInfoList();

        PDU* respdu = mkPDU(fileInfoList.size() * sizeof(FileInfo));
        respdu->uiType = ENUM_TYPE_FLUSH_FILE_RESPOND;
        for (int i=0; i<fileInfoList.size(); i++) {
            FileInfo* pFileInfo = (FileInfo*)respdu->caMsg+i;
            if (fileInfoList[i].isDir()) {
                pFileInfo->iFileType = 0;
            } else {
                pFileInfo->iFileType = 1;
            }
            memcpy(pFileInfo->caName,
            fileInfoList[i].fileName().toStdString().c_str(), 32);
            qDebug() << "pFileInfo->caName" << pFileInfo->caName;
        }
        return respdu;
}

PDU *MsgHandler::delDir()
{
    QString strPath = QString("%1/%2").arg(pdu->caMsg).arg(pdu->caData);
    qDebug() << "delDir strPath" << strPath;
    QFileInfo fileInfo(strPath);
    bool ret = false;
    if (fileInfo.isDir()) {
        QDir dir(strPath);
        ret = dir.removeRecursively();
    } else {
        QFile file(strPath);
        ret = file.remove();
    }
    qDebug() << "delDir ret:" << ret;
    PDU* respdu = mkPDU();
    respdu->uiType = ENUM_TYPE_DEL_DIR_RESPOND;
    memcpy(respdu->caData, &ret, sizeof(bool));
    return respdu;
}

PDU *MsgHandler::renameFile()
{
    char caOldName[32] = {'\0'};
    char caNewName[32] = {'\0'};
    memcpy(caOldName, pdu->caData, 32);
    memcpy(caNewName, pdu->caData+32, 32);
    QString strCurPath = QString(pdu->caMsg);
    QString strOldPath = QString("%1/%2").arg(strCurPath).arg(caOldName);
    QString strNewPath = QString("%1/%2").arg(strCurPath).arg(caNewName);
    qDebug() << "renameFile from" << strOldPath << "to" << strNewPath;
    QDir dir;
    bool ret = dir.rename(strOldPath, strNewPath);
    qDebug() << "renameFile ret:" << ret;
    PDU* respdu = mkPDU();
    respdu->uiType = ENUM_TYPE_RENAME_FILE_RESPOND;
    memcpy(respdu->caData, &ret, sizeof(bool));
    return respdu;
}

PDU *MsgHandler::moveFile()
{
    char caFileName[32] = {'\0'};
    char caTargetDir[32] = {'\0'};
    memcpy(caFileName, pdu->caData, 32);
    memcpy(caTargetDir, pdu->caData+32, 32);
    QString strCurPath(pdu->caMsg);
    QStringList paths = QString(pdu->caMsg).split('\n');
    QString strSrcPath;
    QString strDstPath;
    if (paths.size() >= 2) {
        strSrcPath = paths[0] + "/" + caFileName;
        strDstPath = paths[1] + "/" + caFileName;
    } else {
        strSrcPath = strCurPath + "/" + caFileName;
        strDstPath = QString(caTargetDir) + "/" + caFileName;
    }
    qDebug() << "moveFile from" << strSrcPath << "to" << strDstPath;
    QDir dir;
    bool ret = dir.rename(strSrcPath, strDstPath);
    qDebug() << "moveFile ret:" << ret;
    PDU* respdu = mkPDU();
    respdu->uiType = ENUM_TYPE_MOVE_FILE_RESPOND;
    memcpy(respdu->caData, &ret, sizeof(bool));
    return respdu;
}

PDU *MsgHandler::uploadFile()
{
    char caFileName[32] = {'\0'};
    memcpy(caFileName, pdu->caData, 32);
    qint64 fileSize = QString(pdu->caData+32).toLongLong();
    QString strPath = QString("%1/%2").arg(pdu->caMsg).arg(caFileName);
    qDebug() << "uploadFile path:" << strPath << "size:" << fileSize;
    QFile file(strPath);
    bool ret = false;
    if (file.open(QIODevice::WriteOnly)) {
        int pathLen = strlen(pdu->caMsg) + 1;
        char* fileData = pdu->caMsg + pathLen;
        int dataLen = pdu->uiMsgLen - pathLen;
        if (dataLen > 0) {
            file.write(fileData, dataLen);
        }
        file.close();
        ret = true;
    }
    qDebug() << "uploadFile ret:" << ret;
    PDU* respdu = mkPDU();
    respdu->uiType = ENUM_TYPE_UPLOAD_FILE_RESPOND;
    memcpy(respdu->caData, &ret, sizeof(bool));
    return respdu;
}

PDU *MsgHandler::downloadFile()
{
    char caFileName[32] = {'\0'};
    memcpy(caFileName, pdu->caData, 32);
    QString strPath = QString("%1/%2").arg(pdu->caMsg).arg(caFileName);
    qDebug() << "downloadFile path:" << strPath;
    QFile file(strPath);
    PDU* respdu = nullptr;
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray fileData = file.readAll();
        file.close();
        QString strInfo = QString("%1").arg(pdu->caMsg);
        QByteArray infoBytes = strInfo.toUtf8();
        int totalMsgLen = infoBytes.size() + 1 + fileData.size();
        respdu = mkPDU(totalMsgLen);
        respdu->uiType = ENUM_TYPE_DOWNLOAD_FILE_RESPOND;
        memcpy(respdu->caData, caFileName, 32);
        memcpy(respdu->caMsg, infoBytes.constData(), infoBytes.size() + 1);
        memcpy(respdu->caMsg + infoBytes.size() + 1, fileData.constData(), fileData.size());
    } else {
        respdu = mkPDU();
        respdu->uiType = ENUM_TYPE_DOWNLOAD_FILE_RESPOND;
        bool ret = false;
        memcpy(respdu->caData, &ret, sizeof(bool));
    }
    return respdu;
}

PDU *MsgHandler::shareFile()
{
    char caFileName[32] = {'\0'};
    char caTarName[32] = {'\0'};
    memcpy(caFileName, pdu->caData, 32);
    memcpy(caTarName, pdu->caData+32, 32);
    QString strFilePath = QString("%1/%2").arg(pdu->caMsg).arg(caFileName);
    qDebug() << "shareFile path:" << strFilePath << "to user:" << caTarName;
    QFile file(strFilePath);
    bool ret = false;
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray fileData = file.readAll();
        file.close();
        int pathLen = strlen(pdu->caMsg) + 1;
        int totalMsgLen = pathLen + fileData.size();
        PDU* sharePdu = mkPDU(totalMsgLen);
        sharePdu->uiType = ENUM_TYPE_SHARE_FILE_RESPOND;
        memcpy(sharePdu->caData, caFileName, 32);
        memcpy(sharePdu->caMsg, pdu->caMsg, pathLen);
        memcpy(sharePdu->caMsg + pathLen, fileData.constData(), fileData.size());
        MyTcpServer::getInstance().resend(caTarName, sharePdu);
        ret = true;
    }
    PDU* respdu = mkPDU();
    respdu->uiType = ENUM_TYPE_SHARE_FILE_RESPOND;
    memcpy(respdu->caData, &ret, sizeof(bool));
    return respdu;
}
