#ifndef CHAT_H
#define CHAT_H

#include <QWidget>

namespace Ui {
class Chat;
}

class Chat : public QWidget
{
    Q_OBJECT

public:
    QString m_strChatName;
    explicit Chat(QWidget *parent = nullptr);
    ~Chat();

    void updateShow_TE(QString strMsg);

private slots:
    void on_send_PB_clicked();



private:
    Ui::Chat *ui;
};

#endif // CHAT_H
