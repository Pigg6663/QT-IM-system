#ifndef MYTCPSOCKET_H
#define MYTCPSOCKET_H

#include "msghandler.h"
#include "protocol.h"

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QTcpSocket>
#include <memory>

class QFile;
class QCryptographicHash;

class MyTcpSocket : public QTcpSocket
{
    Q_OBJECT

public:
    MyTcpSocket();
    ~MyTcpSocket();
    QString m_strLoginName;
    MsgHandler* m_pmh;
    void sendMsg(PDU*pdu);
    PDU* handleMsg(PDU* pdu);

    // 最近一次活跃时间（毫秒时间戳），服务端心跳超时巡检用
    qint64 lastActiveMs() const { return m_lastActiveMs; }

    // 登录成功后补发该用户的离线消息
    void pushOfflineMessages();

public slots:
    void recvMsg();
    void clineOffliine();

private slots:
    // 下载推流：写缓冲降下来后接着推下一批分片（反压）
    void pumpDownload();

private:
    // 握手应答发出后，如果 MsgHandler 记下了待推文件，就在这里启动推流
    void tryStartDownload();
    void stopDownload();

    QByteArray m_recvBuf;        // 接收缓冲区：处理半包与粘包
    qint64 m_lastActiveMs = 0;   // 最近一次活跃时间

    // ---- 下载推流状态：一条连接同一时刻只推一个文件 ----
    std::unique_ptr<QFile> m_downloadFile;
    std::unique_ptr<QCryptographicHash> m_downloadHash;  // 边读边算，收尾时给客户端校验
    qint64 m_downloadOffset = 0;   // 下一个要读的偏移
    qint64 m_downloadSize   = 0;   // 文件总长
};

#endif // MYTCPSOCKET_H
