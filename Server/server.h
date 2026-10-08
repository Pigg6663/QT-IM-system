#ifndef SERVER_H
#define SERVER_H

#include <QWidget>

class Server : public QWidget
{
    Q_OBJECT

public:
    Server(QWidget *parent = nullptr);
    ~Server();
    void loadConfig();
    QString m_strIP;
    quint16 m_usPort;
    static Server& getInstance() {
           static Server instance; // 局部静态变量实现单例
           return instance;
       }
        QString m_strRootPath;
};
#endif // SERVER_H
