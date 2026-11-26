#ifndef MSGHANDLER_H
#define MSGHANDLER_H

#include "protocol.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QPointer>
#include <QString>
#include <memory>

class QFile;
class MyTcpSocket;



class MsgHandler
{
public:

    struct FileInfo {
        char caName[32];
        int iFileType;
    };

    MsgHandler();
    PDU* pdu;
    // 当前连接已登录的用户名，由 MyTcpSocket::handleMsg 每次分发前同步
    QString m_loginName;

    // 记住拥有本处理器的连接。递归删除、跨用户拷贝这类慢操作交给线程池后，
    // 结果必须排队回到该连接所属的线程才能写 socket（QTcpSocket 有线程亲和性）。
    // 用 QPointer 是因为连接可能在这期间被断开销毁。
    void setSocket(MyTcpSocket* pSocket) { m_pSocket = pSocket; }
    QPointer<MyTcpSocket> socket() const { return m_pSocket; }

    PDU*regist();
    PDU* findUser();
    PDU* onlineUser();
    PDU* login(QString& strLoginName);
    PDU* addFriend();
    PDU* addFriendAgree();
    PDU* flushFriend();
    PDU* delFriend();
    PDU* chat();
    PDU* mkdir();
    PDU* flushFile();
    PDU* delDir();
    PDU* renameFile();
    PDU* moveFile();
    PDU* uploadFile();       // 上传握手：校验路径 + 返回断点位置
    PDU* uploadChunk();      // 上传分片：追加写 .part
    PDU* uploadFinish();     // 上传收尾：校验长度与 MD5 后转正
    PDU* downloadFile();     // 下载握手：校验路径 + 记录待推文件信息
    PDU* shareFile();

    // ---- 下载推流状态（由 MyTcpSocket 在应答发出后消费）----
    bool hasPendingDownload() const { return m_bDownloadPending; }
    QString takeDownloadPath();
    qint64 takeDownloadOffset();
    // 上传分片会话在出错/完成后复位
    void resetUploadSession();

private:
    // 判断客户端传入的路径是否严格落在当前用户网盘根目录内
    bool isSafePath(const QString& strPath) const;
    // 校验路径；不安全时把失败应答写到 outRespond 并返回 false
    bool isSafePathOrRespond(const QString& strPath,
                             ENUM_TYPE respondType,
                             PDU** outRespond) const;
    // 构造一个只带 bool 结果的应答包
    PDU* makeBoolRespond(ENUM_TYPE type, bool ret) const;

    // ---- 慢操作下沉线程池时需要的东西 ----
    QPointer<MyTcpSocket> m_pSocket;

    // 身份绑定校验：caData 前 32 字节里的"当前用户"必须与会话登录名一致，
    // 不一致说明客户端在冒充别人，直接拒绝。
    bool isOwnIdentity(const char* caClaimedName) const;

    // 身份校验失败时构造失败应答
    PDU* makeIdentityRejectRespond(ENUM_TYPE respondType) const;

    // ---- 上传分片会话（一条连接同时只处理一次上传）----
    QString  m_uploadPath;      // .part 文件的绝对路径
    qint64   m_uploadSize = 0;  // 客户端声明的文件总长
    qint64   m_uploadOffset = 0;// 已经收到并落盘的字节数
    QByteArray m_uploadMd5Hex;  // 客户端声明的整文件 MD5（hex）
    std::unique_ptr<QFile> m_uploadFile;               // 跨消息持有，避免每片重开
    std::unique_ptr<QCryptographicHash> m_uploadHash;  // 边收边算，避免收完再整读一遍

    // ---- 下载推流会话 ----
    bool    m_bDownloadPending = false;
    QString m_downloadPath;
    qint64  m_downloadOffset = 0;
};

#endif // MSGHANDLER_H
