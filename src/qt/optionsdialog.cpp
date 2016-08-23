// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "optionsdialog.h"
#include "ui_optionsdialog.h"

#include "bitcoinunits.h"
#include "guiutil.h"
#include "optionsmodel.h"

#include "main.h" // for DEFAULT_SCRIPTCHECK_THREADS and MAX_SCRIPTCHECK_THREADS
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

OptionsDialog::OptionsDialog(QWidget *parent, bool enableWallet) :
    QDialog(parent),
    ui(new Ui::OptionsDialog),
    model(0),
    mapper(0)
{
    ui->setupUi(this);

    /* Main elements init */
    ui->databaseCache->setMinimum(nMinDbCache);
    ui->databaseCache->setMaximum(nMaxDbCache);
    ui->threadsScriptVerif->setMinimum(-GetNumCores());
    ui->threadsScriptVerif->setMaximum(MAX_SCRIPTCHECK_THREADS);

    /* Network elements init */
#ifndef USE_UPNP
    ui->mapPortUpnp->setEnabled(false);
#endif

    ui->proxyIp->setEnabled(false);
    ui->proxyPort->setEnabled(false);
    ui->proxyPort->setValidator(new QIntValidator(1, 65535, this));

    ui->proxyIpTor->setEnabled(false);
    ui->proxyPortTor->setEnabled(false);
    ui->proxyPortTor->setValidator(new QIntValidator(1, 65535, this));

    connect(ui->connectSocks, SIGNAL(toggled(bool)), ui->proxyIp, SLOT(setEnabled(bool)));
    connect(ui->connectSocks, SIGNAL(toggled(bool)), ui->proxyPort, SLOT(setEnabled(bool)));
    connect(ui->connectSocks, SIGNAL(toggled(bool)), this, SLOT(updateProxyValidationState()));

    connect(ui->connectSocksTor, SIGNAL(toggled(bool)), ui->proxyIpTor, SLOT(setEnabled(bool)));
    connect(ui->connectSocksTor, SIGNAL(toggled(bool)), ui->proxyPortTor, SLOT(setEnabled(bool)));
    connect(ui->connectSocksTor, SIGNAL(toggled(bool)), this, SLOT(updateProxyValidationState()));

    /* Window elements init */
#ifdef Q_OS_MAC
    /* remove Window tab on Mac */
    ui->tabWidget->removeTab(ui->tabWidget->indexOf(ui->tabWindow));
#endif

    /* remove Wallet tab in case of -disablewallet */
    if (!enableWallet) {
        ui->tabWidget->removeTab(ui->tabWidget->indexOf(ui->tabWallet));
        ui->tabWidget->removeTab(ui->tabWidget->indexOf(ui->tabAddrGen));
    }

    /* Display elements init */
    QDir translations(":translations");
    ui->lang->addItem(QString("(") + tr("default") + QString(")"), QVariant(""));
    Q_FOREACH(const QString &langStr, translations.entryList())
    {
        QLocale locale(langStr);

        /** check if the locale name consists of 2 parts (language_country) */
        if(langStr.contains("_"))
        {
#if QT_VERSION >= 0x040800
            /** display language strings as "native language - native country (locale name)", e.g. "Deutsch - Deutschland (de)" */
            ui->lang->addItem(locale.nativeLanguageName() + QString(" - ") + locale.nativeCountryName() + QString(" (") + langStr + QString(")"), QVariant(langStr));
#else
            /** display language strings as "language - country (locale name)", e.g. "German - Germany (de)" */
            ui->lang->addItem(QLocale::languageToString(locale.language()) + QString(" - ") + QLocale::countryToString(locale.country()) + QString(" (") + langStr + QString(")"), QVariant(langStr));
#endif
        }
        else
        {
#if QT_VERSION >= 0x040800
            /** display language strings as "native language (locale name)", e.g. "Deutsch (de)" */
            ui->lang->addItem(locale.nativeLanguageName() + QString(" (") + langStr + QString(")"), QVariant(langStr));
#else
            /** display language strings as "language (locale name)", e.g. "German (de)" */
            ui->lang->addItem(QLocale::languageToString(locale.language()) + QString(" (") + langStr + QString(")"), QVariant(langStr));
#endif
        }
    }
#if QT_VERSION >= 0x040700
    ui->thirdPartyTxUrls->setPlaceholderText("https://example.com/tx/%s");
#endif

    ui->unit->setModel(new BitcoinUnits(this));

    /* Widget-to-option mapper */
    mapper = new QDataWidgetMapper(this);
    mapper->setSubmitPolicy(QDataWidgetMapper::ManualSubmit);
    mapper->setOrientation(Qt::Vertical);

    /* setup/change UI elements when proxy IPs are invalid/valid */
    ui->proxyIp->setCheckValidator(new ProxyAddressValidator(parent));
    ui->proxyIpTor->setCheckValidator(new ProxyAddressValidator(parent));
    connect(ui->proxyIp, SIGNAL(validationDidChange(QValidatedLineEdit *)), this, SLOT(updateProxyValidationState()));
    connect(ui->proxyIpTor, SIGNAL(validationDidChange(QValidatedLineEdit *)), this, SLOT(updateProxyValidationState()));
    connect(ui->proxyPort, SIGNAL(textChanged(const QString&)), this, SLOT(updateProxyValidationState()));
    connect(ui->proxyPortTor, SIGNAL(textChanged(const QString&)), this, SLOT(updateProxyValidationState()));

    net = new QNetworkAccessManager(this);
}

OptionsDialog::~OptionsDialog()
{
    delete net;
    delete ui;
}

void OptionsDialog::setModel(OptionsModel *model)
{
    this->model = model;

    if(model)
    {
        /* check if client restart is needed and show persistent message */
        if (model->isRestartRequired())
            showRestartWarning(true);

        QString strLabel = model->getOverriddenByCommandLine();
        if (strLabel.isEmpty())
            strLabel = tr("none");
        ui->overriddenByCommandLineLabel->setText(strLabel);

        mapper->setModel(model);
        setMapper();
        mapper->toFirst();

        updateDefaultProxyNets();
        loadEnhOptions();
    }

    /* warn when one of the following settings changes by user action (placed here so init via mapper doesn't trigger them) */

    /* Main */
    connect(ui->databaseCache, SIGNAL(valueChanged(int)), this, SLOT(showRestartWarning()));
    connect(ui->threadsScriptVerif, SIGNAL(valueChanged(int)), this, SLOT(showRestartWarning()));
    /* Wallet */
    connect(ui->spendZeroConfChange, SIGNAL(clicked(bool)), this, SLOT(showRestartWarning()));
    /* Network */
    connect(ui->allowIncoming, SIGNAL(clicked(bool)), this, SLOT(showRestartWarning()));
    connect(ui->connectSocks, SIGNAL(clicked(bool)), this, SLOT(showRestartWarning()));
    connect(ui->connectSocksTor, SIGNAL(clicked(bool)), this, SLOT(showRestartWarning()));
    /* Display */
    connect(ui->lang, SIGNAL(valueChanged()), this, SLOT(showRestartWarning()));
    connect(ui->thirdPartyTxUrls, SIGNAL(textChanged(const QString &)), this, SLOT(showRestartWarning()));
}

void OptionsDialog::setMapper()
{
    /* Main */
    mapper->addMapping(ui->bitcoinAtStartup, OptionsModel::StartAtStartup);
    mapper->addMapping(ui->threadsScriptVerif, OptionsModel::ThreadsScriptVerif);
    mapper->addMapping(ui->databaseCache, OptionsModel::DatabaseCache);

    /* Wallet */
    mapper->addMapping(ui->spendZeroConfChange, OptionsModel::SpendZeroConfChange);
    mapper->addMapping(ui->coinControlFeatures, OptionsModel::CoinControlFeatures);

    /* Network */
    mapper->addMapping(ui->mapPortUpnp, OptionsModel::MapPortUPnP);
    mapper->addMapping(ui->allowIncoming, OptionsModel::Listen);

    mapper->addMapping(ui->connectSocks, OptionsModel::ProxyUse);
    mapper->addMapping(ui->proxyIp, OptionsModel::ProxyIP);
    mapper->addMapping(ui->proxyPort, OptionsModel::ProxyPort);

    mapper->addMapping(ui->connectSocksTor, OptionsModel::ProxyUseTor);
    mapper->addMapping(ui->proxyIpTor, OptionsModel::ProxyIPTor);
    mapper->addMapping(ui->proxyPortTor, OptionsModel::ProxyPortTor);

    /* Window */
#ifndef Q_OS_MAC
    mapper->addMapping(ui->minimizeToTray, OptionsModel::MinimizeToTray);
    mapper->addMapping(ui->minimizeOnClose, OptionsModel::MinimizeOnClose);
#endif

    /* Display */
    mapper->addMapping(ui->lang, OptionsModel::Language);
    mapper->addMapping(ui->unit, OptionsModel::DisplayUnit);
    mapper->addMapping(ui->thirdPartyTxUrls, OptionsModel::ThirdPartyTxUrls);
}

void OptionsDialog::setOkButtonState(bool fState)
{
    ui->okButton->setEnabled(fState);
}

void OptionsDialog::on_resetButton_clicked()
{
    if(model)
    {
        // confirmation dialog
        QMessageBox::StandardButton btnRetVal = QMessageBox::question(this, tr("Confirm options reset"),
            tr("Client restart required to activate changes.") + "<br><br>" + tr("Client will be shut down. Do you want to proceed?"),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);

        if(btnRetVal == QMessageBox::Cancel)
            return;

        /* reset all options and close GUI */
        model->Reset();
        QApplication::quit();
    }
}

void OptionsDialog::on_okButton_clicked()
{
    mapper->submit();
    accept();
    updateDefaultProxyNets();
    saveEnhOptions();
}

void OptionsDialog::on_cancelButton_clicked()
{
    reject();
}

void OptionsDialog::addressGenAuthFinished(QNetworkReply* reply) {
    const QString &s_url = ui->leUrl->text();
    if (reply->error() != QNetworkReply::NoError) {
        QMessageBox::critical(this, tr("Error"), tr("Error reading auth response from '%1' (%2)").arg(s_url).arg((int)reply->error()), QMessageBox::Ok, QMessageBox::Ok);
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
    for (int i = 0; i < n_addr; i++) {
        CPubKey pk = pwalletMain->GenerateNewKey();
        CBitcoinAddress ba;
        ba.Set(pk.GetID());
        addresses.insert(addresses.end(), ba.ToString());
        if (i % 10 == 0) {
            ui->statusLabel->setText(tr("Generated %1 addresses.").arg(i));
        }
    }
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

void OptionsDialog::addressGenFinishedPost(QNetworkReply* reply) {
    const QString &s_url = ui->leUrl->text();
    if (reply->error() != QNetworkReply::NoError) {
        QMessageBox::critical(this, tr("Error"), tr("Error reading address POST response from '%1' (%2)").arg(s_url).arg((int)reply->error()), QMessageBox::Ok, QMessageBox::Ok);
        return;
    }

    QMessageBox::information(this, tr("Success"),
        tr("Addresses generated and posted to '%1'.<br>Keep in mind that the private keys for those addresses are in the current wallet.").arg(reply->url().toString()),
        QMessageBox::Ok, QMessageBox::Ok);
    reply->deleteLater();
    ui->statusLabel->clear();
}

void OptionsDialog::on_generateAndSendButton_clicked()
{
#ifdef ENABLE_WALLET
    const int n_addr = ui->sbNumAddresses->value();
    const QString &s_url = ui->leUrl->text();
    const QString &s_username = ui->leUsername->text();
    const QString &s_password = ui->lePassword->text();
    const QString s_client_id = "1";
    const QString s_client_secret = "anothersecretpass34";

    const QUrl url = QUrl::fromUserInput(s_url + "/access_token"); /* The Login URL */
    if (s_url.isEmpty() || !url.isValid()) {
        QMessageBox::warning(this, tr("Invalid URL"), tr("Invalid URL: '%1'").arg(s_url), QMessageBox::Ok, QMessageBox::Ok);
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

void OptionsDialog::showRestartWarning(bool fPersistent)
{
    ui->statusLabel->setStyleSheet("QLabel { color: red; }");

    if(fPersistent)
    {
        ui->statusLabel->setText(tr("Client restart required to activate changes."));
    }
    else
    {
        ui->statusLabel->setText(tr("This change would require a client restart."));
        // clear non-persistent status label after 10 seconds
        // Todo: should perhaps be a class attribute, if we extend the use of statusLabel
        QTimer::singleShot(10000, this, SLOT(clearStatusLabel()));
    }
}

void OptionsDialog::clearStatusLabel()
{
    ui->statusLabel->clear();
}

void OptionsDialog::updateProxyValidationState()
{
    QValidatedLineEdit *pUiProxyIp = ui->proxyIp;
    QValidatedLineEdit *otherProxyWidget = (pUiProxyIp == ui->proxyIpTor) ? ui->proxyIp : ui->proxyIpTor;
    if (pUiProxyIp->isValid() && (!ui->proxyPort->isEnabled() || ui->proxyPort->text().toInt() > 0) && (!ui->proxyPortTor->isEnabled() || ui->proxyPortTor->text().toInt() > 0))
    {
        setOkButtonState(otherProxyWidget->isValid()); //only enable ok button if both proxys are valid
        ui->statusLabel->clear();
    }
    else
    {
        setOkButtonState(false);
        ui->statusLabel->setStyleSheet("QLabel { color: red; }");
        ui->statusLabel->setText(tr("The supplied proxy address is invalid."));
    }
}

void OptionsDialog::updateDefaultProxyNets()
{
    proxyType proxy;
    std::string strProxy;
    QString strDefaultProxyGUI;

    GetProxy(NET_IPV4, proxy);
    strProxy = proxy.proxy.ToStringIP() + ":" + proxy.proxy.ToStringPort();
    strDefaultProxyGUI = ui->proxyIp->text() + ":" + ui->proxyPort->text();
    (strProxy == strDefaultProxyGUI.toStdString()) ? ui->proxyReachIPv4->setChecked(true) : ui->proxyReachIPv4->setChecked(false);

    GetProxy(NET_IPV6, proxy);
    strProxy = proxy.proxy.ToStringIP() + ":" + proxy.proxy.ToStringPort();
    strDefaultProxyGUI = ui->proxyIp->text() + ":" + ui->proxyPort->text();
    (strProxy == strDefaultProxyGUI.toStdString()) ? ui->proxyReachIPv6->setChecked(true) : ui->proxyReachIPv6->setChecked(false);

    GetProxy(NET_TOR, proxy);
    strProxy = proxy.proxy.ToStringIP() + ":" + proxy.proxy.ToStringPort();
    strDefaultProxyGUI = ui->proxyIp->text() + ":" + ui->proxyPort->text();
    (strProxy == strDefaultProxyGUI.toStdString()) ? ui->proxyReachTor->setChecked(true) : ui->proxyReachTor->setChecked(false);
}

// To avoid messing (and maintaining compatibility) with the official QT options model,
// save "enhanced client" settings in a JSON file in the data directory.
void OptionsDialog::saveEnhOptions() {
    QString saveFileName(GetDataDir(false).generic_string().c_str());
    saveFileName.append("/" BITCOIN_ENH_CONF_BASE_FILENAME);
    QFile saveFile(saveFileName);
    if (!saveFile.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, tr("Error"), tr("Cannot open save options file %1").arg(saveFileName), QMessageBox::Ok, QMessageBox::Ok);
        return;
    }
    QJsonObject options;
    options["type"] = QString("bitcoin-enh");
    options["APIUrl"] = ui->leUrl->text();
    options["APIWalletID"] = ui->leWalletId->text();
    options["APIUsername"] = ui->leUsername->text();
    options["APIAddressCount"] = ui->sbNumAddresses->value();
    QJsonDocument saveDoc(options);
    saveFile.write(saveDoc.toJson());
}

// Loads the "enhanced client" settings from the JSON file in the data directory.
void OptionsDialog::loadEnhOptions() {
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
    ui->leUrl->setText(options["APIUrl"].toString());
    ui->leWalletId->setText(options["APIWalletID"].toString());
    ui->leUsername->setText(options["APIUsername"].toString());
    ui->sbNumAddresses->setValue(options["APIAddressCount"].toInt());
}

ProxyAddressValidator::ProxyAddressValidator(QObject *parent) :
QValidator(parent)
{
}

QValidator::State ProxyAddressValidator::validate(QString &input, int &pos) const
{
    Q_UNUSED(pos);
    // Validate the proxy
    proxyType addrProxy = proxyType(CService(input.toStdString(), 9050), true);
    if (addrProxy.IsValid())
        return QValidator::Acceptable;

    return QValidator::Invalid;
}
