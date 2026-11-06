# Qt-IM-System

基于 **Qt5 + TCP 自定义二进制协议 + MySQL** 的即时通讯与文件传输系统（C/S 架构）。

客户端提供用户注册 / 登录、好友管理、在线聊天、**个人网盘**（上传、下载、分享、重命名、移动、删除）等功能；
服务端基于 `QTcpServer` 事件驱动地处理请求、访问数据库、转发消息，并直接管理每个用户的网盘目录。

> 本项目为个人学习实践项目，完整覆盖了 网络编程、自定义协议设计、数据库设计、Qt 信号槽与单例模式 等后端 / 客户端核心知识点。

---

## 一、技术栈

| 分类 | 技术 |
|---|---|
| 语言 / 标准 | C++11 |
| 框架 | Qt5（Core、Gui、Network、Sql、Widgets） |
| 网络 | TCP（`QTcpServer` / `QTcpSocket`），自定义二进制协议（PDU） |
| 数据库 | MySQL + QMYSQL 驱动（`QSqlDatabase` / `QSqlQuery`） |
| 构建 | qmake（Qt Creator 工程） |
| UI | Qt Designer（`.ui` 文件）+ QStackedWidget 多页面 |

---

## 二、目录结构

```
.
├── Client/                     # 客户端工程
│   ├── Client.pro
│   ├── main.cpp
│   ├── client.{h,cpp}          # 单例：登录/注册窗口，持有 QTcpSocket
│   ├── index.{h,cpp,ui}        # 单例：登录后主界面（QStackedWidget 两页）
│   ├── friend.{h,cpp,ui}       # 好友页：查找/刷新/删除好友、发起聊天
│   ├── onlineuser.{h,cpp,ui}   # 在线用户列表，双击发起加好友
│   ├── chat.{h,cpp,ui}         # 聊天窗口
│   ├── file.{h,cpp,ui}         # 网盘页：目录浏览与全部文件操作
│   ├── reshandler.{h,cpp}      # 响应分发层：处理所有 RESPOND / RESEND
│   ├── protocol.{h,cpp}        # 协议定义（与服务端保持一致）
│   └── connect.config.txt      # 服务端 IP / 端口（编译进 qrc 资源）
│
├── Server/                     # 服务端工程
│   ├── Server.pro
│   ├── main.cpp
│   ├── server.{h,cpp}          # 单例：读取配置、启动监听、网盘根目录
│   ├── mytcpserver.{h,cpp}     # 单例：继承 QTcpServer，管理连接与消息转发
│   ├── mytcpsocket.{h,cpp}     # 每个连接一个：拆包 -> 分发 -> 回包
│   ├── msghandler.{h,cpp}      # 业务逻辑层：20 个业务处理函数
│   ├── operatedb.{h,cpp}       # 单例：数据库访问封装
│   ├── protocol.{h,cpp}        # 协议定义（与客户端保持一致）
│   └── connect.config.txt      # 监听 IP / 端口（编译进 qrc 资源）
│
├── sql/init.sql                # 数据库建库建表脚本
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

共 20 种业务消息，均为 `REQUEST` / `RESPOND` 成对出现，另有 2 个 `RESEND`（服务端转发）类型：

注册、登录、查找用户、在线用户列表、加好友、同意加好友、刷新好友、删除好友、聊天、
新建文件夹、刷新文件列表、删除文件/文件夹、重命名、移动文件、上传、下载、分享文件。

### 3. 拆包方式（解决 TCP 粘包问题）

包头记录总长度，接收端「先读 4 字节总长，再按长度读完整包体」：

```cpp
uint uiPDULen = 0;
this->read((char*)&uiPDULen, sizeof(uint));
uint uiMsgLen = uiPDULen - sizeof(PDU);
PDU* pdu = mkPDU(uiMsgLen);
this->read((char*)pdu + sizeof(uint), uiPDULen - sizeof(uint));
```

> ⚠️ 当前实现假定一次 `readyRead` 恰好收到一个完整包，**未处理半包 / 粘包**（没有接收缓冲区累积与循环解析）。
> 这是本项目已知的主要局限，改进方案见第八节。

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
    name   VARCHAR(32) UNIQUE,
    pwd    VARCHAR(32),
    online INT DEFAULT 0          -- 0 离线 / 1 在线
);

CREATE TABLE friend (
    user_id   INT,                -- 外键 -> user_info.id
    friend_id INT                 -- 外键 -> user_info.id
);
```

- **在线状态**：登录置 `online = 1`，断线置 `0`，注册默认 `0`；在线用户列表即 `select name from user_info where online = 1`。
- **好友关系无向**：查询时做双向匹配（`union`）：

```sql
SELECT name FROM user_info
WHERE id IN (
    SELECT user_id   FROM friend WHERE friend_id = (SELECT id FROM user_info WHERE name = '%1')
    UNION
    SELECT friend_id FROM friend WHERE user_id   = (SELECT id FROM user_info WHERE name = '%1')
) AND online = 1;
```

完整建库脚本见 [`sql/init.sql`](sql/init.sql)。

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

---

## 八、已发现的不足与改进方向

> 本节是开发完成后的自我 code review 记录，按「健壮性 → 功能 → 安全」排序，并标注了对应文件，便于后续迭代。
> 其中 8.4 属代码清晰度改进，不是功能缺陷。

### 8.1 健壮性缺陷（建议优先修复）

**1. 拆包未处理半包 / 粘包**

`Server/mytcpsocket.cpp` 的 `readMsg()` 假定一次 `readyRead` 恰好到达一个完整 PDU：

- 未校验 `bytesAvailable()`，短读时剩余字节滞留在 socket 缓冲区，与下一个包的残体错位；
- 一次 `readyRead` 内到达多个 PDU 时，只有第一个被解析，其余被忽略；
- `uiMsgLen = uiPDULen - sizeof(PDU)` 为 `uint` 运算，当 `uiPDULen < 76` 时下溢成极大值并直接传给 `malloc`。

**改进方案**：为每个连接维护接收缓冲区，循环判断「缓冲区 ≥ 4 字节 → 取出总长 → 校验 `sizeof(PDU) ≤ uiPDULen ≤ 上限` → 缓冲区 ≥ uiPDULen 时切出整包」，客户端的收包逻辑同步修改。

**2. 无心跳保活，异常掉线无法感知**

服务端仅在 `QTcpSocket::disconnected` 时把 `online` 置 0。客户端进程被杀、拔网线等场景不会触发该信号，用户会长期停留在「在线」状态，好友列表与消息转发都会失效。

**改进方案**：客户端定时（如 30s）发送心跳请求，服务端记录每个连接的最后活跃时间，超时即主动断开并置 `online = 0`。

**3. 服务端单线程处理**

服务端运行在单个 `QApplication` 事件循环中，所有连接的拆包、业务处理、数据库访问与文件读写都在主线程串行执行，任一慢操作（大文件读写、慢 SQL）都会阻塞全部连接。

**改进方案**：连接层保留在主线程（`QTcpSocket` 有线程亲和性，不能跨线程直接操作），业务处理投递到线程池；数据库访问改为每线程独立连接或引入连接池（`QSqlDatabase` 不支持跨线程共享同一连接）。

### 8.2 功能局限

**4. 大文件整包读入内存**

`MsgHandler::downloadFile()` 与 `shareFile()` 使用 `QFile::readAll()` 把整个文件读入内存再拼进单条 PDU；上传方向同样由客户端一次性发送整包。文件大小同时受 `uiTotalLen`（4 字节）与可用内存限制。

**改进方案**：固定分片 + 分片序号 + 校验和，配合断点续传与客户端进度条。

**5. 无离线消息**

聊天消息不落库，`MsgHandler::chat()` 仅做转发，对方离线时消息被静默丢弃。

**改进方案**：新增 `message` 表持久化；对方离线则入库，其登录后主动推送未读消息。

**6. 聊天窗口全局复用（会导致跨会话串消息）**

`Client/friend.h` 中 `Friend` 只持有一个 `Chat* m_pChat`；`ResHandler` 收到 `CHAT_RESEND` 时无条件调用同一个实例的 `updateShow_TE()`，并把 `m_strChatName` 覆盖为最后发言者（`Client/reshandler.cpp:152-153`）。

**实际后果**：A、B 同时发来消息会显示在同一个文本框；回复时 `m_strChatName` 已被最后一条消息改写，**消息可能发给错误的人**。

**改进方案**：按好友维度维护 `QMap<QString, Chat*>` 窗口管理器，或改造为单窗口 + 多会话标签页，回复时以会话键作为目标用户名，而不是依赖全局字段。

### 8.3 安全改进

**7. 服务端完全信任客户端传入的路径**

所有网盘操作（`mkdir` / `delDir` / `renameFile` / `moveFile` / `uploadFile` / `downloadFile` / `shareFile`）都直接把 `pdu->caMsg` 当作路径使用，服务端不做根目录约束。`Server::m_strRootPath` 只在注册建目录时用过一次，**从未用于路径校验**。篡改客户端或直接构造协议包，即可读写服务器进程有权限的任意路径。

**改进方案**：根目录由「服务端会话中记录的登录名」拼接得出，对客户端路径执行 `QDir::cleanPath()` 后断言其以 `<root>/<username>/` 前缀开头，越界请求一律拒绝。

**8. 密码明文存储**

`sql/init.sql` 中 `pwd VARCHAR(32)` 存明文，`operateDB::handleLogin()` 以明文直接比对。

**改进方案**：`SHA-256 + 随机 salt` 存储，登录时比对哈希值。

**9. SQL 全部为字符串拼接**

`Server/operatedb.cpp` 中全部 SQL 通过 `QString::arg()` 拼接，用户输入（用户名、密码）直接进入 SQL 文本，存在注入风险。

**改进方案**：统一改为 `QSqlQuery::prepare()` + `bindValue()` 预处理绑定。

### 8.4 代码清晰度改进（非功能缺陷）

**10. 跨类型比较依赖隐式转换**

`Server/mytcpserver.cpp` 的 `resend()` 中使用 `caTarName == m_strLoginName`（`char*` 与 `QString`）。该表达式会命中 Qt 提供的 `operator==(const char *, const QString &)` 重载（见 `qstring.h`），**执行的是字符串内容比较，逻辑正确**；但它依赖 `char*` 的隐式 ASCII 转换，且前提是 `caData` 中的目标用户名已按 0 填充（由 `mkPDU()` 的 `memset` 保证）。

**建议**：改写为显式比较以消除歧义，例如 `strcmp(caTarName, m_strLoginName.toUtf8().constData()) == 0`，或在建包处统一使用 `QString` 与明确的编码约定。

**11. 转发时的包体所有权约定不够明确**

`MsgHandler::chat()` / `addFriend()` 等把原始 PDU 改类型后直接转发，随后由 `handleMsg()` 统一 `free()`。**当前是安全的**：`QTcpSocket::write()` 会把数据拷贝进内部写缓冲区，写入与释放互不影响；也不会 double free，因为转发后函数返回 `nullptr`，`sendMsg(nullptr)` 直接返回。

**建议**：这种「同一个 PDU 既转给别人又自己释放」的写法在后续改造中容易演变成悬空指针，建议转发时显式复制一份独立 PDU，或在接口上注释清楚「转发内部不保留指针」的所有权契约。

### 8.5 已知取舍（暂不处理）

- 未编写单元测试，验证方式为双客户端手动联调；
- 服务端无界面，运行日志依赖 `qDebug()` 输出；
- 服务端与客户端地址通过 `.qrc` 编译进可执行文件，修改需重新构建；
- 好友关系以无向表存储，查询时需 `union` 双向匹配；数据量大时可改为冗余存储双向关系，换取查询性能；
- 未做 TLS，协议为明文传输，仅适用于内网/本机演示场景。

## 九、说明

- 本仓库已通过 `.gitignore` 排除 `debug/`、`release/`、`Makefile*`、`moc_*`、`ui_*.h`、`*.o`、`*.exe`、`*.pro.user` 等编译产物与本机 IDE 配置，仅保留源码。

