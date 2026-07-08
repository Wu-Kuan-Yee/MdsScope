// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.hpp"
#include "text_utils.hpp"

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
#ifdef Q_OS_WIN
    material += qgetenv("COMPUTERNAME");
    material += '|';
    material += qgetenv("USERNAME");
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    material += QSysInfo::machineUniqueId();
    material += '|';
    material += QSysInfo::machineHostName().toUtf8();
    material += '|';
    material += qgetenv("USER");
#else
    QFile machineId("/etc/machine-id");
    if (machineId.open(QIODevice::ReadOnly | QIODevice::Text)) {
        material += machineId.readAll().trimmed();
    }
    material += '|';
    material += qgetenv("USER");
#endif
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

