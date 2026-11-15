#ifndef OPERATEDB_H
#define OPERATEDB_H

#include <QObject>
#include <QSqlDatabase>
#include <QStringList>

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

    // ---- 密码加盐哈希 ----
    // 生成 32 位 hex 随机盐（16 字节随机数）
    static QString makeSalt();
    // SHA-256(密码 + 盐) 的 64 位 hex 结果
    static QString hashPwd(const QString& strPwd, const QString& strSalt);

private:
    explicit operateDB(QObject *parent = nullptr);
    operateDB(const operateDB& instance)=delete;
    operateDB& operator=(const operateDB)=delete;
    QSqlDatabase m_db;
signals:

};

#endif // OPERATEDB_H
