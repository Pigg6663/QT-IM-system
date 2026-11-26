#ifndef CLIENT_H
#define CLIENT_H

#include "protocol.h"
#include "reshandler.h"

#include <QByteArray>
#include <QTcpSocket>
#include <QTimer>
#include <QWidget>

#include <memory>

class QFile;

QT_BEGIN_NAMESPACE
namespace Ui { class Client; }
QT_END_NAMESPACE

class Client : public QWidget
{
    Q_OBJECT

public:
    ~Client();
    void loadConfig();
    static Client& getInstance();
    void sendMsg(PDU*pdu);
    void handleMsg(PDU*pdu);
     QString m_strIP;
     quint16 m_usPort;
     QTcpSocket m_socket;
     QString m_strLoginName;
     ResHandler* m_prh;
     QString m_strRootPath;

     // ---- 第 7 项：分片上传状态机 ----
     // 握手应答回来后由 ResHandler 调用 continueUpload 才开始真正推数据
     void beginUpload(QFile* pFile, qint64 llTotal, const QByteArray& md5Hex);
     void continueUpload(qint64 llResume);
     void abortUpload();

public slots:
     void showConnect();
     void recvMsg();
     void onSocketDisconnected();
     void pumpUpload();           // 由 QTcpSocket::bytesWritten 驱动，按写缓冲余量推分片

private slots:
    // void on_sendPB_clicked();

     void on_login_PB_clicked();

     void on_pushButton_2_clicked();

private:
    Ui::Client *ui;
    QByteArray m_recvBuf;        // 接收缓冲区：处理半包与粘包
    QTimer m_heartbeatTimer;     // 心跳定时器

    // ---- 第 7 项：分片上传状态机 ----
    std::unique_ptr<QFile> m_uploadFile;
    qint64 m_uploadTotal  = 0;
    qint64 m_uploadOffset = 0;
    QByteArray m_uploadMd5Hex;

    Client(QWidget*parent=nullptr);
    Client(const Client& instance)=delete;
    Client& operator=(const Client&)=delete;
};
#endif // CLIENT_H
