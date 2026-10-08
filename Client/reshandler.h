#ifndef RESHANDLER_H
#define RESHANDLER_H

#include "protocol.h"
#include "chat.h"
#include "file.h"


class ResHandler
{
public:
    ResHandler();
    PDU*pdu;
    void login();
    void regist();
    void findUser();
    void onlineUser();
    void addFriend();
    void addFriendResend();
    void addFriendAgree();
    void flushFriend();
    void delFriend();
    void chat();
    void mkdir();
    void flushFile();
    void delDir();
    void renameFile();
    void moveFile();
    void uploadFile();
    void downloadFile();
    void shareFile();

    QList<File::FileInfo *> m_pFileInfoList;
};

#endif // RESHANDLER_H
