#ifndef MSGHANDLER_H
#define MSGHANDLER_H

#include "protocol.h"

#include <QString>



class MsgHandler
{
public:

    struct FileInfo {
        char caName[32];
        int iFileType;
    };

    MsgHandler();
    PDU* pdu;
    PDU*regist();
    PDU* findUser();
    PDU* onlineUser();
    PDU* login(QString& strLoginName);
    PDU* addFriend();
    PDU* addFriendAgree();
    PDU* flushFriend();
    PDU* delFriend();
    PDU* chat();
    PDU* mkdir();
    PDU* flushFile();
    PDU* delDir();
    PDU* renameFile();
    PDU* moveFile();
    PDU* uploadFile();
    PDU* downloadFile();
    PDU* shareFile();
};

#endif // MSGHANDLER_H
