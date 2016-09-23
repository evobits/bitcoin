#ifndef ENHOPTIONSDIALOG_H
#define ENHOPTIONSDIALOG_H

#include <QDialog>
class QNetworkAccessManager;
class QNetworkReply;

class PlatformStyle;

#define BITCOIN_ENH_CONF_BASE_FILENAME "enh_options.json"

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

    void saveEnhOptions();
    void loadEnhOptions();
};

#endif // ENHOPTIONSDIALOG_H
