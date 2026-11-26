# QT-IM-system

基于 **Qt5 + TCP 自定义二进制协议 + MySQL** 的即时通讯与文件传输系统（C/S 架构）。

客户端提供用户注册 / 登录、好友管理、在线聊天、**个人网盘**（上传、下载、分享、重命名、移动、删除）等功能；
服务端基于 `QTcpServer` 事件驱动地处理请求、访问数据库、转发消息，并直接管理每个用户的网盘目录。

在基础功能之上完成了三项工程化深化：**健壮的收包状态机**（粘包 / 半包 / 非法长度）、
**连接保活与重复登录互踢**（心跳 + 超时巡检）、**安全加固**（SQL 预处理、口令加盐哈希、网盘路径越权防护）。
所有修复均配有可复现的自动化测试脚本，见 [`tests/`](tests)；第八节列出完整的问题清单与修复状态。

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
│   ├── init.sql                # 建库建表（含 salt 列、好友唯一约束、离线消息表）
│   ├── migrate_v2.sql          # 旧库升级脚本（加盐哈希 + 唯一约束 + 去重）
│   └── migrate_v3.sql          # 旧库升级脚本（离线消息表）
│
└── tests/
    ├── build.ps1               # 一键构建两端，失败即中断
    ├── test_protocol.ps1       # 粘包 / 半包 / 非法包长
    ├── test_heartbeat.ps1      # 心跳 / 重复登录互踢 / 超时回收
    ├── test_security.ps1       # SQL 注入 / 口令哈希 / 路径越权
    ├── test_auth.ps1           # 未登录拦截 / 冒名操作 / 互踢后 online 状态
    └── test_offline.ps1        # 离线落库 / 登录补发顺序 / 已读不重发
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
- **服务端不保存业务状态**：每个 `MyTcpSocket` 只记录一个 `m_strLoginName` 与最后活跃时间，在线状态持久化在数据库 `online` 字段中。
- **单线程事件循环**：服务端无独立界面，完全依靠 `QApplication` 事件循环驱动 `readyRead` 信号处理并发请求；
  心跳巡检由同一事件循环上的 `QTimer` 驱动。

---

## 四、通信协议设计

两端各自维护一份 `protocol.h` / `protocol.cpp`，定义必须**逐字节一致**（见 5.1）。

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

头部布局由编译期断言锁定，不再依赖"编译器恰好这么排"的隐含假设：

```cpp
constexpr unsigned int PDU_HEADER_LEN    = 76;
constexpr unsigned int PDU_MAX_TOTAL_LEN = 8u * 1024u * 1024u;  // 单包上限 8MB
static_assert(sizeof(PDU) == 76, "PDU header layout broken");
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

### 3. 拆包方式（解决 TCP 粘包 / 半包）

包头记录总长度，接收端维护**接收缓冲区**，循环判断「头部长度 + 包体长度」是否收齐后再解析。
完整实现见 8.1，核心思路：

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

## 五、工程细节约定

### 5.1 两份 `protocol.h` 必须同步

`Server/protocol.h` 与 `Client/protocol.h` 是两份独立拷贝，协议靠**数值**传输，任何一处不一致都会导致两端解析错位。

本项目采用的做法：

- 修改时**只改一份**，然后直接覆盖另一份（`Copy-Item` / `cp`），保证逐字节相同；
- 用 `static_assert(sizeof(PDU) == 76)` 在编译期锁定头部布局；
- 提交前用哈希核对一致性：

```powershell
(Get-FileHash Server\protocol.h).Hash -eq (Get-FileHash Client\protocol.h).Hash
# 或
fc /n Server\protocol.h Client\protocol.h
```

> 更彻底的做法是抽出一份共享头文件，两个 `.pro` 都 `INCLUDEPATH += ../common`，
> 把"靠人工保证"变成"靠构建保证"。本项目暂未做该重构，但这是明确的下一步。

### 5.2 内存所有权

项目里 `PDU` 是 `malloc/free`，`MyTcpSocket` 是 `new/deleteLater`。改动时守住三条：

- `mkPDU()` 出来的必须 `free()`，**不能 `delete`**；
- `handleMsg()` 与 `sendMsg()` 内部已各自释放一次，调用方不要重复释放；
- 需要"同一个包既要转发又要自己释放"时，用 `clonePdu()` 复制独立 PDU（见 `msghandler.cpp`）。

### 5.3 配置文件必须使用 CRLF 换行

`Server/connect.config.txt` 与 `Client/connect.config.txt` 通过 `.qrc` 编译进可执行文件。
两者都必须以 **CRLF** 结尾并保留末尾换行；读取侧已改为按行切分并 `trim()`，
若文件为空或只有一行，端口会解析为 0 导致 `listen()` 失败（服务端会打印明确警告）。

---

## 六、核心业务流程

**1. 注册**
输入校验 → `REGIST_REQUEST` → 服务端查重（预处理查询）→ 生成随机盐 + SHA-256 哈希后 `insert`
→ 成功后 `QDir::mkpath` 创建个人网盘目录 → 返回 `bool`。

**2. 登录**
`LOGIN_REQUEST` → 按用户名取出 `pwd`/`salt` → 重算哈希比对 → 数据库 `online = 1`
→ **将该登录名写入 socket 的 `m_strLoginName`** → 踢掉同账号旧连接 → 返回 `bool` → 客户端跳转主界面。

**3. 加好友（三步握手）**

1. A 双击在线用户 B → `ADD_FRIEND_REQUEST`（caData：`A` | `B`）
2. 服务端查库判断：已是好友 `-2` / 用户不存在 `-1` / 对方不在线 `0` / 在线 `1`
3. 对方在线 → 服务端把类型改为 `ADD_FRIEND_RESEND` **转发给 B** → B 弹出确认框
4. B 同意 → `ADD_FRIEND_AGREE_REQUEST` → 服务端写入 `friend` 表 → **双方都收到成功响应**

**4. 聊天**
A 发送 `CHAT_REQUEST`（caData：`A` | `B`，caMsg：消息内容）→ 服务端复制一份 PDU 并将类型改为 `CHAT_RESEND`，
按目标用户登录名在连接列表中查找 socket 并转发 → B 的聊天窗口显示。

对方不在线时不丢弃：消息写入 `message` 表（`is_read = 0`），等 B 下次登录成功后服务端排队补发
（类型 `OFFLINE_MSG_RESEND`），补发完即标记已读。详见 9.7。

**5. 网盘操作**
客户端只发送「操作意图」（当前路径 + 文件名等），服务端先用 `isSafePath()` 校验路径落在用户根目录内，
再通过 `QDir` / `QFile` 实际执行并返回 `bool`。
上传时客户端将文件数据拼入 PDU 一次性发出；下载 / 分享时服务端读取文件拼入 PDU 下发。

**6. 连接保活**
客户端每 15s 发一次 `HEARTBEAT_REQUEST`，服务端只回包不查库；
服务端每 5s 巡检所有已登录连接，超过 35s 无任何上行数据即主动断开，由 `disconnected` 链路把 `online` 置 0。

---

## 七、数据库设计

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
- **口令存储**：注册时生成随机盐，存 `SHA-256(口令 + 盐)`；登录时重新计算比对。
  历史明文记录（`salt` 为空）首次登录成功后会自动升级为哈希。

完整建库脚本见 [`sql/init.sql`](sql/init.sql)，旧库升级见 [`sql/migrate_v2.sql`](sql/migrate_v2.sql)。

---

## 八、构建与运行

### 1. 环境要求

- Qt 5.12 及以上（需包含 **Network**、**Sql**、**Widgets** 模块）
- MySQL 5.7+ / 8.0，且 Qt 具备 **QMYSQL** 驱动
- 编译器：MSVC 2017+ 或 MinGW（本项目在 Windows + Qt 5.14.2 + MinGW 7.3.0 下开发验证）

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
>
> `test_security.ps1` 还需要用 `-ServerRoot` 指向**服务端进程的工作目录**
> （即 `userdata/` 实际生成的位置）。否则"目录确实创建""越权目录未落盘"
> 这类落盘断言会因为两边路径基准不同而失败——那是环境问题，不是服务端缺陷。

---

## 九、问题与修复记录

本节记录**问题清单与修复状态**。复查过程、误判记录、测试设计教训与可复用的排查清单，
见 [`docs/REVIEW-NOTES.md`](docs/REVIEW-NOTES.md)。

本节是开发完成后的自我 code review 记录：先系统梳理出全部问题，再分三个阶段逐项修复，
**下表是真实状态**，未完成的条目仍在待办中。

### 9.1 修复状态总览

| # | 问题 | 状态 | 修复方式 |
|---|---|---|---|
| 1 | 拆包未处理半包 / 粘包；长度未校验可致 `uint` 下溢巨量 `malloc` | ✅ 已修复 | 接收缓冲区 + 长度区间校验 + 循环切分；`static_assert` 锁定 76 字节头 |
| 2 | 无心跳保活，进程被杀 / 断网后 `online` 无法回收 | ✅ 已修复 | 客户端 15s 心跳 + 服务端 5s 巡检 / 35s 超时断开 |
| 3 | 重复登录无处理，同账号可多连接并存 | ✅ 已修复 | 登录成功后下发 `KICKED_RESEND` 并断开同名旧连接 |
| 4 | 服务端完全信任客户端传入的路径（路径穿越） | ✅ 已修复 | 由会话登录名拼出根目录，`QDir::cleanPath` 后做带分隔符的前缀断言 |
| 5 | 口令明文存储 | ✅ 已修复 | 每用户 16 字节随机盐 + SHA-256；历史明文登录时惰性升级 |
| 6 | SQL 全部为字符串拼接（注入风险） | ✅ 已修复 | 全量改 `QSqlQuery::prepare()` + `bindValue()`（12 处 prepare / 19 处 bind） |
| 7 | 大文件整包读入内存 | ⏳ 待办 | 计划改分片传输（固定分片 + 序号）+ 校验和 + 断点续传 |
| 8 | 无离线消息，对方离线即丢弃 | ✅ 已修复 | 对方不在线时消息落 `message` 表；登录应答后排队补发（每批上限 200 条），补发即标记已读 |
| 9 | 服务端单线程处理，慢操作阻塞全部连接 | ⏳ 待办 | 连接层留在主线程（`QTcpSocket` 有线程亲和性），业务投递线程池；`QSqlDatabase` 需每线程独立连接 |
| 10 | 聊天窗口全局复用，跨会话串消息、可能回复错人 | ✅ 已修复 | 按好友维度维护 `QMap<QString, Chat*>`，窗口的会话对象只在创建时赋值一次 |
| 11 | 重复登录踢人后，账号被错误置为离线 | ✅ 已修复 | 断线时先判定是否还有同名有效连接，有则保持 `online = 1` |
| 12 | 服务端不校验操作身份，可冒充他人 | ✅ 已修复 | 分发入口加登录态拦截 + `caData` 自称身份与会话登录名强校验 |

关于第 9 项：Qt 的 `QSqlDatabase` 连接与创建它的线程绑定，不能跨线程共享。
所以"直接加线程池"是错的，正确顺序是先做连接管理（每线程独立连接 / 连接池），再线程化业务——这个取舍本身值得展开讲。

第 11、12 项是后期复查中发现的两处缺陷，详见 9.6。
第 8 项（离线消息）与第 10 项（多会话窗口）的完整实现思路见 9.7、9.8。
剩余的第 7、9 项属于**架构级改造**（协议要重新设计 / 并发模型要换），
不在本轮范围内，仍在待办中——这里如实标 ⏳，不把"知道怎么做"写成"已经做完"。

### 9.2 健壮性：收包状态机（第 1 项）

原始实现假定一次 `readyRead` 恰好到达一个完整 PDU，有三个缺陷：

- 未校验 `read()` 返回值，短读时剩余字节滞留缓冲并与下一个包的残体错位；
- 一次 `readyRead` 内到达多个 PDU 时只解析第一个，其余被忽略；
- `uiMsgLen = uiPDULen - sizeof(PDU)` 为 `uint` 运算，`uiPDULen < 76` 时下溢成极大值并直接传给 `malloc`。

修复后的 `MyTcpSocket::recvMsg()`：

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

客户端 `Client::recvMsg()` 用同样的状态机。`m_recvBuf.remove(0, n)` 每次是 O(n) 拷贝，
数据量小可接受；要优化可改成"记录读偏移 + 攒够再统一 erase"，但先正确、再优化。

### 9.3 连接保活与互踢（第 2、3 项）

- 客户端 15s 心跳（小于服务端 35s 阈值，留出抖动余量）；
- 服务端 `QTimer` 每 5s 巡检，只对**已登录**且未在断开流程中的连接做超时判定；
- 遍历连接列表时用**副本 + `QPointer`**：`disconnectFromHost()` 可能同步触发
  `disconnected → clineOffliine → removeSocket` 修改原列表，直接遍历会迭代器失效；
- 登录成功后 `kickDuplicate(登录名, this)`：先下发 `KICKED_RESEND` 再断开旧连接，
  传 `this` 是为了避免把自己踢掉。顺序必须是**先**让 `login()` 写好 `m_strLoginName`，**再**踢人。

### 9.4 安全加固（第 4、5、6 项）

- **SQL**：全部改为预处理绑定；`const char*` 参数统一走 `QString::fromUtf8`
  （客户端用 `toStdString()` 发送，是 UTF-8，用 `fromLocal8Bit` 会让中文用户名乱码）。
- **口令**：`salt = 16 字节 QRandomGenerator 随机数`，存 `SHA-256(口令 + salt)` 的 64 位 hex；
  登录时按 `name` 取出 `pwd`/`salt` 重算比对；`salt` 为空视为历史明文记录，比对成功后自动升级。
- **路径**：根目录由"服务端会话中记录的登录名"拼出，而不是信任客户端；
  客户端路径先 `QDir::cleanPath()` 消解 `..`，再断言 `abs == root || abs.startsWith(root + "/")`。
  带分隔符比较是必需的，否则 `/userdata/alice2` 会被误判为在 `/userdata/alice` 之内。
  已覆盖 8 处文件操作（`mkdir` / `flushFile` / `delDir` / `renameFile` / `moveFile` / `uploadFile` / `downloadFile` / `shareFile`），
  其中 `renameFile` 与 `moveFile` 对**源和目标**都校验。
  `uploadFile` 的 `caMsg` 是"路径 + `\0` + 文件体"，只校验 `strlen` 之前的路径部分。

### 9.5 关于 `resend()` 的比较写法（已澄清，非缺陷）

原始代码里有一处 `caTarName == m_strLoginName`（`char*` 在左、`QString` 在右）。
这里记录一次**自我更正**：

- 该表达式**结果正确**。`const char*` 会通过 `QString(const char*)` 隐式构造（Qt5 中按 UTF-8 解码），
  随后调用 `QString` 的成员 `operator==`，做的是**字符串内容比较**，不是指针比较；
- 我用 `Qt5Core.dll` 实测确认：同名返回 `true`，异名返回 `false`，与 `strcmp` 结果一致；
- 尽管如此，这种写法依赖隐式转换与"`caData` 已按 0 填充"的前提（由 `mkPDU()` 的 `memset` 保证），
  且 Qt 5.15 已弃用该 ASCII 隐式转换重载。因此代码中改成了**显式内容比较**：

```cpp
const QByteArray tarName(caTarName);          // 按字节保留客户端发来的 UTF-8
if (sock->m_strLoginName.toUtf8() == tarName) // 显式内容比较，无隐式转换歧义
```

同理，`chat()` / `addFriend()` / `addFriendAgree()` 转发时改为 `clonePdu()` 出独立副本。
需要说明的是：**原写法在当前 Qt 版本下并不构成 use-after-free**——`QTcpSocket::write()`
会把数据拷贝进内部写缓冲区，转发后立即释放原始 `PDU` 也不会读到悬空内存。
改成显式副本是为了让所有权契约清楚、避免后续改造（例如写缓冲改为零拷贝）时踩坑，
属于**可读性 / 可维护性改进**，不是修复线上缺陷。

> 这两条值得单独记下来：code review 里"看起来可疑"和"确实是 bug"要分开，
> 后者需要能复现的验证，前者应该写成"建议"而不是断言缺陷。

### 9.6 身份校验与在线状态（第 11、12 项）

这两处是后期复查中发现的缺陷，都会造成实际后果。

**第 11 项：重复登录互踢后，账号被错误置为离线**

踢人的时序是这样的：新连接登录 → `handleLogin()` 把 `online` 置 1 → `kickDuplicate()` 断开旧连接
→ 旧连接触发 `disconnected` → `clineOffliine()` **无条件**执行 `handleOffline()` 把 `online` 置 0。
结果：新连接明明还在线，数据库里却已经变成离线，好友列表和消息转发都会认为该用户不在线。

修复思路：断线时先确认该账号是否还有其它**有效在线连接**，有则不动 `online`。

```cpp
void MyTcpSocket::clineOffliine()
{
    if (!m_strLoginName.isEmpty())
    {
        // 只要还有同名连接在线，就不能把 online 置 0
        if (MyTcpServer::getInstance().hasOtherActiveSession(m_strLoginName, this))
        {
            qDebug() << "账号" << m_strLoginName << "仍有其它在线连接，保持 online=1";
        }
        else
        {
            operateDB::getInstance().handleOffline(m_strLoginName.toStdString().c_str());
        }
    }
    MyTcpServer::getInstance().removeSocket(this);
}
```

`hasOtherActiveSession()` 会排除自己、排除已在 `UnconnectedState` 的连接（正在断开的不算有效在线）。
这比"给被踢连接打一个 `m_bKicked` 标记"更通用：它同样能处理"用户开两个客户端"这类历史上没有约束的情况。

**第 12 项：服务端不校验操作身份，可冒充他人**

协议里部分请求的 `caData` 前 32 字节是"发起者自己"（聊天、加好友、同意加好友、刷新好友、删好友）。
原实现直接采信这个字段，于是任何已登录账号都能：

| 伪造方式 | 后果 |
|---|---|
| `CHAT_REQUEST` 把 `caData[0:32]` 写成别人 | 以他人名义发言，接收方看到的是被冒名者 |
| `ADD_FRIEND_REQUEST` 冒名 | 以他人名义建立好友关系 |
| `ADD_FRIEND_AGREE_REQUEST` 冒名 | 替受害者同意好友申请 |
| `FLUSH_FRIEND_REQUEST` 冒名 | 查看他人好友列表 |
| `DEL_FRIEND_REQUEST` 冒名 | 删除他人的好友关系 |

更基础的问题是：**未登录的 TCP 连接也能直接调用业务接口**（网盘操作只校验路径在 `userdata/<登录名>/` 内，
而 `登录名为空` 时这个校验对空目录恰好成立）。

修复分两道：

1. **分发入口拦截**（`MyTcpSocket::handleMsg`）：除注册、登录、心跳外，`m_strLoginName` 为空一律拒绝：

```cpp
if (m_strLoginName.isEmpty()
    && pdu->uiType != ENUM_TYPE_REGIST_REQUEST
    && pdu->uiType != ENUM_TYPE_LOGIN_REQUEST
    && pdu->uiType != ENUM_TYPE_HEARTBEAT_REQUEST)
{
    qWarning() << "未登录连接尝试调用业务接口, uiType=" << pdu->uiType << "，已拒绝";
    free(pdu);
    return NULL;
}
```

2. **身份绑定校验**（`MsgHandler::isOwnIdentity`）：`caData` 里自称的用户必须等于会话登录名：

```cpp
bool MsgHandler::isOwnIdentity(const char* caClaimedName) const
{
    if (caClaimedName == NULL || m_loginName.isEmpty()) return false;
    // caData 是定长 32 字节字段，显式限长，避免字段未以 0 结尾时越界读
    const QString claimed = QString::fromUtf8(caClaimedName, int(qstrnlen(caClaimedName, 32)));
    return !claimed.isEmpty() && claimed == m_loginName;
}
```

覆盖 `chat` / `addFriend` / `addFriendAgree` / `flushFriend` / `delFriend` 五处。
聊天请求没有应答包，校验失败直接静默丢弃（不回应，避免给攻击者反馈）。

⚠️ **`ADD_FRIEND_AGREE` 的 `caData` 语义与其余四类不同**，这是本节最容易踩的坑：

| 消息 | `caData[0:32]` | `caData[32:64]` | 该校验哪一段 |
|---|---|---|---|
| `CHAT` / `ADD_FRIEND` / `FLUSH_FRIEND` / `DEL_FRIEND` | 当前用户 | 对方 | 前 32 字节 |
| `ADD_FRIEND_AGREE` | **发起方（对方）** | **同意方（自己）** | **后 32 字节** |

原因：客户端收到 `ADD_FRIEND_RESEND` 后是**原样沿用整段 `caData`**（里面是发起方）
再补上自己的名字发出，所以"当前登录用户"落在后半段。若这里也按前半段校验，
真实客户端"同意好友"会 100% 失败。

更麻烦的是：手工构造测试包时如果把这两个字段写反，测试**反而会通过**——
这就是"实现和测试同时错"的典型形态，只靠测试绿灯发现不了，必须回到
真实客户端的发包路径去对照。这类一致性缺陷值得单独记一笔。

`shareFile` 虽然没有身份字段，但源路径必须落在 `userdata/<会话登录名>/` 内（第 4 项的路径约束），
已隐式绑定会话身份，且它不转发发送者姓名，因此**无法冒充**——不需要额外加字段。

> 设计取舍：身份校验失败的应答尽量与"业务失败"保持同形。
> 例如 `FLUSH_FRIEND_RESPOND` 本身不承载 `bool`，拒绝时返回空列表，
> 与"确实没有好友"在线上不可区分——这样既不泄漏信息，也不用改协议。

### 9.7 离线消息（第 8 项）

原实现里 `MyTcpServer::resend()` 找不到在线目标时只打印一行日志就把 PDU 丢掉，
对方不在线期间的聊天消息彻底丢失。

**落库时机**：`resend()` 返回 `bool`（命中在线连接 `true`，未命中 `false`），
`MsgHandler::chat()` 只在 `false` 时才落库——在线投递成功不应该在数据库里再留一份。

```cpp
const bool delivered = MyTcpServer::getInstance().resend(tarName, resendPdu);
free(resendPdu);
if (!delivered)
{
    operateDB::getInstance().handleSaveOfflineMsg(strSender, strReceiver, strContent);
}
```

**补发时机**：登录成功后立刻补发，但必须排在 `LOGIN_RESPOND` 之后。
`recvMsg()` 里 `handleMsg()` 只**返回**应答包，真正写 socket 是在 `sendMsg()`；
若在 LOGIN 分支里直接推送离线消息，它会先于登录应答到达客户端，
客户端此刻还没跳转主界面，`Friend` 窗口尚未创建，消息就丢了。
所以排队到下一轮事件循环：

```cpp
QPointer<MyTcpSocket> guard(this);
QMetaObject::invokeMethod(this, [guard]() {
    if (guard) { guard->pushOfflineMessages(); }
}, Qt::QueuedConnection);
```

`QPointer` 是必需的：排队期间客户端可能已断开，lambda 执行时 `this` 已被 `deleteLater()` 释放。

**数据模型**：`message(id, sender, receiver, content, is_read, created_at)`。
`sender` / `receiver` 存**登录名**而不是 `user_info.id`——补发时按登录名直接查，省一次关联；
本项目没有改名功能，不会失效。`KEY idx_receiver_read(receiver, is_read)` 覆盖"查某人未读"这个唯一的查询。

**两个上限**：

| 常量 | 值 | 作用 |
|---|---|---|
| `kMaxOfflineBatch` | 200 | 单次登录最多补发条数，避免积压上万条时登录瞬间灌满 socket 缓冲 |
| `kMaxOfflineContentBytes` | 60000 | `content` 是 `TEXT`（65535 字节），超长直接拒绝入库，否则整条 insert 失败、消息静默丢失 |

**投递语义：宁可重复，不可丢失。**
实现是"先查出未读 → 逐条下发 → 再批量标记已读"。
若在"下发完"与"标记已读"之间进程崩溃，这一批会重复投递一次。
要严格一次投递，需要客户端回 ACK + 服务端按消息 id 去重，那要改协议，本版本没做。

**客户端**：收到 `OFFLINE_MSG_RESEND` 后走 `ResHandler::offlineMsg()`，
内容加 `[离线]` 前缀写进对应会话窗口。这里用 `appendIncoming(..., bPopUp = false)`
**不弹窗**——刚登录就为每个发送者弹一个窗口太打扰，用户点开聊天记录自然能看到。

验证见 `tests/test_offline.ps1`（17 / 17）：落库、补发顺序、补发后已读、重新登录不重发、
双方在线直投不落库、冒名发送被丢弃。

### 9.8 多会话聊天窗口（第 10 项）

原实现 `Friend` 只持有**一个**全局 `Chat* m_pChat`，`reshandler.cpp::chat()` 每收到一条消息
就把它当作当前会话：

```cpp
// 原写法：每次收到消息都覆盖全局会话对象
m_pChat->m_strChatName = strSender;
```

后果：A 和 B 的消息混进同一个窗口，界面上的"聊天对象"被最后一次消息的发送者覆盖，
回复会发给**最后说话的人**而不是窗口标题显示的人——多人聊天时会串消息、发错人。

修复：按好友维度维护窗口表，会话对象只在**创建窗口时**赋值一次，之后不再被任何消息改写。

```cpp
QMap<QString, Chat*> m_chatMap;   // 好友名 -> 聊天窗口

Chat* Friend::openChat(const QString& strPeerName)
{
    Chat* pChat = m_chatMap.value(strPeerName, nullptr);
    if (!pChat) {
        pChat = new Chat;
        pChat->m_strChatName = strPeerName;                       // 只在这里赋值
        pChat->setWindowTitle(QString("与 %1 聊天").arg(strPeerName));
        m_chatMap.insert(strPeerName, pChat);
    }
    return pChat;
}
```

`reshandler.cpp::chat()` 改为按 `caData[0:32]`（发送者）定位窗口后追加内容，
不再触碰任何全局字段；析构时 `qDeleteAll(m_chatMap)`。

代价：窗口数量随聊天过的好友数增长，一个好友一个窗口，没有做数量上限。
对本项目（演示规模）可以接受，真要长期运行应加 LRU 关窗或改成单窗口多会话页签。

### 9.9 测试脚本与验证方式

`tests/` 下的脚本**手工构造 PDU 字节流**，不依赖 GUI，可直接复现上述修复：

| 脚本 | 覆盖内容 | 结果 |
|---|---|---|
| `tests/build.ps1` | 构建两端，失败即中断 | — |
| `tests/test_protocol.ps1` | 粘包（两包一次写）、半包（40+36 分两次写）、非法包长（10 字节）、超大包长（100MB）、异常后服务存活 | 6 / 6 通过 |
| `tests/test_heartbeat.ps1` | 心跳应答、重复登录互踢、异常掉线 35s 超时回收 | 8 / 8 通过 |
| `tests/test_security.ps1` | SQL 注入被拒、口令哈希格式、错误口令、路径越权（列目录 / mkdir，并验证未落盘）、合法路径回归、历史明文自动升级 | 14 / 14 通过 |
| `tests/test_auth.ps1` | 未登录调用业务接口被拒、冒名聊天 / 加好友 / 删好友 / 查好友列表被拒、合法操作未误杀；互踢后 `online` 仍为 1、唯一连接断开后归 0 | 18 / 18 通过 |
| `tests/test_offline.ps1` | 离线落库、登录补发顺序（`LOGIN_RESPOND` 在前）、补发后已读、重新登录不重发、在线直投不落库、冒名发送被拒 | 17 / 17 通过 |

> `test_protocol.ps1` 的用例需要**先登录**才能触发业务应答（服务端已加登录态校验），
> 脚本里封装了 `New-AuthenticatedClient`。

### 9.10 已知取舍（暂不处理）

- 未引入单元测试框架，验证方式为协议级脚本 + 双客户端手动联调；
- 服务端无界面，运行日志依赖 `qDebug()` 输出；
- 服务端与客户端地址通过 `.qrc` 编译进可执行文件，修改需重新构建；
- 好友关系以无向表存储，查询时需 `union` 双向匹配；数据量大时可冗余存储双向关系换取查询性能；
- 未做 TLS，协议为明文传输，仅适用于内网 / 本机演示场景；
- `caData` 里的身份是"客户端自称 + 服务端校验"，仍依赖两端字段约定一致；
  更彻底的做法是身份只由会话决定、协议里根本不传"我是谁"，但那需要改客户端与协议格式。

---

## 十、可考察的知识点

| 知识点 | 项目对应实现 |
|---|---|
| TCP 粘包 / 半包 | 接收缓冲区 + 长度区间校验 + 循环切分；`static_assert(sizeof(PDU)==76)` 编译期锁定头部布局 |
| 为什么选择 TCP | 聊天与文件传输要求可靠、有序，TCP 自带可靠传输，适合自定义协议封装 |
| 二进制协议 vs JSON | 自定义 PDU 紧凑高效、解析快、字段定长易对齐；代价是可读性差、`caData[64]` 扩展性受限 |
| 单例模式 | `Client` / `Index` / `File` / `Server` / `MyTcpServer` / `operateDB` 均为单例：Meyers 局部静态变量（C++11 线程安全）+ `delete` 拷贝构造与赋值 |
| 信号槽机制 | `readyRead → recvMsg` 异步收发、`disconnected → clineOffliine` 断线清理、`on_xxx_clicked` 自动连接 |
| 断线处理 | `disconnected` 信号 → 判定是否仍有同名在线连接 → 无则 `online` 置 0 → `deleteLater()`；异常掉线由心跳超时兜底 |
| 连接保活 | 客户端 15s 心跳 + 服务端 35s 超时巡检；遍历连接列表用副本 + `QPointer`，避免同步删除导致迭代器失效 |
| 服务端并发模型 | 单线程事件循环 + 非阻塞 IO（Qt 内部封装 select / epoll） |
| 内存管理 | `malloc` / `free` 配对、谁创建谁释放、柔性数组、转发前 `clonePdu()` 明确所有权 |
| 数据库访问 | `QSqlDatabase` + `QSqlQuery`，全局单例复用连接；全部预处理绑定 |
| 口令安全 | 每用户随机盐 + SHA-256，不存明文；旧数据登录时惰性升级 |
| 路径安全 | `QDir::cleanPath` 消解 `..` 后与用户根目录做前缀比较（带分隔符，避免 `/alice2` 命中 `/alice`） |
| 协议双端一致性 | 两份 `protocol.h` 必须逐字节一致（见 5.1），枚举只能追加不能插队 |
| 离线消息 / 投递语义 | 对方不在线时落 `message` 表，登录后排队补发；"先查未读 → 下发 → 标记已读"的 at-least-once 取舍 |
| 客户端多会话 | `QMap<QString, Chat*>` 按好友维度维护窗口，会话对象只在创建时赋值，避免串消息 |
| 弱指针防悬空 | `QPointer<MyTcpSocket>` + `Qt::QueuedConnection` 排队回调，跨事件循环访问已析构对象时自动判空 |

---

## 十一、说明

- 本仓库已通过 `.gitignore` 排除 `debug/`、`release/`、`Makefile*`、`moc_*`、`ui_*.h`、`*.o`、`*.exe`、`*.pro.user` 等编译产物与本机 IDE 配置，仅保留源码。
- 数据库若从旧版本升级，请执行 [`sql/migrate_v2.sql`](sql/migrate_v2.sql)：
  先补 `friend.id` 主键列，再加 `salt` 列、`pwd` 扩容、好友关系去重与唯一键。
  旧版 `friend` 表只有 `(user_id, friend_id)` 两列，去重语句依赖 `id` 判重，
  缺这一步会直接报 `Unknown column 'f1.id'`。
