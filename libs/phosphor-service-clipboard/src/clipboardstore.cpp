// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "clipboardstore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

#include <utility>

namespace PhosphorServiceClipboard {

namespace {
QString blobHash(const QByteArray& content)
{
    return QString::fromLatin1(QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex());
}

bool atomicWrite(const QString& path, const QByteArray& bytes)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}
} // namespace

ClipboardStore::ClipboardStore(QString directory)
    : m_directory(std::move(directory))
{
}

QString ClipboardStore::directory() const
{
    return m_directory;
}

QString ClipboardStore::defaultDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath(QStringLiteral("phosphor-clipboard"));
}

QString ClipboardStore::indexPath() const
{
    return QDir(m_directory).filePath(QStringLiteral("index.json"));
}

QString ClipboardStore::blobsDir() const
{
    return QDir(m_directory).filePath(QStringLiteral("blobs"));
}

QList<ClipboardEntry> ClipboardStore::load() const
{
    QList<ClipboardEntry> entries;

    QFile index(indexPath());
    if (!index.open(QIODevice::ReadOnly))
        return entries; // first run or unreadable: empty history.

    const QJsonDocument doc = QJsonDocument::fromJson(index.readAll());
    if (!doc.isArray())
        return entries;

    const QDir blobs(blobsDir());
    const QJsonArray array = doc.array();
    entries.reserve(array.size());
    for (const QJsonValue& value : array) {
        const QJsonObject obj = value.toObject();
        const QString hash = obj.value(QLatin1String("blob")).toString();
        if (hash.isEmpty())
            continue;
        QFile blob(blobs.filePath(hash));
        if (!blob.open(QIODevice::ReadOnly))
            continue; // a missing blob means a corrupt entry; skip it.

        ClipboardEntry entry;
        entry.content = blob.readAll();
        entry.mimeType = obj.value(QLatin1String("mime")).toString();
        const QJsonArray offered = obj.value(QLatin1String("offered")).toArray();
        for (const QJsonValue& type : offered)
            entry.offeredTypes.append(type.toString());
        entry.preview = obj.value(QLatin1String("preview")).toString();
        entry.timestamp =
            QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(obj.value(QLatin1String("timestamp")).toDouble()));
        entry.sensitive = false; // sensitive entries are never persisted.
        entries.append(entry);
    }
    return entries;
}

bool ClipboardStore::save(const QList<ClipboardEntry>& entries) const
{
    QDir dir(m_directory);
    if (!dir.mkpath(QStringLiteral(".")) || !dir.mkpath(QStringLiteral("blobs")))
        return false;

    const QDir blobs(blobsDir());

    QJsonArray array;
    QSet<QString> liveHashes;
    for (const ClipboardEntry& entry : entries) {
        if (entry.sensitive)
            continue; // defence in depth: secrets never hit the disk.
        const QString hash = blobHash(entry.content);
        liveHashes.insert(hash);

        const QString blobPath = blobs.filePath(hash);
        if (!QFile::exists(blobPath)) {
            if (!atomicWrite(blobPath, entry.content))
                return false;
        }

        QJsonObject obj;
        obj.insert(QLatin1String("mime"), entry.mimeType);
        obj.insert(QLatin1String("offered"), QJsonArray::fromStringList(entry.offeredTypes));
        obj.insert(QLatin1String("preview"), entry.preview);
        obj.insert(QLatin1String("timestamp"), static_cast<double>(entry.timestamp.toMSecsSinceEpoch()));
        obj.insert(QLatin1String("blob"), hash);
        array.append(obj);
    }

    if (!atomicWrite(indexPath(), QJsonDocument(array).toJson(QJsonDocument::Compact)))
        return false;

    // Prune blobs no longer referenced by the current index.
    const QStringList existing = blobs.entryList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QString& name : existing) {
        if (!liveHashes.contains(name))
            QFile::remove(blobs.filePath(name));
    }

    return true;
}

} // namespace PhosphorServiceClipboard
