#ifndef CLIENT_H
#define CLIENT_H

#include "protocol.h"
#include "reshandler.h"

#include <QTcpSocket>
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
    PDU* readMsg();
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

private slots:
    // void on_sendPB_clicked();

     void on_login_PB_clicked();

     void on_pushButton_2_clicked();

private:
    Ui::Client *ui;
    Client(QWidget*parent=nullptr);
    Client(const Client& instance)=delete;
    Client& operator=(const Client&)=delete;
};
#endif // CLIENT_H
