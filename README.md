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

这部分是作者对项目的主动复盘，也是后续迭代的路线图：

**正确性缺陷（优先修复）**

1. **消息转发失效**：`mytcpserver.cpp` 的 `resend()` 中使用 `caTarName == m_strLoginName` 比较 `char*` 与 `QString`，
   实际比较的是裸指针地址，条件几乎恒为 `false`，导致聊天 / 加好友 / 分享文件的转发全部无法命中。
   应改为 `strcmp(caTarName, m_strLoginName.toStdString().c_str()) == 0`。
2. **PDU 生命周期错误**：`chat()`、`addFriend()` 等在 `handleMsg()` 中把同一个 `PDU` 既用于转发又交由 `sendMsg()` 释放，
   存在 **use-after-free / double free** 风险；转发时应拷贝一份独立的 PDU。
3. **拆包不完整**：未校验 `bytesAvailable()`，半包时会读出未初始化的数据；应维护接收缓冲区，循环判断「头部长度 + 包体长度」后再解析。

**架构与健壮性改进**

4. **大文件一次性读入内存**：`readAll()` 整个文件打包，大文件易导致内存暴涨；
   改进为分片传输（固定分片 + 序号）、MD5 校验、断点续传与进度条。
5. **无离线消息**：消息未落库，对方离线即丢弃；改进为消息持久化 + 登录后推送。
6. **服务端单线程**：连接量大时响应变慢；改进为线程池处理业务 + 数据库连接池。
7. **无心跳包**：客户端异常掉线（如拔网线）服务端无法及时感知；改进为定时心跳 + 超时踢下线。
8. **聊天窗口复用**：全局仅一个 `Chat` 实例，与多人聊天会互相覆盖；改进为每好友一个窗口或窗口管理器。
9. **路径穿越风险**：文件操作直接拼接客户端传入的路径，未做规范化与越权校验，可访问到用户目录之外；
   改进为 `QDir::cleanPath` 校验 + 强制限制在用户根目录内。

**安全改进**

10. **密码明文存储**：应改为加盐哈希（如 `SHA-256 + salt`）。
11. **SQL 注入风险**：SQL 全部使用 `QString::arg()` 拼接；
    应改为 `QSqlQuery::prepare()` + `bindValue()` 预处理绑定。

---

## 九、可考察的知识点

| 知识点 | 项目对应实现 |
|---|---|
| TCP 粘包 / 半包 | 包头记录总长度，先读长度再读包体（含已知局限与改进方案） |
| 为什么选择 TCP | 聊天与文件传输要求可靠、有序，TCP 自带可靠传输，适合自定义协议封装 |
| 二进制协议 vs JSON | 自定义 PDU 紧凑高效、解析快、字段定长易对齐；代价是可读性差、`caData[64]` 扩展性受限 |
| 单例模式 | `Client` / `Index` / `File` / `Server` / `MyTcpServer` / `operateDB` 均为单例：Meyers 局部静态变量（C++11 线程安全）+ `delete` 拷贝构造与赋值 |
| 信号槽机制 | `readyRead → recvMsg` 异步收发、`disconnected → clineOffliine` 断线清理、`on_xxx_clicked` 自动连接 |
| 断线处理 | `disconnected` 信号 → 数据库 `online` 置 0 → 从连接列表移除并 `deleteLater()` |
| 服务端并发模型 | 单线程事件循环 + 非阻塞 IO（Qt 内部封装 select / epoll） |
| 内存管理 | `malloc` / `free` 配对、谁创建谁释放、柔性数组、QObject 使用 `deleteLater()` |
| 数据库访问 | `QSqlDatabase` + `QSqlQuery`，全局单例复用连接 |

---

## 十、说明

- 本仓库已通过 `.gitignore` 排除 `debug/`、`release/`、`Makefile*`、`moc_*`、`ui_*.h`、`*.o`、`*.exe`、`*.pro.user` 等编译产物与本机 IDE 配置，仅保留源码。
- `项目总结.md` 是作者整理的中文面试复习文档，包含更详细的模块讲解与问答思路，可作为本 README 的补充阅读。
