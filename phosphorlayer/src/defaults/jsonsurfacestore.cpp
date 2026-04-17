// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/defaults/JsonSurfaceStore.h>

#include "../internal.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>

namespace PhosphorLayer {

namespace {

/**
 * True if @p path is safe to use as a surface-store target. Rejects:
 *   - empty paths
 *   - paths containing any literal ".." segment, even if the path would
 *     cleanPath-collapse to a harmless-looking string — the author wrote
 *     `..`, so they intended traversal
 *   - paths whose existing target is a symlink
 *   - paths whose direct parent directory is a symlink (prevents a
 *     malicious symlink from redirecting writes to e.g. ~/.bashrc)
 *
 * A non-existent target whose parent is a real directory is fine — that
 * is the "first run" case. Deeper symlinks in the ancestor chain are
 * accepted because they are legitimate on systems where home dirs live
 * under symlinked mount points (e.g. `/home` → `/var/home` on Silverblue).
 */
bool isSafeStorePath(const QString& path)
{
    if (path.isEmpty()) {
        return false;
    }
    // Split on '/' and reject any literal `..` segment. cleanPath collapses
    // `foo/bar/../..` to `.` so a naive `contains("..")` check on the
    // normalised output misses it; looking at raw segments catches every
    // escape regardless of collapsing.
    const auto segments = path.split(QLatin1Char('/'));
    for (const auto& seg : segments) {
        if (seg == QLatin1String("..")) {
            return false;
        }
    }
    const QFileInfo info(path);
    if (info.isSymLink()) {
        return false;
    }
    const QFileInfo parent(info.dir().absolutePath());
    if (parent.exists() && parent.isSymLink()) {
        return false;
    }
    return true;
}

} // namespace

class JsonSurfaceStore::Impl
{
public:
    explicit Impl(QString path)
        : m_path(std::move(path))
        , m_pathSafe(isSafeStorePath(m_path))
    {
        if (!m_pathSafe) {
            qCWarning(lcPhosphorLayer) << "JsonSurfaceStore: refusing unsafe path" << m_path
                                       << "(traversal segment or symlink in path/parent) — "
                                       << "store will behave as in-memory-only";
            m_root = {};
            return;
        }
        loadFromDisk();
    }

    void loadFromDisk()
    {
        QFile file(m_path);
        if (!file.exists()) {
            m_root = {};
            return;
        }
        if (!file.open(QIODevice::ReadOnly)) {
            qCWarning(lcPhosphorLayer) << "JsonSurfaceStore: cannot read" << m_path << ":" << file.errorString();
            m_root = {};
            return;
        }
        QJsonParseError err;
        const auto doc = QJsonDocument::fromJson(file.readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            qCWarning(lcPhosphorLayer) << "JsonSurfaceStore: parse error in" << m_path << ":" << err.errorString()
                                       << "— renaming corrupt file and starting empty";
            // Preserve forensic evidence — next save() would otherwise
            // overwrite the corrupt file with a fresh empty one and destroy
            // whatever state the user had. QFile::rename() overwrites on
            // Windows but not POSIX; an existing .corrupt is acceptable
            // collateral either way because the current state is already
            // unreadable.
            file.close();
            const QString bak = m_path + QStringLiteral(".corrupt");
            QFile::remove(bak);
            if (!QFile::rename(m_path, bak)) {
                qCWarning(lcPhosphorLayer) << "JsonSurfaceStore: could not rename corrupt file to" << bak;
            }
            m_root = {};
            return;
        }
        m_root = doc.object();
    }

    bool flushToDisk()
    {
        if (!m_pathSafe) {
            return false;
        }
        QFileInfo(m_path).dir().mkpath(QStringLiteral("."));
        QSaveFile file(m_path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qCWarning(lcPhosphorLayer) << "JsonSurfaceStore: cannot write" << m_path << ":" << file.errorString();
            return false;
        }
        const auto data = QJsonDocument(m_root).toJson(QJsonDocument::Indented);
        if (file.write(data) != data.size()) {
            qCWarning(lcPhosphorLayer) << "JsonSurfaceStore: short write to" << m_path;
            return false;
        }
        if (!file.commit()) {
            qCWarning(lcPhosphorLayer) << "JsonSurfaceStore: commit failed for" << m_path << ":" << file.errorString();
            return false;
        }
        // 0600 — these state files can hold sensitive window layout data
        // and should not be world- or group-readable. Failure here is
        // non-fatal; the payload is written regardless.
        QFile::setPermissions(m_path, QFile::ReadOwner | QFile::WriteOwner);
        return true;
    }

    QString m_path;
    bool m_pathSafe;
    QJsonObject m_root;
};

JsonSurfaceStore::JsonSurfaceStore(QString filePath)
    : m_impl(std::make_unique<Impl>(std::move(filePath)))
{
}

JsonSurfaceStore::~JsonSurfaceStore() = default;

QString JsonSurfaceStore::filePath() const
{
    return m_impl->m_path;
}

bool JsonSurfaceStore::save(const QString& key, const QJsonObject& data)
{
    // Refuse unsafe-path stores before touching in-memory state. If we updated
    // m_root and then flushToDisk refused, a subsequent has()/load() would
    // report state that was never persisted — surprising to callers who only
    // observe save()'s bool. Fail cleanly: nothing in memory, nothing on disk.
    if (!m_impl->m_pathSafe) {
        return false;
    }
    m_impl->m_root.insert(key, data);
    return m_impl->flushToDisk();
}

QJsonObject JsonSurfaceStore::load(const QString& key) const
{
    const auto v = m_impl->m_root.value(key);
    return v.isObject() ? v.toObject() : QJsonObject();
}

bool JsonSurfaceStore::has(const QString& key) const
{
    return m_impl->m_root.contains(key);
}

void JsonSurfaceStore::remove(const QString& key)
{
    if (!m_impl->m_root.contains(key)) {
        return;
    }
    m_impl->m_root.remove(key);
    m_impl->flushToDisk();
}

} // namespace PhosphorLayer
