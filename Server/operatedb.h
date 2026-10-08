#ifndef OPERATEDB_H
#define OPERATEDB_H

#include <QObject>
#include <QSqlDatabase>

class operateDB : public QObject
{
    Q_OBJECT
public:
    static operateDB& getInstance();
    void connectSQL();
    ~operateDB();
    bool handleRegist(const char*caName,const char*caPwd);
    bool handleLogin(const char*caName,const char*caPwd);
    void handleOffline(const char*caName);
    int handleFindUser(const char*caName);

    QStringList handleOnlineUser();
    int handleAddFriend(const char* caCurName,const char* caTarName);
    bool handleAddFriendAGree(const char* caCurName,const char* caTarName);
    QStringList handleFlushFriend(const char*caName);
    bool handleDelFriend(const char *caCurName,const char *caTarName);
private:
    explicit operateDB(QObject *parent = nullptr);
    operateDB(const operateDB& instance)=delete;
    operateDB& operator=(const operateDB)=delete;
    QSqlDatabase m_db;
signals:

};

#endif // OPERATEDB_H
