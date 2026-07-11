// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "mdsscope_internal.hpp"

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class SshTunnelManager;

class SshDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SshDialog(SshTunnelManager* manager, QWidget* parent = nullptr);

private:
    SshSettings currentSettings() const;
    bool validateSettings(const SshSettings& settings);
    bool saveSettings(const SshSettings& settings);
    void testConnection();
    void saveAndAccept();

    SshTunnelManager* manager_ = nullptr;
    QComboBox* modeCombo_ = nullptr;
    QLineEdit* hostEdit_ = nullptr;
    QSpinBox* portSpin_ = nullptr;
    QLineEdit* userEdit_ = nullptr;
    QLineEdit* passwordEdit_ = nullptr;
    QLineEdit* identityEdit_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPushButton* testButton_ = nullptr;
};
