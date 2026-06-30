#include "mdsscope_app.h"
#include "mdsscope_internal.h"

#include <QApplication>
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
#include <QStringList>
#include <QThreadPool>

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

void applySystemColorScheme(QApplication& app, uint scheme)
{
    app.setPalette(scheme == 1 ? darkPalette() : lightPalette());
}

#ifdef Q_OS_LINUX
class SystemThemeWatcher final : public QObject {
    Q_OBJECT

public:
    explicit SystemThemeWatcher(QApplication& app)
        : QObject(&app), app_(app)
    {
        applySystemColorScheme(app_, readPortalColorScheme());
        QDBusConnection::sessionBus().connect(QStringLiteral("org.freedesktop.portal.Desktop"),
                                              QStringLiteral("/org/freedesktop/portal/desktop"),
                                              QStringLiteral("org.freedesktop.portal.Settings"),
                                              QStringLiteral("SettingChanged"),
                                              this,
                                              SLOT(settingChanged(QString,QString,QDBusVariant)));
    }

private slots:
    void settingChanged(const QString& group, const QString& key, const QDBusVariant& value)
    {
        if (group == QStringLiteral("org.freedesktop.appearance") && key == QStringLiteral("color-scheme")) {
            applySystemColorScheme(app_, value.variant().toUInt());
        }
    }

private:
    static uint readFallbackColorScheme()
    {
        const QString gtkTheme = qEnvironmentVariable("GTK_THEME").toLower();
        if (gtkTheme.contains(QStringLiteral("dark"))) {
            return 1;
        }

        QProcess process;
        process.setProgram(QStringLiteral("gsettings"));
        process.setArguments({QStringLiteral("get"),
                              QStringLiteral("org.gnome.desktop.interface"),
                              QStringLiteral("color-scheme")});
        process.start();
        if (process.waitForFinished(500)) {
            const QString output = QString::fromUtf8(process.readAllStandardOutput()).trimmed().toLower();
            if (output.contains(QStringLiteral("prefer-dark"))) {
                return 1;
            }
            if (output.contains(QStringLiteral("prefer-light"))) {
                return 2;
            }
        }

        process.setProgram(QStringLiteral("gsettings"));
        process.setArguments({QStringLiteral("get"),
                              QStringLiteral("org.gnome.desktop.interface"),
                              QStringLiteral("gtk-theme")});
        process.start();
        if (process.waitForFinished(500)) {
            const QString output = QString::fromUtf8(process.readAllStandardOutput()).trimmed().toLower();
            if (output.contains(QStringLiteral("dark"))) {
                return 1;
            }
        }
        return 2;
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
            if (scheme == 1 || scheme == 2) {
                return scheme;
            }
        }
        return readFallbackColorScheme();
    }

    QApplication& app_;
};
#endif

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
#ifdef Q_OS_LINUX
    SystemThemeWatcher themeWatcher(app);
#else
    app.setPalette(lightPalette());
#endif
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
