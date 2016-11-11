#ifndef ENHOPTIONSDIALOG_H
#define ENHOPTIONSDIALOG_H

#include <QDialog>
class QNetworkAccessManager;
class QNetworkReply;

class PlatformStyle;

#define BITCOIN_ENH_CONF_BASE_FILENAME "enh_options.json"
#define BITCOIN_ENH_API_URL "http://api.coinpay.co/api/v1"

namespace Ui {
class EnhOptionsDialog;
}

class EnhOptionsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit EnhOptionsDialog(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~EnhOptionsDialog();

private Q_SLOTS:
    void addressGenAuthFinished(QNetworkReply* reply);
    void addressGenFinishedPost(QNetworkReply* reply);
    void on_generateAndSendButton_clicked();

private:
    Ui::EnhOptionsDialog *ui;
    QNetworkAccessManager *net;
    QString api_url;

    void saveEnhOptions();
    void loadEnhOptions();
};

#endif // ENHOPTIONSDIALOG_H
