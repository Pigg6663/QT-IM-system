#ifndef OPERATEDB_H
#define OPERATEDB_H

#include <QList>
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

class operateDB : public QObject
{
    Q_OBJECT
public:
    // 离线消息：对方不在线时落库，其登录后补发
    struct OfflineMsg {
        QString strSender;
        QString strContent;
    };

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

    // ---- 离线消息 ----
    // 保存一条待投递消息
    bool handleSaveOfflineMsg(const QString& strSender,
                              const QString& strReceiver,
                              const QString& strContent);
    // 取出并标记已读（按时间正序，一次最多 kMaxOfflineBatch 条）
    QList<OfflineMsg> handleTakeOfflineMsgs(const QString& strReceiver);

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
