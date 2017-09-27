#ifndef ENHUPLOADHDKEYDIALOG_H
#define ENHUPLOADHDKEYDIALOG_H

#include <QDialog>
class QNetworkAccessManager;
class QNetworkReply;

class PlatformStyle;

#define BITCOIN_ENH_CONF_BASE_FILENAME "enh_options.json"
#define BITCOIN_ENH_API_URL "http://api.coinpay.co/api/v1"

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
    void addressGenAuthFinished(QNetworkReply* reply);
    void addressGenFinishedPost(QNetworkReply* reply);
    void on_generateAndSendButton_clicked();

private:
    Ui::EnhUploadHDKeyDialog *ui;
    QNetworkAccessManager *net;
    QString api_url;

    void saveEnhOptions();
    void loadEnhOptions();
};

#endif // ENHUPLOADHDKEYDIALOG_H
