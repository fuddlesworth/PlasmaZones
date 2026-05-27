// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorRegistry/Manifest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>

namespace PhosphorRegistry {

namespace {

Manifest invalid(const QString& reason)
{
    Manifest m;
    m.isValid = false;
    m.parseError = reason;
    return m;
}

QString readJsonFile(const QString& path, QString& parseError)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        parseError = QStringLiteral("cannot open manifest: %1").arg(file.errorString());
        return {};
    }
    const QByteArray bytes = file.readAll();
    return QString::fromUtf8(bytes);
}

} // namespace

Manifest Manifest::parse(const QString& manifestJsonPath, const QString& pluginDir)
{
    QString readError;
    const QString text = readJsonFile(manifestJsonPath, readError);
    if (!readError.isEmpty()) {
        return invalid(readError);
    }

    QJsonParseError parseErr{};
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &parseErr);
    if (parseErr.error != QJsonParseError::NoError) {
        return invalid(
            QStringLiteral("malformed JSON at offset %1: %2").arg(parseErr.offset).arg(parseErr.errorString()));
    }
    if (!doc.isObject()) {
        return invalid(QStringLiteral("manifest root is not a JSON object"));
    }

    Manifest m = parseObject(doc.object(), pluginDir);
    m.manifestPath = QFileInfo(manifestJsonPath).absoluteFilePath();
    return m;
}

Manifest Manifest::parseObject(const QJsonObject& obj, const QString& pluginDir)
{
    const QString idField = obj.value(QLatin1String("id")).toString();
    const QString displayNameField = obj.value(QLatin1String("displayName")).toString();
    const int abiField = obj.value(QLatin1String("abi")).toInt(-1);
    const QJsonValue capsField = obj.value(QLatin1String("capabilities"));

    if (idField.isEmpty()) {
        return invalid(QStringLiteral("missing or empty 'id'"));
    }
    if (displayNameField.isEmpty()) {
        return invalid(QStringLiteral("missing or empty 'displayName'"));
    }
    if (abiField < 0) {
        return invalid(QStringLiteral("missing or invalid 'abi'"));
    }
    if (abiField != kPluginAbiVersion) {
        return invalid(QStringLiteral("abi mismatch: manifest=%1 expected=%2").arg(abiField).arg(kPluginAbiVersion));
    }

    // The plugin's directory basename must match the manifest id so
    // the on-disk layout and the registered id stay aligned.
    // Disconnects between the two confuse hot-reload bookkeeping
    // (the watcher fires by directory, the registry keys by id).
    const QString dirBasename = QFileInfo(QDir(pluginDir).absolutePath()).fileName();
    if (!pluginDir.isEmpty() && dirBasename != idField) {
        return invalid(
            QStringLiteral("id '%1' does not match plugin directory basename '%2'").arg(idField, dirBasename));
    }

    QStringList caps;
    if (capsField.isArray()) {
        for (const QJsonValue& v : capsField.toArray()) {
            if (v.isString()) {
                caps.append(v.toString());
            }
        }
    }

    Manifest m;
    m.id = idField;
    m.displayName = displayNameField;
    m.abi = abiField;
    m.capabilities = caps;
    m.isValid = true;
    return m;
}

} // namespace PhosphorRegistry
