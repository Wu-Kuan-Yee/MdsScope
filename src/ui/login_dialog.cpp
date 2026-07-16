// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.hpp"
#include <QNetworkProxy>

LoginDialog::LoginDialog(QString rootPath, QWidget* parent, QString apiOverride)
    : QDialog(parent), rootPath_(std::move(rootPath)), apiOverride_(std::move(apiOverride))
{
    setWindowTitle("Login");
    setModal(true);
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    setMinimumWidth(280);
#else
    setFixedWidth(420);
#endif
#ifdef Q_OS_IOS
    // Send a dummy request to a domain to trigger the iOS China network permission prompt
    auto* dummyManager = new QNetworkAccessManager(this);
    dummyManager->get(QNetworkRequest(QUrl("http://captive.apple.com/hotspot-detect.html")));
#endif
    QString styleSheet =
        "QDialog { background: palette(base); }"
        "QLabel#title { font-size: 24px; font-weight: 600; color: palette(text); }"
        "QLabel#subtitle { color: palette(mid); }"
        "QLabel#status { color: #b91c1c; }"
        "QLineEdit { min-height: 32px; padding: 4px 8px; border: 1px solid palette(mid); border-radius: 4px; }"
        "QLineEdit:focus { border: 1px solid palette(highlight); }"
        "QPushButton { min-height: 32px; padding: 4px 14px; border-radius: 4px; }"
        "QPushButton#primary { background: palette(highlight); color: palette(highlighted-text); border: 1px solid palette(highlight); }";
    if (QApplication::palette().color(QPalette::Window).lightness() >= 128) {
        styleSheet +=
            "QLabel { background: transparent; }"
            "QLineEdit { background: #ffffff; color: #111827; border-color: #cbd5e1; selection-background-color: #2563eb; selection-color: #ffffff; }"
            "QLineEdit:focus { border-color: #2563eb; }";
    }
    setStyleSheet(styleSheet);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(8);

    auto* title = new QLabel("MdsScope", this);
    title->setObjectName("title");
    layout->addWidget(title);

    auto* subtitle = new QLabel("Sign in to access EAST shot metadata.", this);
    subtitle->setObjectName("subtitle");
    layout->addWidget(subtitle);

    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName("status");
    statusLabel_->setWordWrap(true);
    statusLabel_->hide();
    layout->addWidget(statusLabel_);

    layout->addSpacing(8);

    auto* form = new QVBoxLayout;
    form->setSpacing(10);
    
    apiEdit_ = new QLineEdit(this);
    apiEdit_->setPlaceholderText("http://202.127.204.26:80/api");
    
    userEdit_ = new QLineEdit(this);
    userEdit_->setPlaceholderText("Username");
    passwordEdit_ = new QLineEdit(this);
    passwordEdit_->setPlaceholderText("Password");
    passwordEdit_->setEchoMode(QLineEdit::Password);
    
    auto* apiLabel = new QLabel("API URL", this);
    auto* userLabel = new QLabel("Username", this);
    auto* passLabel = new QLabel("Password", this);
    
    form->addWidget(apiLabel);
    form->addWidget(apiEdit_);
    form->addWidget(userLabel);
    form->addWidget(userEdit_);
    form->addWidget(passLabel);
    form->addWidget(passwordEdit_);
    layout->addLayout(form);

    auto* buttons = new QVBoxLayout;
    buttons->setSpacing(10);
    auto* cancel = new QPushButton("Cancel", this);
    loginButton_ = new QPushButton("Login", this);
    loginButton_->setObjectName("primary");
    buttons->addWidget(loginButton_);
    buttons->addWidget(cancel);
    layout->addLayout(buttons);

    connect(loginButton_, &QPushButton::clicked, this, &LoginDialog::tryLogin);
    connect(passwordEdit_, &QLineEdit::returnPressed, this, &LoginDialog::tryLogin);
    connect(userEdit_, &QLineEdit::returnPressed, passwordEdit_, qOverload<>(&QWidget::setFocus));
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    loadProperties();
}

void LoginDialog::loadProperties()
{
    properties_ = readApiSettings(rootPath_);
    if (!apiOverride_.trimmed().isEmpty()) {
        properties_.insert(QStringLiteral("ApiUrl"), apiOverride_.trimmed());
    }
    
    QString currentApi = properties_.value("ApiUrl").trimmed();
    apiEdit_->setText(currentApi);

    if (currentApi.isEmpty()) {
        statusLabel_->setText("Missing API configuration.");
        statusLabel_->show();
        loginButton_->setEnabled(false);
    }
    CachedAuth auth;
    if (loadCachedAuth(&auth)) {
        userEdit_->setText(auth.userName);
        passwordEdit_->setText(auth.password);
    }
    statusLabel_->clear();
    statusLabel_->hide();
    if (userEdit_->text().trimmed().isEmpty()) {
        userEdit_->setFocus();
    } else {
        passwordEdit_->setFocus();
    }
}

void LoginDialog::tryLogin()
{
    if (loginInProgress_) {
        return;
    }
    const QString api = apiEdit_->text().trimmed();
    const QString charset = properties_.value("Charset", "UTF-8");

    if (api.isEmpty()) {
        statusLabel_->setText("Missing API URL.");
        statusLabel_->show();
        return;
    }

    const QString userName = userEdit_->text().trimmed();
    const QString password = passwordEdit_->text();
    loginInProgress_ = true;
    loginButton_->setEnabled(false);
    loginButton_->setText(QStringLiteral("Signing in..."));
    statusLabel_->setText(QStringLiteral("Signing in..."));
    statusLabel_->show();

    auto* manager = new QNetworkAccessManager(this);
    QString baseUrl = api.trimmed();
    if (baseUrl.endsWith('/')) {
        baseUrl.chop(1);
    }
    QNetworkRequest request(QUrl(baseUrl + "/login"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json; charset=" + charset);
    request.setRawHeader("User-Agent", "MdsScope/1.0");

    QJsonObject payload;
    payload.insert("userName", userName.trimmed());
    payload.insert("password", password);

    QNetworkReply* reply = manager->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));

    connect(reply, &QNetworkReply::finished, this, [this, reply, userName, password, api]() {
        reply->deleteLater();
        reply->manager()->deleteLater();
        loginInProgress_ = false;
        loginButton_->setEnabled(true);
        loginButton_->setText(QStringLiteral("Login"));

        if (reply->error() == QNetworkReply::NoError) {
            QByteArray rawResponse = reply->readAll();
            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(rawResponse, &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                QJsonObject json = doc.object();
                bool ok = json.value("code").toString() == "20000" || json.value("code").toInt() == 20000;
                QString token = json.value("data").toObject().value("token").toString();
                if (ok && !token.isEmpty()) {
                    CachedAuth auth;
                    loadCachedAuth(&auth);
                    auth.userName = userName;
                    auth.password = password;
                    auth.token = token;
                    saveCachedAuth(auth);
                    QSettings().setValue("ApiUrlOverride", api);
                    accept();
                    return;
                }
                
                // If it's valid JSON but not 20000 or token is empty, show the real message!
                QString msg = json.value("message").toString();
                if (!msg.isEmpty()) {
                    statusLabel_->setText(msg);
                    return;
                }
            }
            QString debugMsg = QString("Invalid response.\nError: %1\nRaw: %2")
                                   .arg(err.errorString())
                                   .arg(QString::fromUtf8(rawResponse).left(200));
            statusLabel_->setText("Invalid response from server.\n" + debugMsg);
        } else {
            statusLabel_->setText(reply->errorString());
        }
        statusLabel_->show();
    });
}
