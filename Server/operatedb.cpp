#include "operatedb.h"
#include<QCryptographicHash>
#include<QDebug>
#include<QRandomGenerator>
#include<QSqlError>
#include <QSqlQuery>

namespace {
// 统一把协议里的 const char* 参数转成 QString。
// 客户端发来的名字/密码是 toStdString() 的结果（UTF-8），必须用 fromUtf8，
// 用 fromLocal8Bit 会让中文用户名乱码。
inline QString toStr(const char* p)
{
    return QString::fromUtf8(p);
}

// 单次补发的离线消息条数上限：
// 避免某个用户积压上万条时，登录瞬间把整批数据塞进 socket 缓冲区。
constexpr int kMaxOfflineBatch = 200;
// message.content 列是 TEXT（上限 65535 字节），入库前先挡掉超长消息，
// 否则 MySQL 会整个 insert 失败、消息静默丢失，且错误日志被刷。
constexpr int kMaxOfflineContentBytes = 60000;
}

operateDB &operateDB::getInstance()
{
    static operateDB instance;
    return instance;
}

void operateDB::connectSQL()
{
    m_db.setHostName("localhost");
    m_db.setPort(3306);
    m_db.setUserName("root");
    m_db.setPassword("123456");
    m_db.setDatabaseName("mydb2501");
    if(m_db.open()){
        qDebug()<<"数据库连接成功";
    }
    else{
        qDebug()<<"数据库连接失败"<<m_db.lastError().text();
    }

}

operateDB::~operateDB()
{
    m_db.close();
}

QString operateDB::makeSalt()
{
    QByteArray raw(16, 0);
    for (int i = 0; i < 16; ++i)
    {
        // 用 QRandomGenerator::system()：由操作系统 CSPRNG 提供的密码学安全随机源。
        // 不要用 global()——它是可预测的伪随机数生成器，不适合生成盐。
        raw[i] = char(QRandomGenerator::system()->bounded(256));
    }
    return QString::fromLatin1(raw.toHex());      // 32 位 hex
}

QString operateDB::hashPwd(const QString& strPwd, const QString& strSalt)
{
    const QByteArray h = QCryptographicHash::hash((strPwd + strSalt).toUtf8(),
                                                 QCryptographicHash::Sha256);
    return QString::fromLatin1(h.toHex());        // 64 位 hex
}

bool operateDB::handleRegist(const char *caName, const char *caPwd)
{
    if(caName==NULL||caPwd==NULL)
    {
        return false;
    }
    const QString name = toStr(caName);
    const QString pwd  = toStr(caPwd);

    // 查重：预处理绑定，杜绝 SQL 注入
    QSqlQuery q;
    q.prepare("select id from user_info where name = :name");
    q.bindValue(":name", name);
    if(!q.exec())
    {
        qDebug()<<"注册查重失败"<<q.lastError().text();
        return false;
    }
    if(q.next())
    {
        return false;                     // 用户名已存在
    }

    // 加盐哈希后再入库，数据库里不再保存明文口令
    const QString salt = makeSalt();
    const QString hash = hashPwd(pwd, salt);

    QSqlQuery ins;
    ins.prepare("insert into user_info(name, pwd, salt) values(:name, :pwd, :salt)");
    ins.bindValue(":name", name);
    ins.bindValue(":pwd",  hash);
    ins.bindValue(":salt", salt);
    const bool ok = ins.exec();
    if(!ok)
    {
        qDebug()<<"注册插入失败"<<ins.lastError().text();
    }
    return ok;
}

bool operateDB::handleLogin(const char *caName, const char *caPwd)
{
    if(caName==NULL||caPwd==NULL)
    {
        return false;
    }
    const QString name = toStr(caName);
    const QString pwd  = toStr(caPwd);

    // 只取 id/pwd/salt，口令正确性在应用层用哈希比对
    QSqlQuery q;
    q.prepare("select id, pwd, salt from user_info where name = :name");
    q.bindValue(":name", name);
    if(!q.exec())
    {
        qDebug()<<"登录查询失败"<<q.lastError().text();
        return false;
    }
    if(!q.next())
    {
        return false;                     // 用户不存在
    }

    const QString storedHash = q.value(1).toString();
    const QString salt       = q.value(2).toString();

    if(salt.isEmpty())
    {
        // 兼容历史明文数据：salt 为空说明是旧记录，按明文比对，
        // 比对成功后立即升级为加盐哈希，避免继续保存明文。
        if(storedHash != pwd)
        {
            return false;
        }
        const QString newSalt = makeSalt();
        const QString newHash = hashPwd(pwd, newSalt);
        QSqlQuery up;
        up.prepare("update user_info set pwd = :pwd, salt = :salt where name = :name");
        up.bindValue(":pwd",  newHash);
        up.bindValue(":salt", newSalt);
        up.bindValue(":name", name);
        if(up.exec())
        {
            qDebug()<<"已把用户"<<name<<"的明文口令升级为加盐哈希";
        }
    }
    else if(hashPwd(pwd, salt) != storedHash)
    {
        return false;                     // 口令错误
    }

    QSqlQuery up;
    up.prepare("update user_info set online = 1 where name = :name");
    up.bindValue(":name", name);
    if(!up.exec())
    {
        qDebug()<<"置 online=1 失败"<<up.lastError().text();
        return false;
    }
    return true;
}

void operateDB::handleOffline(const char*caName)
{
    if(caName==NULL)
    {
        return;
    }
    QSqlQuery q;
    q.prepare("update user_info set online = 0 where name = :name");
    q.bindValue(":name", toStr(caName));
    const bool ret = q.exec();
    if(!ret)
    {
        qDebug()<<"置 online=0 失败"<<q.lastError().text();
    }
    qDebug()<<"handleOffline ret"<<ret;
}

int operateDB::handleFindUser(const char *caName)
{
    if(caName==NULL)
    {
        return -1;
    }
    QSqlQuery q;
    q.prepare("select online from user_info where name = :name");
    q.bindValue(":name", toStr(caName));
    if(!q.exec())
    {
        qDebug()<<"查找用户失败"<<q.lastError().text();
        return -1;
    }
    if(q.next())
    {
        return q.value(0).toInt();
    }
    return 2;
}

QStringList operateDB::handleOnlineUser()
{
    QStringList res;
    QSqlQuery q;
    if(!q.exec("select name from user_info where online = 1"))
    {
        qDebug()<<"查询在线用户失败"<<q.lastError().text();
        return res;
    }
    while (q.next())
    {
        res.append(q.value(0).toString());
    }
    return res;
}

int operateDB::handleAddFriend(const char *caCurName, const char *caTarName)
{
    if(caCurName==NULL||caTarName==NULL)
    {
        return -1;
    }
    const QString cur = toStr(caCurName);
    const QString tar = toStr(caTarName);

    // 判断是否已经是好友（好友关系无向，双向匹配）
    QSqlQuery q;
    q.prepare(R"(select * from friend where
                 (user_id     = (select id from user_info where name = :cur)
                  and friend_id = (select id from user_info where name = :tar))
                 or
                 (user_id     = (select id from user_info where name = :tar)
                  and friend_id = (select id from user_info where name = :cur)))");
    q.bindValue(":cur", cur);
    q.bindValue(":tar", tar);
    if(!q.exec())
    {
        qDebug()<<"判断好友关系失败"<<q.lastError().text();
        return -1;
    }
    if(q.next())
    {
        return -2;                        // 已经是好友
    }

    QSqlQuery q2;
    q2.prepare("select online from user_info where name = :tar");
    q2.bindValue(":tar", tar);
    if(!q2.exec())
    {
        qDebug()<<"查询对方在线状态失败"<<q2.lastError().text();
        return -1;
    }
    if(q2.next())
    {
        return q2.value(0).toInt();       // 0 不在线 / 1 在线
    }
    return -1;                            // 用户不存在
}

bool operateDB::handleAddFriendAGree(const char *caCurName, const char *caTarName)
{
    if(caCurName==NULL||caTarName==NULL)
    {
        return false;
    }
    // 依赖 friend 表的唯一约束 (user_id, friend_id)：
    // 双方同时互加也只会落一行，不会产生重复关系
    QSqlQuery q;
    q.prepare(R"(insert into friend(user_id, friend_id)
                 select u1.id, u2.id
                 from user_info u1, user_info u2
                 where u1.name = :cur and u2.name = :tar
                 limit 1)");
    q.bindValue(":cur", toStr(caCurName));
    q.bindValue(":tar", toStr(caTarName));
    const bool ok = q.exec();
    if(!ok)
    {
        qDebug()<<"插入好友关系失败"<<q.lastError().text();
    }
    return ok;
}

QStringList operateDB::handleFlushFriend(const char *caCurName)
{
    QStringList res;
    if(caCurName==NULL)
    {
        return res;
    }
    QSqlQuery q;
    q.prepare(R"(select name from user_info
                 where id in (
                     select user_id   from friend where
                         friend_id = (select id from user_info where name = :cur)
                     union
                     select friend_id from friend where
                         user_id   = (select id from user_info where name = :cur)
                 ) and online = 1)");
    q.bindValue(":cur", toStr(caCurName));
    if(!q.exec())
    {
        qDebug()<<"查询好友列表失败"<<q.lastError().text();
        return res;
    }
    while (q.next())
    {
        res.append(q.value(0).toString());
    }
    return res;
}

bool operateDB::handleDelFriend(const char *caCurName, const char *caTarName)
{
    if (caCurName == NULL || caTarName == NULL)
    {
        return false;
    }
    const QString cur = toStr(caCurName);
    const QString tar = toStr(caTarName);

    QSqlQuery q;
    q.prepare(R"(delete from friend where
                 (user_id     = (select id from user_info where name = :cur)
                  and friend_id = (select id from user_info where name = :tar))
                 or
                 (friend_id   = (select id from user_info where name = :cur)
                  and user_id   = (select id from user_info where name = :tar)))");
    q.bindValue(":cur", cur);
    q.bindValue(":tar", tar);
    const bool ok = q.exec();
    if(!ok)
    {
        qDebug()<<"删除好友失败"<<q.lastError().text();
    }
    return ok;
}

// ---------------------------------------------------------------
// 离线消息：对方不在线时先落库，等其登录后再补发。
// 只保证"不丢"，不保证严格一次投递：先查出未读、推送成功后才标记已读，
// 万一服务端在两者之间崩溃，用户会重复收到一条（可接受，优于丢消息）。
// ---------------------------------------------------------------
bool operateDB::handleSaveOfflineMsg(const QString& strSender,
                                     const QString& strReceiver,
                                     const QString& strContent)
{
    if (strSender.isEmpty() || strReceiver.isEmpty())
    {
        return false;
    }
    if (strContent.toUtf8().size() > kMaxOfflineContentBytes)
    {
        qDebug() << "离线消息超长，拒绝入库 长度" << strContent.toUtf8().size();
        return false;
    }

    QSqlQuery q;
    q.prepare("insert into `message`(`sender`, `receiver`, `content`) "
              "values(:sender, :receiver, :content)");
    q.bindValue(":sender",   strSender);
    q.bindValue(":receiver", strReceiver);
    q.bindValue(":content",  strContent);
    if (!q.exec())
    {
        qDebug() << "离线消息入库失败" << q.lastError().text();
        return false;
    }
    return true;
}

QList<operateDB::OfflineMsg> operateDB::handleTakeOfflineMsgs(const QString& strReceiver)
{
    QList<OfflineMsg> res;
    if (strReceiver.isEmpty())
    {
        return res;
    }

    QSqlQuery q;
    // limit 直接写常量：部分 MySQL 驱动对绑定到 LIMIT 的参数支持不一致
    q.prepare(QString("select `id`, `sender`, `content` from `message` "
                      "where `receiver` = :receiver and `is_read` = 0 "
                      "order by `id` asc limit %1").arg(kMaxOfflineBatch));
    q.bindValue(":receiver", strReceiver);
    if (!q.exec())
    {
        qDebug() << "查询离线消息失败" << q.lastError().text();
        return res;
    }

    QList<qint64> ids;
    while (q.next())
    {
        OfflineMsg msg;
        ids.append(q.value(0).toLongLong());
        msg.strSender  = q.value(1).toString();
        msg.strContent = q.value(2).toString();
        res.append(msg);
    }

    if (ids.isEmpty())
    {
        return res;
    }

    // 批量标记已读：占位符全部由代码生成，不拼用户数据
    QStringList placeholders;
    for (int i = 0; i < ids.size(); ++i)
    {
        placeholders << QString(":id%1").arg(i);
    }
    QSqlQuery up;
    up.prepare(QString("update `message` set `is_read` = 1 where `id` in (%1)")
               .arg(placeholders.join(",")));
    for (int i = 0; i < ids.size(); ++i)
    {
        up.bindValue(placeholders[i], ids[i]);
    }
    if (!up.exec())
    {
        qDebug() << "标记离线消息已读失败" << up.lastError().text();
    }

    qDebug() << "取出离线消息" << res.size() << "条 用户" << strReceiver;
    return res;
}

operateDB::operateDB(QObject *parent) : QObject(parent)
{
    m_db=QSqlDatabase::addDatabase("QMYSQL");
}
