#include "enhoptionsdialog.h"
#include "ui_enhoptionsdialog.h"

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


EnhOptionsDialog::EnhOptionsDialog(const PlatformStyle *platformStyle, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::EnhOptionsDialog),
    api_url(BITCOIN_ENH_API_URL)
{
    ui->setupUi(this);
    net = new QNetworkAccessManager(this);
    /* By the time this is run, the bitcoin core is running. */
    loadEnhOptions();
    ui->statusLabel->setText(tr("API URL: %1").arg(api_url));
}

EnhOptionsDialog::~EnhOptionsDialog()
{
    saveEnhOptions();
    delete ui;
    delete net;
}

void EnhOptionsDialog::addressGenAuthFinished(QNetworkReply* reply) {
    const QString &s_url = api_url;
    if (reply->error() != QNetworkReply::NoError) {
        /* Though there could be many reasons for this failure, KISS and don't bother the user with them. */
        QMessageBox::critical(this, tr("Error"), tr("Authentication failure for '%1' at '%2'").arg(ui->leUsername->text()).arg(s_url), QMessageBox::Ok, QMessageBox::Ok);
        return;
    }

    const QString &s_wallet_id = ui->leWalletId->text();
    const int n_addr = ui->sbNumAddresses->value();
    const unsigned int DATA_BUF_SIZE = 2048;
    char response_buffer[DATA_BUF_SIZE + 1];

    QByteArray auth_response_buffer = reply->readAll();
    if (auth_response_buffer.isEmpty()) {
        QMessageBox::critical(this, tr("Error"), tr("Error reading auth response from '%1'").arg(s_url), QMessageBox::Ok, QMessageBox::Ok);
        return;
    }

    QJsonParseError err;
    QJsonDocument json_doc = QJsonDocument::fromJson(auth_response_buffer, &err);
    if (err.error != QJsonParseError::NoError) {
        QMessageBox::critical(this, tr("Error"), tr("Error parsing auth response from '%1' (%2)").arg(s_url).arg((int)err.error), QMessageBox::Ok, QMessageBox::Ok);
        return;
    }
    QJsonObject json = json_doc.object();

    QString jwt = json["access_token"].toString();

    const QUrl url = QUrl::fromUserInput(s_url + "/receiving_wallet/wallet/" + s_wallet_id + "/address");

#ifdef ENABLE_WALLET
    std::vector<std::string> addresses;
    bool send_all = ui->cbSendAll->checkState() == Qt::Checked;
    int address_count = 0;
    auto pwalletMain = vpwallets[0];
    CWalletDBWrapper &wdb = pwalletMain->GetDBHandle();
    CWalletDB db(wdb);
    for (int i = 0; i < n_addr; i++) {
        CPubKey pk = pwalletMain->GenerateNewKey(db, false);
        CKeyID keyID = pk.GetID();
        pwalletMain->SetAddressBook(keyID, "autogenerated", "receive");
        CBitcoinAddress ba;
        ba.Set(pk.GetID());
        if (!send_all) {
            addresses.insert(addresses.end(), ba.ToString());
            address_count++;
        }
        if (i % 10 == 0) {
            ui->statusLabel->setText(tr("Generated %1 addresses.").arg(i));
            qApp->processEvents();
        }
    }

    if (send_all) {
        // Iterate all addresses in the wallet
        LOCK2(cs_main, pwalletMain->cs_wallet);
        for (const std::pair<CBitcoinAddress, CAddressBookData>& item: pwalletMain->mapAddressBook) {
            const CBitcoinAddress& address = item.first;
            addresses.insert(addresses.end(), address.ToString());
            address_count++;
        }
        ui->statusLabel->setText(tr("Collected %1 addresses.").arg(address_count));
    } else
        ui->statusLabel->setText(tr("Generated %1 addresses.").arg(n_addr));

    std::string addr_blob = boost::algorithm::join(addresses, "\",\n\"");
    addr_blob.insert(0, "{ \"addresses\":\n[\n \"");
    addr_blob.append("\"\n]\n}");

    QUrlQuery post_data;
    post_data.addQueryItem("addresses", QString(addr_blob.c_str()));

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QByteArray hdr_bearer("Bearer ");
    hdr_bearer.append(jwt);
    req.setRawHeader(QByteArray("Authorization"), hdr_bearer);

    disconnect(net, SIGNAL(finished(QNetworkReply*)), 0, 0);
    connect(net, SIGNAL(finished(QNetworkReply*)), this, SLOT(addressGenFinishedPost(QNetworkReply*)));
    QNetworkReply *reply2 = net->post(req, addr_blob.c_str());

    if (reply2->error() != QNetworkReply::NoError) {
        QMessageBox::critical(this, tr("Error"), tr("Error POST-ing to: '%1'").arg(s_url), QMessageBox::Ok, QMessageBox::Ok);
    }
#endif
}

void EnhOptionsDialog::addressGenFinishedPost(QNetworkReply* reply) {
    if (reply->error() != QNetworkReply::NoError) {
        if (reply->error() == QNetworkReply::ContentNotFoundError)
            QMessageBox::critical(this, tr("Error"), tr("Wallet not found: '%1'").arg(ui->leWalletId->text()), QMessageBox::Ok, QMessageBox::Ok);
        else
            QMessageBox::critical(this, tr("Error"), tr("Error reading address POST response from '%1' (%2)").arg(api_url).arg((int)reply->error()), QMessageBox::Ok, QMessageBox::Ok);
        return;
    }

    QMessageBox::information(this, tr("Success"),
        tr("Addresses generated and posted to '%1'.<br>Keep in mind that the private keys for those addresses are in the current (desktop) wallet.").arg(reply->url().toString()),
        QMessageBox::Ok, QMessageBox::Ok);
    reply->deleteLater();
    ui->statusLabel->clear();
}

void EnhOptionsDialog::on_generateAndSendButton_clicked()
{
#ifdef ENABLE_WALLET
    const int n_addr = ui->sbNumAddresses->value();
    const QString s_url = api_url;
    const QString &s_username = ui->leUsername->text();
    const QString &s_password = ui->lePassword->text();
    const QString &s_wallet_id = ui->leWalletId->text();
    const QString s_client_id = "1";
    const QString s_client_secret = "anothersecretpass34";

    const QUrl url = QUrl::fromUserInput(s_url + "/access_token"); /* The Login URL */
    if (s_url.isEmpty() || !url.isValid()) {
        QMessageBox::warning(this, tr("Invalid URL"), tr("Invalid URL: '%1'").arg(s_url), QMessageBox::Ok, QMessageBox::Ok);
        return;
    }

    if (s_username.length() < 2 || s_password.length() < 2) {
        QMessageBox::warning(this, tr("Invalid username and password"), tr("Both the username and password are required entries"), QMessageBox::Ok, QMessageBox::Ok);
        return;
    }
    if (s_wallet_id.length() != 36) {
        QMessageBox::warning(this, tr("Invalid wallet ID"), tr("The Wallet ID entry is required, and must be an UUID"), QMessageBox::Ok, QMessageBox::Ok);
        return;
    }

    QMessageBox::StandardButton btnRetVal = QMessageBox::question(this, tr("Confirm generate and send"),
        tr("Generate %1 addresses and send them to '%2' ?").arg(n_addr).arg(s_url),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);

    if (btnRetVal == QMessageBox::Cancel)
        return;

    ui->statusLabel->clear();

    /* Disconnect previous QT network request finished signals */
    disconnect(net, SIGNAL(finished(QNetworkReply*)), 0, 0);

    /* Initiate JWT auth; request the token */
    QUrlQuery post_data;
    post_data.addQueryItem("grant_type", "password");
    post_data.addQueryItem("client_id", s_client_id);
    post_data.addQueryItem("client_secret", s_client_secret);
    post_data.addQueryItem("username", s_username);
    post_data.addQueryItem("password", s_password);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    connect(net, SIGNAL(finished(QNetworkReply*)), this, SLOT(addressGenAuthFinished(QNetworkReply*)));
    QNetworkReply *reply = net->post(req, post_data.toString(QUrl::FullyEncoded).toUtf8());

    if (reply->error() != QNetworkReply::NoError) {
        QMessageBox::critical(this, tr("Error"), tr("Logging in to: '%1'").arg(s_url), QMessageBox::Ok, QMessageBox::Ok);
    }
#else
    QMessageBox::critical(this, tr("Error"), tr("Wallet not compiled in"), QMessageBox::Ok, QMessageBox::Ok);
#endif
}

// To avoid messing (and maintaining compatibility) with the official QT options model,
// save "enhanced client" settings in a JSON file in the data directory.
void EnhOptionsDialog::saveEnhOptions() {
    QString saveFileName(GetDataDir(false).generic_string().c_str());
    saveFileName.append("/" BITCOIN_ENH_CONF_BASE_FILENAME);
    QFile saveFile(saveFileName);
    if (!saveFile.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, tr("Error"), tr("Cannot open save options file %1").arg(saveFileName), QMessageBox::Ok, QMessageBox::Ok);
        return;
    }
    QJsonObject options;
    options["type"] = QString("bitcoin-enh");
    if (strcmp(BITCOIN_ENH_API_URL, api_url.toStdString().c_str()) != 0)
        options["APIUrl"] = api_url;
    options["APIWalletID"] = ui->leWalletId->text();
    options["APIUsername"] = ui->leUsername->text();
    options["APIAddressCount"] = ui->sbNumAddresses->value();
    QJsonDocument saveDoc(options);
    saveFile.write(saveDoc.toJson());
}

// Loads the "enhanced client" settings from the JSON file in the data directory.
void EnhOptionsDialog::loadEnhOptions() {
    QString saveFileName(GetDataDir(false).generic_string().c_str());
    saveFileName.append("/" BITCOIN_ENH_CONF_BASE_FILENAME);
    QFile saveFile(saveFileName);
    if (!saveFile.open(QIODevice::ReadOnly)) {
        return;
    }
    QByteArray saveData = saveFile.readAll();
    QJsonDocument saveDoc(QJsonDocument::fromJson(saveData));
    QJsonObject options = saveDoc.object();
    if (options["type"] != QString("bitcoin-enh")) {
        QMessageBox::critical(this, tr("Error"), tr("Corrupted options file %1").arg(saveFileName), QMessageBox::Ok, QMessageBox::Ok);
        return;
    }
    if (options["APIUrl"] != QJsonValue::Undefined && options["APIUrl"].toString() != "")
        api_url = options["APIUrl"].toString();
    ui->leWalletId->setText(options["APIWalletID"].toString());
    ui->leUsername->setText(options["APIUsername"].toString());
    ui->sbNumAddresses->setValue(options["APIAddressCount"].toInt());
}
