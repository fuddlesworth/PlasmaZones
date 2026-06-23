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

// Reject id fields that could escape the plugin root via directory
// traversal, or that would later be used as a path segment without
// safety review. The id is used as a registry key only in Phase
// 1.3, but the Phase-5 sandbox plans to derive on-disk caches /
// IPC socket names from it; tightening at the gate keeps every
// downstream consumer safe.
//
// Strictness rationale: a literal ".." substring (e.g. "weather..uk")
// is not a real traversal vector since the loader never
// concatenates ids into paths, but allowing dots-anywhere widens
// the attack surface for future code paths that DO build paths
// from ids. We rule them out here and accept the small cost in
// expressivity — plugin authors picking exotic names can use "_"
// or "-" instead.
bool isSafeId(const QString& id)
{
    if (id.isEmpty()) {
        return false;
    }
    if (id.startsWith(QLatin1Char('.'))) {
        return false; // disallow hidden / parent-relative ids
    }
    if (id.contains(QLatin1Char('/')) || id.contains(QLatin1Char('\\'))) {
        return false; // no path separators
    }
    if (id.contains(QLatin1String(".."))) {
        return false; // no traversal sequences anywhere in the name
    }
    return true;
}

// Read the manifest.json bytes from disk. Returns raw UTF-8 bytes
// rather than a QString — passing the QByteArray directly to
// QJsonDocument::fromJson preserves byte-level diagnostics for
// invalid encodings (a QString round-trip would substitute U+FFFD
// for malformed UTF-8 and the parser would then see the substituted
// characters instead of failing cleanly).
//
// Failure modes (parseError populated, returns {}):
//   - open() rejected (file missing, permissions, ...)
//   - size exceeds ManifestMaxBytes
//   - file is empty (zero bytes) — explicitly flagged so the caller
//     doesn't see a confusing "malformed JSON at offset 0" later
QByteArray readJsonFile(const QString& path, QString& parseError)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        parseError = QStringLiteral("cannot open manifest: %1").arg(file.errorString());
        return {};
    }
    const qint64 size = file.size();
    if (size > ManifestMaxBytes) {
        parseError = QStringLiteral("manifest exceeds %1-byte cap (was %2)").arg(ManifestMaxBytes).arg(size);
        return {};
    }
    if (size == 0) {
        parseError = QStringLiteral("manifest is empty");
        return {};
    }
    return file.readAll();
}

} // namespace

Manifest Manifest::parse(const QString& manifestJsonPath, const QString& pluginDir)
{
    QString readError;
    const QByteArray bytes = readJsonFile(manifestJsonPath, readError);
    if (!readError.isEmpty()) {
        return invalid(readError);
    }

    QJsonParseError parseErr{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseErr);
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
    const QJsonValue abiValue = obj.value(QLatin1String("abi"));
    const QJsonValue capsField = obj.value(QLatin1String("capabilities"));

    if (idField.isEmpty()) {
        return invalid(QStringLiteral("missing or empty 'id'"));
    }
    if (!isSafeId(idField)) {
        return invalid(
            QStringLiteral("'id' contains unsafe characters (path separators, traversal, or hidden prefix): '%1'")
                .arg(idField));
    }
    if (displayNameField.isEmpty()) {
        return invalid(QStringLiteral("missing or empty 'displayName'"));
    }
    if (abiValue.isUndefined() || abiValue.isNull()) {
        return invalid(QStringLiteral("missing 'abi' field"));
    }
    if (!abiValue.isDouble()) {
        // QJsonValue::toInt accepts double-shaped values only; a
        // string/object/array shaped abi is a manifest authoring
        // error distinct from "missing." Diagnose it separately so
        // plugin authors get an actionable message.
        return invalid(QStringLiteral("'abi' must be an integer, got non-numeric type"));
    }
    const int abiField = abiValue.toInt(-1);
    if (abiField != PluginAbiVersion) {
        return invalid(QStringLiteral("abi mismatch: manifest=%1 expected=%2").arg(abiField).arg(PluginAbiVersion));
    }

    // The plugin's directory basename must match the manifest id so
    // the on-disk layout and the registered id stay aligned.
    // Disconnects between the two confuse hot-reload bookkeeping
    // (the watcher fires by directory, the registry keys by id).
    // An empty pluginDir skips this check — that's the in-memory
    // parseObject test seam (test_manifest.cpp exercises every
    // rejection path without staging real directories).
    if (!pluginDir.isEmpty()) {
        const QString dirBasename = QFileInfo(QDir(pluginDir).absolutePath()).fileName();
        if (dirBasename != idField) {
            return invalid(
                QStringLiteral("id '%1' does not match plugin directory basename '%2'").arg(idField, dirBasename));
        }
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
