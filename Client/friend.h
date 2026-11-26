#ifndef FRIEND_H
#define FRIEND_H

#include "chat.h"
#include "onlineuser.h"

#include <QMap>
#include <QString>
#include <QWidget>

namespace Ui {
class Friend;
}

class Friend : public QWidget
{
    Q_OBJECT

public:
    explicit Friend(QWidget *parent = nullptr);
    ~Friend();
    OnlineUser* m_pOnlineUser;

    // 按好友名维护聊天窗口：每个会话一个独立窗口，
    // 窗口自己的 m_strChatName 只在该会话创建时赋值一次，不再被全局覆盖，
    // 因此回复不会发错人。
    Chat* openChat(const QString& strPeerName);
    // 收到消息（在线或离线补发）时调用：取窗口、追加内容、显示
    Chat* appendIncoming(const QString& strPeerName, const QString& strContent);

    void flushFriend();
    void update_LW(QStringList friendList);
private slots:
    void on_findUser_clicked();

    void on_online_PB_clicked();

    void on_flush_PB_clicked();


    void on_del_PB_clicked();
    
    void on_chat_PB_clicked();

private:

    Ui::Friend *ui;
    QMap<QString, Chat*> m_chatMap;   // 好友名 -> 聊天窗口
};

#endif // FRIEND_H
