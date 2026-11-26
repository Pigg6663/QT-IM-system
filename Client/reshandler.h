#ifndef RESHANDLER_H
#define RESHANDLER_H

#include "protocol.h"
#include "chat.h"
#include "file.h"

#include <QCryptographicHash>
#include <QString>
#include <memory>

class QFile;


class ResHandler
{
public:
    ResHandler();
    PDU*pdu;
    void login();
    void regist();
    void findUser();
    void onlineUser();
    void addFriend();
    void addFriendResend();
    void addFriendAgree();
    void flushFriend();
    void delFriend();
    void chat();
    void offlineMsg();
    void mkdir();
    void flushFile();
    void delDir();
    void renameFile();
    void moveFile();
    void uploadFile();       // 上传握手应答：拿到断点后开始推分片
    void downloadFile();     // 下载握手应答：打开本地文件，准备接分片
    void downloadChunk();    // 下载分片：按偏移写入并增量算 MD5
    void downloadFinish();   // 下载收尾：比对 MD5
    void shareFile();

    QList<File::FileInfo *> m_pFileInfoList;

    // 下载请求发出前由 File 页写入：本地目标保存路径
    QString m_downloadSavePath;

private:
    // ---- 下载接收状态：同一时刻只接一个文件 ----
    std::unique_ptr<QFile> m_downloadFile;
    std::unique_ptr<QCryptographicHash> m_downloadHash;
    qint64 m_downloadOffset = 0;   // 已写入的字节数
    qint64 m_downloadTotal  = 0;   // 服务端声明的文件总长
    void abortDownload();
};

#endif // RESHANDLER_H
