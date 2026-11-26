#ifndef PROTOCOL_H
#define PROTOCOL_H

// 只用 qint64 这类跨平台定长类型；协议本身与 Qt 无关，
// 但两端都链接 Qt，为避免 int64 在 32/64 位下的歧义，直接借用 QtGlobal
#include <QtGlobal>

typedef unsigned int uint;

enum ENUM_TYPE{
    ENUM_TYPE_MIN=0,
    ENUM_TYPE_REGIST_REQUEST,
    ENUM_TYPE_REGIST_RESPOND,
    ENUM_TYPE_LOGIN_REQUEST,
    ENUM_TYPE_LOGIN_RESPOND,
    ENUM_TYPE_FIND_USER_REQUEST,
    ENUM_TYPE_FIND_USER_RESPOND,
    ENUM_TYPE_ONLINEUSER_REQUEST,
    ENUM_TYPE_ONLINEUSER_RESPOND,
    ENUM_TYPE_ADD_FRIEND_REQUEST,
    ENUM_TYPE_ADD_FRIEND_RESPOND,
    ENUM_TYPE_ADD_FRIEND_RESEND,
    ENUM_TYPE_ADD_FRIEND_AGREE_REQUEST,
    ENUM_TYPE_ADD_FRIEND_AGREE_RESPOND,
    ENUM_TYPE_FLUSH_FRIEND_REQUEST,
    ENUM_TYPE_FLUSH_FRIEND_RESPOND,
    ENUM_TYPE_DEL_FRIEND_REQUEST,
    ENUM_TYPE_DEL_FRIEND_RESPOND,
    ENUM_TYPE_CHAT_REQUEST,
    ENUM_TYPE_CHAT_RESPOND,
    ENUM_TYPE_CHAT_RESEND,
    ENUM_TYPE_MKDIR_REQUEST,
    ENUM_TYPE_MKDIR_RESPOND,
    ENUM_TYPE_FLUSH_FILE_REQUEST,
    ENUM_TYPE_FLUSH_FILE_RESPOND,
    ENUM_TYPE_DEL_DIR_REQUEST,
    ENUM_TYPE_DEL_DIR_RESPOND,
    ENUM_TYPE_RENAME_FILE_REQUEST,
    ENUM_TYPE_RENAME_FILE_RESPOND,
    ENUM_TYPE_MOVE_FILE_REQUEST,
    ENUM_TYPE_MOVE_FILE_RESPOND,
    ENUM_TYPE_UPLOAD_FILE_REQUEST,
    ENUM_TYPE_UPLOAD_FILE_RESPOND,
    ENUM_TYPE_DOWNLOAD_FILE_REQUEST,
    ENUM_TYPE_DOWNLOAD_FILE_RESPOND,
    ENUM_TYPE_SHARE_FILE_REQUEST,
    ENUM_TYPE_SHARE_FILE_RESPOND,
    // ---- 深化阶段新增：必须追加在末尾，保证既有消息数值不变 ----
    // 两份 protocol.h 的枚举顺序必须完全一致，协议靠数值传输
    ENUM_TYPE_HEARTBEAT_REQUEST,
    ENUM_TYPE_HEARTBEAT_RESPOND,
    ENUM_TYPE_KICKED_RESEND,
    // 离线消息补发：客户端登录成功后，服务端把落库的未读消息逐条推下来
    // caData = [发送者, 接收者]，caMsg = 消息内容
    ENUM_TYPE_OFFLINE_MSG_RESEND,
    // ---- 第 7 项：大文件分片传输（上传 / 下载）----
    // 上传：UPLOAD_FILE_REQUEST 作握手（带总长与 MD5），随后一串 CHUNK，
    //       最后 FINISH 由服务端校验并落盘
    ENUM_TYPE_UPLOAD_FILE_CHUNK_REQUEST,
    ENUM_TYPE_UPLOAD_FILE_FINISH_REQUEST,
    ENUM_TYPE_UPLOAD_FILE_FINISH_RESPOND,
    // 下载：DOWNLOAD_FILE_REQUEST 作握手，服务端回 UPLOAD 之外的一串 CHUNK，
    //       最后 FINISH 带整文件 MD5 供客户端校验
    ENUM_TYPE_DOWNLOAD_FILE_CHUNK_RESPOND,
    ENUM_TYPE_DOWNLOAD_FILE_FINISH_RESPOND,
    ENUM_TYPE_MAX=99999,
};

struct PDU{
    uint uiTotalLen;
    uint uiMsgLen;
    uint uiType;
    char caData[64];
    char caMsg[];
};

// ---- 协议常量（两端必须一致）----
// caMsg[] 是 C99 柔性数组，sizeof 计算时占 0 字节，故头部固定 76 = 3*4 + 64
constexpr unsigned int PDU_HEADER_LEN    = 76;
constexpr unsigned int PDU_MAX_TOTAL_LEN = 8u * 1024u * 1024u;  // 单包上限 8MB

// ---- 分片传输参数（两端必须一致）----
// 分片大小 256KB：远小于单包上限，内存占用可控；又不太小，避免包数过多
constexpr unsigned int FILE_CHUNK_SIZE = 256u * 1024u;
// 发送端写缓冲高水位：排队字节超过它就暂停推分片，靠 bytesWritten 续推（反压）
constexpr qint64 SOCKET_WRITE_HIGH_WATER = 1024 * 1024;
// MD5 十六进制串长度
constexpr int MD5_HEX_LEN = 32;
// MD5 原始值长度
constexpr int MD5_RAW_LEN = 16;

// caData 各字段偏移（caData 固定 64 字节，布局随消息类型不同而不同）
// 上传握手 UPLOAD_FILE_REQUEST：[0:32] 文件名 | [32:40] uint64 总长 | [40:56] 16 字节原始 MD5
constexpr int UPLOAD_SIZE_POS   = 32;
constexpr int UPLOAD_MD5_POS    = 40;
// 上传握手应答 UPLOAD_FILE_RESPOND：[0:8] uint64 断点位置 | [8] bool ok
constexpr int UPLOAD_RESUME_POS = 0;
constexpr int UPLOAD_OK_POS     = 8;
// 上传分片 UPLOAD_FILE_CHUNK_REQUEST：[0:8] uint64 偏移（caMsg 为数据）
constexpr int CHUNK_OFFSET_POS  = 0;
// 下载请求 DOWNLOAD_FILE_REQUEST：[0:32] 文件名 | [32:40] uint64 客户端已有大小
constexpr int DL_RESUME_POS     = 32;
// 下载握手应答 DOWNLOAD_FILE_RESPOND：[0] bool ok | [1:9] uint64 起始偏移 | [9:17] uint64 总长
constexpr int DL_RESP_OK_POS    = 0;
constexpr int DL_RESP_START_POS = 1;
constexpr int DL_RESP_SIZE_POS  = 9;
// 下载完成应答 DOWNLOAD_FILE_FINISH_RESPOND：[0] bool ok | [1:17] 16 字节原始 MD5
constexpr int DL_FINISH_MD5_POS = 1;

// 编译期锁定头部布局：把"依赖编译器假设"变成"编译期强制约束"
static_assert(sizeof(PDU) == 76, "PDU header layout broken");

PDU *mkPDU(uint uiMsgLen=0);



#endif // PROTOCOL_H
