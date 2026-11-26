#include "filetask.h"

#include <QMetaObject>
#include <QRunnable>
#include <QThreadPool>

#include <utility>

namespace {

class FileTaskRunnable : public QRunnable
{
public:
    FileTaskRunnable(const QPointer<QObject>& context, FileTask::Work work, FileTask::Done done)
        : m_context(context)
        , m_work(std::move(work))
        , m_done(std::move(done))
    {
        setAutoDelete(true);
    }

    void run() override
    {
        bool ok = false;
        if (m_work)
        {
            ok = m_work();
        }

        QObject* context = m_context.data();
        if (context == NULL)
        {
            // 目标对象已经销毁（比如用户删完文件就断线了），结果没人接收
            return;
        }

        // 排队投递：done 会在 context 所属线程里执行，调用方可以放心地
        // 在里面碰 socket、数据库连接这些有线程亲和性的东西。
        // 再次用 QPointer 兜住"投递之后、执行之前"对象被销毁的窗口期。
        const QPointer<QObject> contextGuard(m_context);
        FileTask::Done done = std::move(m_done);
        QMetaObject::invokeMethod(context, [contextGuard, done, ok]()
        {
            if (contextGuard)
            {
                done(ok);
            }
        }, Qt::QueuedConnection);
    }

private:
    QPointer<QObject> m_context;
    FileTask::Work m_work;
    FileTask::Done m_done;
};

}

void FileTask::runAsyncImpl(QPointer<QObject> context, Work work, Done done)
{
    if (context.isNull() || !work || !done)
    {
        return;
    }
    QThreadPool::globalInstance()->start(new FileTaskRunnable(context, std::move(work), std::move(done)));
}
