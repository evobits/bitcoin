#ifndef ENHUPLOADHDKEYDIALOG_H
#define ENHUPLOADHDKEYDIALOG_H

#include <QDialog>
class QNetworkAccessManager;
class QNetworkReply;

class PlatformStyle;

namespace Ui {
class EnhUploadHDKeyDialog;
}

class EnhUploadHDKeyDialog : public QDialog
{
    Q_OBJECT

public:
    explicit EnhUploadHDKeyDialog(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~EnhUploadHDKeyDialog();

private Q_SLOTS:
    void on_btnChoose_clicked();

private:
    Ui::EnhUploadHDKeyDialog *ui;
};

#endif // ENHUPLOADHDKEYDIALOG_H
