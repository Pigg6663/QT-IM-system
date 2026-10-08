#include "client.h"
#include "file.h"
#include "index.h"
#include "protocol.h"
#include "ui_file.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>

File::File(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::File)
{
    ui->setupUi(this);
    m_strUserPath = QString("%1/%2").arg(Client::getInstance().m_strRootPath).arg(Client::getInstance().m_strLoginName);
    m_strCurPath = m_strUserPath;
}

File::~File()
{
    delete ui;
}

File &File::getInstance()
{
    return *Index::getInstance().getFile();
}

void File::on_mkdir_PB_clicked()
{
    QString strDirName = QInputDialog::getText(this, "新建文件夹", "新建文件夹名: ");
    if (strDirName.isEmpty() || strDirName.toStdString().size() > 32) {
        QMessageBox::information(this, "提示", "文件夹名长度非法");
        return;
    }
    PDU* pdu = mkPDU(m_strCurPath.toStdString().size()+1);
    pdu->uiType = ENUM_TYPE_MKDIR_REQUEST;
    memcpy(pdu->caData, strDirName.toStdString().c_str(), 32);
    memcpy(pdu->caMsg, m_strCurPath.toStdString().c_str(), m_strCurPath.toStdString().size());
    Client::getInstance().sendMsg(pdu);
}

void File::flushFile()
{
    PDU* pdu = mkPDU(m_strCurPath.toStdString().size()+1);
    pdu->uiType = ENUM_TYPE_FLUSH_FILE_REQUEST;
    memcpy(pdu->caMsg, m_strCurPath.toStdString().c_str(),
    m_strCurPath.toStdString().size());
    Client::getInstance().sendMsg(pdu);
}

void File::on_flush_pb_clicked()
{
    flushFile();
}

void File::updateFileList(QList<File::FileInfo*>  pFileInfoList)
{
    foreach (FileInfo * pFileInfo, m_pFileInfoList) {
        delete pFileInfo;
    }
    m_pFileInfoList.clear();

    m_pFileInfoList = pFileInfoList;

    ui->listWidget->clear();
    foreach (FileInfo * pFileInfo, pFileInfoList) {
        QListWidgetItem* pItem = new QListWidgetItem;
        if (pFileInfo->iFileType == 0) {
            pItem->setIcon(QIcon(QPixmap(":/dir.png")));
        } else {
            pItem->setIcon(QIcon(QPixmap(":/file.png")));
        }
        pItem->setText(pFileInfo->caName);
        ui->listWidget->addItem(pItem);
    }
}

void File::enterDir(const QString &strDirName)
{
    if (strDirName == ".") {
        return;
    }
    if (strDirName == "..") {
        returnPreDir();
        return;
    }
    m_strCurPath = QString("%1/%2").arg(m_strCurPath).arg(strDirName);
    flushFile();
}

void File::returnPreDir()
{
    if (m_strCurPath == m_strUserPath) {
        QMessageBox::information(this, "提示", "已经是根目录");
        return;
    }
    int index = m_strCurPath.lastIndexOf('/');
    if (index != -1) {
        m_strCurPath = m_strCurPath.left(index);
    }
    flushFile();
}

void File::on_return_PB_clicked()
{
    returnPreDir();
}

void File::on_del_PB_clicked()
{
    QListWidgetItem* pItem = ui->listWidget->currentItem();
    if (!pItem) {
        QMessageBox::information(this, "提示", "请选择要删除的文件或文件夹");
        return;
    }
    QString strFileName = pItem->text();
    int ret = QMessageBox::question(this, "删除", QString("确定要删除 %1 吗？").arg(strFileName));
    if (ret != QMessageBox::Yes) {
        return;
    }

    PDU* pdu = mkPDU(m_strCurPath.toStdString().size()+1);
    pdu->uiType = ENUM_TYPE_DEL_DIR_REQUEST;
    memcpy(pdu->caData, strFileName.toStdString().c_str(), 32);
    memcpy(pdu->caMsg, m_strCurPath.toStdString().c_str(), m_strCurPath.toStdString().size());
    Client::getInstance().sendMsg(pdu);
}

void File::on_rename_PB_clicked()
{
    QListWidgetItem* pItem = ui->listWidget->currentItem();
    if (!pItem) {
        QMessageBox::information(this, "提示", "请选择要重命名的文件或文件夹");
        return;
    }
    QString strOldName = pItem->text();
    QString strNewName = QInputDialog::getText(this, "重命名", "新名称：", QLineEdit::Normal, strOldName);
    if (strNewName.isEmpty() || strNewName.toStdString().size() > 32) {
        QMessageBox::information(this, "提示", "名称长度非法");
        return;
    }
    if (strNewName == strOldName) {
        return;
    }

    PDU* pdu = mkPDU(m_strCurPath.toStdString().size()+1);
    pdu->uiType = ENUM_TYPE_RENAME_FILE_REQUEST;
    memcpy(pdu->caData, strOldName.toStdString().c_str(), 32);
    memcpy(pdu->caData+32, strNewName.toStdString().c_str(), 32);
    memcpy(pdu->caMsg, m_strCurPath.toStdString().c_str(), m_strCurPath.toStdString().size());
    Client::getInstance().sendMsg(pdu);
}

void File::on_pushButton_6_clicked()
{
    QListWidgetItem* pItem = ui->listWidget->currentItem();
    if (!pItem) {
        QMessageBox::information(this, "提示", "请选择要移动的文件或文件夹");
        return;
    }
    QString strFileName = pItem->text();
    QString strTargetDir = QInputDialog::getText(this, "移动文件", "目标路径：");
    if (strTargetDir.isEmpty()) {
        return;
    }

    QString strMsg = QString("%1\n%2").arg(m_strCurPath).arg(strTargetDir);
    PDU* pdu = mkPDU(strMsg.toStdString().size()+1);
    pdu->uiType = ENUM_TYPE_MOVE_FILE_REQUEST;
    memcpy(pdu->caData, strFileName.toStdString().c_str(), 32);
    memcpy(pdu->caData+32, strTargetDir.toStdString().c_str(), 32);
    memcpy(pdu->caMsg, strMsg.toStdString().c_str(), strMsg.toStdString().size());
    Client::getInstance().sendMsg(pdu);
}

void File::on_upload_PB_clicked()
{
    QString strFilePath = QFileDialog::getOpenFileName(this, "选择上传文件");
    if (strFilePath.isEmpty()) {
        return;
    }

    QFile file(strFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::information(this, "提示", "无法打开文件");
        return;
    }

    QByteArray fileData = file.readAll();
    file.close();

    QFileInfo fileInfo(strFilePath);
    QString strFileName = fileInfo.fileName();
    qint64 fileSize = fileData.size();


    QString strSizeStr = QString::number(fileSize);
    int pathLen = m_strCurPath.toStdString().size() + 1;
    int totalMsgLen = pathLen + fileData.size();

    PDU* pdu = mkPDU(totalMsgLen);
    pdu->uiType = ENUM_TYPE_UPLOAD_FILE_REQUEST;
    memcpy(pdu->caData, strFileName.toStdString().c_str(), 32);
    memcpy(pdu->caData+32, strSizeStr.toStdString().c_str(), 32);
    memcpy(pdu->caMsg, m_strCurPath.toStdString().c_str(), pathLen);
    memcpy(pdu->caMsg + pathLen, fileData.constData(), fileData.size());

    Client::getInstance().sendMsg(pdu);
}

void File::on_download_PB_clicked()
{
    QListWidgetItem* pItem = ui->listWidget->currentItem();
    if (!pItem) {
        QMessageBox::information(this, "提示", "请选择要下载的文件");
        return;
    }
    QString strFileName = pItem->text();

    PDU* pdu = mkPDU(m_strCurPath.toStdString().size()+1);
    pdu->uiType = ENUM_TYPE_DOWNLOAD_FILE_REQUEST;
    memcpy(pdu->caData, strFileName.toStdString().c_str(), 32);
    memcpy(pdu->caMsg, m_strCurPath.toStdString().c_str(), m_strCurPath.toStdString().size());
    Client::getInstance().sendMsg(pdu);
}

void File::on_share_PB_clicked()
{
    QListWidgetItem* pItem = ui->listWidget->currentItem();
    if (!pItem) {
        QMessageBox::information(this, "提示", "请选择要分享的文件");
        return;
    }
    QString strFileName = pItem->text();
    QString strTarName = QInputDialog::getText(this, "分享文件", "目标用户名：");
    if (strTarName.isEmpty() || strTarName.toStdString().size() > 32) {
        QMessageBox::information(this, "提示", "用户名非法");
        return;
    }

    PDU* pdu = mkPDU(m_strCurPath.toStdString().size()+1);
    pdu->uiType = ENUM_TYPE_SHARE_FILE_REQUEST;
    memcpy(pdu->caData, strFileName.toStdString().c_str(), 32);
    memcpy(pdu->caData+32, strTarName.toStdString().c_str(), 32);
    memcpy(pdu->caMsg, m_strCurPath.toStdString().c_str(), m_strCurPath.toStdString().size());
    Client::getInstance().sendMsg(pdu);
}

void File::on_listWidget_itemDoubleClicked(QListWidgetItem *item)
{
    QString strFileName = item->text();

    foreach (FileInfo* pFileInfo, m_pFileInfoList) {
        if (QString(pFileInfo->caName) == strFileName) {
            if (pFileInfo->iFileType == 0) {

                enterDir(strFileName);
            }
            break;
        }
    }
}
