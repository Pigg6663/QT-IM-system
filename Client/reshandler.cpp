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
    // caData = [发送者, 接收者]，取前半段作为会话对象
    char caSender[32] = {'\0'};
    memcpy(caSender, pdu->caData, 32);
    const QString strSender = QString::fromUtf8(caSender);

    // 按发送者取对应窗口，而不是复用同一个全局窗口：
    // 原来会在每次收到消息时把 m_strChatName 改成发送者，
    // 导致 A、B 的消息混在一个窗口里，回复还可能发错人。
    Chat* pChat = Index::getInstance().getFriend()->appendIncoming(
                strSender,
                QString("%1 : %2").arg(strSender).arg(QString::fromUtf8(pdu->caMsg)));
    if (!pChat) {
        qDebug() << "chat: 发送者名为空，无法定位会话窗口";
    }
}

void ResHandler::offlineMsg()
{
    // 服务端在登录成功后补发积压的离线消息：
    // caData = [发送者, 接收者]，caMsg = 消息内容
    char caSender[32] = {'\0'};
    memcpy(caSender, pdu->caData, 32);
    const QString strSender = QString::fromUtf8(caSender);
    if (strSender.isEmpty()) {
        qDebug() << "offlineMsg: 发送者为空，丢弃";
        return;
    }

    // 不弹窗：刚登录就为每个发送者弹一个窗口太打扰，
    // 只把内容写进对应会话，用户点开聊天窗口即可看到。
    const QString strContent = QString::fromUtf8(pdu->caMsg);
    Chat* pChat = Index::getInstance().getFriend()->appendIncoming(
                strSender,
                QString("[离线] %1 : %2").arg(strSender).arg(strContent),
                false);
    if (!pChat) {
        qDebug() << "offlineMsg: 无法定位会话窗口" << strSender;
    }
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


// ---------------------------------------------------------------
// 第 7 项：分片传输 —— 上传握手应答
// 客户端先发 UPLOAD_FILE_REQUEST（文件名 + 总长 + 整文件 MD5），
// 服务端回"能从哪个字节开始"（断点），然后客户端才推分片。
// ---------------------------------------------------------------
void ResHandler::uploadFile()
{
    qint64 llResume = 0;
    bool ok = false;
    memcpy(&llResume, pdu->caData + UPLOAD_RESUME_POS, sizeof(qint64));
    memcpy(&ok, pdu->caData + UPLOAD_OK_POS, sizeof(bool));
    qDebug() << "uploadFile 握手 ok:" << ok << "断点:" << llResume;

    if (!ok)
    {
        Client::getInstance().abortUpload();
        QMessageBox::information(&Index::getInstance(), "提示", "上传失败：服务端无法创建文件");
        return;
    }

    if (llResume > 0)
    {
        QMessageBox::information(&Index::getInstance(), "提示",
                                 QString("检测到未完成的上传，从 %1 字节处续传").arg(llResume));
    }
    // 真正的"上传成功/失败"在 UPLOAD_FILE_FINISH_RESPOND 里
    Client::getInstance().continueUpload(llResume);
}

// ---------------------------------------------------------------
// 第 7 项：下载握手应答 —— 打开本地文件，准备接分片
// ---------------------------------------------------------------
void ResHandler::downloadFile()
{
    bool ok = false;
    qint64 llStart = 0;
    qint64 llSize  = 0;
    memcpy(&ok, pdu->caData + DL_RESP_OK_POS, sizeof(bool));
    memcpy(&llStart, pdu->caData + DL_RESP_START_POS, sizeof(qint64));
    memcpy(&llSize, pdu->caData + DL_RESP_SIZE_POS, sizeof(qint64));
    qDebug() << "downloadFile 握手 ok:" << ok << "起始:" << llStart << "总长:" << llSize;

    abortDownload();
    if (!ok)
    {
        QMessageBox::information(&Index::getInstance(), "提示", "下载失败：文件不存在");
        return;
    }
    if (m_downloadSavePath.isEmpty())
    {
        QMessageBox::information(&Index::getInstance(), "提示", "下载失败：没有指定保存位置");
        return;
    }

    m_downloadFile.reset(new QFile(m_downloadSavePath));
    if (!m_downloadFile->open(QIODevice::ReadWrite))
    {
        QMessageBox::information(&Index::getInstance(), "提示",
                                 QString("下载失败：无法保存文件 %1").arg(m_downloadSavePath));
        abortDownload();
        return;
    }

    // 服务端可能不认可我们提议的断点（本地那份比服务端还大），以服务端为准
    m_downloadFile->resize(llStart);
    m_downloadFile->seek(llStart);
    m_downloadOffset = llStart;
    m_downloadTotal  = llSize;
    m_downloadHash.reset(new QCryptographicHash(QCryptographicHash::Md5));

    // 续传：先把已有的前缀读一遍喂进哈希，收尾时才能和整文件 MD5 对上
    if (llStart > 0)
    {
        m_downloadFile->seek(0);
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
        m_downloadFile->seek(llStart);
    }
}

void ResHandler::downloadChunk()
{
    if (!m_downloadFile || !m_downloadFile->isOpen())
    {
        return;
    }

    qint64 llOffset = 0;
    memcpy(&llOffset, pdu->caData + CHUNK_OFFSET_POS, sizeof(qint64));
    if (llOffset != m_downloadOffset)
    {
        qWarning() << "下载分片偏移不连续，期望" << m_downloadOffset << "实际" << llOffset;
        abortDownload();
        QMessageBox::information(&Index::getInstance(), "提示", "下载中断：数据错位，请重新下载");
        return;
    }

    if (m_downloadFile->write(pdu->caMsg, pdu->uiMsgLen) != qint64(pdu->uiMsgLen))
    {
        abortDownload();
        QMessageBox::information(&Index::getInstance(), "提示", "下载失败：写入本地文件出错");
        return;
    }
    m_downloadHash->addData(pdu->caMsg, int(pdu->uiMsgLen));
    m_downloadOffset += pdu->uiMsgLen;
}

void ResHandler::downloadFinish()
{
    if (!m_downloadFile || !m_downloadFile->isOpen())
    {
        return;
    }
    m_downloadFile->close();

    bool ok = false;
    memcpy(&ok, pdu->caData + DL_RESP_OK_POS, sizeof(bool));
    const QByteArray expectMd5(pdu->caData + DL_FINISH_MD5_POS, MD5_RAW_LEN);
    const QByteArray actualMd5 = m_downloadHash ? m_downloadHash->result() : QByteArray();
    const bool sizeOk = (m_downloadOffset == m_downloadTotal);
    const bool md5Ok  = (actualMd5 == expectMd5);
    const QString strPath = m_downloadFile->fileName();

    m_downloadFile.reset();
    m_downloadHash.reset();
    m_downloadOffset = 0;
    m_downloadTotal  = 0;

    if (ok && sizeOk && md5Ok)
    {
        QMessageBox::information(&Index::getInstance(), "提示", "下载成功");
        return;
    }

    // 校验不过的残file留着没意义，还会污染下次续传
    QFile::remove(strPath);
    QMessageBox::information(&Index::getInstance(), "提示",
                             QString("下载失败：文件校验不通过（长度%1，MD5%2）")
                                 .arg(sizeOk ? "正确" : "不符")
                                 .arg(md5Ok ? "正确" : "不符"));
}

void ResHandler::abortDownload()
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
    m_downloadTotal  = 0;
}

// ---------------------------------------------------------------
// 分享：服务端负责把文件复制进接收方网盘，客户端只等一个 bool。
// 接收方收到的是 CHAT_RESEND 系统通知，不再由客户端写本地文件。
// ---------------------------------------------------------------
void ResHandler::shareFile()
{
    bool ret;
    memcpy(&ret, pdu->caData, sizeof(bool));
    qDebug() << "shareFile ret" << ret;
    if (ret)
    {
        QMessageBox::information(&Index::getInstance(), "提示", "分享成功，对方网盘中已生成副本");
    }
    else
    {
        QMessageBox::information(&Index::getInstance(), "提示", "分享失败：目标用户不存在或文件无法复制");
    }
}
