#ifndef FILE_H
#define FILE_H

#include <QListWidgetItem>
#include <QWidget>

namespace Ui {
class File;
}

class File : public QWidget
{
    Q_OBJECT

public:
    struct FileInfo {
        char caName[32];
        int iFileType;
    };

    QString m_strUserPath;
    QString m_strCurPath;
    explicit File(QWidget *parent = nullptr);
    ~File();
    void updateFileList(QList<FileInfo *>pFileInfoList);
    QList<File::FileInfo *> m_pFileInfoList;
    static File& getInstance();
    void flushFile();

private slots:
    void on_mkdir_PB_clicked();
    void on_flush_pb_clicked();
    void on_return_PB_clicked();
    void on_del_PB_clicked();
    void on_rename_PB_clicked();
    void on_pushButton_6_clicked();
    void on_upload_PB_clicked();
    void on_download_PB_clicked();
    void on_share_PB_clicked();
    void on_listWidget_itemDoubleClicked(QListWidgetItem *item);

private:
    Ui::File *ui;
    void enterDir(const QString &strDirName);
    void returnPreDir();
};

#endif // FILE_H
