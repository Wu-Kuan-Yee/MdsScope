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
#include <QMessageBox>
#include <QPalette>
#include <QProcess>
#include <QSettings>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>

#include <algorithm>

namespace {
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
               "QPushButton, QToolButton {"
               "  background: %3;"
               "  color: %4;"
               "  border: 1px solid %5;"
               "  border-radius: 4px;"
               "}"
               "QPushButton:hover, QToolButton:hover {"
               "  background: %6;"
               "  border-color: %7;"
               "}"
               "QPushButton:pressed, QToolButton:pressed, QToolButton:checked {"
               "  background: %6;"
               "  border-color: %7;"
               "}"
               "QPushButton:disabled, QToolButton:disabled {"
               "  background: %6;"
               "  color: %8;"
               "  border-color: %5;"
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
        applyIfChanged(readCurrentColorScheme());
#ifdef Q_OS_LINUX
        QDBusConnection::sessionBus().connect(QStringLiteral("org.freedesktop.portal.Desktop"),
                                              QStringLiteral("/org/freedesktop/portal/desktop"),
                                              QStringLiteral("org.freedesktop.portal.Settings"),
                                              QStringLiteral("SettingChanged"),
                                              this,
                                              SLOT(settingChanged(QString,QString,QDBusVariant)));
#endif
        connect(&pollTimer_, &QTimer::timeout, this, [this] {
            applyIfChanged(readCurrentColorScheme());
        });
        pollTimer_.start(3000);
    }

#ifdef Q_OS_LINUX
private slots:
    void settingChanged(const QString& group, const QString& key, const QDBusVariant& value)
    {
        if (group == QStringLiteral("org.freedesktop.appearance") && key == QStringLiteral("color-scheme")) {
            applyIfChanged(resolveColorScheme(value.variant().toUInt()));
        }
    }
#endif

private:
#ifdef Q_OS_LINUX
    static QString processOutput(const QString& program, const QStringList& arguments)
    {
        QProcess process;
        process.setProgram(program);
        process.setArguments(arguments);
        process.start();
        if (!process.waitForFinished(500)) {
            return {};
        }
        return QString::fromUtf8(process.readAllStandardOutput()).trimmed().toLower();
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

    static uint readFallbackColorScheme()
    {
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
        if (output.contains(QStringLiteral("prefer-light"))) {
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

        scheme = readKdeColorScheme();
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
            const uint scheme = reply.value().variant().toUInt();
            return resolveColorScheme(scheme);
        }
        return readFallbackColorScheme();
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
#elif defined(Q_OS_WIN)
        return readWindowsColorScheme();
#else
        return 2;
#endif
    }

    static uint readCurrentColorScheme()
    {
#ifdef Q_OS_LINUX
        return readPortalColorScheme();
#elif defined(Q_OS_WIN)
        return readWindowsColorScheme();
#else
        return 2;
#endif
    }

    void applyIfChanged(uint scheme)
    {
        scheme = resolveColorScheme(scheme);
        if (scheme == currentScheme_) {
            return;
        }
        currentScheme_ = scheme;
        applySystemColorScheme(app_, scheme);
    }

    QApplication& app_;
    QTimer pollTimer_;
    uint currentScheme_ = 0;
};

QDir runtimeRootDir()
{
    QDir dir(QCoreApplication::applicationDirPath());
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
