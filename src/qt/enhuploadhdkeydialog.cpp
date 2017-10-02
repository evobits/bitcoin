#include "enhuploadhdkeydialog.h"
#include "ui_enhuploadhdkeydialog.h"

#include "bitcoinunits.h"
#include "guiutil.h"
#include "optionsmodel.h"

#include "validation.h" // for DEFAULT_SCRIPTCHECK_THREADS and MAX_SCRIPTCHECK_THREADS
#include "netbase.h"
#include "txdb.h" // for -dbcache defaults

#include "base58.h"
#include "init.h"
#include "pubkey.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h" // for CWallet::GetRequiredFee()
#endif

#include <boost/thread.hpp>
#include <boost/algorithm/string/join.hpp>

#include <QApplication>
#include <QDataWidgetMapper>
#include <QDir>
#include <QFile>
#include <QIntValidator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLocale>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>


EnhUploadHDKeyDialog::EnhUploadHDKeyDialog(const PlatformStyle *platformStyle, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::EnhUploadHDKeyDialog)
{
    ui->setupUi(this);
}

EnhUploadHDKeyDialog::~EnhUploadHDKeyDialog()
{
    delete ui;
}

void EnhUploadHDKeyDialog::on_btnChoose_clicked()
{
#ifdef ENABLE_WALLET
    QMessageBox::critical(this, tr("Information"), tr("Something something wallet"), QMessageBox::Ok, QMessageBox::Ok);
#else
    QMessageBox::critical(this, tr("Error"), tr("Wallet not compiled in"), QMessageBox::Ok, QMessageBox::Ok);
#endif
}