// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorJsonLoader/DirectoryLoader.h>

#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QLoggingCategory>
#include <QStandardPaths>

#include <algorithm>

namespace PhosphorJsonLoader {

namespace {
Q_LOGGING_CATEGORY(lcLoader, "phosphorjsonloader.directoryloader")

/// Paths we refuse to install a `QFileSystemWatcher` on. If the "nearest
/// existing ancestor" climb from `attachWatcherForDir` lands on one of
/// these, we decline the watch and wait for the target tree to be created
/// before arming anything. Watching `$HOME` turns every file operation in
/// the user's home directory into a full rescan of all configured dirs;
/// watching `/` is nonsensical.
bool isForbiddenWatchRoot(const QString& path)
{
    const QString cleaned = QDir::cleanPath(path);
    if (cleaned.isEmpty() || cleaned == QDir::rootPath()) {
        return true;
    }
    const QString home = QDir::cleanPath(QDir::homePath());
    if (cleaned == home) {
        return true;
    }
    // Also refuse to watch the GenericDataLocation / ConfigLocation
    // roots themselves — a rescan fires for every unrelated app writing
    // into those shared trees (GTK recently-used files, KDE session
    // state, etc.), which is effectively equivalent to watching $HOME.
    const QString genericData = QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
    if (!genericData.isEmpty() && cleaned == genericData) {
        return true;
    }
    const QString configLoc = QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation));
    if (!configLoc.isEmpty() && cleaned == configLoc) {
        return true;
    }
    return false;
}
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
    QList<Entry> sorted = m_entries.values();
    std::sort(sorted.begin(), sorted.end(), [](const Entry& a, const Entry& b) {
        return a.key < b.key;
    });
    return sorted;
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
        // Sort within each directory so duplicate-name resolution is
        // deterministic across platforms (ext4/btrfs/APFS differ on
        // entryList ordering).
        QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);

        // Track keys already seen within THIS directory so we can warn
        // on intra-directory collisions (across directories is legitimate
        // user-wins-over-system layering, which we handle below).
        // Map value is the absolute path of the first file claiming the
        // key so the warning can name both the winning file and the
        // ignored file — anything less leaves the user hunting for the
        // collision by grep.
        QHash<QString, QString> keysInThisDir;

        for (const QString& file : files) {
            const QString fullPath = dir.absoluteFilePath(file);

            // DoS / foot-gun guard: untrusted same-user files should not
            // be able to stall the GUI thread with a 2 GB blob. Stat
            // first; skip + warn on oversize. Sinks that want a lower
            // cap enforce their own on top of this.
            const QFileInfo fileInfo(fullPath);
            if (fileInfo.size() > kMaxFileBytes) {
                qCWarning(lcLoader) << "Skipping oversized file" << fullPath << "(" << fileInfo.size() << "bytes, cap"
                                    << kMaxFileBytes << ")";
                continue;
            }

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

            // Intra-directory duplicate: two files in the SAME dir with
            // the same key. Filesystem enumeration is alphabetic (we
            // sort above), so the first-seen file wins deterministically.
            // Warn naming BOTH files so the user can actually find the
            // collision without grepping.
            if (auto winnerIt = keysInThisDir.constFind(key); winnerIt != keysInThisDir.constEnd()) {
                qCWarning(lcLoader).nospace()
                    << "Duplicate key '" << key << "' within directory " << directory << " — kept '" << winnerIt.value()
                    << "', ignored '" << fullPath << "' (winner is alphabetically first)";
                continue;
            }
            keysInThisDir.insert(key, fullPath);

            // Cross-directory: user-wins layering. Record the previous
            // source as the shadowed system path on the new entry, then
            // overwrite. The shadowed `ParsedEntry` in `freshParsed` is
            // replaced in-place below. When the user copy is later
            // deleted, the next rescan re-parses the system file fresh
            // (no need to consult `systemSourcePath` at commit time —
            // the field is introspection metadata, not a restore hook).
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

    // Arm per-file watches AFTER commit so the watch set exactly
    // matches the current tracked set. QFileSystemWatcher auto-drops
    // entries on atomic-rename saves (most editors), so we re-sync on
    // every rescan — the add/remove diff makes this cheap.
    if (m_liveReloadEnabled) {
        syncFileWatches();
    }

    Q_EMIT entriesChanged();
}

void DirectoryLoader::syncFileWatches()
{
    if (!m_watcher) {
        return;
    }

    // Desired file-watch set = every currently-tracked source path.
    QSet<QString> desired;
    desired.reserve(m_entries.size());
    for (const auto& entry : std::as_const(m_entries)) {
        desired.insert(entry.sourcePath);
    }

    // Drop watches for files no longer tracked (deleted between rescans).
    for (auto it = m_watchedFiles.begin(); it != m_watchedFiles.end();) {
        if (!desired.contains(*it)) {
            m_watcher->removePath(*it);
            it = m_watchedFiles.erase(it);
        } else {
            ++it;
        }
    }

    // Re-add every desired path. `addPath` no-ops (returns false) if
    // the path is already watched, but after a save-via-atomic-rename
    // Qt silently drops the old inode from the watch set — the user's
    // new inode must be explicitly re-added. Cheapest correct shape is
    // "always try to add; trust QFSW to dedupe the no-op case".
    for (const QString& path : desired) {
        if (m_watcher->addPath(path)) {
            m_watchedFiles.insert(path);
        } else if (!m_watchedFiles.contains(path)) {
            // addPath returned false AND we didn't already have it —
            // means QFSW couldn't watch this file (permissions, inotify
            // quota hit, path vanished between stat and addPath). Log
            // once at debug so it shows up under lcLoader if the user
            // is troubleshooting.
            qCDebug(lcLoader) << "syncFileWatches: failed to add file watch for" << path;
        }
    }
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
        //
        // Crucially: refuse to climb onto `$HOME`, root, or
        // GenericData/ConfigLocation roots. A pristine install where
        // `~/.local/share/plasmazones` does not exist would otherwise
        // end up watching `$HOME`, making every file operation in the
        // user's home trigger a full rescan. If the immediate
        // parent(s) don't exist, we silently skip the watch until the
        // tree materialises — consumers should call `requestRescan()`
        // after creating the directory structure.
        QString ancestor = info.absolutePath();
        while (!ancestor.isEmpty() && !QDir(ancestor).exists()) {
            const QString next = QFileInfo(ancestor).absolutePath();
            if (next == ancestor) {
                break; // reached root
            }
            ancestor = next;
        }
        if (ancestor.isEmpty() || !QDir(ancestor).exists()) {
            return;
        }
        if (isForbiddenWatchRoot(ancestor)) {
            qCDebug(lcLoader) << "Refusing to watch forbidden ancestor" << ancestor << "for target" << directory
                              << "— call requestRescan() after creating the directory tree.";
            return;
        }
        if (!m_watchedParents.contains(ancestor)) {
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
