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

// ---------------------------------------------------------------
// 第 7 项：大文件分片上传
//   1) UPLOAD_FILE_REQUEST        握手：文件名 + 总长 + 整文件 MD5
//      服务端回 UPLOAD_FILE_RESPOND：断点位置 + 是否就绪
//   2) UPLOAD_FILE_CHUNK_REQUEST  每个分片带自己的偏移，顺序推
//   3) UPLOAD_FILE_FINISH_REQUEST 收尾，服务端校验长度与 MD5
// 数据先落到 <目标路径>.part，校验通过才 rename 转正；不一致就删掉半成品，
// 否则下次续传会接在一个已经坏掉的前缀上，越传越错。
// ---------------------------------------------------------------
PDU *MsgHandler::uploadFile()
{
    char caFileName[32] = {'\0'};
    memcpy(caFileName, pdu->caData, 32);
    qint64 llTotal = 0;
    memcpy(&llTotal, pdu->caData + UPLOAD_SIZE_POS, sizeof(qint64));
    const QByteArray md5Raw(pdu->caData + UPLOAD_MD5_POS, MD5_RAW_LEN);

    const QString strDir  = QString::fromUtf8(pdu->caMsg);
    const QString strPath = QString("%1/%2").arg(strDir).arg(caFileName);
    qDebug() << "uploadFile(init) path:" << strPath << "size:" << llTotal;

    PDU* errRespond = NULL;
    // caMsg 只有目录路径，文件名在 caData，两段都要卡在用户根目录内
    if (!isSafePathOrRespond(strDir, ENUM_TYPE_UPLOAD_FILE_RESPOND, &errRespond)
        || !isSafePathOrRespond(strPath, ENUM_TYPE_UPLOAD_FILE_RESPOND, &errRespond))
    {
        return errRespond;
    }

    // 上一次上传没收到 FINISH 就来了新握手，直接作废
    resetUploadSession();
    if (llTotal < 0)
    {
        return makeBoolRespond(ENUM_TYPE_UPLOAD_FILE_RESPOND, false);
    }

    m_uploadPath   = strPath + ".part";
    m_uploadSize   = llTotal;
    m_uploadMd5Hex = md5Raw.toHex();
    m_uploadHash.reset(new QCryptographicHash(QCryptographicHash::Md5));

    // 断点续传：上一次留下的 .part 有多大就从哪里接着收
    qint64 llResume = QFileInfo(m_uploadPath).size();
    if (llResume > llTotal)          // 比目标还大，说明不是同一次上传的残留
    {
        QFile::remove(m_uploadPath);
        llResume = 0;
    }

    bool ok = false;
    m_uploadOffset = 0;
    m_uploadFile.reset(new QFile(m_uploadPath));
    QIODevice::OpenMode mode = QIODevice::WriteOnly;
    mode |= (llResume > 0) ? QIODevice::Append : QIODevice::Truncate;
    if (m_uploadFile->open(mode))
    {
        ok = true;
        m_uploadOffset = llResume;
        if (llResume > 0)
        {
            // 续传时必须把已落盘的前缀重新喂进哈希，否则整文件 MD5 根本算不出来
            QFile part(m_uploadPath);
            if (part.open(QIODevice::ReadOnly))
            {
                while (!part.atEnd())
                {
                    const QByteArray block = part.read(int(FILE_CHUNK_SIZE));
                    if (block.isEmpty())
                    {
                        break;
                    }
                    m_uploadHash->addData(block);
                }
                part.close();
            }
            qDebug() << "uploadFile 断点续传，已有" << llResume << "字节";
        }
    }
    else
    {
        qWarning() << "无法创建临时文件:" << m_uploadPath << m_uploadFile->errorString();
        resetUploadSession();
    }

    // 应答：[0:8] 断点位置 [8] 是否就绪
    PDU* respdu = mkPDU();
    respdu->uiType = ENUM_TYPE_UPLOAD_FILE_RESPOND;
    memcpy(respdu->caData + UPLOAD_RESUME_POS, &m_uploadOffset, sizeof(qint64));
    memcpy(respdu->caData + UPLOAD_OK_POS, &ok, sizeof(bool));
    return respdu;
}

PDU *MsgHandler::uploadChunk()
{
    if (!m_uploadFile || !m_uploadFile->isOpen())
    {
        qWarning() << "收到分片但没有进行中的上传会话，丢弃";
        return NULL;
    }

    qint64 llOffset = 0;
    memcpy(&llOffset, pdu->caData + CHUNK_OFFSET_POS, sizeof(qint64));
    if (llOffset != m_uploadOffset)
    {
        // 偏移必须严格连续：客户端是顺序推的，跳过或重复都说明字节流已经错位，
        // 继续写下去只会得到一个看起来完整、实际错位的文件
        qWarning() << "分片偏移不连续，期望" << m_uploadOffset << "实际" << llOffset;
        return NULL;
    }

    const qint64 llWritten = m_uploadFile->write(pdu->caMsg, pdu->uiMsgLen);
    if (llWritten != qint64(pdu->uiMsgLen))
    {
        qWarning() << "分片写盘失败:" << m_uploadFile->errorString();
        return NULL;
    }
    m_uploadFile->flush();
    m_uploadHash->addData(pdu->caMsg, int(pdu->uiMsgLen));
    m_uploadOffset += llWritten;

    // 分片不带逐片应答：一次上传几百上千片，逐片应答会明显拖慢速度。
    // 真正的确认放在 FINISH 里，用"长度 + 整文件 MD5"双重校验。
    return NULL;
}

PDU *MsgHandler::uploadFinish()
{
    bool ok = false;
    if (m_uploadFile && m_uploadFile->isOpen())
    {
        m_uploadFile->close();
        const QByteArray actualHex = m_uploadHash ? m_uploadHash->result().toHex() : QByteArray();
        // 客户端在 FINISH 里再报一次整文件 MD5，与服务端边收边算的结果比对
        // 按十六进制字符串比，不区分大小写：不同客户端可能发大写，语义是同一个摘要
        const QByteArray expectHex = QByteArray(pdu->caData, MD5_HEX_LEN).toLower();
        ok = (m_uploadOffset == m_uploadSize) && (actualHex == expectHex);
        qDebug() << "uploadFinish 期望长度" << m_uploadSize << "实收" << m_uploadOffset
                 << "服务端 MD5" << actualHex << "客户端声明" << expectHex << "一致" << ok;
        if (ok)
        {
            const QString strFinalPath = m_uploadPath.left(m_uploadPath.size() - 5); // 去掉 ".part"
            QFile::remove(strFinalPath);
            ok = QFile::rename(m_uploadPath, strFinalPath);
            if (!ok)
            {
                qWarning() << "重命名临时文件失败:" << m_uploadPath;
            }
        }
        if (!ok)
        {
            QFile::remove(m_uploadPath);
        }
    }
    resetUploadSession();
    return makeBoolRespond(ENUM_TYPE_UPLOAD_FILE_FINISH_RESPOND, ok);
}

void MsgHandler::resetUploadSession()
{
    if (m_uploadFile)
    {
        if (m_uploadFile->isOpen())
        {
            m_uploadFile->close();
        }
        m_uploadFile.reset();
    }
    m_uploadPath.clear();
    m_uploadSize   = 0;
    m_uploadOffset = 0;
    m_uploadMd5Hex.clear();
    m_uploadHash.reset();
}

// ---------------------------------------------------------------
// 第 7 项：下载握手
// 客户端只说"要哪个文件、我本地已经有多少字节"，数据由 MyTcpSocket
// 在握手应答发出去之后主动推（边读边推，靠 bytesWritten 反压），
// 不再把整文件读进内存拼成一个 PDU。
// ---------------------------------------------------------------
PDU *MsgHandler::downloadFile()
{
    char caFileName[32] = {'\0'};
    memcpy(caFileName, pdu->caData, 32);
    const QString strDir  = QString::fromUtf8(pdu->caMsg);
    const QString strPath = QString("%1/%2").arg(strDir).arg(caFileName);
    qint64 llResume = 0;
    memcpy(&llResume, pdu->caData + DL_RESUME_POS, sizeof(qint64));
    qDebug() << "downloadFile(init) path:" << strPath << "客户端已有:" << llResume;

    PDU* errRespond = NULL;
    if (!isSafePathOrRespond(strDir, ENUM_TYPE_DOWNLOAD_FILE_RESPOND, &errRespond)
        || !isSafePathOrRespond(strPath, ENUM_TYPE_DOWNLOAD_FILE_RESPOND, &errRespond))
    {
        return errRespond;
    }

    const QFileInfo info(strPath);
    const bool ok = info.exists() && info.isFile();
    qint64 llSize  = ok ? info.size() : 0;
    qint64 llStart = llResume;
    if (llStart < 0 || llStart > llSize)
    {
        llStart = 0;   // 客户端本地那份比服务端还大，只能从头传
    }

    PDU* respdu = mkPDU();
    respdu->uiType = ENUM_TYPE_DOWNLOAD_FILE_RESPOND;
    memcpy(respdu->caData + DL_RESP_OK_POS, &ok, sizeof(bool));
    memcpy(respdu->caData + DL_RESP_START_POS, &llStart, sizeof(qint64));
    memcpy(respdu->caData + DL_RESP_SIZE_POS, &llSize, sizeof(qint64));

    // 应答先发出去，数据流由调用方在 sendMsg 之后启动
    m_bDownloadPending = ok;
    m_downloadPath     = strPath;
    m_downloadOffset   = llStart;
    return respdu;
}

QString MsgHandler::takeDownloadPath()
{
    m_bDownloadPending = false;
    return m_downloadPath;
}

qint64 MsgHandler::takeDownloadOffset()
{
    return m_downloadOffset;
}

// ---------------------------------------------------------------
// 分享文件：服务端在自己的磁盘上把文件复制进接收方网盘，再给接收方
// 推一条系统通知。
// 旧实现是把文件内容整包发给接收方客户端、由客户端写进 **本地** 目录，
// 而网盘目录其实在服务端，结果"分享成功"了但对方网盘里看不到。
// ---------------------------------------------------------------
PDU *MsgHandler::shareFile()
{
    char caFileName[32] = {'\0'};
    char caTarName[32] = {'\0'};
    memcpy(caFileName, pdu->caData, 32);
    memcpy(caTarName, pdu->caData + 32, 32);
    const QString strDir = QString::fromUtf8(pdu->caMsg);
    const QString strSrcPath = QString("%1/%2").arg(strDir).arg(caFileName);
    const QString strTarName = QString::fromUtf8(caTarName, int(qstrnlen(caTarName, 32)));
    qDebug() << "shareFile" << strSrcPath << "to user:" << strTarName;

    PDU* errRespond = NULL;
    // 只允许分享自己网盘目录内的文件
    if (!isSafePathOrRespond(strDir, ENUM_TYPE_SHARE_FILE_RESPOND, &errRespond)
        || !isSafePathOrRespond(strSrcPath, ENUM_TYPE_SHARE_FILE_RESPOND, &errRespond))
    {
        return errRespond;
    }

    // 目标必须是已注册用户，且不能分享给自己。
    // handleFindUser 返回 2 表示用户不存在；这里只借它做存在性判断。
    if (strTarName.isEmpty()
        || strTarName == m_loginName
        || operateDB::getInstance().handleFindUser(strTarName.toUtf8().constData()) == 2)
    {
        qWarning() << "分享目标非法:" << strTarName;
        return makeBoolRespond(ENUM_TYPE_SHARE_FILE_RESPOND, false);
    }

    const QString strTarDir = userRootAbs(strTarName);
    QDir().mkpath(strTarDir);            // 老账号可能没有网盘目录，顺手补建
    const QString strBaseName = QFileInfo(strSrcPath).fileName();
    QString strDstPath = strTarDir + "/" + strBaseName;
    // 同名文件不覆盖，加 (1)(2) 序号
    for (int i = 1; QFile::exists(strDstPath) && i < 1000; ++i)
    {
        const QFileInfo fi(strBaseName);
        const QString strSuffix = fi.suffix().isEmpty() ? QString() : "." + fi.suffix();
        strDstPath = QString("%1/%2(%3)%4").arg(strTarDir).arg(fi.completeBaseName()).arg(i).arg(strSuffix);
    }

    bool ret = QFile::copy(strSrcPath, strDstPath);
    if (!ret)
    {
        qWarning() << "分享复制失败:" << strSrcPath << "->" << strDstPath;
        return makeBoolRespond(ENUM_TYPE_SHARE_FILE_RESPOND, false);
    }

    // 通知接收方。用 CHAT_RESEND 而不是新加一个类型：客户端本来就会把它
    // 显示在对应会话窗口里，接收方在线就即时看到，离线则走离线消息补发。
    const QString strNotice = QString("[系统] %1 向您分享了文件 %2，已存入您的网盘")
                                  .arg(m_loginName)
                                  .arg(QFileInfo(strDstPath).fileName());
    const QByteArray noticeBytes = strNotice.toUtf8();
    const QByteArray senderBytes = m_loginName.toUtf8();
    const QByteArray targetBytes = strTarName.toUtf8();

    PDU* noticePdu = mkPDU(noticeBytes.size());
    noticePdu->uiType = ENUM_TYPE_CHAT_RESEND;
    memcpy(noticePdu->caData, senderBytes.constData(), size_t(qMin(32, senderBytes.size())));
    memcpy(noticePdu->caData + 32, targetBytes.constData(), size_t(qMin(32, targetBytes.size())));
    memcpy(noticePdu->caMsg, noticeBytes.constData(), size_t(noticeBytes.size()));

    if (!MyTcpServer::getInstance().resend(caTarName, noticePdu))
    {
        operateDB::getInstance().handleSaveOfflineMsg(m_loginName, strTarName, strNotice);
    }
    free(noticePdu);

    return makeBoolRespond(ENUM_TYPE_SHARE_FILE_RESPOND, true);
}
