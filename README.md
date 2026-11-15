# QT-IM-system

基于 **Qt5 + TCP 自定义二进制协议 + MySQL** 的即时通讯与文件传输系统（C/S 架构）。

客户端提供用户注册 / 登录、好友管理、在线聊天、**个人网盘**（上传、下载、分享、重命名、移动、删除）等功能；
服务端基于 `QTcpServer` 事件驱动地处理请求、访问数据库、转发消息，并直接管理每个用户的网盘目录。

在基础功能之上完成了三项工程化深化：**健壮的收包状态机**（粘包 / 半包 / 非法长度）、
**连接保活与重复登录互踢**（心跳 + 超时巡检）、**安全加固**（SQL 预处理、口令加盐哈希、网盘路径越权防护）。
所有修复均配有可复现的自动化测试脚本，见 [`tests/`](tests)。

> 本项目为个人学习实践项目，完整覆盖了 网络编程、自定义协议设计、数据库设计、Qt 信号槽与单例模式 等后端 / 客户端核心知识点。

---

## 一、技术栈

| 分类 | 技术 |
|---|---|
| 语言 / 标准 | C++11 |
| 框架 | Qt5（Core、Gui、Network、Sql、Widgets） |
| 网络 | TCP（`QTcpServer` / `QTcpSocket`），自定义二进制协议（PDU），收包状态机 |
| 连接管理 | 客户端心跳 + 服务端超时巡检 + 重复登录互踢 |
| 安全 | `QSqlQuery::prepare()` 预处理绑定、SHA-256 加盐口令、网盘根目录路径约束 |
| 数据库 | MySQL + QMYSQL 驱动（`QSqlDatabase` / `QSqlQuery`） |
| 构建 | qmake（Qt Creator 工程） |
| 测试 | PowerShell 协议测试脚本（手工构造 PDU 字节流） |
| UI | Qt Designer（`.ui` 文件）+ QStackedWidget 多页面 |

---

## 二、目录结构

```
.
├── Client/                     # 客户端工程
│   ├── Client.pro
│   ├── main.cpp
│   ├── client.{h,cpp}          # 单例：登录/注册窗口，持有 QTcpSocket，心跳定时器
│   ├── index.{h,cpp,ui}        # 单例：登录后主界面（QStackedWidget 两页）
│   ├── friend.{h,cpp,ui}       # 好友页：查找/刷新/删除好友、发起聊天
│   ├── onlineuser.{h,cpp,ui}   # 在线用户列表，双击发起加好友
│   ├── chat.{h,cpp,ui}         # 聊天窗口
│   ├── file.{h,cpp,ui}         # 网盘页：目录浏览与全部文件操作
│   ├── reshandler.{h,cpp}      # 响应分发层：处理所有 RESPOND / RESEND
│   ├── protocol.{h,cpp}        # 协议定义（与服务端逐字节一致）
│   └── connect.config.txt      # 服务端 IP / 端口（编译进 qrc 资源）
│
├── Server/                     # 服务端工程
│   ├── Server.pro
│   ├── main.cpp
│   ├── server.{h,cpp}          # 单例：读取配置、启动监听、网盘根目录
│   ├── mytcpserver.{h,cpp}     # 单例：管理连接、消息转发、心跳巡检、互踢
│   ├── mytcpsocket.{h,cpp}     # 每个连接一个：收包状态机 -> 分发 -> 回包
│   ├── msghandler.{h,cpp}      # 业务逻辑层：20 个业务处理函数 + 路径越权校验
│   ├── operatedb.{h,cpp}       # 单例：预处理绑定的数据库访问 + 口令加盐哈希
│   ├── protocol.{h,cpp}        # 协议定义（与客户端逐字节一致）
│   └── connect.config.txt      # 监听 IP / 端口（必须 CRLF 换行，编译进 qrc）
│
├── sql/
│   ├── init.sql                # 建库建表（含 salt 列、好友唯一约束）
│   └── migrate_v2.sql          # 旧库升级脚本（加盐哈希 + 唯一约束 + 去重）
│
├── tests/
│   ├── build.ps1               # 一键构建两端，失败即中断
│   ├── test_protocol.ps1       # 粘包 / 半包 / 非法包长
│   ├── test_heartbeat.ps1      # 心跳 / 重复登录互踢 / 超时回收
│   └── test_security.ps1       # SQL 注入 / 口令哈希 / 路径越权
│
└── 项目总结.md                  # 面试复习文档（架构 / 考点 / 改进方向）
```

---

## 三、系统架构

```
┌────────────────────────┐            ┌──────────────────────────────────┐
│        客户端 Client     │    TCP     │           服务端 Server           │
│                        │  ───────▶  │                                  │
│  Client（登录 / 注册）   │   PDU 包   │  MyTcpServer  (QTcpServer)       │
│   └ Index（主界面）      │            │   └ MyTcpSocket × N（每连接一个）  │
│      ├ Friend（好友页）  │  ◀───────  │      └ MsgHandler（业务逻辑）      │
│      ├ File（网盘页）    │  响应 / 转发 │         └ operateDB（单例）       │
│      ├ Chat（聊天窗口）  │            └───────────────┬──────────────────┘
│      └ OnlineUser       │                            │
│   └ ResHandler（响应分发）│                  ┌─────────▼─────────┐
│      QTcpSocket         │                  │  MySQL (3306)     │
└────────────────────────┘                  │  mydb2501         │
                                            └───────────────────┘
                                            ┌───────────────────┐
                                            │ ./userdata/<用户名>/ │
                                            │   个人网盘目录       │
                                            └───────────────────┘
```

设计要点：

- **请求 - 响应模型**：客户端发送 `*_REQUEST`，服务端处理后回 `*_RESPOND`；服务端主动转发的消息使用 `*_RESEND` 类型。
- **服务端不保存业务状态**：每个 `MyTcpSocket` 只记录一个 `m_strLoginName`，在线状态持久化在数据库 `online` 字段中。
- **单线程事件循环**：服务端无独立界面，完全依靠 `QApplication` 事件循环驱动 `readyRead` 信号处理并发请求。

---

## 四、通信协议设计

两端各自维护一份 `protocol.h` / `protocol.cpp`，定义必须严格一致。

### 1. PDU 数据包结构

```cpp
struct PDU {
    uint uiTotalLen;   // 整个数据包的总长度（含头部）
    uint uiMsgLen;     // caMsg 变长数据区长度
    uint uiType;       // 消息类型（枚举）
    char caData[64];   // 定长参数区：前 32 字节 + 后 32 字节（放两个字符串）
    char caMsg[];      // 柔性数组，变长数据区
};
// 头部固定 76 字节 = 3 × 4 + 64
```

分配时使用 `malloc` 一次性申请「头部 + 数据区」的连续内存，柔性数组 `caMsg` 紧跟在头部之后；遵循**谁创建谁释放**原则（`free`）。

```cpp
PDU* mkPDU(uint uiMsgLen) {
    uint uiTotalLen = uiMsgLen + sizeof(PDU);
    PDU* pdu = (PDU*)malloc(uiTotalLen);
    if (pdu == 0) exit(1);
    memset(pdu, 0, uiTotalLen);
    pdu->uiTotalLen = uiTotalLen;
    pdu->uiMsgLen   = uiMsgLen;
    return pdu;
}
```

### 2. 消息类型

共 **18 组业务消息**（`REQUEST` / `RESPOND` 成对），另有 2 个服务端转发类型和 1 个强制下线通知：

| 分类 | 消息 |
|---|---|
| 账号 | 注册、登录 |
| 好友 | 查找用户、在线用户列表、加好友、同意加好友、刷新好友、删除好友 |
| 聊天 | 聊天 |
| 网盘 | 新建文件夹、刷新文件列表、删除文件/文件夹、重命名、移动文件、上传、下载、分享文件 |
| 保活 | 心跳（`HEARTBEAT_REQUEST` / `HEARTBEAT_RESPOND`） |
| 服务端主动下发 | `ADD_FRIEND_RESEND`（加好友请求转发）、`CHAT_RESEND`（聊天消息转发）、`KICKED_RESEND`（重复登录踢线通知） |

> ⚠️ **枚举只能追加在 `ENUM_TYPE_MAX` 之前（即末尾）**。协议靠数值传输，
> 若把新类型插到中间，后面所有类型的数值都会整体后移，新旧客户端直接对不上。
> 两份 `protocol.h` 的枚举顺序必须完全一致。

### 3. 拆包方式（解决 TCP 粘包 / 半包）

包头记录总长度，接收端维护**接收缓冲区**，循环判断「头部长度 + 包体长度」是否收齐后再解析。
完整实现见第八节 8.1，核心思路：

```cpp
m_recvBuf.append(this->readAll());                        // 累积字节流
while (true) {
    if (m_recvBuf.size() < 4) break;                      // 半包：长度字段都不够
    uint uiPDULen = 0;
    memcpy(&uiPDULen, m_recvBuf.constData(), 4);

    if (uiPDULen < PDU_HEADER_LEN ||                      // 非法：下溢 / 超限 -> 断开
        uiPDULen > PDU_MAX_TOTAL_LEN) { /* disconnect */ }

    if (m_recvBuf.size() < int(uiPDULen)) break;          // 半包：等下一次 readyRead
    PDU* pdu = mkPDU(uiPDULen - PDU_HEADER_LEN);          // 粘包：切出一个完整包
    memcpy(pdu, m_recvBuf.constData(), uiPDULen);
    m_recvBuf.remove(0, int(uiPDULen));
    /* handleMsg(pdu) ... */
}
```

> 半包要缓存等待、粘包要循环切分、非法长度要断开——三者在同一个循环里处理，
> 避免"读一次当一个包"造成的解析错位，也避免 `uiTotalLen < 76` 时 `uint` 下溢成约 42 亿后巨量 `malloc`。

### 4. 变长数据（`caMsg`）的承载方式

| 场景 | `caMsg` 用法 |
|---|---|
| 用户名 / 好友列表 | 每 32 字节定长拼一个名字，`uiMsgLen / 32` 即条目数 |
| 文件列表 | 每个 `FileInfo`（36 字节 = 32 字节名 + 4 字节类型）一条 |
| 聊天消息 | 纯文本 |
| 上传文件 | `路径字符串 + '\0' + 文件二进制`，靠 `strlen` 定位分隔点 |
| 下载 / 分享文件 | 同上，服务端读文件后打包下发 |

---

## 五、核心业务流程

**1. 注册**
输入校验 → `REGIST_REQUEST` → 服务端查询是否重名 → `insert` → 成功后 `QDir::mkpath` 创建个人网盘目录 → 返回 `bool`。

**2. 登录**
`LOGIN_REQUEST` → 校验账号密码 → 数据库 `online = 1` → **将该登录名写入 socket 的 `m_strLoginName`** → 返回 `bool` → 客户端跳转主界面。

**3. 加好友（三步握手）**

1. A 双击在线用户 B → `ADD_FRIEND_REQUEST`（caData：`A` | `B`）
2. 服务端查库判断：已是好友 `-2` / 用户不存在 `-1` / 对方不在线 `0` / 在线 `1`
3. 对方在线 → 服务端把类型改为 `ADD_FRIEND_RESEND` **转发给 B** → B 弹出确认框
4. B 同意 → `ADD_FRIEND_AGREE_REQUEST` → 服务端写入 `friend` 表 → **双方都收到成功响应**

**4. 聊天**
A 发送 `CHAT_REQUEST`（caData：`A` | `B`，caMsg：消息内容）→ 服务端仅将类型改为 `CHAT_RESEND`，
按目标用户登录名在连接列表中查找 socket 并转发 → B 的聊天窗口显示。
（对方离线时消息直接丢弃，暂无离线消息存储。）

**5. 网盘操作**
客户端只发送「操作意图」（当前路径 + 文件名等），服务端在 `./userdata/<用户名>/` 下通过 `QDir` / `QFile` 实际执行并返回 `bool`。
上传时客户端将文件数据拼入 PDU 一次性发出；下载 / 分享时服务端读取文件拼入 PDU 下发。

---

## 六、数据库设计

```sql
CREATE TABLE user_info (
    id     INT PRIMARY KEY AUTO_INCREMENT,
    name   VARCHAR(32) NOT NULL UNIQUE,
    pwd    VARCHAR(64) NOT NULL,   -- SHA-256(口令 + salt) 的 64 位 hex，不存明文
    online TINYINT(1) DEFAULT 0,   -- 0 离线 / 1 在线
    salt   VARCHAR(32) DEFAULT ''  -- 每用户独立 16 字节随机盐（hex 32 位）
);

CREATE TABLE friend (
    id        INT PRIMARY KEY AUTO_INCREMENT,
    user_id   INT NOT NULL,        -- 外键 -> user_info.id
    friend_id INT NOT NULL,        -- 外键 -> user_info.id
    UNIQUE KEY uk_friend_pair (user_id, friend_id)
);
```

- **在线状态**：登录置 `online = 1`，正常断线置 `0`，注册默认 `0`；
  进程被杀 / 断网这类收不到 `disconnected` 的情况，由心跳超时巡检兜底置 0。
  在线用户列表即 `select name from user_info where online = 1`。
- **好友关系无向**：查询时做双向匹配（`union`）：

```sql
SELECT name FROM user_info
WHERE id IN (
    SELECT user_id   FROM friend WHERE friend_id = (SELECT id FROM user_info WHERE name = :cur)
    UNION
    SELECT friend_id FROM friend WHERE user_id   = (SELECT id FROM user_info WHERE name = :cur)
) AND online = 1;
```

- **好友唯一约束**：`uk_friend_pair` 保证双方同时互加好友时只落一行关系，不会产生重复记录。
- **口令存储**：注册时生成随机盐，存 `SHA-256(口令 + 盐)`；登录时按用户名取出 `pwd`/`salt` 重新计算比对。
  历史明文记录（`salt` 为空）首次登录成功后会自动升级为哈希。

完整建库脚本见 [`sql/init.sql`](sql/init.sql)，旧库升级见 [`sql/migrate_v2.sql`](sql/migrate_v2.sql)。

---

## 七、构建与运行

### 1. 环境要求

- Qt 5.12 及以上（需包含 **Network**、**Sql**、**Widgets** 模块）
- MySQL 5.7+ / 8.0，且 Qt 具备 **QMYSQL** 驱动
- 编译器：MSVC 2017+ 或 MinGW（本项目在 Windows + Qt Creator 下开发）

### 2. 初始化数据库

```bash
mysql -uroot -p < sql/init.sql
```

数据库连接参数在 `Server/operatedb.cpp` 的 `connectSQL()` 中配置（默认 `localhost:3306`、用户 `root`、密码 `123456`、库名 `mydb2501`），请按本机环境修改。

### 3. 编译运行

分别用 Qt Creator 打开两个工程并构建：

```bash
# 服务端
cd Server && qmake Server.pro && make      # MinGW
cd Server && qmake Server.pro && nmake     # MSVC

# 客户端
cd Client && qmake Client.pro && make
cd Client && qmake Client.pro && nmake
```

**启动顺序**：先启动 Server（监听 `5000` 端口），再启动 Client。
服务端会在工作目录下自动创建 `./userdata/` 作为所有用户网盘的根目录。

> 网络地址配置位于 `Server/connect.config.txt` 与 `Client/connect.config.txt`（第一行 IP，第二行端口），
> 两个文件均通过 `.qrc` 资源打包进可执行文件，修改后需重新构建。
> ⚠️ 两个文件**必须使用 CRLF 换行**并保留结尾换行；服务端读取时会按行切分并 trim，
> 若文件只是一个空行会导致端口解析为 0、`listen()` 失败。

### 4. 一键构建与测试（可选）

仓库附带 PowerShell 脚本，便于快速验证（需先启动服务端）：

```powershell
# 构建两端，失败即中断（避免误测旧二进制）
powershell -ExecutionPolicy Bypass -File tests\build.ps1

# 协议层：粘包 / 半包 / 非法包长
powershell -ExecutionPolicy Bypass -File tests\test_protocol.ps1

# 心跳 / 重复登录互踢 / 异常掉线超时回收（含 45 秒等待用例）
powershell -ExecutionPolicy Bypass -File tests\test_heartbeat.ps1

# 安全：SQL 注入 / 口令哈希 / 网盘路径越权
powershell -ExecutionPolicy Bypass -File tests\test_security.ps1
```

> 脚本默认按本机路径查找 Qt / MinGW / MySQL，可通过参数覆盖，例如
> `-QtBin "D:\Qt\5.14.2\mingw73_64\bin" -MingwBin "D:\Qt\Tools\mingw730_64\bin"`。

---

## 八、问题与修复记录

项目最初是一份课程作业，作者系统梳理出 9 个缺陷，并分三个阶段做了针对性深化。
下表是**真实状态**，未完成的条目仍在待办中。

| # | 问题 | 状态 | 修复方式 |
|---|---|---|---|
| 1 | 拆包未处理半包 / 粘包，长度字段未校验可致 `uint` 下溢巨量 `malloc` | ✅ 已修复 | 接收缓冲区 + 长度区间校验 + 循环切分（`static_assert` 锁定 76 字节头） |
| 2 | 无心跳，进程被杀 / 断网后服务端无法感知，`online` 永远停在 1 | ✅ 已修复 | 客户端 15s 心跳 + 服务端 5s 巡检 / 35s 超时断开 |
| 3 | 重复登录无处理，同账号可多连接并存 | ✅ 已修复 | 登录成功后下发 `KICKED_RESEND` 并断开同名旧连接 |
| 4 | SQL 全部用 `QString::arg()` 拼接，存在注入风险 | ✅ 已修复 | 全量改 `QSqlQuery::prepare()` + `bindValue()` 预处理绑定 |
| 5 | 口令明文存储 | ✅ 已修复 | SHA-256 + 每用户 16 字节随机盐；历史明文记录登录时自动升级 |
| 6 | 网盘文件操作直接使用客户端路径，可越权访问目录外文件 | ✅ 已修复 | `QDir::cleanPath` 规范化 + 用户根目录前缀断言（带分隔符比较） |
| 7 | 消息转发用裸指针比较，聊天 / 加好友 / 分享文件全部无法命中 | ✅ 已修复 | 改为按字符串内容比较（`QByteArray` 比对，兼容 UTF-8 登录名） |
| 8 | 转发时与 `handleMsg` 共用同一 `PDU`，存在 use-after-free / double free | ✅ 已修复 | 转发前 `clonePdu()` 出独立拷贝，各自负责释放 |
| 9 | 大文件整包 `readAll()` 读入内存 | ⏳ 待办 | 计划改分片传输（固定分片 + 序号）+ 校验和 + 断点续传 |
| 10 | 无离线消息，对方离线即丢弃 | ⏳ 待办 | 计划消息落库 + 登录后推送 |
| 11 | 服务端单线程，连接量大时响应变慢 | ⏳ 待办 | 受 `QSqlDatabase` 不能跨线程共享连接限制，需先做连接管理重构 |
| 12 | 聊天窗口全局复用，与多人聊天会串台 | ⏳ 待办 | 计划按会话维度维护窗口表 |

> 说明：第 11 项没有直接上线程池，是因为 Qt 的 `QSqlDatabase` 连接绑定创建它的线程，
> 盲目改多线程会引入连接竞争。正确做法是"每线程独立连接 + 连接池"，这本身就是可以展开讲的取舍。

### 8.1 收包状态机（阶段 1 的核心）

```cpp
void MyTcpSocket::recvMsg()
{
    m_lastActiveMs = QDateTime::currentMSecsSinceEpoch();
    m_recvBuf.append(this->readAll());

    while (true)
    {
        if (m_recvBuf.size() < int(sizeof(uint))) break;          // 连长度字段都不够

        uint uiPDULen = 0;
        memcpy(&uiPDULen, m_recvBuf.constData(), sizeof(uint));

        if (uiPDULen < PDU_HEADER_LEN || uiPDULen > PDU_MAX_TOTAL_LEN)
        {                                                          // 非法长度：断开
            m_recvBuf.clear();
            this->disconnectFromHost();
            return;
        }

        if (m_recvBuf.size() < int(uiPDULen)) break;               // 半包：等下一次

        PDU* pdu = mkPDU(uiPDULen - PDU_HEADER_LEN);               // 粘包：切一个出来
        memcpy(pdu, m_recvBuf.constData(), uiPDULen);
        m_recvBuf.remove(0, int(uiPDULen));

        PDU* respdu = handleMsg(pdu);   // 内部 free(pdu)
        sendMsg(respdu);                // 内部 free(respdu)
    }
}
```

半包要缓存等待、粘包要循环切分、非法长度要断开，三者在同一个循环里处理，
避免"读一次当一个包"造成的解析错位。

### 8.2 测试脚本与验证方式

`tests/` 目录下的脚本**手工构造 PDU 字节流**，不依赖 GUI，可以直接复现上述修复：

| 脚本 | 覆盖内容 | 结果 |
|---|---|---|
| `tests/build.ps1` | 构建两端，失败即中断 | — |
| `tests/test_protocol.ps1` | 粘包（两包一次写）、半包（40+36 分两次写）、非法包长（10 字节）、超大包长（100MB）、异常后服务存活 | 6 / 6 通过 |
| `tests/test_heartbeat.ps1` | 心跳应答、重复登录互踢、异常掉线 35s 超时回收 | 8 / 8 通过 |
| `tests/test_security.ps1` | SQL 注入、口令哈希、错误口令、路径越权（列目录 / mkdir）、合法路径回归、历史明文升级 | 14 / 14 通过 |

运行方式（先启动服务端，再执行脚本）：

```powershell
powershell -ExecutionPolicy Bypass -File tests\build.ps1
powershell -ExecutionPolicy Bypass -File tests\test_protocol.ps1
powershell -ExecutionPolicy Bypass -File tests\test_heartbeat.ps1
powershell -ExecutionPolicy Bypass -File tests\test_security.ps1
```

---

## 九、可考察的知识点

| 知识点 | 项目对应实现 |
|---|---|
| TCP 粘包 / 半包 | 接收缓冲区 + 长度区间校验 + 循环切分；`static_assert(sizeof(PDU)==76)` 编译期锁定头部布局 |
| 为什么选择 TCP | 聊天与文件传输要求可靠、有序，TCP 自带可靠传输，适合自定义协议封装 |
| 二进制协议 vs JSON | 自定义 PDU 紧凑高效、解析快、字段定长易对齐；代价是可读性差、`caData[64]` 扩展性受限 |
| 单例模式 | `Client` / `Index` / `File` / `Server` / `MyTcpServer` / `operateDB` 均为单例：Meyers 局部静态变量（C++11 线程安全）+ `delete` 拷贝构造与赋值 |
| 信号槽机制 | `readyRead → recvMsg` 异步收发、`disconnected → clineOffliine` 断线清理、`on_xxx_clicked` 自动连接 |
| 断线处理 | `disconnected` 信号 → `online` 置 0 → 从连接列表移除并 `deleteLater()`；异常掉线由心跳超时兜底 |
| 连接保活 | 客户端 15s 心跳 + 服务端 35s 超时巡检；遍历连接列表时用副本 + `QPointer`，避免 `disconnectFromHost` 同步触发删除导致迭代器失效 |
| 服务端并发模型 | 单线程事件循环 + 非阻塞 IO（Qt 内部封装 select / epoll） |
| 内存管理 | `malloc` / `free` 配对、谁创建谁释放、柔性数组、转发前克隆 `PDU` 规避 use-after-free |
| 数据库访问 | `QSqlDatabase` + `QSqlQuery`，全局单例复用连接；全部预处理绑定 |
| 口令安全 | 每用户随机盐 + SHA-256，不存明文；旧数据登录时惰性升级 |
| 路径安全 | `QDir::cleanPath` 消解 `..` 后与用户根目录做前缀比较（带分隔符，避免 `/alice2` 命中 `/alice`） |
| 协议双端一致性 | 两份 `protocol.h` 必须逐字节一致（§5.1），枚举只能追加不能插队 |

---

## 十、说明

- 本仓库已通过 `.gitignore` 排除 `debug/`、`release/`、`Makefile*`、`moc_*`、`ui_*.h`、`*.o`、`*.exe`、`*.pro.user` 等编译产物与本机 IDE 配置，仅保留源码。
- `项目总结.md` 是作者整理的中文面试复习文档，包含更详细的模块讲解与问答思路，可作为本 README 的补充阅读。
- 数据库若从旧版本升级，请执行 [`sql/migrate_v2.sql`](sql/migrate_v2.sql)（加 `salt` 列、`pwd` 扩容、好友唯一键与去重）。
