// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.hpp"

namespace {
QMutex& sourceIndexMutex()
{
    static QMutex mutex;
    return mutex;
}

QHash<QString, QSet<QString>>& sourceIndexContents()
{
    static QHash<QString, QSet<QString>> contents;
    return contents;
}
}

void traceMdsLine(const QString& line)
{
    if (!qEnvironmentVariableIsSet("MDSSCOPE_MDS_TRACE") && !qEnvironmentVariableIsSet("WEBSCOPE_MDS_TRACE")) {
        return;
    }
    static QMutex mutex;
    QMutexLocker locker(&mutex);
    QFile file(QDir::temp().filePath("mdsscope_mds_trace.log"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        file.write(line.toUtf8());
        file.write("\n");
    }
}

QString appConfigDir()
{
#ifdef Q_OS_WIN
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return path.isEmpty() ? QDir::home().filePath("AppData/Local/MdsScope") : path;
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return path.isEmpty() ? QDir::home().filePath("Library/Application Support/MdsScope") : path;
#else
    return QDir::home().filePath(".config/mdsscope");
#endif
}

QString appCacheDir()
{
#ifdef Q_OS_WIN
    const QString path = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return path.isEmpty() ? QDir::home().filePath("AppData/Local/MdsScope/cache") : path;
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    const QString path = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return path.isEmpty() ? QDir::home().filePath("Library/Caches/MdsScope") : path;
#else
    return QDir::home().filePath(".cache/mdsscope");
#endif
}

static void copyFileIfMissing(const QString& source, const QString& target)
{
    if (source.isEmpty() || target.isEmpty() || QFile::exists(target) || !QFile::exists(source)) {
        return;
    }
    QDir().mkpath(QFileInfo(target).absolutePath());
    QFile::copy(source, target);
}

static QString sourceIndexFileName(QString tree)
{
    tree = tree.trimmed().toLower();
    tree.replace(QRegularExpression("[^a-z0-9_-]+"), "_");
    tree = tree.trimmed();
    return tree.isEmpty() ? QString() : tree + QStringLiteral(".txt");
}

static QString sourceIndexCacheDir()
{
    return QDir(appCacheDir()).filePath("source_index");
}

static bool appendUniqueLine(const QString& path, const QString& value, Qt::CaseSensitivity caseSensitivity)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    QMutexLocker locker(&sourceIndexMutex());
    const QString cacheKey = QFileInfo(path).absoluteFilePath()
                             + (caseSensitivity == Qt::CaseInsensitive ? QStringLiteral("|i") : QStringLiteral("|s"));
    auto& cache = sourceIndexContents();
    if (!cache.contains(cacheKey)) {
        QSet<QString> existing;
        QFile readFile(path);
        if (readFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&readFile);
            while (!in.atEnd()) {
                const QString line = in.readLine().trimmed();
                if (!line.isEmpty()) {
                    existing.insert(caseSensitivity == Qt::CaseInsensitive ? line.toLower() : line);
                }
            }
        }
        cache.insert(cacheKey, std::move(existing));
    }
    QSet<QString>& existing = cache[cacheKey];
    const QString normalized = caseSensitivity == Qt::CaseInsensitive ? trimmed.toLower() : trimmed;
    if (existing.contains(normalized)) {
        return false;
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile writeFile(path);
    if (!writeFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return false;
    }
    QTextStream out(&writeFile);
    out << trimmed << '\n';
    existing.insert(normalized);
    return true;
}

static void mergeSourceIndexFile(const QString& source, const QString& target)
{
    QFile sourceFile(source);
    if (!sourceFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QSet<QString> existing;
    QFile targetFile(target);
    if (targetFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&targetFile);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (!line.isEmpty()) {
                existing.insert(line.toLower());
            }
        }
    }

    QStringList missing;
    QTextStream in(&sourceFile);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (!line.isEmpty() && !existing.contains(line.toLower())) {
            missing.push_back(line);
            existing.insert(line.toLower());
        }
    }
    if (missing.isEmpty()) {
        return;
    }

    QDir().mkpath(QFileInfo(target).absolutePath());
    QFile appendFile(target);
    if (!appendFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }
    QTextStream out(&appendFile);
    for (const QString& line : std::as_const(missing)) {
        out << line << '\n';
    }
}

static void seedUserConfigFiles(const QString& rootPath)
{
    Q_UNUSED(rootPath);
    const QString configDir = appConfigDir();
    QDir().mkpath(configDir);
    QDir().mkpath(appCacheDir());
}

QString appEnvironmentDir(const QString& rootPath)
{
    seedUserConfigFiles(rootPath);

    const QString userEnvironment = QDir(appConfigDir()).filePath("environment");
    QDir userDir(userEnvironment);
    if (!userDir.exists()) {
        QDir().mkpath(userEnvironment);
    }

    const QDir sourceDir(QDir(rootPath).filePath("environment"));
    const auto existing = userDir.entryInfoList({"*.toml", "*.webscp"}, QDir::Files, QDir::Name);
    if (existing.isEmpty() && sourceDir.exists()) {
        const auto templates = sourceDir.entryInfoList({"*.toml", "*.webscp"}, QDir::Files, QDir::Name);
        for (const QFileInfo& file : templates) {
            copyFileIfMissing(file.absoluteFilePath(), userDir.filePath(file.fileName()));
        }
    }
    return userEnvironment;
}

QString appSourceIndexDir(const QString& rootPath)
{
    ensureSourceIndexCache(rootPath);
    return sourceIndexCacheDir();
}

bool ensureSourceIndexCache(const QString& rootPath)
{
    seedUserConfigFiles(rootPath);

    const QString cacheRoot = sourceIndexCacheDir();
    QDir().mkpath(cacheRoot);
    QDir().mkpath(QDir(cacheRoot).filePath("signals"));

    const QDir sourceRoot(QDir(rootPath).filePath("source_index"));
    if (!sourceRoot.exists()) {
        return false;
    }

    mergeSourceIndexFile(sourceRoot.filePath("trees.txt"), QDir(cacheRoot).filePath("trees.txt"));

    const QDir sourceSignals(sourceRoot.filePath("signals"));
    if (sourceSignals.exists()) {
        const QDir cacheSignals(QDir(cacheRoot).filePath("signals"));
        const auto files = sourceSignals.entryInfoList({"*.txt"}, QDir::Files, QDir::Name);
        for (const QFileInfo& file : files) {
            mergeSourceIndexFile(file.absoluteFilePath(), cacheSignals.filePath(file.fileName()));
        }
    }
    return true;
}

bool addSourceIndexSignal(const QString& tree, const QString& signal)
{
    const QString treeName = tree.trimmed();
    const QString signalName = normalizedMdsSignal(signal);
    const QString signalFile = sourceIndexFileName(treeName);
    if (treeName.isEmpty() || signalName.isEmpty() || signalFile.isEmpty()) {
        return false;
    }

    const QString cacheRoot = sourceIndexCacheDir();
    QDir().mkpath(QDir(cacheRoot).filePath("signals"));
    appendUniqueLine(QDir(cacheRoot).filePath("trees.txt"), treeName.toLower(), Qt::CaseInsensitive);
    return appendUniqueLine(QDir(QDir(cacheRoot).filePath("signals")).filePath(signalFile),
                            signalName,
                            Qt::CaseInsensitive);
}

QString defaultExportBaseDir()
{
    QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (downloads.isEmpty()) {
        downloads = QDir::home().filePath("Downloads");
    }
    const QString path = QDir(downloads).filePath("mdsscope");
    QDir().mkpath(path);
    return path;
}

QString uiSettingsPath(const QString& rootPath)
{
    seedUserConfigFiles(rootPath);
    return QDir(appConfigDir()).filePath("mdsscope_ui.ini");
}

FontSettings& fontSettings()
{
    static FontSettings settings;
    return settings;
}

void loadFontSettings(const QString& rootPath)
{
    QSettings settings(uiSettingsPath(rootPath), QSettings::IniFormat);
    FontSettings& fonts = fontSettings();
    fonts.family = settings.value("font/family", fonts.family).toString();
    fonts.legendSize = settings.value("font/legend_size", fonts.legendSize).toInt();
    fonts.axisSize = settings.value("font/axis_size", fonts.axisSize).toInt();
    fonts.unitSize = settings.value("font/unit_size", fonts.unitSize).toInt();
    fonts.uiSize = settings.value("font/ui_size", fonts.uiSize).toInt();
}

void saveFontSettings(const QString& rootPath)
{
    const FontSettings& fonts = fontSettings();
    QSettings settings(uiSettingsPath(rootPath), QSettings::IniFormat);
    settings.setValue("font/family", fonts.family);
    settings.setValue("font/legend_size", fonts.legendSize);
    settings.setValue("font/axis_size", fonts.axisSize);
    settings.setValue("font/unit_size", fonts.unitSize);
    settings.setValue("font/ui_size", fonts.uiSize);
}
