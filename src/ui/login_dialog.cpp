// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.hpp"

LoginDialog::LoginDialog(QString rootPath, QWidget* parent, QString apiOverride)
    : QDialog(parent), rootPath_(std::move(rootPath)), apiOverride_(std::move(apiOverride))
{
    setWindowTitle("Login");
    setModal(true);
    setFixedWidth(420);
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

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(10);
    userEdit_ = new QLineEdit(this);
    userEdit_->setPlaceholderText("Username");
    passwordEdit_ = new QLineEdit(this);
    passwordEdit_->setPlaceholderText("Password");
    passwordEdit_->setEchoMode(QLineEdit::Password);
    form->addRow("Username", userEdit_);
    form->addRow("Password", passwordEdit_);
    layout->addLayout(form);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* cancel = new QPushButton("Cancel", this);
    loginButton_ = new QPushButton("Login", this);
    loginButton_->setObjectName("primary");
    buttons->addWidget(cancel);
    buttons->addWidget(loginButton_);
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
    if (properties_.value("ApiUrl").trimmed().isEmpty()) {
        statusLabel_->setText("Missing API configuration.");
        statusLabel_->show();
        loginButton_->setEnabled(false);
        return;
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
    const QString api = properties_.value("ApiUrl");
    const QString charset = properties_.value("Charset", "UTF-8");

    if (api.trimmed().isEmpty()) {
        QMessageBox::warning(this, "Login", "Missing API URL.");
        return;
    }

    const QString userName = userEdit_->text().trimmed();
    const QString password = passwordEdit_->text();
    loginInProgress_ = true;
    loginButton_->setEnabled(false);
    statusLabel_->setText(QStringLiteral("Signing in..."));
    statusLabel_->show();
    auto* watcher = new QFutureWatcher<ApiLoginResult>(this);
    connect(watcher, &QFutureWatcher<ApiLoginResult>::finished, this, [this, watcher, userName, password] {
        const ApiLoginResult result = watcher->result();
        watcher->deleteLater();
        loginInProgress_ = false;
        loginButton_->setEnabled(true);
        if (result.ok && !result.token.isEmpty()) {
            CachedAuth auth;
            loadCachedAuth(&auth);
            auth.userName = userName;
            auth.password = password;
            auth.token = result.token;
            saveCachedAuth(auth);
            accept();
            return;
        }
        statusLabel_->setText(result.error.isEmpty() ? QStringLiteral("Failed to login.") : result.error);
        statusLabel_->show();
        userEdit_->clear();
        passwordEdit_->clear();
        userEdit_->setFocus();
    });
    watcher->setFuture(QtConcurrent::run([api, charset, userName, password] {
        return requestApiToken(api, charset, userName, password);
    }));
}
