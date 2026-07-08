// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mds_ip_client.hpp"

namespace mds_client_internal {

using Message = MdsIpClient::Message;

QByteArray MdsIpClient::message(qint8 dtype, qint8 nargs, qint8 descrIdx, quint8 messageId, const QByteArray& body)
{
        QByteArray packet;
        QDataStream out(&packet, QIODevice::WriteOnly);
        out.setByteOrder(QDataStream::BigEndian);
        out << qint32(48 + body.size());
        out << qint32(0);
        out << qint16(body.size());
        out << nargs;
        out << descrIdx;
        out << qint8(messageId);
        out << dtype;
        out << qint8(0xC3);
        out << qint8(0);
        for (int i = 0; i < 8; ++i) {
            out << qint32(0);
        }
        packet.append(body);
        return packet;
    }

bool MdsIpClient::writeMessage(QTcpSocket& socket, const QByteArray& packet, QString* error)
{
        socket.write(packet);
        QElapsedTimer timer;
        timer.start();
        while (socket.bytesToWrite() > 0) {
            if (currentCanceled()) {
                socket.abort();
                *error = "operation canceled";
                return false;
            }
            if (!socket.waitForBytesWritten(50)) {
                if (timer.elapsed() >= kNetworkTimeoutMs) {
                    *error = socket.errorString();
                    return false;
                }
            }
        }
        return true;
    }

bool MdsIpClient::readFully(QTcpSocket& socket, QByteArray* out, qsizetype size, QString* error)
{
        QElapsedTimer timer;
        timer.start();
        while (out->size() < size) {
            if (currentCanceled()) {
                socket.abort();
                *error = "operation canceled";
                return false;
            }
            if (socket.bytesAvailable() <= 0) {
                if (!socket.waitForReadyRead(50)) {
                    if (timer.elapsed() >= kNetworkTimeoutMs) {
                        *error = socket.errorString();
                        return false;
                    }
                    continue;
                }
            }
            out->append(socket.read(size - out->size()));
        }
        return true;
    }

Message MdsIpClient::readMessage(QTcpSocket& socket, QString* error)
{
        error->clear();
        QByteArray header;
        Message msg;
        if (!readFully(socket, &header, 48, error)) {
            return msg;
        }
        QDataStream in(header);
        in.setByteOrder(QDataStream::BigEndian);
        qint32 msgLen = 0;
        qint8 nargs = 0;
        qint8 descrIdx = 0;
        qint8 messageId = 0;
        qint8 clientType = 0;
        qint8 ndims = 0;
        in >> msgLen >> msg.status >> msg.length >> nargs >> descrIdx >> messageId >> msg.dtype >> clientType >> ndims;
        if (msgLen < 48 || msgLen > 128 * 1024 * 1024) {
            *error = "invalid MDSIP message length";
            return msg;
        }
        if (!readFully(socket, &msg.body, msgLen - 48, error)) {
            msg.body.clear();
        }
        return msg;
    }

bool MdsIpClient::handshake(QTcpSocket& socket, QString* error)
{
        const QByteArray user("JAVA_USER");
        if (!writeMessage(socket, message(14, 1, 0, 1, user), error)) {
            return false;
        }
        readMessage(socket, error);
        return error->isEmpty();
    }

quint8 MdsIpClient::nextMessageId()
{
        thread_local quint8 messageId = 2;
        const quint8 currentMessageId = messageId;
        if (++messageId == 0) {
            messageId = 1;
        }
        return currentMessageId;
    }

bool MdsIpClient::sendValue(QTcpSocket& socket, const QString& expr, QString* error)
{
        error->clear();
        const QByteArray body = expr.toUtf8();
        return writeMessage(socket, message(14, 1, 0, nextMessageId(), body), error);
    }

bool MdsIpClient::queueValue(QTcpSocket& socket, const QString& expr, QString* error)
{
        error->clear();
        const QByteArray body = expr.toUtf8();
        const QByteArray packet = message(14, 1, 0, nextMessageId(), body);
        if (socket.write(packet) != packet.size()) {
            *error = socket.errorString();
            return false;
        }
        return true;
    }

bool MdsIpClient::flushQueuedValues(QTcpSocket& socket, QString* error)
{
        QElapsedTimer timer;
        timer.start();
        while (socket.bytesToWrite() > 0) {
            if (currentCanceled()) {
                socket.abort();
                *error = "operation canceled";
                return false;
            }
            if (!socket.waitForBytesWritten(50)) {
                if (timer.elapsed() >= kNetworkTimeoutMs) {
                    *error = socket.errorString();
                    return false;
                }
            }
        }
        return true;
    }

Message MdsIpClient::value(QTcpSocket& socket, const QString& expr, QString* error)
{
        error->clear();
        const QByteArray body = expr.toUtf8();
        const quint8 currentMessageId = nextMessageId();
        if (!writeMessage(socket, message(14, 1, 0, currentMessageId, body), error)) {
            return {};
        }
        return readMessage(socket, error);
    }

QVector<double> MdsIpClient::numericFromMessage(const Message& msg, QString* error)
{
        if (!error->isEmpty() || msg.body.isEmpty()) {
            return {};
        }
        if (msg.dtype == 14) {
            *error = QString::fromUtf8(msg.body).trimmed();
            return {};
        }
        QVector<double> values;
        QDataStream in(msg.body);
        in.setByteOrder(QDataStream::BigEndian);

        if (msg.dtype == 11 || msg.dtype == 53) {
            in.setFloatingPointPrecision(QDataStream::DoublePrecision);
            values.reserve(msg.body.size() / 8);
            while (!in.atEnd()) {
                double v = 0.0;
                in >> v;
                if (std::isfinite(v)) {
                    values.push_back(v);
                }
            }
            return values;
        }

        if (msg.dtype == 10 || msg.dtype == 52 || msg.body.size() % 4 == 0) {
            in.setFloatingPointPrecision(QDataStream::SinglePrecision);
            values.reserve(msg.body.size() / 4);
            while (!in.atEnd()) {
                float v = 0.0f;
                in >> v;
                if (std::isfinite(v)) {
                    values.push_back(v);
                }
            }
        }
        return values;
    }

QVector<double> MdsIpClient::numericValue(QTcpSocket& socket, const QString& expr, QString* error)
{
        return numericFromMessage(value(socket, expr, error), error);
    }

QVector<double> MdsIpClient::cachedNumericValue(QTcpSocket& socket,
                                          const QString& expr,
                                          QHash<QString, QVector<double>>* cache,
                                          QString* error)
{
        if (!cache) {
            return numericValue(socket, expr, error);
        }
        if (cache->contains(expr)) {
            if (error) {
                error->clear();
            }
            return cache->value(expr);
        }
        QVector<double> values = numericValue(socket, expr, error);
        if (!values.isEmpty()) {
            cache->insert(expr, values);
        }
        return values;
    }

int MdsIpClient::intValue(QTcpSocket& socket, const QString& expr, QString* error)
{
        const Message msg = value(socket, expr, error);
        if (!error->isEmpty() || msg.body.isEmpty()) {
            return 0;
        }
        if (msg.dtype == 14) {
            *error = QString::fromUtf8(msg.body).trimmed();
            return 0;
        }

        QDataStream in(msg.body);
        in.setByteOrder(QDataStream::BigEndian);
        if ((msg.dtype == 11 || msg.dtype == 53) && msg.body.size() >= 8) {
            in.setFloatingPointPrecision(QDataStream::DoublePrecision);
            double v = 0.0;
            in >> v;
            return std::isfinite(v) && v > 0.0 && v <= std::numeric_limits<int>::max()
                       ? static_cast<int>(std::llround(v))
                       : 0;
        }
        if ((msg.dtype == 10 || msg.dtype == 52) && msg.body.size() >= 4) {
            in.setFloatingPointPrecision(QDataStream::SinglePrecision);
            float v = 0.0f;
            in >> v;
            return std::isfinite(v) && v > 0.0f && v <= static_cast<float>(std::numeric_limits<int>::max())
                       ? static_cast<int>(std::lround(v))
                       : 0;
        }
        if (msg.body.size() == 8 && msg.dtype != 11 && msg.dtype != 53) {
            qint64 v = 0;
            in >> v;
            return v > 0 && v <= std::numeric_limits<int>::max() ? static_cast<int>(v) : 0;
        }
        if (msg.body.size() >= 4) {
            qint32 v = 0;
            in >> v;
            return v > 0 ? v : 0;
        }
        if (msg.body.size() >= 2) {
            qint16 v = 0;
            in >> v;
            return v > 0 ? v : 0;
        }
        if (msg.body.size() == 1) {
            return static_cast<quint8>(msg.body[0]);
        }
        return 0;
    }

QVector<int> MdsIpClient::intVectorValue(QTcpSocket& socket, const QString& expr, QString* error)
{
        const Message msg = value(socket, expr, error);
        if (!error->isEmpty() || msg.body.isEmpty()) {
            return {};
        }
        if (msg.dtype == 14) {
            *error = QString::fromUtf8(msg.body).trimmed();
            return {};
        }

        QVector<int> values;
        QDataStream in(msg.body);
        in.setByteOrder(QDataStream::BigEndian);
        if (msg.body.size() % 4 == 0) {
            values.reserve(msg.body.size() / 4);
            while (!in.atEnd()) {
                qint32 v = 0;
                in >> v;
                values.push_back(v > 0 ? v : 0);
            }
            return values;
        }
        if (msg.body.size() % 2 == 0) {
            values.reserve(msg.body.size() / 2);
            while (!in.atEnd()) {
                qint16 v = 0;
                in >> v;
                values.push_back(v > 0 ? v : 0);
            }
            return values;
        }
        values.reserve(msg.body.size());
        for (char byte : msg.body) {
            values.push_back(static_cast<quint8>(byte));
        }
        return values;
    }
} // namespace mds_client_internal
