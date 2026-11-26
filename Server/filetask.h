#ifndef FILETASK_H
#define FILETASK_H

#include <QObject>
#include <QPointer>

#include <functional>
#include <utility>

// 把"耗时和字节数成正比"的磁盘操作下沉到线程池，做完再把结果投递回原线程。
//
// 为什么只下沉文件 IO，而不是把整套业务都改成多线程：
//   1) QTcpSocket 有线程亲和性，一个连接的读写只能发生在它所属的那个线程里，
//      跨线程 write() 是未定义行为；
//   2) QSqlDatabase 的连接对象同样不能跨线程使用，每个线程都得独立 connect，
//      否则 Qt 会直接报 "QSqlDatabasePrivate::database: requested database
//      does not belong to the connection"。
// 所以连接层和数据库操作仍然留在服务端主线程，只有递归删除几十万个小文件、
// 跨用户拷贝一个大文件这类"慢且与网络无关"的活才交给 QThreadPool。这样慢
// 操作执行期间事件循环还在转，其它连接的心跳、聊天、刷新目录都能正常响应。

namespace FileTask {

// 真正的磁盘操作，在线程池线程里执行，返回"成功与否"
using Work = std::function<bool()>;
// 结果回调，通过排队调用回到 context 所在线程执行
using Done = std::function<void(bool)>;

// 内部实现，业务代码用下面的模板版本即可
void runAsyncImpl(QPointer<QObject> context, Work work, Done done);

// context 是回调的目标对象（必须是有线程亲和性的 QObject）。
// 如果它在 work 执行期间被销毁，done 不会被调用——调用方不需要自己加锁，
// 但要在 done 里重新确认被操作对象是否还活着。
template <class T>
void runAsync(const QPointer<T>& context, Work work, Done done)
{
    runAsyncImpl(QPointer<QObject>(context.data()), std::move(work), std::move(done));
}

}

#endif // FILETASK_H
