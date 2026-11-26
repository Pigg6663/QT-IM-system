#include "client.h"
#include "friend.h"
#include "onlineuser.h"
#include "protocol.h"
#include "ui_friend.h"

#include <QInputDialog>
#include <QMessageBox>
#include <QtAlgorithms>

Friend::Friend(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::Friend)
{
    ui->setupUi(this);
    m_pOnlineUser=new OnlineUser;
}

Friend::~Friend()
{
    // 聊天窗口是独立顶层窗口（无父对象），必须显式释放
    qDeleteAll(m_chatMap);
    m_chatMap.clear();
    delete ui;
    delete m_pOnlineUser;
}

Chat* Friend::openChat(const QString& strPeerName)
{
    if (strPeerName.isEmpty()) {
        return nullptr;
    }
    Chat* pChat = m_chatMap.value(strPeerName, nullptr);
    if (!pChat) {
        pChat = new Chat;
        // 只在创建时设定一次会话对象，之后不再被其它会话覆盖
        pChat->m_strChatName = strPeerName;
        pChat->setWindowTitle(QString("与 %1 聊天").arg(strPeerName));
        m_chatMap.insert(strPeerName, pChat);
    }
    return pChat;
}

Chat* Friend::appendIncoming(const QString& strPeerName, const QString& strContent)
{
    Chat* pChat = openChat(strPeerName);
    if (!pChat) {
        return nullptr;
    }
    pChat->updateShow_TE(strContent);
    if (pChat->isHidden()) {
        pChat->show();
    }
    pChat->raise();
    pChat->activateWindow();
    return pChat;
}

void Friend::on_findUser_clicked()
{
    QString strName=QInputDialog::getText(this,"提示","用户名：");
    if(strName.isEmpty()||strName.toStdString().size()>32){
        QMessageBox::critical(this,"提示","长度非法");
        return;
    }
    PDU* pdu=mkPDU();
    memcpy(pdu->caData,strName.toStdString().c_str(),32);
    pdu->uiType=ENUM_TYPE_FIND_USER_REQUEST;
    Client::getInstance().sendMsg(pdu);
}

void Friend::on_online_PB_clicked()
{
    if(m_pOnlineUser->isHidden())
    {
        m_pOnlineUser->show();
    }
    PDU* pdu=mkPDU();
    pdu->uiType=ENUM_TYPE_ONLINEUSER_REQUEST;
    Client::getInstance().sendMsg(pdu);
}

void Friend::flushFriend()
{
    PDU* pdu = mkPDU();
    pdu->uiType=ENUM_TYPE_FLUSH_FRIEND_REQUEST;
    memcpy(pdu->caData,
           Client::getInstance().m_strLoginName.toStdString().c_str(),32);
          Client ::getInstance().sendMsg(pdu);
}

void Friend::update_LW(QStringList friendList)
{
    ui->listWidget->clear();
    ui->listWidget->addItems(friendList);
}   

void Friend::on_flush_PB_clicked()
{
    flushFriend();
}

void Friend::on_del_PB_clicked()
{
    QListWidgetItem* pItem=ui->listWidget->currentItem();
    if(!pItem)
    {
        return;
    }
    QString strTarName=pItem->text();
    int ret = QMessageBox::question(this, "删除好友",QString("是否确定删除好友 %1").arg(strTarName));
    if (ret != QMessageBox::Yes) {
        return;
    }

    PDU* pdu = mkPDU(0);
    pdu->uiType = ENUM_TYPE_DEL_FRIEND_REQUEST;
    QString strCurName = Client::getInstance().m_strLoginName;
    memcpy(pdu->caData, strCurName.toStdString().c_str(), strCurName.size());
    memcpy(pdu->caData+32, strTarName.toStdString().c_str(), strTarName.size());

    Client::getInstance().sendMsg(pdu);
}

void Friend::on_chat_PB_clicked()
{
    QListWidgetItem* pItem = ui->listWidget->currentItem();
    if (!pItem) {
        return;
    }

    // 每个好友一个独立窗口：重复点击只是把已存在的窗口提到前面
    Chat* pChat = openChat(pItem->text());
    if (!pChat) {
        return;
    }
    pChat->show();
    pChat->raise();
    pChat->activateWindow();
}
