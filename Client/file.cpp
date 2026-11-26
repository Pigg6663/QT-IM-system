#include "client.h"
#include "file.h"
#include "index.h"
#include "protocol.h"
#include "ui_file.h"

#include <QCryptographicHash>
#include <QFile>
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

// ---------------------------------------------------------------
// 第 7 项：上传。不再把整个文件读进 QByteArray，而是
//   1) 分块读一遍算整文件 MD5（每块 256KB，内存有界）
//   2) 把文件名 / 总长 / MD5 交给服务端，由服务端告知断点
//   3) 真正的数据由 Client::pumpUpload 按写缓冲水位线推分片
// ---------------------------------------------------------------
void File::on_upload_PB_clicked()
{
    QString strFilePath = QFileDialog::getOpenFileName(this, "选择上传文件");
    if (strFilePath.isEmpty()) {
        return;
    }

    QFileInfo fileInfo(strFilePath);
    QFile* pFile = new QFile(strFilePath);
    if (!pFile->open(QIODevice::ReadOnly)) {
        delete pFile;
        QMessageBox::information(this, "提示", "无法打开文件");
        return;
    }

    const qint64 llTotal = pFile->size();
    if (llTotal <= 0) {
        pFile->close();
        delete pFile;
        QMessageBox::information(this, "提示", "不能上传空文件");
        return;
    }

    // 第一遍：流式算 MD5，为的是让服务端能用它判断 .part 是否完整
    QCryptographicHash hash(QCryptographicHash::Md5);
    while (!pFile->atEnd()) {
        const QByteArray block = pFile->read(FILE_CHUNK_SIZE);
        if (block.isEmpty()) {
            break;
        }
        hash.addData(block);
    }
    const QByteArray md5Raw = hash.result();
    pFile->seek(0);

    const QString strFileName = fileInfo.fileName();
    const QByteArray nameBytes = strFileName.toUtf8();
    const QByteArray pathBytes = m_strCurPath.toUtf8();

    PDU* pdu = mkPDU(pathBytes.size() + 1);
    pdu->uiType = ENUM_TYPE_UPLOAD_FILE_REQUEST;
    memcpy(pdu->caData, nameBytes.constData(), size_t(qMin(32, nameBytes.size())));
    memcpy(pdu->caData + UPLOAD_SIZE_POS, &llTotal, sizeof(qint64));
    memcpy(pdu->caData + UPLOAD_MD5_POS, md5Raw.constData(), MD5_RAW_LEN);
    memcpy(pdu->caMsg, pathBytes.constData(), size_t(pathBytes.size()));

    Client::getInstance().beginUpload(pFile, llTotal, md5Raw.toHex());
    Client::getInstance().sendMsg(pdu);
}

// ---------------------------------------------------------------
// 第 7 项：下载。先让用户选保存位置，本地已有非空文件时询问是否续传，
// 之后数据由服务端主动推，客户端在 ResHandler 里边收边写边算 MD5。
// ---------------------------------------------------------------
void File::on_download_PB_clicked()
{
    QListWidgetItem* pItem = ui->listWidget->currentItem();
    if (!pItem) {
        QMessageBox::information(this, "提示", "请选择要下载的文件");
        return;
    }
    QString strFileName = pItem->text();

    const QString strSavePath = QFileDialog::getSaveFileName(this, "保存文件", strFileName);
    if (strSavePath.isEmpty()) {
        return;
    }

    qint64 llResume = 0;
    const QFileInfo localInfo(strSavePath);
    if (localInfo.exists() && localInfo.isFile() && localInfo.size() > 0) {
        const QMessageBox::StandardButton btn = QMessageBox::question(
            this, "续传确认",
            QString("本地已存在 %1（%2 字节），是否从断点续传？\n选“否”将从零开始下载并覆盖。")
                .arg(strFileName).arg(localInfo.size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (btn == QMessageBox::Yes) {
            llResume = localInfo.size();
        }
    }
    // ResHandler 需要知道往哪写，握手应答回来时它才打开文件
    Client::getInstance().m_prh->m_downloadSavePath = strSavePath;

    const QByteArray nameBytes = strFileName.toUtf8();
    const QByteArray pathBytes = m_strCurPath.toUtf8();

    PDU* pdu = mkPDU(pathBytes.size() + 1);
    pdu->uiType = ENUM_TYPE_DOWNLOAD_FILE_REQUEST;
    memcpy(pdu->caData, nameBytes.constData(), size_t(qMin(32, nameBytes.size())));
    memcpy(pdu->caData + DL_RESUME_POS, &llResume, sizeof(qint64));
    memcpy(pdu->caMsg, pathBytes.constData(), size_t(pathBytes.size()));

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
