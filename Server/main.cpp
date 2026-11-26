#include "operatedb.h"
#include "server.h"

#include<QDebug>

#include <QApplication>
#include <QDateTime>
#include <QFile>
#include <QMutex>

// 可选的日志落盘：设置环境变量 IM_SERVER_LOG 为文件路径即可。
// 服务端在 Windows 上是 GUI 子系统程序，stdout/stderr 不可见，
// 排查问题时靠 qDebug 输出到控制台是行不通的，所以提供一个最小的落盘通道。
// 未设置该环境变量时行为完全不变（仍然是默认的 stderr/debugger 输出）。
static QtMessageHandler s_prevHandler = nullptr;

static void fileMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    static QMutex mutex;
    static QFile file(QString::fromLocal8Bit(qgetenv("IM_SERVER_LOG")));

    {
        QMutexLocker locker(&mutex);
        if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        {
            static const char* const kLevelName[] = {"DEBUG", "WARN", "CRIT", "FATAL", "INFO"};
            const int iLevel = int(type);
            const char* pszLevel = (iLevel >= 0 && iLevel < 5) ? kLevelName[iLevel] : "LOG";

            const QString strLine = QString("%1 [%2] %3\n")
                                        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz"))
                                        .arg(pszLevel)
                                        .arg(msg);
            file.write(strLine.toUtf8());
            file.flush();
            file.close();
        }
    }

    if (s_prevHandler)
    {
        s_prevHandler(type, context, msg);
    }
}

int main(int argc, char *argv[])
{
    if (!qgetenv("IM_SERVER_LOG").isEmpty())
    {
        s_prevHandler = qInstallMessageHandler(fileMessageHandler);
    }

    QApplication a(argc, argv);
    operateDB::getInstance().connectSQL();
    // 必须用单例，不能在栈上再构造一个 Server：MsgHandler 计算用户网盘根路径时
    // 走的是 Server::getInstance()，两个实例会导致第二次 listen() 失败，日志里
    // 每次都会多出一条误导性的"监听失败"警告。
    Server::getInstance();
    //Server::getInstance().show();

    return a.exec();
}
