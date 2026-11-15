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
    // 当前连接已登录的用户名，由 MyTcpSocket::handleMsg 每次分发前同步
    QString m_loginName;

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

private:
    // 判断客户端传入的路径是否严格落在当前用户网盘根目录内
    bool isSafePath(const QString& strPath) const;
    // 校验路径；不安全时把失败应答写到 outRespond 并返回 false
    bool isSafePathOrRespond(const QString& strPath,
                             ENUM_TYPE respondType,
                             PDU** outRespond) const;
    // 构造一个只带 bool 结果的应答包
    PDU* makeBoolRespond(ENUM_TYPE type, bool ret) const;
};

#endif // MSGHANDLER_H
