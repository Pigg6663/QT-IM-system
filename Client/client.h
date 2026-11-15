#ifndef CLIENT_H
#define CLIENT_H

#include "protocol.h"
#include "reshandler.h"

#include <QByteArray>
#include <QTcpSocket>
#include <QTimer>
#include <QWidget>

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

public slots:
     void showConnect();
     void recvMsg();
     void onSocketDisconnected();

private slots:
    // void on_sendPB_clicked();

     void on_login_PB_clicked();

     void on_pushButton_2_clicked();

private:
    Ui::Client *ui;
    QByteArray m_recvBuf;        // 接收缓冲区：处理半包与粘包
    QTimer m_heartbeatTimer;     // 心跳定时器
    Client(QWidget*parent=nullptr);
    Client(const Client& instance)=delete;
    Client& operator=(const Client&)=delete;
};
#endif // CLIENT_H
