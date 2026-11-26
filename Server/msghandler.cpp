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
#include <QByteArray>

namespace {
// 当前用户网盘的绝对路径根：<m_strRootPath>/<登录名>
QString userRootAbs(const QString& loginName)
{
    const QString root = Server::getInstance().m_strRootPath + "/" + loginName;
    return QDir(root).absolutePath();
}

// 浅拷贝一份完整的 PDU。
// 用于"既要把包转发给别人、又要保证原 pdu 生命周期不被提前结束"的场景：
// handleMsg() 在分派结束后会 free(pdu)，如果不复制就转发，
// 接收方拿到的是已释放内存（use-after-free）。
PDU* clonePdu(const PDU* src)
{
    if (src == NULL)
    {
        return NULL;
    }
    PDU* dst = mkPDU(src->uiMsgLen);
    memcpy(dst, src, src->uiTotalLen);
    return dst;
}
}

MsgHandler::MsgHandler()
{

}

// ---------------------------------------------------------------
// 路径安全：客户端传来的路径必须严格落在当前用户的网盘根目录内
// ---------------------------------------------------------------
bool MsgHandler::isSafePath(const QString& strPath) const
{
    if (m_loginName.isEmpty() || strPath.isEmpty())
    {
        return false;
    }
    // 拒绝含空字节的路径（可能截断底层 API 的字符串）
    if (strPath.contains(QChar('\0')))
    {
        return false;
    }

    const QString rootAbs = QDir::cleanPath(userRootAbs(m_loginName));
    // QDir::absolutePath() 内部会做 cleanPath，".." 在此被解析消掉
    const QString abs     = QDir::cleanPath(QDir(strPath).absolutePath());

    // 必须等于根目录，或位于根目录下。
    // 注意带上分隔符再比较，否则 /userdata/alice2 会被误判为在 /userdata/alice 之内。
    return abs == rootAbs || abs.startsWith(rootAbs + "/");
}

bool MsgHandler::isSafePathOrRespond(const QString& strPath,
                                     ENUM_TYPE respondType,
                                     PDU** outRespond) const
{
    if (isSafePath(strPath))
    {
        return true;
    }
    qWarning() << "拒绝越权路径访问:" << strPath << " 登录名:" << m_loginName;
    if (outRespond != NULL)
    {
        *outRespond = makeBoolRespond(respondType, false);
    }
    return false;
}

PDU* MsgHandler::makeBoolRespond(ENUM_TYPE type, bool ret) const
{
    PDU* respdu = mkPDU();
    respdu->uiType = type;
    memcpy(respdu->caData, &ret, sizeof(bool));
    return respdu;
}

// ---------------------------------------------------------------
// 身份绑定校验
// 协议里部分请求的 caData 前 32 字节是"发起者自己"（聊天/加好友/删好友/
// 分享文件等）。这个字段由客户端填写，服务端不能直接信任——否则任何已登录
// 账号都能把自己的名字写成别人，以对方身份发消息、加好友、删好友关系。
// 这里要求它与当前连接的登录名严格一致，不一致一律拒绝。
// ---------------------------------------------------------------
bool MsgHandler::isOwnIdentity(const char* caClaimedName) const
{
    if (caClaimedName == NULL || m_loginName.isEmpty())
    {
        return false;
    }
    // caData 是定长 32 字节字段，显式限制长度，避免字段未以 0 结尾时越界读
    const QString claimed = QString::fromUtf8(caClaimedName, int(qstrnlen(caClaimedName, 32)));
    if (claimed.isEmpty())
    {
        return false;
    }
    return claimed == m_loginName;
}

PDU* MsgHandler::makeIdentityRejectRespond(ENUM_TYPE respondType) const
{
    qWarning() << "身份校验失败：会话用户为" << m_loginName
               << "，但请求声称的操作者不是本人，已拒绝";
    return makeBoolRespond(respondType, false);
}

PDU *MsgHandler::regist()
{
    char caName[32]={'\0'};
    memcpy(caName,pdu->caData,32);
    char caPWD[32]={'\0'};
    memcpy(caPWD,pdu->caData+32,32);
    qDebug()<<"regist caName"<<caName;
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
    qDebug()<<"login caName"<<caName;
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
    // 身份校验：caData 前半段的"发起者"必须是本连接的真实登录用户
    if (!isOwnIdentity(caCurName))
    {
        return makeIdentityRejectRespond(ENUM_TYPE_ADD_FRIEND_RESPOND);
    }
    int ret =operateDB::getInstance().handleAddFriend(caCurName,caTarName);
    if(ret==1)
    {
        // 转发给对方的是一份独立拷贝：原 pdu 随后会被 handleMsg 释放
        PDU* resendPdu = clonePdu(pdu);
        resendPdu->uiType = ENUM_TYPE_ADD_FRIEND_RESEND;
        MyTcpServer::getInstance().resend(caTarName,resendPdu);
        free(resendPdu);
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
    // 身份校验：只能以自己名义同意好友申请，不能替别人同意。
    //
    // 注意本消息 caData 的语义与前几类不同：它是 [发起方, 同意方]。
    // 客户端收到 ADD_FRIEND_RESEND 后直接沿用原包 caData（发起方 + 自己），
    // 所以"当前登录用户"落在 caTarName（后半段），不是 caCurName。
    // 若在这里误校验 caCurName，真实客户端的同意流程会被一律拒绝。
    if (!isOwnIdentity(caTarName))
    {
        return makeIdentityRejectRespond(ENUM_TYPE_ADD_FRIEND_AGREE_RESPOND);
    }
    bool ret =operateDB::getInstance().handleAddFriendAGree(caCurName,caTarName);
    qDebug()<<"addFriendAgree ret"<<ret;
    PDU*respdu=mkPDU();

    respdu->uiType=ENUM_TYPE_ADD_FRIEND_AGREE_RESPOND;
    memcpy(respdu->caData,&ret,sizeof(bool));
    // 通知请求发起方：用独立拷贝，respdu 本身还要返回给当前连接并会被释放
    PDU* notifyPdu = clonePdu(respdu);
    MyTcpServer::getInstance().resend(caCurName,notifyPdu);
    free(notifyPdu);
    return respdu;
}

PDU *MsgHandler::flushFriend()
{
    char caCurName[32]={'\0'};
    memcpy(caCurName,pdu->caData,32);
    // 身份校验：只能查自己的好友列表
    if (!isOwnIdentity(caCurName))
    {
        return makeIdentityRejectRespond(ENUM_TYPE_FLUSH_FRIEND_RESPOND);
    }
    QStringList res =
    operateDB::getInstance().handleFlushFriend(caCurName);
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
        // 身份校验：不能替别人删除好友关系
        if (!isOwnIdentity(curName))
        {
            return makeIdentityRejectRespond(ENUM_TYPE_DEL_FRIEND_RESPOND);
        }

        bool ret = operateDB::getInstance().handleDelFriend(curName, tarName);
        qDebug() << "delFriend ret: " << ret;

        PDU* respdu = mkPDU(0);
        respdu->uiType = ENUM_TYPE_DEL_FRIEND_RESPOND;
        memcpy(respdu->caData, &ret, sizeof(bool));

        return respdu;
}

PDU *MsgHandler::chat()
{
    char caCurName[32] = {'\0'};
    char tarName[32] = {'\0'};
    memcpy(caCurName, pdu->caData, 32);
    memcpy(tarName, pdu->caData+32, 32);
    // 身份校验：caData 前半段是"发送者"，必须是自己，否则就是冒名发言。
    // 聊天请求没有应答包，校验失败直接静默丢弃。
    if (!isOwnIdentity(caCurName))
    {
        qWarning() << "聊天身份校验失败：会话用户" << m_loginName << "试图以他人名义发送消息，已丢弃";
        return NULL;
    }
    // 转发独立拷贝，避免原 pdu 被 handleMsg 释放后接收方读到已释放内存
    PDU* resendPdu = clonePdu(pdu);
    resendPdu->uiType = ENUM_TYPE_CHAT_RESEND;
    const bool delivered = MyTcpServer::getInstance().resend(tarName, resendPdu);
    free(resendPdu);

    if (!delivered)
    {
        // 对方不在线：落库，等其登录后由 MyTcpSocket::pushOfflineMessages 补发。
        // 名字字段可能不带终止符，按 32 字节上限取长度。
        const QString strSender   = QString::fromUtf8(caCurName, int(qstrnlen(caCurName, 32)));
        const QString strReceiver = QString::fromUtf8(tarName,   int(qstrnlen(tarName, 32)));
        const QString strContent  = QString::fromUtf8(pdu->caMsg);
        const bool saved = operateDB::getInstance().handleSaveOfflineMsg(strSender, strReceiver, strContent);
        qDebug() << "聊天转发失败，离线落库" << saved
                 << strSender << "->" << strReceiver;
    }
    return NULL;
}

PDU *MsgHandler::mkdir()
{
    // caMsg 是当前目录，caData 是新建的文件夹名
    const QString strPath = QString("%1/%2").arg(QString(pdu->caMsg)).arg(pdu->caData);
    qDebug() << "mkdir strPath" << strPath;

    PDU* errRespond = NULL;
    if (!isSafePathOrRespond(strPath, ENUM_TYPE_MKDIR_RESPOND, &errRespond))
    {
        return errRespond;
    }

    QDir dir;
    bool ret = dir.mkpath(strPath);
    qDebug() << "mkdir ret: " << ret;
    return makeBoolRespond(ENUM_TYPE_MKDIR_RESPOND, ret);
}


PDU *MsgHandler::flushFile()
{
    // 目录列表同样要校验，否则可以枚举服务器任意目录
    const QString strPath = QString(pdu->caMsg);
    qDebug() << "flushFile strPath" << strPath;

    PDU* errRespond = NULL;
    if (!isSafePathOrRespond(strPath, ENUM_TYPE_FLUSH_FILE_RESPOND, &errRespond))
    {
        return errRespond;
    }

    QDir dir(strPath);
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
    }
    return respdu;
}

PDU *MsgHandler::delDir()
{
    const QString strPath = QString("%1/%2").arg(QString(pdu->caMsg)).arg(pdu->caData);
    qDebug() << "delDir strPath" << strPath;

    PDU* errRespond = NULL;
    if (!isSafePathOrRespond(strPath, ENUM_TYPE_DEL_DIR_RESPOND, &errRespond))
    {
        return errRespond;
    }

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
    return makeBoolRespond(ENUM_TYPE_DEL_DIR_RESPOND, ret);
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

    PDU* errRespond = NULL;
    // 源和目标都要校验，防止借重命名把文件写到根目录之外
    if (!isSafePathOrRespond(strOldPath, ENUM_TYPE_RENAME_FILE_RESPOND, &errRespond)
        || !isSafePathOrRespond(strNewPath, ENUM_TYPE_RENAME_FILE_RESPOND, &errRespond))
    {
        return errRespond;
    }

    QDir dir;
    bool ret = dir.rename(strOldPath, strNewPath);
    qDebug() << "renameFile ret:" << ret;
    return makeBoolRespond(ENUM_TYPE_RENAME_FILE_RESPOND, ret);
}

PDU *MsgHandler::moveFile()
{
    char caFileName[32] = {'\0'};
    char caTargetDir[32] = {'\0'};
    memcpy(caFileName, pdu->caData, 32);
    memcpy(caTargetDir, pdu->caData+32, 32);
    QStringList paths = QString(pdu->caMsg).split('\n');
    QString strSrcPath;
    QString strDstPath;
    if (paths.size() >= 2) {
        strSrcPath = paths[0] + "/" + caFileName;
        strDstPath = paths[1] + "/" + caFileName;
    } else {
        strSrcPath = QString(pdu->caMsg) + "/" + caFileName;
        strDstPath = QString(caTargetDir) + "/" + caFileName;
    }
    qDebug() << "moveFile from" << strSrcPath << "to" << strDstPath;

    PDU* errRespond = NULL;
    // 源与目标都必须落在用户根目录内
    if (!isSafePathOrRespond(strSrcPath, ENUM_TYPE_MOVE_FILE_RESPOND, &errRespond)
        || !isSafePathOrRespond(strDstPath, ENUM_TYPE_MOVE_FILE_RESPOND, &errRespond))
    {
        return errRespond;
    }

    QDir dir;
    bool ret = dir.rename(strSrcPath, strDstPath);
    qDebug() << "moveFile ret:" << ret;
    return makeBoolRespond(ENUM_TYPE_MOVE_FILE_RESPOND, ret);
}

PDU *MsgHandler::uploadFile()
{
    char caFileName[32] = {'\0'};
    memcpy(caFileName, pdu->caData, 32);
    // caMsg = "目标目录路径 + '\0' + 文件二进制数据"
    const QString strDir  = QString::fromUtf8(pdu->caMsg);
    const QString strPath = QString("%1/%2").arg(strDir).arg(caFileName);
    qDebug() << "uploadFile path:" << strPath;

    PDU* errRespond = NULL;
    // 只校验 '\0' 之前的路径部分，不能把文件体当路径
    if (!isSafePathOrRespond(strDir, ENUM_TYPE_UPLOAD_FILE_RESPOND, &errRespond)
        || !isSafePathOrRespond(strPath, ENUM_TYPE_UPLOAD_FILE_RESPOND, &errRespond))
    {
        return errRespond;
    }

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
    return makeBoolRespond(ENUM_TYPE_UPLOAD_FILE_RESPOND, ret);
}

PDU *MsgHandler::downloadFile()
{
    char caFileName[32] = {'\0'};
    memcpy(caFileName, pdu->caData, 32);
    const QString strDir  = QString(pdu->caMsg);
    const QString strPath = QString("%1/%2").arg(strDir).arg(caFileName);
    qDebug() << "downloadFile path:" << strPath;

    PDU* errRespond = NULL;
    if (!isSafePathOrRespond(strDir, ENUM_TYPE_DOWNLOAD_FILE_RESPOND, &errRespond)
        || !isSafePathOrRespond(strPath, ENUM_TYPE_DOWNLOAD_FILE_RESPOND, &errRespond))
    {
        return errRespond;
    }

    QFile file(strPath);
    PDU* respdu = nullptr;
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray fileData = file.readAll();
        file.close();
        QByteArray infoBytes = strDir.toUtf8();
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
    const QString strDir  = QString(pdu->caMsg);
    const QString strFilePath = QString("%1/%2").arg(strDir).arg(caFileName);
    qDebug() << "shareFile path:" << strFilePath << "to user:" << caTarName;

    PDU* errRespond = NULL;
    // 只允许分享自己网盘目录内的文件
    if (!isSafePathOrRespond(strDir, ENUM_TYPE_SHARE_FILE_RESPOND, &errRespond)
        || !isSafePathOrRespond(strFilePath, ENUM_TYPE_SHARE_FILE_RESPOND, &errRespond))
    {
        return errRespond;
    }

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
        free(sharePdu);
        ret = true;
    }
    return makeBoolRespond(ENUM_TYPE_SHARE_FILE_RESPOND, ret);
}
