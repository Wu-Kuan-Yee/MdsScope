// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_app.h"
#include "mdsscope_internal.h"

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#ifdef Q_OS_LINUX
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>
#endif
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMessageBox>
#include <QPalette>
#include <QProcess>
#include <QSettings>
#include <QStyleHints>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>

#include <algorithm>

namespace {
class SystemThemeWatcher;

SystemThemeWatcher* gThemeWatcher = nullptr;

QPalette lightPalette()
{
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#f6f6f6"));
    palette.setColor(QPalette::WindowText, QColor("#111827"));
    palette.setColor(QPalette::Base, QColor("#ffffff"));
    palette.setColor(QPalette::AlternateBase, QColor("#f1f5f9"));
    palette.setColor(QPalette::ToolTipBase, QColor("#ffffff"));
    palette.setColor(QPalette::ToolTipText, QColor("#111827"));
    palette.setColor(QPalette::Text, QColor("#111827"));
    palette.setColor(QPalette::Button, QColor("#f3f4f6"));
    palette.setColor(QPalette::ButtonText, QColor("#111827"));
    palette.setColor(QPalette::BrightText, QColor("#ffffff"));
    palette.setColor(QPalette::Highlight, QColor("#2563eb"));
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    palette.setColor(QPalette::Link, QColor("#2563eb"));
    palette.setColor(QPalette::Mid, QColor("#cbd5e1"));
    palette.setColor(QPalette::Midlight, QColor("#e2e8f0"));
    palette.setColor(QPalette::Dark, QColor("#94a3b8"));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#64748b"));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#64748b"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#64748b"));
    return palette;
}

QPalette darkPalette()
{
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#111827"));
    palette.setColor(QPalette::WindowText, QColor("#f8fafc"));
    palette.setColor(QPalette::Base, QColor("#0f172a"));
    palette.setColor(QPalette::AlternateBase, QColor("#1e293b"));
    palette.setColor(QPalette::ToolTipBase, QColor("#1e293b"));
    palette.setColor(QPalette::ToolTipText, QColor("#f8fafc"));
    palette.setColor(QPalette::Text, QColor("#f8fafc"));
    palette.setColor(QPalette::Button, QColor("#1f2937"));
    palette.setColor(QPalette::ButtonText, QColor("#f8fafc"));
    palette.setColor(QPalette::BrightText, QColor("#ffffff"));
    palette.setColor(QPalette::Highlight, QColor("#60a5fa"));
    palette.setColor(QPalette::HighlightedText, QColor("#0f172a"));
    palette.setColor(QPalette::Link, QColor("#93c5fd"));
    palette.setColor(QPalette::Mid, QColor("#475569"));
    palette.setColor(QPalette::Midlight, QColor("#334155"));
    palette.setColor(QPalette::Dark, QColor("#020617"));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#94a3b8"));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#94a3b8"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#94a3b8"));
    return palette;
}

QString cssColor(const QPalette& palette, QPalette::ColorRole role)
{
    return palette.color(role).name(QColor::HexRgb);
}

QString cssColor(const QPalette& palette, QPalette::ColorGroup group, QPalette::ColorRole role)
{
    return palette.color(group, role).name(QColor::HexRgb);
}

QString applicationStyleSheet(const QPalette& palette)
{
    return QStringLiteral(
               "QMainWindow, QDialog, QWidget {"
               "  background: %1;"
               "  color: %2;"
               "}"
               "QToolBar, QStatusBar {"
               "  background: %1;"
               "  color: %2;"
               "}"
               "QPushButton {"
               "  background: %3;"
               "  color: %4;"
               "  border: 1px solid %5;"
               "  border-radius: 4px;"
               "}"
               "QPushButton:hover {"
               "  background: %6;"
               "  border-color: %7;"
               "}"
               "QPushButton:pressed {"
               "  background: %6;"
               "  border-color: %7;"
               "}"
               "QPushButton:disabled {"
               "  background: %6;"
               "  color: %8;"
               "  border-color: %5;"
               "}"
               "QToolButton {"
               "  background: transparent;"
               "  color: %4;"
               "  border: 1px solid transparent;"
               "  border-radius: 4px;"
               "}"
               "QToolButton:hover, QToolButton:pressed {"
               "  background: %6;"
               "  border-color: transparent;"
               "}"
               "QToolButton:checked {"
               "  background: transparent;"
               "  border-color: transparent;"
               "}"
               "QToolButton:disabled {"
               "  background: transparent;"
               "  color: %8;"
               "  border-color: transparent;"
               "}"
               "QLineEdit, QComboBox {"
               "  background: %9;"
               "  color: %10;"
               "  border: 1px solid %5;"
               "  border-radius: 3px;"
               "  selection-background-color: %7;"
               "  selection-color: %11;"
               "}"
               "QLineEdit:focus, QComboBox:focus {"
               "  border-color: %7;"
               "}"
               "QLineEdit:disabled, QComboBox:disabled {"
               "  background: %6;"
               "  color: %12;"
               "}"
               "QComboBox QAbstractItemView {"
               "  background: %9;"
               "  color: %10;"
               "  border: 1px solid %5;"
               "  selection-background-color: %7;"
               "  selection-color: %11;"
               "}"
               "QMenu {"
               "  background: %9;"
               "  color: %10;"
               "  border: 1px solid %5;"
               "}"
               "QMenu::item:selected {"
               "  background: %7;"
               "  color: %11;"
               "}")
        .arg(cssColor(palette, QPalette::Window),
             cssColor(palette, QPalette::WindowText),
             cssColor(palette, QPalette::Button),
             cssColor(palette, QPalette::ButtonText),
             cssColor(palette, QPalette::Mid),
             cssColor(palette, QPalette::AlternateBase),
             cssColor(palette, QPalette::Highlight),
             cssColor(palette, QPalette::Disabled, QPalette::ButtonText),
             cssColor(palette, QPalette::Base),
             cssColor(palette, QPalette::Text),
             cssColor(palette, QPalette::HighlightedText),
             cssColor(palette, QPalette::Disabled, QPalette::Text));
}

constexpr const char* kThemeModeSetting = "ui/themeMode";
constexpr const char* kThemeModeProperty = "mdsscope.themeMode";

ThemeMode normalizedThemeMode(int value)
{
    switch (value) {
    case static_cast<int>(ThemeMode::Light):
        return ThemeMode::Light;
    case static_cast<int>(ThemeMode::Dark):
        return ThemeMode::Dark;
    default:
        return ThemeMode::Auto;
    }
}

ThemeMode readStoredThemeMode()
{
    return normalizedThemeMode(QSettings().value(QString::fromLatin1(kThemeModeSetting),
                                                 static_cast<int>(ThemeMode::Auto))
                                   .toInt());
}

uint schemeForThemeMode(ThemeMode mode, uint systemScheme)
{
    if (mode == ThemeMode::Dark) {
        return 1;
    }
    if (mode == ThemeMode::Light) {
        return 2;
    }
    return systemScheme == 1 ? 1 : 2;
}

void applySystemColorScheme(QApplication& app, uint scheme)
{
    const QPalette palette = scheme == 1 ? darkPalette() : lightPalette();
    app.setPalette(palette);
    app.setStyleSheet(applicationStyleSheet(palette));
}

class SystemThemeWatcher final : public QObject {
    Q_OBJECT

public:
    explicit SystemThemeWatcher(QApplication& app)
        : QObject(&app), app_(app)
    {
        gThemeWatcher = this;
        currentMode_ = readStoredThemeMode();
        currentScheme_ = resolveColorScheme(readCurrentColorScheme());
        applyCurrentTheme();
#ifdef Q_OS_LINUX
        QDBusConnection::sessionBus().connect(QStringLiteral("org.freedesktop.portal.Desktop"),
                                              QStringLiteral("/org/freedesktop/portal/desktop"),
                                              QStringLiteral("org.freedesktop.portal.Settings"),
                                              QStringLiteral("SettingChanged"),
                                              this,
                                              SLOT(settingChanged(QString,QString,QDBusVariant)));
#endif
        connect(&pollTimer_, &QTimer::timeout, this, [this] {
            if (currentMode_ != ThemeMode::Auto) {
                return;
            }
            setSystemScheme(readCurrentColorScheme());
        });
        pollTimer_.start(3000);
        QTimer::singleShot(0, this, [this] { setSystemScheme(readCurrentColorScheme()); });
        QTimer::singleShot(750, this, [this] { setSystemScheme(readCurrentColorScheme()); });
        QTimer::singleShot(2000, this, [this] { setSystemScheme(readCurrentColorScheme()); });
    }

    ~SystemThemeWatcher() override
    {
        if (gThemeWatcher == this) {
            gThemeWatcher = nullptr;
        }
    }

    ThemeMode themeMode() const
    {
        return currentMode_;
    }

    void setThemeMode(ThemeMode mode)
    {
        mode = normalizedThemeMode(static_cast<int>(mode));
        if (mode == ThemeMode::Auto) {
            setSystemScheme(readCurrentColorScheme());
        }
        if (mode == currentMode_) {
            return;
        }
        currentMode_ = mode;
        QSettings().setValue(QString::fromLatin1(kThemeModeSetting), static_cast<int>(mode));
        applyCurrentTheme();
    }

#ifdef Q_OS_LINUX
private slots:
    void settingChanged(const QString& group, const QString& key, const QDBusVariant& value)
    {
        if (group == QStringLiteral("org.freedesktop.appearance") && key == QStringLiteral("color-scheme")) {
            setSystemScheme(portalColorScheme(value.variant().toUInt()));
        }
    }
#endif

private:
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    static QString processOutput(const QString& program, const QStringList& arguments)
    {
        QProcess process;
        process.setProgram(program);
        process.setArguments(arguments);
        process.start();
        if (!process.waitForFinished(1200)) {
            return {};
        }
        return QString::fromUtf8(process.readAllStandardOutput()).trimmed().toLower();
    }
#endif

#ifdef Q_OS_LINUX
    static uint readQtColorScheme()
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        if (auto* hints = QGuiApplication::styleHints()) {
            if (hints->colorScheme() == Qt::ColorScheme::Dark) {
                return 1;
            }
        }
#endif
        return 0;
    }

    static uint schemeFromText(const QString& text)
    {
        if (text.contains(QStringLiteral("dark"))) {
            return 1;
        }
        if (text.contains(QStringLiteral("light"))) {
            return 2;
        }
        return 0;
    }

    static uint portalColorScheme(uint scheme)
    {
        if (scheme == 1) {
            return 1;
        }
        if (scheme == 2) {
            return 2;
        }
        return 0;
    }

    static uint readKdeColorScheme()
    {
        for (const QString& tool : {QStringLiteral("kreadconfig6"), QStringLiteral("kreadconfig5")}) {
            uint scheme = schemeFromText(processOutput(tool,
                                                       {QStringLiteral("--file"),
                                                        QStringLiteral("kdeglobals"),
                                                        QStringLiteral("--group"),
                                                        QStringLiteral("General"),
                                                        QStringLiteral("--key"),
                                                        QStringLiteral("ColorScheme")}));
            if (scheme != 0) {
                return scheme;
            }

            scheme = schemeFromText(processOutput(tool,
                                                  {QStringLiteral("--file"),
                                                   QStringLiteral("kdeglobals"),
                                                   QStringLiteral("--group"),
                                                   QStringLiteral("KDE"),
                                                   QStringLiteral("--key"),
                                                   QStringLiteral("LookAndFeelPackage")}));
            if (scheme != 0) {
                return scheme;
            }
        }
        return 0;
    }

    static uint readConfiguredDesktopColorScheme()
    {
        const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower();
        const bool kdeSession = desktop.contains(QStringLiteral("kde")) || desktop.contains(QStringLiteral("plasma"))
                                || qEnvironmentVariableIsSet("KDE_FULL_SESSION");
        if (kdeSession) {
            const uint scheme = readKdeColorScheme();
            if (scheme != 0) {
                return scheme;
            }
        }

        const QString gtkTheme = qEnvironmentVariable("GTK_THEME").toLower();
        if (gtkTheme.contains(QStringLiteral("dark"))) {
            return 1;
        }

        QString output = processOutput(QStringLiteral("gsettings"),
                                       {QStringLiteral("get"),
                                        QStringLiteral("org.gnome.desktop.interface"),
                                        QStringLiteral("color-scheme")});
        if (output.contains(QStringLiteral("prefer-dark"))) {
            return 1;
        }
        if (output.contains(QStringLiteral("prefer-light")) || output.contains(QStringLiteral("default"))) {
            return 2;
        }

        output = processOutput(QStringLiteral("gsettings"),
                               {QStringLiteral("get"),
                                QStringLiteral("org.gnome.desktop.interface"),
                                QStringLiteral("gtk-theme")});
        uint scheme = schemeFromText(output);
        if (scheme != 0) {
            return scheme;
        }

        if (!kdeSession) {
            scheme = readKdeColorScheme();
        }
        return scheme;
    }

    static uint readFallbackColorScheme()
    {
        uint scheme = readConfiguredDesktopColorScheme();
        if (scheme != 0) {
            return scheme;
        }

        scheme = readQtColorScheme();
        return scheme != 0 ? scheme : 2;
    }

    static uint readPortalColorScheme()
    {
        QDBusInterface settings(QStringLiteral("org.freedesktop.portal.Desktop"),
                                QStringLiteral("/org/freedesktop/portal/desktop"),
                                QStringLiteral("org.freedesktop.portal.Settings"),
                                QDBusConnection::sessionBus());
        const QDBusReply<QDBusVariant> reply = settings.call(QStringLiteral("Read"),
                                                             QStringLiteral("org.freedesktop.appearance"),
                                                             QStringLiteral("color-scheme"));
        if (reply.isValid()) {
            return portalColorScheme(reply.value().variant().toUInt());
        }
        return 0;
    }
#endif

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    static uint readMacColorScheme()
    {
        const QString output = processOutput(QStringLiteral("defaults"),
                                             {QStringLiteral("read"),
                                              QStringLiteral("-g"),
                                              QStringLiteral("AppleInterfaceStyle")});
        return output.contains(QStringLiteral("dark")) ? 1 : 2;
    }
#endif

#ifdef Q_OS_WIN
    static uint readWindowsColorScheme()
    {
        QSettings settings(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
                           QSettings::NativeFormat);
        return settings.value(QStringLiteral("AppsUseLightTheme"), 1).toInt() == 0 ? 1 : 2;
    }
#endif

    static uint resolveColorScheme(uint scheme)
    {
        if (scheme == 1 || scheme == 2) {
            return scheme;
        }
#ifdef Q_OS_LINUX
        return readFallbackColorScheme();
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
        return readMacColorScheme();
#elif defined(Q_OS_WIN)
        return readWindowsColorScheme();
#else
        return 2;
#endif
    }

    static uint readCurrentColorScheme()
    {
#ifdef Q_OS_LINUX
        uint scheme = readPortalColorScheme();
        return scheme != 0 ? scheme : readFallbackColorScheme();
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
        return readMacColorScheme();
#elif defined(Q_OS_WIN)
        return readWindowsColorScheme();
#else
        return 2;
#endif
    }

    void setSystemScheme(uint scheme)
    {
        scheme = resolveColorScheme(scheme);
        if (scheme == currentScheme_) {
            return;
        }
        currentScheme_ = scheme;
        if (currentMode_ == ThemeMode::Auto) {
            applyCurrentTheme();
        }
    }

    void applyCurrentTheme()
    {
        app_.setProperty(kThemeModeProperty, static_cast<int>(currentMode_));
        applySystemColorScheme(app_, schemeForThemeMode(currentMode_, currentScheme_));
    }

    QApplication& app_;
    QTimer pollTimer_;
    uint currentScheme_ = 0;
    ThemeMode currentMode_ = ThemeMode::Auto;
};

QDir runtimeRootDir()
{
    QDir dir(QCoreApplication::applicationDirPath());
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    QDir bundleResources(dir);
    if (bundleResources.cdUp() && bundleResources.cd("Resources") && bundleResources.exists("environment")) {
        return bundleResources;
    }
#endif
    for (int i = 0; i < 4 && !dir.exists("environment"); ++i) {
        dir.cdUp();
    }
    if (dir.exists("environment")) {
        return dir;
    }
    if (QDir::current().exists("environment")) {
        return QDir::current();
    }

    QDir installDir(QCoreApplication::applicationDirPath());
    if (installDir.cdUp() && installDir.cd("share") && installDir.cd("mdsscope") && installDir.exists("environment")) {
        return installDir;
    }
    return QDir(QCoreApplication::applicationDirPath());
}

bool desktopFileIsInstalled(const QString& desktopFileName)
{
    const QString fileName = desktopFileName.endsWith(".desktop") ? desktopFileName : desktopFileName + ".desktop";
    QStringList dataDirs;
    const QString dataHome = qEnvironmentVariable("XDG_DATA_HOME");
    dataDirs.push_back(dataHome.isEmpty() ? QDir::home().filePath(".local/share") : dataHome);

    const QString dataDirsEnv = qEnvironmentVariable("XDG_DATA_DIRS");
    if (dataDirsEnv.isEmpty()) {
        dataDirs << "/usr/local/share" << "/usr/share";
    } else {
        dataDirs += dataDirsEnv.split(':', Qt::SkipEmptyParts);
    }

    for (const QString& dir : std::as_const(dataDirs)) {
        if (QFileInfo::exists(QDir(QDir(dir).filePath("applications")).filePath(fileName))) {
            return true;
        }
    }
    return false;
}

bool ensureApiLoginBeforeMain(const QString& rootPath)
{
    QHash<QString, QString> properties = readApiSettings(rootPath);
    CachedAuth auth;
    if (loadCachedAuth(&auth) && !auth.token.trimmed().isEmpty() && !tokenExpiresSoon(auth.token)) {
        return true;
    }

    const QString api = properties.value("ApiUrl").trimmed();
    if (!api.isEmpty() && (!auth.userName.isEmpty() || !auth.password.isEmpty())) {
        ApiLoginResult result = requestApiToken(api, properties.value("Charset", "UTF-8"), auth.userName, auth.password);
        if (result.ok) {
            auth.token = result.token;
            return saveCachedAuth(auth);
        }
    }

    LoginDialog dialog(rootPath);
    dialog.setWindowIcon(appIcon());
    return dialog.exec() == QDialog::Accepted;
}
}

ThemeMode mdsScopeThemeMode()
{
    if (gThemeWatcher) {
        return gThemeWatcher->themeMode();
    }
    const QVariant value = qApp ? qApp->property(kThemeModeProperty) : QVariant();
    if (value.isValid()) {
        return normalizedThemeMode(value.toInt());
    }
    return readStoredThemeMode();
}

void setMdsScopeThemeMode(ThemeMode mode)
{
    mode = normalizedThemeMode(static_cast<int>(mode));
    if (gThemeWatcher) {
        gThemeWatcher->setThemeMode(mode);
        return;
    }
    QSettings().setValue(QString::fromLatin1(kThemeModeSetting), static_cast<int>(mode));
}

int main(int argc, char* argv[])
{
    QApplication::setApplicationName("mdsscope");
    QApplication::setApplicationDisplayName("MdsScope");
    QApplication::setApplicationVersion(QStringLiteral(MDSSCOPE_VERSION));
    if (desktopFileIsInstalled("mdsscope")) {
        QApplication::setDesktopFileName("mdsscope");
    }
    QApplication::setOrganizationName("MdsScope");
    QApplication app(argc, argv);
    QApplication::setWindowIcon(appIcon());
    SystemThemeWatcher themeWatcher(app);
    QThreadPool::globalInstance()->setMaxThreadCount(16);
    QThreadPool::globalInstance()->setExpiryTimeout(300000);

    QDir workDir = runtimeRootDir();

    const QStringList args = QCoreApplication::arguments();
    const int benchmarkIndex = args.indexOf("--benchmark") >= 0 ? args.indexOf("--benchmark") : args.indexOf("--bench");
    if (benchmarkIndex >= 0) {
        QString configPath = QDir(workDir).filePath("environment/high_desity_impurity_0626.webscp");
        if (benchmarkIndex + 1 < args.size() && !args[benchmarkIndex + 1].startsWith("--")) {
            configPath = args[benchmarkIndex + 1];
        }
        QString shotOverride;
        const int shotIndex = args.indexOf("--shot");
        if (shotIndex >= 0 && shotIndex + 1 < args.size()) {
            shotOverride = args[shotIndex + 1].trimmed();
        }
        int repeat = 1;
        const int repeatIndex = args.indexOf("--repeat");
        if (repeatIndex >= 0 && repeatIndex + 1 < args.size()) {
            bool ok = false;
            const int parsed = args[repeatIndex + 1].toInt(&ok);
            if (ok && parsed > 0) {
                repeat = parsed;
            }
        }
        const DataReadMode readMode = args.contains("--full") ? DataReadMode::Full : DataReadMode::Thin;
        int code = 0;
        for (int i = 0; i < repeat; ++i) {
            code = std::max(code, runMdsScopeBenchmark(configPath,
                                                       readMode,
                                                       shotOverride,
                                                       args.contains("--summary"),
                                                       args.contains("--prewarm")));
        }
        return code;
    }

    if (!ensureApiLoginBeforeMain(workDir.absolutePath())) {
        shutdownMdsScopeWorkers();
        return 1;
    }

    MainWindow window(workDir.absolutePath());
    window.resize(1440, 920);
    window.show();
    const int code = app.exec();
    shutdownMdsScopeWorkers();
    return code;
}

#include "main.moc"
