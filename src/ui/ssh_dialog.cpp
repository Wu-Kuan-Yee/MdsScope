// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ssh_dialog.hpp"
#include "ssh_tunnel_manager.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

SshDialog::SshDialog(SshTunnelManager* manager, QWidget* parent)
    : QDialog(parent), manager_(manager)
{
    setWindowTitle(QStringLiteral("SSH Remote Access"));
    setModal(true);
    setMinimumWidth(460);

    CachedAuth auth;
    const bool hasCachedAuth = loadCachedAuth(&auth);
    if (!hasCachedAuth || auth.ssh.host.trimmed().isEmpty()) {
        auth.ssh.mode = SshMode::Auto;
        auth.ssh.port = 22;
    }

    auto* layout = new QVBoxLayout(this);
    auto* description = new QLabel(
        QStringLiteral("Use SSH automatically when an MDS server is not reachable directly.\n"
                       "Enter a password, select an identity file, or leave both empty to use OpenSSH defaults."), this);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto* form = new QFormLayout;
    modeCombo_ = new QComboBox(this);
    modeCombo_->addItem(QStringLiteral("Disabled"), static_cast<int>(SshMode::Disabled));
    modeCombo_->addItem(QStringLiteral("Automatic fallback"), static_cast<int>(SshMode::Auto));
    modeCombo_->addItem(QStringLiteral("Always use SSH"), static_cast<int>(SshMode::Always));
    modeCombo_->setCurrentIndex(std::max(0, modeCombo_->findData(static_cast<int>(auth.ssh.mode))));
    hostEdit_ = new QLineEdit(auth.ssh.host, this);
    hostEdit_->setPlaceholderText(QStringLiteral("SSH host, IP, or ~/.ssh/config alias"));
    portSpin_ = new QSpinBox(this);
    portSpin_->setRange(1, 65535);
    portSpin_->setValue(auth.ssh.port > 0 ? auth.ssh.port : 22);
    userEdit_ = new QLineEdit(auth.ssh.user, this);
    userEdit_->setPlaceholderText(QStringLiteral("Optional when defined by SSH config"));
    passwordEdit_ = new QLineEdit(auth.ssh.password, this);
    passwordEdit_->setEchoMode(QLineEdit::Password);
    passwordEdit_->setPlaceholderText(QStringLiteral("Optional: use password authentication"));
    identityEdit_ = new QLineEdit(auth.ssh.identityFile, this);
    identityEdit_->setPlaceholderText(QStringLiteral("Empty: use default SSH keys / ssh-agent"));
    auto* identityRow = new QWidget(this);
    auto* identityLayout = new QHBoxLayout(identityRow);
    identityLayout->setContentsMargins(0, 0, 0, 0);
    identityLayout->setSpacing(6);
    identityLayout->addWidget(identityEdit_, 1);
    auto* browseIdentity = new QPushButton(QStringLiteral("Browse..."), identityRow);
    identityLayout->addWidget(browseIdentity);
    form->addRow(QStringLiteral("Mode"), modeCombo_);
    form->addRow(QStringLiteral("Host / IP"), hostEdit_);
    form->addRow(QStringLiteral("Port"), portSpin_);
    form->addRow(QStringLiteral("Username"), userEdit_);
    form->addRow(QStringLiteral("Password"), passwordEdit_);
    form->addRow(QStringLiteral("Identity file"), identityRow);
    layout->addLayout(form);

    connect(passwordEdit_, &QLineEdit::textEdited, this, [this](const QString& text) {
        if (!text.isEmpty()) {
            identityEdit_->clear();
        }
    });
    connect(identityEdit_, &QLineEdit::textEdited, this, [this](const QString& text) {
        if (!text.isEmpty()) {
            passwordEdit_->clear();
        }
    });
    connect(browseIdentity, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this,
            QStringLiteral("Select SSH Identity File"),
            identityEdit_->text().trimmed().isEmpty() ? QDir::homePath() + QStringLiteral("/.ssh")
                                                       : identityEdit_->text().trimmed());
        if (!path.isEmpty()) {
            identityEdit_->setText(path);
            passwordEdit_->clear();
        }
    });

    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);
    statusLabel_->hide();
    layout->addWidget(statusLabel_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    testButton_ = buttons->addButton(QStringLiteral("Test Connection"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    connect(testButton_, &QPushButton::clicked, this, &SshDialog::testConnection);
    connect(buttons->button(QDialogButtonBox::Save), &QPushButton::clicked, this, &SshDialog::saveAndAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

SshSettings SshDialog::currentSettings() const
{
    SshSettings settings;
    settings.mode = static_cast<SshMode>(modeCombo_->currentData().toInt());
    settings.host = hostEdit_->text().trimmed();
    settings.port = portSpin_->value();
    settings.user = userEdit_->text().trimmed();
    settings.password = passwordEdit_->text();
    settings.identityFile = identityEdit_->text().trimmed();
    return settings;
}

bool SshDialog::validateSettings(const SshSettings& settings)
{
    if (settings.mode != SshMode::Disabled && settings.host.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("SSH Remote Access"), QStringLiteral("Enter an SSH host or disable SSH."));
        hostEdit_->setFocus();
        return false;
    }
    if (!settings.identityFile.isEmpty()) {
        QString identity = settings.identityFile;
        if (identity == QStringLiteral("~")) {
            identity = QDir::homePath();
        } else if (identity.startsWith(QStringLiteral("~/"))) {
            identity = QDir::home().filePath(identity.mid(2));
        }
        const QFileInfo info(identity);
        if (!info.isFile() || !info.isReadable()) {
            QMessageBox::warning(this, QStringLiteral("SSH Remote Access"), QStringLiteral("The selected identity file is not readable."));
            identityEdit_->setFocus();
            return false;
        }
    }
    return true;
}

bool SshDialog::saveSettings(const SshSettings& settings)
{
    CachedAuth auth;
    loadCachedAuth(&auth);
    auth.ssh = settings;
    if (!saveCachedAuth(auth)) {
        QMessageBox::warning(this, QStringLiteral("SSH Remote Access"), QStringLiteral("Could not save the encrypted SSH settings."));
        return false;
    }
    manager_->disconnectAll();
    manager_->reloadSettings();
    return true;
}

void SshDialog::testConnection()
{
    const SshSettings settings = currentSettings();
    if (!validateSettings(settings) || !saveSettings(settings)) {
        return;
    }
    if (settings.mode == SshMode::Disabled) {
        statusLabel_->setText(QStringLiteral("SSH is disabled."));
        statusLabel_->show();
        return;
    }
    testButton_->setEnabled(false);
    statusLabel_->setText(QStringLiteral("Testing SSH login..."));
    statusLabel_->show();
    QString error;
    const bool ok = manager_->testConnection(settings, &error);
    statusLabel_->setText(ok ? QStringLiteral("SSH login succeeded.") : error);
    statusLabel_->setStyleSheet(ok ? QStringLiteral("color: #15803d;") : QStringLiteral("color: #b91c1c;"));
    testButton_->setEnabled(true);
}

void SshDialog::saveAndAccept()
{
    const SshSettings settings = currentSettings();
    if (validateSettings(settings) && saveSettings(settings)) {
        accept();
    }
}
