#include "mdsscope_internal.h"

void traceMdsLine(const QString& line)
{
    if (!qEnvironmentVariableIsSet("MDSSCOPE_MDS_TRACE") && !qEnvironmentVariableIsSet("WEBSCOPE_MDS_TRACE")) {
        return;
    }
    static QMutex mutex;
    QMutexLocker locker(&mutex);
    QFile file("/tmp/mdsscope_mds_trace.log");
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        file.write(line.toUtf8());
        file.write("\n");
    }
}

QString appConfigDir()
{
    return QDir::home().filePath(".config/mdsscope");
}

QString appCacheDir()
{
    return QDir::home().filePath(".cache/mdsscope");
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

    QFile readFile(path);
    if (readFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&readFile);
        while (!in.atEnd()) {
            if (in.readLine().trimmed().compare(trimmed, caseSensitivity) == 0) {
                return false;
            }
        }
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile writeFile(path);
    if (!writeFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return false;
    }
    QTextStream out(&writeFile);
    out << trimmed << '\n';
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

QString trimQuotes(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2) {
        const QChar first = value.front();
        const QChar last = value.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            value = value.mid(1, value.size() - 2);
        }
    }
    return value.trimmed();
}

double parseDouble(const QString& value)
{
    bool ok = false;
    const double parsed = trimQuotes(value).toDouble(&ok);
    return ok ? parsed : qQNaN();
}

int parseInt(const QHash<QString, QString>& map, const QString& key, int fallback = 0)
{
    bool ok = false;
    const int parsed = map.value(key).trimmed().toInt(&ok);
    return ok ? parsed : fallback;
}

QString javaUnescape(QString value)
{
    value.replace("\\:", ":");
    value.replace("\\=", "=");
    value.replace("\\\\", "\\");
    return value;
}

QHash<QString, QString> readKeyValueFile(const QString& path)
{
    QHash<QString, QString> out;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return out;
    }

    while (!file.atEnd()) {
        QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }
        int pos = line.indexOf(':');
        const int eq = line.indexOf('=');
        if (eq >= 0 && (pos < 0 || eq < pos)) {
            pos = eq;
        }
        if (pos < 0) {
            continue;
        }
        const QString key = line.left(pos).trimmed();
        const QString value = javaUnescape(line.mid(pos + 1).trimmed());
        out.insert(key, value);
    }
    return out;
}

QHash<QString, QString> defaultApiProperties()
{
    return {
        {"Authorization_Prefix", "Bearer"},
        {"Charset", "UTF-8"},
    };
}

QString apiUrlPath(const QString& rootPath)
{
    return QDir(rootPath).filePath("APIurl");
}

QString readApiUrl(const QString& rootPath)
{
    QFile file(apiUrlPath(rootPath));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine()).trimmed();
            if (!line.isEmpty() && !line.startsWith('#')) {
                return javaUnescape(line);
            }
        }
    }

    return {};
}

QHash<QString, QString> readApiSettings(const QString& rootPath)
{
    auto properties = defaultApiProperties();
    properties.insert("ApiUrl", readApiUrl(rootPath));

    CachedAuth auth;
    if (loadCachedAuth(&auth)) {
        properties.insert("Token", auth.token);
    }

    return properties;
}

bool tokenExpiresSoon(const QString& token)
{
    const QStringList parts = token.split('.');
    if (parts.size() < 2) {
        return false;
    }

    QByteArray payload = parts.at(1).toUtf8();
    payload.replace('-', '+');
    payload.replace('_', '/');
    while (payload.size() % 4 != 0) {
        payload.append('=');
    }

    const QJsonObject json = QJsonDocument::fromJson(QByteArray::fromBase64(payload)).object();
    const QJsonValue expValue = json.value("exp");
    if (!expValue.isDouble()) {
        return false;
    }

    const qint64 exp = static_cast<qint64>(expValue.toDouble());
    return exp <= QDateTime::currentSecsSinceEpoch() + 300;
}

QString authCachePath()
{
    QDir().mkpath(appCacheDir());
    return QDir(appCacheDir()).filePath("auth.cache");
}

QByteArray localAuthKey()
{
    QByteArray material;
    QFile machineId("/etc/machine-id");
    if (machineId.open(QIODevice::ReadOnly | QIODevice::Text)) {
        material += machineId.readAll().trimmed();
    }
    material += '|';
    material += qgetenv("USER");
    material += '|';
    material += QDir::homePath().toUtf8();
    material += "|MdsScope EAST auth cache";
    return QCryptographicHash::hash(material, QCryptographicHash::Sha256);
}

QByteArray cryptAuthPayload(const QByteArray& data, const QByteArray& salt)
{
    const QByteArray key = localAuthKey();
    QByteArray out;
    out.resize(data.size());
    int offset = 0;
    quint64 counter = 0;
    while (offset < data.size()) {
        QByteArray counterBytes;
        counterBytes.resize(static_cast<int>(sizeof(counter)));
        qToLittleEndian(counter, reinterpret_cast<uchar*>(counterBytes.data()));
        const QByteArray stream = QCryptographicHash::hash(key + salt + counterBytes, QCryptographicHash::Sha256);
        const int n = std::min(stream.size(), data.size() - offset);
        for (int i = 0; i < n; ++i) {
            out[offset + i] = data[offset + i] ^ stream[i];
        }
        offset += n;
        ++counter;
    }
    return out;
}

bool loadCachedAuth(CachedAuth* auth)
{
    if (!auth) {
        return false;
    }
    QFile file(authCachePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    const QJsonObject wrapper = QJsonDocument::fromJson(file.readAll()).object();
    if (wrapper.value("version").toInt() != 1) {
        return false;
    }
    const QByteArray salt = QByteArray::fromBase64(wrapper.value("salt").toString().toUtf8());
    const QByteArray encrypted = QByteArray::fromBase64(wrapper.value("payload").toString().toUtf8());
    if (salt.isEmpty() || encrypted.isEmpty()) {
        return false;
    }
    const QJsonObject obj = QJsonDocument::fromJson(cryptAuthPayload(encrypted, salt)).object();
    auth->userName = obj.value("userName").toString();
    auth->password = obj.value("password").toString();
    auth->token = obj.value("token").toString();
    return !auth->token.isEmpty() || !auth->userName.isEmpty() || !auth->password.isEmpty();
}

bool saveCachedAuth(const CachedAuth& auth)
{
    QJsonObject obj;
    obj.insert("userName", auth.userName);
    obj.insert("password", auth.password);
    obj.insert("token", auth.token);

    QByteArray salt;
    salt.resize(16);
    for (char& ch : salt) {
        ch = static_cast<char>(QRandomGenerator::global()->generate() & 0xff);
    }
    const QByteArray plain = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    const QByteArray encrypted = cryptAuthPayload(plain, salt);

    QJsonObject wrapper;
    wrapper.insert("version", 1);
    wrapper.insert("salt", QString::fromLatin1(salt.toBase64()));
    wrapper.insert("payload", QString::fromLatin1(encrypted.toBase64()));

    QFile file(authCachePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }
    file.write(QJsonDocument(wrapper).toJson(QJsonDocument::Compact));
    file.write("\n");
    return true;
}

ApiLoginResult requestApiToken(const QString& api,
                               const QString& charset,
                               const QString& userName,
                               const QString& password)
{
    ApiLoginResult result;
    if (api.trimmed().isEmpty()) {
        result.error = QStringLiteral("Missing API URL.");
        return result;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(api.trimmed() + "/login"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json; charset=" + charset);
    request.setRawHeader("User-Agent", "MdsScope/0.1");
    request.setTransferTimeout(5000);

    QJsonObject payload;
    payload.insert("userName", userName.trimmed());
    payload.insert("password", password);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QNetworkReply* reply = manager.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(5000);
    loop.exec();

    if (!timer.isActive()) {
        reply->abort();
        reply->deleteLater();
        result.error = QStringLiteral("Login request timed out.");
        return result;
    }

    const QByteArray body = reply->readAll();
    const auto error = reply->error();
    reply->deleteLater();
    if (error != QNetworkReply::NoError) {
        result.error = QStringLiteral("Login request failed.");
        return result;
    }

    const QJsonObject json = QJsonDocument::fromJson(body).object();
    const bool ok = json.value("code").toString() == "20000" || json.value("code").toInt() == 20000;
    const QString token = json.value("data").toObject().value("token").toString();
    if (!ok || token.isEmpty()) {
        result.error = QStringLiteral("Invalid username or password.");
        return result;
    }

    result.ok = true;
    result.token = token;
    return result;
}

QString firstShotLikeText(const QString& text)
{
    static const QRegularExpression re(R"(\b\d{4,8}\b)");
    const auto match = re.match(text);
    return match.hasMatch() ? match.captured(0) : QString();
}

QString firstShotFromJsonValue(const QJsonValue& value)
{
    if (value.isDouble()) {
        return QString::number(static_cast<qint64>(value.toDouble()));
    }
    if (value.isString()) {
        return firstShotLikeText(value.toString());
    }
    if (value.isArray()) {
        const auto arr = value.toArray();
        for (const QJsonValue& item : arr) {
            const QString shot = firstShotFromJsonValue(item);
            if (!shot.isEmpty()) {
                return shot;
            }
        }
    }
    if (value.isObject()) {
        const auto obj = value.toObject();
        for (const QString& key : {"shot", "shotNo", "shotno", "treeShot", "value", "name"}) {
            const QString shot = firstShotFromJsonValue(obj.value(key));
            if (!shot.isEmpty()) {
                return shot;
            }
        }
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            const QString shot = firstShotFromJsonValue(it.value());
            if (!shot.isEmpty()) {
                return shot;
            }
        }
    }
    return {};
}

QString colorForIndex(int index)
{
    static const QStringList colors = {
        "#2364aa", "#c44e52", "#2f855a", "#805ad5", "#d97706",
        "#0f766e", "#9f1239", "#4a5568", "#db2777", "#16a34a",
        "#ea580c", "#0891b2", "#7c3aed", "#ca8a04", "#0ea5e9",
        "#be123c"
    };
    return colors.at(index % colors.size());
}

bool isDefaultSeriesColor(const QString& colorName, int index)
{
    const QColor color(colorName);
    if (!color.isValid()) {
        return true;
    }
    return color.name().compare(QColor(colorForIndex(index)).name(), Qt::CaseInsensitive) == 0;
}

int colorIndexForName(const QString& colorName, int fallback)
{
    const QColor color(colorName);
    if (!color.isValid()) {
        return fallback;
    }
    const QString normalized = color.name().toLower();
    for (int i = 0; i < 32; ++i) {
        if (QColor(colorForIndex(i)).name().toLower() == normalized) {
            return i;
        }
    }
    return fallback;
}

void normalizePresetColors(QVector<SignalSpec>& specs)
{
    for (int i = 0; i < specs.size(); ++i) {
        if (!specs[i].manualColor) {
            specs[i].colorName = colorForIndex(i);
        }
    }
}

QString compactAxisValue(double value)
{
    if (!std::isfinite(value)) {
        return {};
    }
    const double absValue = std::abs(value);
    if (absValue >= 1000.0) {
        return QString::number(value, 'e', 1);
    }
    if (absValue > 0.0 && absValue < 0.001) {
        return QString::number(value, 'g', 2);
    }
    if (absValue >= 100.0) {
        return QString::number(value, 'f', 0);
    }
    if (absValue >= 10.0) {
        return QString::number(value, 'f', 1);
    }
    return QString::number(value, 'g', 3);
}

QStringList uniformAxisValues(const QVector<double>& values)
{
    QStringList labels;
    labels.reserve(values.size());
    bool scientific = false;
    double minStep = std::numeric_limits<double>::infinity();
    for (int i = 0; i < values.size(); ++i) {
        const double value = values[i];
        if (!std::isfinite(value)) {
            labels.push_back(QString());
            continue;
        }
        const double absValue = std::abs(value);
        if (absValue >= 1000.0 || (absValue > 0.0 && absValue < 0.001)) {
            scientific = true;
        }
        if (i > 0 && std::isfinite(values[i - 1])) {
            const double step = std::abs(value - values[i - 1]);
            if (step > 0.0) {
                minStep = std::min(minStep, step);
            }
        }
        labels.push_back(QString());
    }

    int decimals = 0;
    if (scientific) {
        decimals = 2;
    } else if (std::isfinite(minStep) && minStep > 0.0) {
        if (minStep >= 10.0) {
            decimals = 0;
        } else {
            decimals = std::clamp(static_cast<int>(std::ceil(-std::log10(minStep))) + 1, 0, 5);
        }
    }

    auto formatted = [&](int precision) {
        QStringList out;
        out.reserve(values.size());
        for (double value : values) {
            out.push_back(std::isfinite(value) ? QString::number(value, scientific ? 'e' : 'f', precision) : QString());
        }
        return out;
    };

    labels = formatted(decimals);
    while (!scientific && decimals < 6) {
        QSet<QString> seen;
        bool duplicate = false;
        for (const QString& label : labels) {
            if (label.isEmpty()) {
                continue;
            }
            if (seen.contains(label)) {
                duplicate = true;
                break;
            }
            seen.insert(label);
        }
        if (!duplicate) {
            break;
        }
        labels = formatted(++decimals);
    }
    return labels;
}

QIcon gearIcon()
{
    QPixmap pixmap(22, 22);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPointF center(11.0, 11.0);
    QPainterPath teeth;
    for (int i = 0; i < 8; ++i) {
        const double angle = (M_PI * 2.0 * i / 8.0) + M_PI / 8.0;
        const QPointF outer(center.x() + std::cos(angle) * 8.7,
                            center.y() + std::sin(angle) * 8.7);
        QRectF tooth(outer.x() - 1.8, outer.y() - 1.8, 3.6, 3.6);
        teeth.addEllipse(tooth);
    }
    painter.setPen(QPen(QColor("#475569"), 1.7));
    painter.setBrush(QColor("#64748b"));
    painter.drawPath(teeth);
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(center, 6.9, 6.9);
    painter.drawEllipse(center, 2.5, 2.5);
    return QIcon(pixmap);
}

QIcon fontIcon()
{
    QPixmap pixmap(22, 22);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor("#475569"), 1.6));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(2.5, 2.5, 17.0, 17.0));
    QFont iconFont(QStringLiteral("Times New Roman"), 15, QFont::Bold);
    painter.setFont(iconFont);
    painter.setPen(QColor("#2563eb"));
    painter.drawText(pixmap.rect().adjusted(0, -1, 0, 0), Qt::AlignCenter, "A");
    return QIcon(pixmap);
}

QIcon saveIcon()
{
    QPixmap pixmap(22, 22);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor("#334155"), 1.6));
    painter.setBrush(QColor("#e2e8f0"));
    painter.drawRoundedRect(QRectF(2.5, 2.0, 17.0, 18.5), 1.8, 1.8);
    painter.setBrush(QColor("#475569"));
    painter.drawRect(QRectF(5.0, 4.5, 11.0, 5.8));
    painter.setBrush(QColor("#ffffff"));
    painter.drawRect(QRectF(6.0, 13.2, 10.0, 5.2));
    painter.setPen(QPen(QColor("#94a3b8"), 1.0));
    painter.drawLine(QPointF(7.8, 15.3), QPointF(14.2, 15.3));
    painter.drawLine(QPointF(7.8, 17.0), QPointF(14.2, 17.0));
    return QIcon(pixmap);
}

QIcon loginIcon(bool loggedIn)
{
    QPixmap pixmap(22, 22);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QColor frame = loggedIn ? QColor("#15803d") : QColor("#64748b");
    const QColor fill = loggedIn ? QColor("#dcfce7") : QColor("#e2e8f0");
    const QColor person = loggedIn ? QColor("#16a34a") : QColor("#94a3b8");

    painter.setPen(QPen(frame, 1.5));
    painter.drawLine(QPointF(6.5, 1.3), QPointF(11.0, 4.0));
    painter.drawLine(QPointF(15.5, 1.3), QPointF(11.0, 4.0));

    painter.setPen(QPen(frame, 1.6));
    painter.setBrush(fill);
    painter.drawRoundedRect(QRectF(1.5, 3.5, 19.0, 17.7), 2.0, 2.0);

    painter.setPen(Qt::NoPen);
    painter.setBrush(frame);
    painter.drawRoundedRect(QRectF(6.7, 2.7, 8.6, 2.7), 0.9, 0.9);

    painter.setBrush(person);
    painter.drawEllipse(QPointF(11.0, 10.3), 3.2, 3.2);

    QPainterPath shoulders;
    shoulders.moveTo(4.2, 18.0);
    shoulders.cubicTo(5.3, 13.1, 16.7, 13.1, 17.8, 18.0);
    shoulders.lineTo(17.8, 19.2);
    shoulders.lineTo(4.2, 19.2);
    shoulders.closeSubpath();
    painter.drawPath(shoulders);

    return QIcon(pixmap);
}

QIcon modeIcon(InteractionMode mode, bool active)
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor color = active ? QColor("#2563eb") : QColor("#9ca3af");
    painter.setPen(QPen(color, 2.3));
    painter.setBrush(Qt::NoBrush);
    if (mode == InteractionMode::Zoom) {
        painter.drawEllipse(QRectF(4.0, 4.0, 11.8, 11.8));
        painter.drawLine(QPointF(14.5, 14.5), QPointF(19.8, 19.8));
        painter.drawLine(QPointF(7.2, 9.9), QPointF(12.6, 9.9));
        painter.drawLine(QPointF(9.9, 7.2), QPointF(9.9, 12.6));
    } else if (mode == InteractionMode::Point) {
        painter.drawLine(QPointF(12.0, 3.8), QPointF(12.0, 20.2));
        painter.drawLine(QPointF(3.8, 12.0), QPointF(20.2, 12.0));
        painter.setBrush(color);
        painter.drawEllipse(QPointF(12.0, 12.0), 2.9, 2.9);
    } else {
        painter.setPen(color);
        QFont iconFont = painter.font();
        iconFont.setPointSize(17);
        painter.setFont(iconFont);
        painter.drawText(pixmap.rect(), Qt::AlignCenter, QString::fromUtf8("✋"));
    }
    return QIcon(pixmap);
}

QString tomlEscape(QString value)
{
    value.replace("\\", "\\\\");
    value.replace("\"", "\\\"");
    value.replace("\n", "\\n");
    value.replace("\r", "\\r");
    value.replace("\t", "\\t");
    return "\"" + value + "\"";
}

QString tomlUnescape(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.mid(1, value.size() - 2);
    }
    QString out;
    out.reserve(value.size());
    bool escaped = false;
    for (QChar ch : value) {
        if (escaped) {
            if (ch == 'n') {
                out += '\n';
            } else if (ch == 'r') {
                out += '\r';
            } else if (ch == 't') {
                out += '\t';
            } else {
                out += ch;
            }
            escaped = false;
        } else if (ch == '\\') {
            escaped = true;
        } else {
            out += ch;
        }
    }
    if (escaped) {
        out += '\\';
    }
    return out;
}

QString stripTomlComment(QString line)
{
    bool inString = false;
    bool escaped = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\' && inString) {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            inString = !inString;
            continue;
        }
        if (ch == '#' && !inString) {
            return line.left(i).trimmed();
        }
    }
    return line.trimmed();
}

bool tomlBool(const QString& value, bool fallback = false)
{
    const QString v = value.trimmed().toLower();
    if (v == "true") {
        return true;
    }
    if (v == "false") {
        return false;
    }
    return fallback;
}

int tomlInt(const QString& value, int fallback = 0)
{
    bool ok = false;
    const int parsed = value.trimmed().toInt(&ok);
    return ok ? parsed : fallback;
}

double tomlDouble(const QString& value)
{
    bool ok = false;
    const double parsed = value.trimmed().toDouble(&ok);
    return ok ? parsed : qQNaN();
}

void writeTomlString(QTextStream& out, const QString& key, const QString& value)
{
    out << key << " = " << tomlEscape(value) << '\n';
}

void writeTomlStringIfNotEmpty(QTextStream& out, const QString& key, const QString& value)
{
    if (!value.isEmpty()) {
        writeTomlString(out, key, value);
    }
}

void writeTomlBool(QTextStream& out, const QString& key, bool value)
{
    out << key << " = " << (value ? "true" : "false") << '\n';
}

void writeTomlBoolIfNotDefault(QTextStream& out, const QString& key, bool value, bool defaultValue)
{
    if (value != defaultValue) {
        writeTomlBool(out, key, value);
    }
}

void writeTomlDouble(QTextStream& out, const QString& key, double value)
{
    if (std::isfinite(value)) {
        out << key << " = " << QString::number(value, 'g', 12) << '\n';
    }
}

LayoutConfig parseTomlEnvironment(const QString& path)
{
    LayoutConfig config;
    config.filePath = path;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return config;
    }

    struct PanelSlot {
        int column = 1;
        int row = 1;
        PlotSpec plot;
    };
    QVector<PanelSlot> panels;
    PanelSlot* currentPanel = nullptr;
    SignalSpec* currentSignal = nullptr;
    QString section;

    while (!file.atEnd()) {
        const QString line = stripTomlComment(QString::fromUtf8(file.readLine()));
        if (line.isEmpty()) {
            continue;
        }
        if (line == "[[panels]]") {
            PanelSlot panel;
            panels.push_back(std::move(panel));
            currentPanel = &panels.back();
            currentSignal = nullptr;
            section = "panel";
            continue;
        }
        if (line == "[[panels.signals]]") {
            if (!currentPanel) {
                continue;
            }
            SignalSpec sig;
            currentPanel->plot.signalSpecs.push_back(sig);
            currentSignal = &currentPanel->plot.signalSpecs.back();
            section = "signal";
            continue;
        }

        const int eq = line.indexOf('=');
        if (eq < 0) {
            continue;
        }
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();

        if (section == "panel" && currentPanel) {
            PlotSpec& plot = currentPanel->plot;
            if (key == "column") currentPanel->column = std::max(1, tomlInt(value, 1));
            else if (key == "row") currentPanel->row = std::max(1, tomlInt(value, 1));
            else if (key == "shot") plot.shot = tomlUnescape(value);
            else if (key == "title") plot.title = tomlUnescape(value);
            else if (key == "x_label") plot.xLabel = tomlUnescape(value);
            else if (key == "y_label") plot.yLabel = tomlUnescape(value);
            else if (key == "extraction_points") plot.extractionPoints = tomlInt(value, 2000);
            else if (key == "grid") plot.grid = tomlBool(value, true);
            else if (key == "custom_x_range") plot.customXRange = tomlBool(value, false);
            else if (key == "custom_y_range") plot.customYRange = tomlBool(value, false);
            else if (key == "xmin") plot.xmin = tomlDouble(value);
            else if (key == "xmax") plot.xmax = tomlDouble(value);
            else if (key == "ymin") plot.ymin = tomlDouble(value);
            else if (key == "ymax") plot.ymax = tomlDouble(value);
        } else if (section == "signal" && currentSignal) {
            if (key == "shot") currentSignal->shot = tomlUnescape(value);
            else if (key == "tree") currentSignal->experiment = tomlUnescape(value);
            else if (key == "server") currentSignal->serverIp = tomlUnescape(value);
            else if (key == "y") currentSignal->yExpr = tomlUnescape(value);
            else if (key == "x") currentSignal->xExpr = tomlUnescape(value);
            else if (key == "color") currentSignal->colorName = tomlUnescape(value);
            else if (key == "manual_color") currentSignal->manualColor = tomlBool(value, false);
            else if (key == "hidden") currentSignal->hidden = tomlBool(value, false);
            else if (key == "full") currentSignal->fullData = tomlBool(value, false);
            else if (key == "read_mode") currentSignal->fullData = tomlUnescape(value).compare("full", Qt::CaseInsensitive) == 0;
        }
    }

    int maxColumn = 0;
    for (const PanelSlot& panel : panels) {
        maxColumn = std::max(maxColumn, panel.column);
    }
    config.columns.resize(std::max(1, maxColumn));
    std::sort(panels.begin(), panels.end(), [](const PanelSlot& a, const PanelSlot& b) {
        if (a.column != b.column) {
            return a.column < b.column;
        }
        return a.row < b.row;
    });
    for (PanelSlot& panel : panels) {
        normalizePresetColors(panel.plot.signalSpecs);
        config.columns[panel.column - 1].push_back(std::move(panel.plot));
    }
    return config;
}

LayoutConfig parseWebscpEnvironment(const QString& path)
{
    const auto map = readKeyValueFile(path);
    LayoutConfig config;
    config.filePath = path;
    const int cols = std::max(1, parseInt(map, "cols", 1));
    config.columns.resize(cols);

    for (int c = 1; c <= cols; ++c) {
        const int rows = std::max(0, parseInt(map, QString::number(c) + ".rows", 0));
        config.columns[c - 1].reserve(rows);
        for (int r = 1; r <= rows; ++r) {
            const QString prefix = QString("%1_%2.").arg(c).arg(r);
            PlotSpec plot;
            plot.shot = trimQuotes(map.value(prefix + "shot_txt"));
            plot.title = trimQuotes(map.value(prefix + "title"));
            plot.xLabel = trimQuotes(map.value(prefix + "xlabel"));
            plot.yLabel = trimQuotes(map.value(prefix + "ylabel"));
            plot.extractionPoints = parseInt(map, prefix + "extraction_points", parseInt(map, "Extraction_points", 2000));
            plot.grid = parseInt(map, prefix + "grid_mode", parseInt(map, "Grid_Mode", 1)) != 0;
            plot.customXRange = parseInt(map, prefix + "xseting_mode", 1) == 0;
            plot.customYRange = parseInt(map, prefix + "yseting_mode", 1) == 0;
            plot.xmin = plot.customXRange ? parseDouble(map.value(prefix + "xmin_custom", map.value("xmin"))) : qQNaN();
            plot.xmax = plot.customXRange ? parseDouble(map.value(prefix + "xmax_custom", map.value("xmax"))) : qQNaN();
            plot.ymin = plot.customYRange ? parseDouble(map.value(prefix + "ymin_custom", map.value("ymin"))) : qQNaN();
            plot.ymax = plot.customYRange ? parseDouble(map.value(prefix + "ymax_custom", map.value("ymax"))) : qQNaN();

            const int signalCount = std::max(1, parseInt(map, prefix + "num_sig", 1));
            plot.signalSpecs.reserve(signalCount);
            for (int s = 1; s <= signalCount; ++s) {
                SignalSpec sig;
                sig.shot = trimQuotes(map.value(prefix + QString("shot_%1").arg(s)));
                sig.yExpr = trimQuotes(map.value(prefix + QString("y_expr_%1").arg(s)));
                sig.xExpr = trimQuotes(map.value(prefix + QString("x_expr_%1").arg(s)));
                sig.experiment = trimQuotes(map.value(prefix + QString("experiment_%1").arg(s)));
                sig.serverIp = trimQuotes(map.value(prefix + QString("server_ip_%1").arg(s)));
                const QString colorName = trimQuotes(map.value(prefix + QString("color_name_%1").arg(s)));
                sig.manualColor = parseInt(map, prefix + QString("color_manual_%1").arg(s), 0) != 0;
                if (sig.manualColor && !colorName.isEmpty()) {
                    sig.colorName = colorName;
                } else {
                    sig.colorName = colorForIndex(s - 1);
                    sig.manualColor = false;
                }
                if (!sig.yExpr.isEmpty()) {
                    plot.signalSpecs.push_back(sig);
                }
            }
            normalizePresetColors(plot.signalSpecs);
            config.columns[c - 1].push_back(plot);
        }
    }
    return config;
}

LayoutConfig parseEnvironment(const QString& path)
{
    if (path.endsWith(".toml", Qt::CaseInsensitive)) {
        return parseTomlEnvironment(path);
    }
    return parseWebscpEnvironment(path);
}

bool writeEnvironmentToml(const LayoutConfig& config, const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (error) {
            *error = "Cannot write " + path;
        }
        return false;
    }

    QTextStream out(&file);
    out << "version = 1\n\n";

    QHash<QString, int> shotCounts;
    for (const auto& column : config.columns) {
        for (const PlotSpec& plot : column) {
            const QString shot = plot.shot.trimmed();
            if (!shot.isEmpty()) {
                shotCounts[shot] += std::max(1, static_cast<int>(plot.signalSpecs.size()));
            }
        }
    }
    QString defaultShot;
    int defaultShotCount = 0;
    for (auto it = shotCounts.cbegin(); it != shotCounts.cend(); ++it) {
        if (it.value() > defaultShotCount) {
            defaultShot = it.key();
            defaultShotCount = it.value();
        }
    }

    for (int c = 0; c < config.columns.size(); ++c) {
        for (int r = 0; r < config.columns[c].size(); ++r) {
            const PlotSpec& plot = config.columns[c][r];
            const QString panelShot = plot.shot.trimmed();
            out << "[[panels]]\n";
            out << "column = " << c + 1 << '\n';
            out << "row = " << r + 1 << '\n';
            writeTomlStringIfNotEmpty(out, "title", plot.title);
            writeTomlStringIfNotEmpty(out, "x_label", plot.xLabel);
            writeTomlStringIfNotEmpty(out, "y_label", plot.yLabel);
            if (plot.extractionPoints != 2000) {
                out << "extraction_points = " << plot.extractionPoints << '\n';
            }
            writeTomlBoolIfNotDefault(out, "grid", plot.grid, true);
            writeTomlBoolIfNotDefault(out, "custom_x_range", plot.customXRange, false);
            writeTomlBoolIfNotDefault(out, "custom_y_range", plot.customYRange, false);
            writeTomlDouble(out, "xmin", plot.xmin);
            writeTomlDouble(out, "xmax", plot.xmax);
            writeTomlDouble(out, "ymin", plot.ymin);
            writeTomlDouble(out, "ymax", plot.ymax);
            out << '\n';

            for (int s = 0; s < plot.signalSpecs.size(); ++s) {
                const SignalSpec& sig = plot.signalSpecs[s];
                out << "[[panels.signals]]\n";
                QString signalShot = sig.shot.trimmed();
                if (signalShot.isEmpty() && !panelShot.isEmpty() && panelShot != defaultShot) {
                    signalShot = panelShot;
                }
                if (!signalShot.isEmpty() && signalShot != defaultShot) {
                    writeTomlString(out, "shot", signalShot);
                }
                writeTomlStringIfNotEmpty(out, "tree", sig.experiment);
                writeTomlStringIfNotEmpty(out, "server", sig.serverIp);
                writeTomlStringIfNotEmpty(out, "y", sig.yExpr);
                writeTomlStringIfNotEmpty(out, "x", sig.xExpr);
                if (sig.manualColor) {
                    writeTomlString(out, "color", sig.colorName.isEmpty() ? colorForIndex(s) : sig.colorName);
                    writeTomlBool(out, "manual_color", true);
                }
                if (sig.hidden) {
                    writeTomlBool(out, "hidden", true);
                }
                if (sig.fullData) {
                    writeTomlBool(out, "full", true);
                }
                out << '\n';
            }
        }
    }
    return true;
}

void writeLine(QTextStream& out, const QString& key, const QString& value)
{
    out << key << ':' << value << '\n';
}

QString escapedMdsExpr(QString expr)
{
    expr = expr.trimmed();
    expr.replace("\"", "\\\"");
    return expr;
}

QString normalizedMdsSignal(QString expr)
{
    expr = expr.trimmed();
    while (expr.startsWith("\\\\")) {
        expr.remove(0, 1);
    }
    return expr;
}

QString effectiveSignalShot(const PlotSpec& plot, const SignalSpec& sig)
{
    const QString shot = sig.shot.trimmed();
    return shot.isEmpty() ? plot.shot.trimmed() : shot;
}

DataReadMode effectiveSignalReadMode(DataReadMode globalMode, const SignalSpec& sig)
{
    return globalMode == DataReadMode::Full || sig.fullData ? DataReadMode::Full : DataReadMode::Thin;
}
