// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorJsonLoader/DirectoryLoader.h>

#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QLoggingCategory>

namespace PhosphorJsonLoader {

namespace {
Q_LOGGING_CATEGORY(lcLoader, "phosphorjsonloader.directoryloader")
} // namespace

DirectoryLoader::DirectoryLoader(IDirectoryLoaderSink* sink, QObject* parent)
    : QObject(parent)
    , m_sink(sink)
{
    Q_ASSERT_X(sink, "DirectoryLoader", "sink must not be null");

    // Single-shot debounce coalesces the save-temp-rename storm most
    // editors produce (QSaveFile / most IDEs fire three events in the
    // same ~10 ms) into one rescan.
    m_debounceTimer.setSingleShot(true);
    m_debounceTimer.setInterval(50);
    connect(&m_debounceTimer, &QTimer::timeout, this, &DirectoryLoader::rescanAll);
}

DirectoryLoader::~DirectoryLoader() = default;

int DirectoryLoader::loadFromDirectory(const QString& directory, LiveReload liveReload)
{
    if (!m_directories.contains(directory)) {
        m_directories.append(directory);
    }
    if (liveReload == LiveReload::On) {
        m_liveReloadEnabled = true;
        installWatcherIfNeeded();
        attachWatcherForDir(directory);
    }

    rescanAll();
    return m_entries.size();
}

int DirectoryLoader::loadFromDirectories(const QStringList& directories, LiveReload liveReload)
{
    // Register all directories first so the rescan sees the complete
    // scan order and applies user-wins collision in one pass (instead
    // of N passes each of which might emit its own batch).
    for (const QString& dir : directories) {
        if (!m_directories.contains(dir)) {
            m_directories.append(dir);
        }
        if (liveReload == LiveReload::On) {
            m_liveReloadEnabled = true;
            installWatcherIfNeeded();
            attachWatcherForDir(dir);
        }
    }

    rescanAll();
    return m_entries.size();
}

void DirectoryLoader::requestRescan()
{
    // Safe to call unconditionally — the single-shot timer coalesces
    // back-to-back calls into one rescanAll() on timeout.
    m_debounceTimer.start();
}

int DirectoryLoader::registeredCount() const
{
    return m_entries.size();
}

QList<DirectoryLoader::Entry> DirectoryLoader::entries() const
{
    return m_entries.values();
}

void DirectoryLoader::rescanAll()
{
    // If live-reload is enabled, the watcher needs a chance to promote
    // any parent-watched target that just materialised. Do this before
    // the scan so a dir created between the last rescan and this one
    // is fully wired.
    if (m_liveReloadEnabled) {
        for (const QString& dir : m_directories) {
            attachWatcherForDir(dir);
        }
    }

    // Build the fresh set in scan order — later directories' entries
    // override earlier on key collision. Track the shadowed source
    // path so deleting the user copy restores the system path on a
    // future rescan (the sink decides what to do with that metadata).
    QHash<QString, Entry> fresh;
    QList<ParsedEntry> freshParsed;

    for (const QString& directory : m_directories) {
        QDir dir(directory);
        if (!dir.exists()) {
            qCDebug(lcLoader) << "rescan: directory does not exist (yet?)" << directory;
            continue;
        }
        const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files);
        for (const QString& file : files) {
            const QString fullPath = dir.absoluteFilePath(file);
            auto parsed = m_sink->parseFile(fullPath);
            if (!parsed) {
                continue;
            }
            const QString key = parsed->key;
            if (key.isEmpty()) {
                qCWarning(lcLoader) << "parseFile returned entry with empty key from" << fullPath
                                    << "— sinks must set ParsedEntry::key";
                continue;
            }

            // user-wins: record the previous source as the shadowed
            // system path on the new entry, then overwrite. The
            // previous parsed entry is dropped (it's still in
            // freshParsed list earlier in the iteration — compact
            // below).
            if (auto existing = fresh.find(key); existing != fresh.end()) {
                parsed->systemSourcePath = existing->sourcePath;
            }

            Entry trackedEntry;
            trackedEntry.key = key;
            trackedEntry.sourcePath = parsed->sourcePath;
            trackedEntry.systemSourcePath = parsed->systemSourcePath;
            fresh.insert(key, trackedEntry);

            // Replace any earlier-scanned parsed entry for the same
            // key (in-place, preserving order is not required — the
            // sink only needs the final set).
            bool replaced = false;
            for (auto& p : freshParsed) {
                if (p.key == key) {
                    p = *parsed;
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                freshParsed.push_back(std::move(*parsed));
            }
        }
    }

    // Diff against the previous tracked set — anything that was
    // registered before but is no longer on disk is a removal. The
    // sink is responsible for unregistering these from the target
    // registry so they don't linger as ghosts after a file delete.
    QStringList removedKeys;
    for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
        if (!fresh.contains(it.key())) {
            removedKeys.append(it.key());
        }
    }

    m_entries = std::move(fresh);

    // Hand the batch to the sink — single call, so the sink can emit
    // one bulk-reload signal on its registry instead of N per-key
    // signals (QFileSystemWatcher delivers batches on multi-file
    // saves; respecting that batching avoids N² re-evaluation cost
    // on every bound consumer).
    m_sink->commitBatch(removedKeys, freshParsed);

    Q_EMIT entriesChanged();
}

void DirectoryLoader::installWatcherIfNeeded()
{
    if (m_watcher) {
        return;
    }
    m_watcher = new QFileSystemWatcher(this);
    // Both signals route through the debounce timer. We don't
    // differentiate file-changed vs directory-changed — the rescan
    // sees the full filesystem state regardless.
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString&) {
        onWatchedPathChanged();
    });
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString&) {
        onWatchedPathChanged();
    });
}

void DirectoryLoader::attachWatcherForDir(const QString& directory)
{
    if (!m_watcher) {
        return;
    }
    const QFileInfo info(directory);
    if (info.exists() && info.isDir()) {
        // Target exists — watch it directly. If we were watching the
        // parent as a proxy, we can stop (direct watch is strictly
        // better and avoids duplicate rescan events when a sibling
        // file elsewhere in the parent changes).
        if (!m_watcher->directories().contains(directory)) {
            m_watcher->addPath(directory);
        }
        const QString parent = info.absolutePath();
        if (m_watchedParents.contains(parent)) {
            // Only remove the parent watch if no OTHER configured
            // directory still relies on it (multiple missing-sibling
            // dirs can share a parent).
            bool parentStillNeeded = false;
            for (const QString& other : m_directories) {
                if (other == directory) {
                    continue;
                }
                const QFileInfo otherInfo(other);
                if (!otherInfo.exists() && otherInfo.absolutePath() == parent) {
                    parentStillNeeded = true;
                    break;
                }
            }
            if (!parentStillNeeded) {
                m_watcher->removePath(parent);
                m_watchedParents.remove(parent);
            }
        }
    } else {
        // Target doesn't exist (yet). Watch the nearest existing
        // ancestor as a proxy — when the ancestor changes (typically
        // because the target dir was just created), we rescan and
        // attachWatcherForDir promotes to a direct watch.
        QString ancestor = info.absolutePath();
        while (!ancestor.isEmpty() && !QDir(ancestor).exists()) {
            const QString next = QFileInfo(ancestor).absolutePath();
            if (next == ancestor) {
                break; // reached root
            }
            ancestor = next;
        }
        if (!ancestor.isEmpty() && QDir(ancestor).exists() && !m_watchedParents.contains(ancestor)) {
            if (!m_watcher->directories().contains(ancestor)) {
                m_watcher->addPath(ancestor);
            }
            m_watchedParents.insert(ancestor);
        }
    }
}

void DirectoryLoader::onWatchedPathChanged()
{
    // Debounce — a single save typically fires 2-3 inotify events in
    // the same ~10 ms window (create temp → rename → fsync). The
    // single-shot timer collapses that into one rescan.
    requestRescan();
}

} // namespace PhosphorJsonLoader
