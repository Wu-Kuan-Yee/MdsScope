// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "shared.h"

inline QString htmlLink(const QString& label, const QString& url)
{
    return QStringLiteral("<a style=\"color:#ff8a65; text-decoration:none; font-weight:700;\" href=\"%1\">%2</a>")
        .arg(url.toHtmlEscaped(), label.toHtmlEscaped());
}

inline void openExternalUrlQuietly(const QString& urlText)
{
    const QUrl url(urlText);
    if (!url.isValid()) {
        return;
    }

#if defined(Q_OS_LINUX)
    QProcess opener;
    opener.setProgram(QStringLiteral("xdg-open"));
    opener.setArguments({url.toString()});
    opener.setStandardOutputFile(QProcess::nullDevice());
    opener.setStandardErrorFile(QProcess::nullDevice());
    if (opener.startDetached()) {
        return;
    }
#endif

    QDesktopServices::openUrl(url);
}

struct ParsedVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;
    bool valid = false;
};

inline ParsedVersion parseVersionTag(QString text)
{
    text = text.trimmed();
    if (text.startsWith('v', Qt::CaseInsensitive)) {
        text.remove(0, 1);
    }

    const QRegularExpression re(QStringLiteral(R"(^(\d+)(?:\.(\d+))?(?:\.(\d+))?$)"));
    const QRegularExpressionMatch match = re.match(text);
    if (!match.hasMatch()) {
        return {};
    }

    ParsedVersion version;
    version.major = match.captured(1).toInt();
    version.minor = match.captured(2).isEmpty() ? 0 : match.captured(2).toInt();
    version.patch = match.captured(3).isEmpty() ? 0 : match.captured(3).toInt();
    version.valid = true;
    return version;
}

inline int compareVersions(const ParsedVersion& lhs, const ParsedVersion& rhs)
{
    if (lhs.major != rhs.major) {
        return lhs.major < rhs.major ? -1 : 1;
    }
    if (lhs.minor != rhs.minor) {
        return lhs.minor < rhs.minor ? -1 : 1;
    }
    if (lhs.patch != rhs.patch) {
        return lhs.patch < rhs.patch ? -1 : 1;
    }
    return 0;
}

struct AboutColors {
    QString window;
    QString panel;
    QString panelBorder;
    QString iconPanel;
    QString separator;
    QString text;
    QString title;
    QString subtle;
    QString button;
    QString buttonBorder;
    QString buttonHover;
    QString buttonHoverBorder;
    QString disabledButton;
    QString disabledBorder;
    QString disabledText;
    QString status;
};

inline AboutColors aboutColors()
{
    const QPalette pal = QApplication::palette();
    const bool dark = pal.color(QPalette::Window).lightness() < 128;
    if (!dark) {
        return AboutColors{
            QStringLiteral("#f8fafc"),
            QStringLiteral("#f3f4f6"),
            QStringLiteral("#d1d5db"),
            QStringLiteral("#e5e7eb"),
            QStringLiteral("#d1d5db"),
            QStringLiteral("#111827"),
            QStringLiteral("#0f172a"),
            QStringLiteral("#64748b"),
            QStringLiteral("#f3f4f6"),
            QStringLiteral("#cbd5e1"),
            QStringLiteral("#e5e7eb"),
            QStringLiteral("#94a3b8"),
            QStringLiteral("#e5e7eb"),
            QStringLiteral("#cbd5e1"),
            QStringLiteral("#64748b"),
            QStringLiteral("#475569"),
        };
    }

    return AboutColors{
        pal.color(QPalette::Window).name(QColor::HexRgb),
        pal.color(QPalette::AlternateBase).name(QColor::HexRgb),
        pal.color(QPalette::Mid).name(QColor::HexRgb),
        pal.color(QPalette::Button).name(QColor::HexRgb),
        pal.color(QPalette::Mid).name(QColor::HexRgb),
        pal.color(QPalette::WindowText).name(QColor::HexRgb),
        pal.color(QPalette::Text).name(QColor::HexRgb),
        pal.color(QPalette::Disabled, QPalette::WindowText).name(QColor::HexRgb),
        pal.color(QPalette::Button).name(QColor::HexRgb),
        pal.color(QPalette::Mid).name(QColor::HexRgb),
        pal.color(QPalette::Midlight).name(QColor::HexRgb),
        pal.color(QPalette::Highlight).name(QColor::HexRgb),
        pal.color(QPalette::AlternateBase).name(QColor::HexRgb),
        pal.color(QPalette::Mid).name(QColor::HexRgb),
        pal.color(QPalette::Disabled, QPalette::ButtonText).name(QColor::HexRgb),
        pal.color(QPalette::Disabled, QPalette::WindowText).name(QColor::HexRgb),
    };
}

inline QString aboutDialogStyleSheet()
{
    const AboutColors c = aboutColors();
    return QStringLiteral(
        "QDialog {"
        "  background: %1;"
        "  color: %6;"
        "}"
        "QLabel {"
        "  background: transparent;"
        "}"
        "QWidget#aboutHeader {"
        "  background: %2;"
        "  border: 1px solid %3;"
        "  border-radius: 10px;"
        "}"
        "QWidget#aboutHeader QWidget {"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QWidget#aboutIconBadge {"
        "  background: %4;"
        "  border: 1px solid %3;"
        "  border-radius: 12px;"
        "}"
        "QWidget#aboutCard {"
        "  background: %2;"
        "  border: 1px solid %3;"
        "  border-radius: 10px;"
        "}"
        "QWidget#aboutCard QWidget {"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QWidget#aboutRow {"
        "  background: transparent;"
        "}"
        "QLabel#aboutTitle {"
        "  font-weight: 700;"
        "  color: %7;"
        "}"
        "QLabel#aboutSubtitle {"
        "  color: %8;"
        "}"
        "QLabel#aboutKey {"
        "  color: %6;"
        "}"
        "QLabel#aboutValue {"
        "  color: %7;"
        "  font-weight: 700;"
        "}"
        "QFrame#aboutSeparator {"
        "  color: %5;"
        "  background: %5;"
        "  max-height: 1px;"
        "}"
        "QPushButton#aboutCloseButton, QPushButton#aboutUpdateButton {"
        "  background: %9;"
        "  border: 1px solid %10;"
        "  color: %7;"
        "  font-weight: 700;"
        "  padding: 7px 20px;"
        "  border-radius: 6px;"
        "}"
        "QPushButton#aboutCloseButton:hover, QPushButton#aboutUpdateButton:hover {"
        "  background: %11;"
        "  border-color: %12;"
        "}"
        "QPushButton#aboutUpdateButton:disabled {"
        "  background: %13;"
        "  border-color: %14;"
        "  color: %15;"
        "}"
        "QLabel#aboutUpdateStatus {"
        "  color: %16;"
        "  padding-left: 8px;"
        "}")
        .arg(c.window,
             c.panel,
             c.panelBorder,
             c.iconPanel,
             c.separator,
             c.text,
             c.title,
             c.subtle,
             c.button,
             c.buttonBorder,
             c.buttonHover,
             c.buttonHoverBorder,
             c.disabledButton,
             c.disabledBorder,
             c.disabledText,
             c.status);
}

inline QString aboutMessageBoxStyleSheet()
{
    const AboutColors c = aboutColors();
    return QStringLiteral(
        "QMessageBox {"
        "  background: %1;"
        "  color: %2;"
        "}"
        "QLabel {"
        "  color: %2;"
        "}"
        "QPushButton {"
        "  background: %3;"
        "  border: 1px solid %4;"
        "  color: %5;"
        "  font-weight: 700;"
        "  padding: 7px 18px;"
        "  border-radius: 6px;"
        "}"
        "QPushButton:hover {"
        "  background: %6;"
        "  border-color: %7;"
        "}")
        .arg(c.window,
             c.text,
             c.button,
             c.buttonBorder,
             c.title,
             c.buttonHover,
             c.buttonHoverBorder);
}

inline QMessageBox* makeAboutMessageBox(QWidget* parent,
                                 QMessageBox::Icon icon,
                                 const QString& title,
                                 const QString& text,
                                 const QString& informativeText)
{
    auto* message = new QMessageBox(parent);
    message->setWindowTitle(title);
    message->setIcon(icon);
    message->setText(text);
    message->setInformativeText(informativeText);
    message->setStyleSheet(aboutMessageBoxStyleSheet());
    return message;
}

class AboutDialog final : public QDialog {
public:
    explicit AboutDialog(QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("About MdsScope");
        setWindowIcon(appIcon());
        setModal(true);
        setSizeGripEnabled(false);
        networkManager_ = new QNetworkAccessManager(this);

        const FontSettings& fonts = fontSettings();
        const QFont baseFont(fonts.family, fonts.uiSize);
        const QFontMetrics baseMetrics(baseFont);
        setFont(baseFont);
        setFixedWidth(std::max(620, baseMetrics.horizontalAdvance(QStringLiteral("Git Version 3.0.r000.g000000000.dirty")) + 210));
        QFont titleFont = baseFont;
        titleFont.setBold(true);
        titleFont.setPointSize(std::max(18, baseFont.pointSize() + 6));
        QFont labelFont = baseFont;
        QFont valueFont = baseFont;
        valueFont.setBold(true);

        QString systemText = QSysInfo::prettyProductName().trimmed();
        if (systemText.isEmpty()) {
            systemText = QSysInfo::kernelType() + QStringLiteral(" ") + QSysInfo::kernelVersion();
        }
        const QString arch = QSysInfo::currentCpuArchitecture();
        if (!arch.isEmpty()) {
            systemText += QStringLiteral(" (%1)").arg(arch);
        }

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(20, 18, 20, 18);
        layout->setSpacing(12);

        auto* header = new QWidget(this);
        header->setObjectName("aboutHeader");
        auto* headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(18, 16, 18, 16);
        headerLayout->setSpacing(18);

        auto* iconBadge = new QWidget(header);
        iconBadge->setObjectName("aboutIconBadge");
        iconBadge->setFixedSize(62, 62);
        auto* iconLayout = new QVBoxLayout(iconBadge);
        iconLayout->setContentsMargins(0, 0, 0, 0);
        auto* icon = new QLabel(iconBadge);
        icon->setAlignment(Qt::AlignCenter);
        icon->setPixmap(appIcon().pixmap(46, 46));
        iconLayout->addWidget(icon);

        auto* leftBlock = new QWidget(header);
        auto* leftLayout = new QHBoxLayout(leftBlock);
        leftLayout->setContentsMargins(0, 0, 0, 0);
        leftLayout->setSpacing(14);
        leftLayout->addWidget(iconBadge);
        auto* title = new QLabel("MdsScope", leftBlock);
        title->setObjectName("aboutTitle");
        title->setFont(titleFont);
        title->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        leftLayout->addWidget(title);
        headerLayout->addWidget(leftBlock, 0, Qt::AlignLeft | Qt::AlignVCenter);

        auto* subtitle = new QLabel("Signal data plotting for MDSplus experiments.", header);
        subtitle->setObjectName("aboutSubtitle");
        subtitle->setFont(baseFont);
        subtitle->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        subtitle->setWordWrap(true);
        subtitle->setMinimumWidth(230);
        headerLayout->addStretch(1);
        headerLayout->addWidget(subtitle, 1);
        layout->addWidget(header);

        auto* card = new QWidget(this);
        card->setObjectName("aboutCard");
        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(0, 6, 0, 6);
        cardLayout->setSpacing(0);

        auto addRow = [cardLayout, card, labelFont, valueFont](const QString& name, const QString& value, bool rich = false, bool separator = true) {
            auto* rowWidget = new QWidget(card);
            rowWidget->setObjectName("aboutRow");
            auto* rowLayout = new QHBoxLayout(rowWidget);
            rowLayout->setContentsMargins(16, 10, 16, 10);
            rowLayout->setSpacing(18);

            auto* nameLabel = new QLabel(name, rowWidget);
            nameLabel->setObjectName("aboutKey");
            nameLabel->setFont(labelFont);
            auto* valueLabel = new QLabel(value, rowWidget);
            valueLabel->setObjectName("aboutValue");
            valueLabel->setFont(valueFont);
            valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            valueLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
            valueLabel->setFocusPolicy(Qt::NoFocus);
            valueLabel->setOpenExternalLinks(false);
            valueLabel->setTextFormat(rich ? Qt::RichText : Qt::PlainText);
            valueLabel->setWordWrap(true);
            valueLabel->setMinimumWidth(300);
            if (rich) {
                connect(valueLabel, &QLabel::linkActivated, valueLabel, [](const QString& url) {
                    openExternalUrlQuietly(url);
                    if (QWidget* focus = QApplication::focusWidget()) {
                        focus->clearFocus();
                    }
                });
            }
            rowLayout->addWidget(nameLabel);
            rowLayout->addWidget(valueLabel, 1);
            cardLayout->addWidget(rowWidget);
            if (separator) {
                auto* line = new QFrame(card);
                line->setObjectName("aboutSeparator");
                line->setFrameShape(QFrame::HLine);
                line->setFrameShadow(QFrame::Plain);
                cardLayout->addWidget(line);
            }
        };

        addRow("MdsScope Version", QStringLiteral(MDSSCOPE_VERSION));
        addRow("Git Version", QStringLiteral(MDSSCOPE_GIT_VERSION));
        addRow("Qt Version", QString::fromLatin1(qVersion()));
        addRow("System", systemText);
        addRow("Copyright", QStringLiteral("Copyright (C) 2026 ") + htmlLink("Weikang Wang", "https://github.com/wwktz"), true);
        addRow("License", htmlLink("GPL-3.0-or-later", "https://www.gnu.org/licenses/gpl-3.0.html"), true);
        addRow("Source", htmlLink("GitHub", "https://github.com/wwktz/MdsScope"), true, false);
        layout->addWidget(card);

        auto* close = new QPushButton("Close", this);
        close->setObjectName("aboutCloseButton");
        close->setDefault(true);
        updateButton_ = new QPushButton("Update", this);
        updateButton_->setObjectName("aboutUpdateButton");
        updateStatus_ = new QLabel(this);
        updateStatus_->setObjectName("aboutUpdateStatus");
        auto* footer = new QHBoxLayout;
        footer->addWidget(updateButton_);
        footer->addWidget(updateStatus_, 1);
        footer->addStretch(1);
        footer->addWidget(close);
        layout->addLayout(footer);

        setStyleSheet(aboutDialogStyleSheet());

        adjustSize();
        setFixedHeight(sizeHint().height());
        connect(updateButton_, &QPushButton::clicked, this, [this] {
            checkForUpdate();
        });
        connect(close, &QPushButton::clicked, this, &QDialog::accept);
    }

private:
    void setUpdateBusy(bool busy, const QString& status)
    {
        if (updateButton_) {
            updateButton_->setEnabled(!busy);
        }
        if (updateStatus_) {
            updateStatus_->setText(status);
        }
    }

    QNetworkRequest updateRequest(const QUrl& url) const
    {
        QNetworkRequest request(url);
        request.setRawHeader("User-Agent", "MdsScope");
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
        return request;
    }

    void checkForUpdate()
    {
        setUpdateBusy(true, "Checking...");
        QNetworkReply* reply = networkManager_->get(updateRequest(QUrl(QStringLiteral("https://api.github.com/repos/wwktz/MdsScope/releases/latest"))));
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            handleLatestRelease(reply);
            reply->deleteLater();
        });
    }

    void handleLatestRelease(QNetworkReply* reply)
    {
        if (reply->error() != QNetworkReply::NoError) {
            setUpdateBusy(false, "Check failed");
            auto* message = makeAboutMessageBox(this,
                                                QMessageBox::Warning,
                                                "Update",
                                                "Could not check for updates.",
                                                reply->errorString());
            message->exec();
            message->deleteLater();
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(reply->readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            setUpdateBusy(false, "Check failed");
            auto* message = makeAboutMessageBox(this,
                                                QMessageBox::Warning,
                                                "Update",
                                                "Could not check for updates.",
                                                "GitHub returned an invalid release response.");
            message->exec();
            message->deleteLater();
            return;
        }

        const QJsonObject root = document.object();
        const QString tagName = root.value(QStringLiteral("tag_name")).toString().trimmed();
        const QString releaseUrl = root.value(QStringLiteral("html_url")).toString(QStringLiteral("https://github.com/wwktz/MdsScope/releases"));
        const ParsedVersion current = parseVersionTag(QStringLiteral(MDSSCOPE_VERSION));
        const ParsedVersion latest = parseVersionTag(tagName);
        if (!current.valid || !latest.valid) {
            setUpdateBusy(false, "Check failed");
            auto* message = makeAboutMessageBox(this,
                                                QMessageBox::Warning,
                                                "Update",
                                                "Could not compare release versions.",
                                                QStringLiteral("Current: %1\nLatest: %2").arg(QStringLiteral(MDSSCOPE_VERSION), tagName));
            message->exec();
            message->deleteLater();
            return;
        }

        if (compareVersions(latest, current) <= 0) {
            setUpdateBusy(false, "Up to date");
            auto* message = makeAboutMessageBox(this,
                                                QMessageBox::Information,
                                                "Update",
                                                QStringLiteral("MdsScope %1 is up to date.").arg(QStringLiteral(MDSSCOPE_VERSION)),
                                                {});
            message->exec();
            message->deleteLater();
            return;
        }

        setUpdateBusy(false, "Update available");
        auto* message = makeAboutMessageBox(this,
                                            QMessageBox::Information,
                                            "Update",
                                            QStringLiteral("MdsScope %1 is available.").arg(tagName),
                                            "Open the GitHub release page to download it?");
        QPushButton* openRelease = message->addButton("Open Release", QMessageBox::AcceptRole);
        message->addButton(QMessageBox::Cancel);
        message->exec();
        if (message->clickedButton() == openRelease) {
            openExternalUrlQuietly(releaseUrl);
        }
        message->deleteLater();
    }

    QNetworkAccessManager* networkManager_ = nullptr;
    QPushButton* updateButton_ = nullptr;
    QLabel* updateStatus_ = nullptr;
};
