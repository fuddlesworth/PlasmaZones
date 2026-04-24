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
///
/// Comparisons go through `samePath` (case-insensitive on macOS APFS /
/// NTFS, case-sensitive on Linux) so a hand-edited XDG dir spelt with
/// different case (`~/.Local/Share/...`) still trips the guard on
/// case-insensitive filesystems where it resolves to the same inode as
/// the canonical `$HOME`. Without this a re-cased ancestor would slip
/// past the byte-equal compare and end up watching `$HOME`.
bool samePath(const QString& a, const QString& b)
{
    if (a.isEmpty() || b.isEmpty()) {
        return false;
    }
#if defined(Q_OS_DARWIN) || defined(Q_OS_WIN)
    return a.compare(b, Qt::CaseInsensitive) == 0;
#else
    return a == b;
#endif
}

bool isForbiddenWatchRoot(const QString& path)
{
    const QString cleaned = QDir::cleanPath(path);
    if (cleaned.isEmpty() || samePath(cleaned, QDir::rootPath())) {
        return true;
    }
    const QString home = QDir::cleanPath(QDir::homePath());
    if (samePath(cleaned, home)) {
        return true;
    }
    // Also refuse to watch the GenericDataLocation / ConfigLocation
    // roots themselves — a rescan fires for every unrelated app writing
    // into those shared trees (GTK recently-used files, KDE session
    // state, etc.), which is effectively equivalent to watching $HOME.
    const QString genericData = QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
    if (samePath(cleaned, genericData)) {
        return true;
    }
    const QString configLoc = QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation));
    if (samePath(cleaned, configLoc)) {
        return true;
    }
    return false;
}
} // namespace

DirectoryLoader::DirectoryLoader(IDirectoryLoaderSink& sink, QObject* parent)
    : QObject(parent)
    , m_sink(&sink)
{
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

void DirectoryLoader::setDebounceIntervalForTest(int ms)
{
    m_debounceTimer.setInterval(ms);
}

void DirectoryLoader::setMaxEntriesForTest(int cap)
{
    m_maxEntries = cap;
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
    // Parsed entries accumulated by key. Keyed rather than listed so
    // overwrite on cross-directory user-wins is O(1). The final
    // `freshParsed` list handed to the sink is rebuilt from this hash
    // AFTER the scan loop — the previous implementation did an O(N)
    // linear replace per file, which was O(N²) across a dir full of
    // overrides.
    QHash<QString, ParsedEntry> freshParsedByKey;

    // Entry-count cap breaker: a rescan that hit `m_maxEntries` short-
    // circuits the remaining scan. Logged once (on first trip) so the
    // user sees the cap, not every file it decided to skip.
    bool capTripped = false;

    for (const QString& directory : m_directories) {
        if (capTripped) {
            break;
        }
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
        //
        // Lookup key is case-folded so rsync-from-macOS-APFS
        // (case-insensitive) onto ext4 (case-sensitive) — where
        // `Curve.json` and `curve.json` coexist on ext4 but collide on
        // APFS — warns consistently on both platforms. The ORIGINAL
        // case is still used for the registered entry key (sinks may
        // care about case for display purposes) — only the collision
        // check itself is case-folded.
        QHash<QString, QString> keysInThisDir;

        for (const QString& file : files) {
            // Entry-count DoS guard — paired with the per-file byte cap
            // below. A directory sprayed with tens of thousands of empty
            // `*.json` files would otherwise parse every one of them on
            // the GUI thread on every watcher fire. The cap short-circuits
            // the scan as soon as the tracked-entries count reaches the
            // configured maximum; later files (alphabetically) are
            // silently dropped with one aggregate warning below.
            if (freshParsedByKey.size() >= m_maxEntries) {
                capTripped = true;
                break;
            }

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
            // the same key (case-folded). Filesystem enumeration is
            // alphabetic (we sort above), so the first-seen file wins
            // deterministically. Warn naming BOTH files so the user can
            // actually find the collision without grepping.
            const QString foldedKey = key.toLower();
            if (auto winnerIt = keysInThisDir.constFind(foldedKey); winnerIt != keysInThisDir.constEnd()) {
                qCWarning(lcLoader).nospace()
                    << "Duplicate key '" << key << "' within directory " << directory << " — kept '" << winnerIt.value()
                    << "', ignored '" << fullPath << "' (winner is alphabetically first)";
                continue;
            }
            keysInThisDir.insert(foldedKey, fullPath);

            // Cross-directory: user-wins layering. Record the previous
            // source as the shadowed system path on the new entry, then
            // overwrite. When the user copy is later deleted, the next
            // rescan re-parses the system file fresh (no need to
            // consult `systemSourcePath` at commit time — the field is
            // introspection metadata, not a restore hook).
            if (auto existing = fresh.find(key); existing != fresh.end()) {
                parsed->systemSourcePath = existing->sourcePath;
            }

            Entry trackedEntry;
            trackedEntry.key = key;
            trackedEntry.sourcePath = parsed->sourcePath;
            trackedEntry.systemSourcePath = parsed->systemSourcePath;
            fresh.insert(key, trackedEntry);

            // Keyed insert — later directory wins on cross-dir
            // collision, consistent with `fresh`'s override semantics.
            freshParsedByKey.insert(key, std::move(*parsed));
        }
    }

    if (capTripped) {
        qCWarning(lcLoader).nospace()
            << "DirectoryLoader: reached entry cap (" << m_maxEntries
            << ") — later files skipped to protect the GUI thread. Raise kMaxEntries or prune the watched directories.";
    }

    // Materialise the final parsed-entry list from the hash. Sorting
    // by key gives the sink a stable iteration order across platforms
    // and Qt versions (QHash iteration order is randomised in Qt6).
    QList<ParsedEntry> freshParsed;
    freshParsed.reserve(freshParsedByKey.size());
    for (auto it = freshParsedByKey.cbegin(); it != freshParsedByKey.cend(); ++it) {
        freshParsed.append(it.value());
    }
    std::sort(freshParsed.begin(), freshParsed.end(), [](const ParsedEntry& a, const ParsedEntry& b) {
        return a.key < b.key;
    });

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
        //
        // Resolve symlinks at the bottom of the chain via
        // canonicalPath(). A pathological symlink loop
        // (`/a/b -> /a`) would otherwise make the while loop below
        // climb forever as `next != ancestor` every iteration.
        // canonicalPath() returns empty on unresolvable chains, which
        // the first guard below catches.
        QString ancestor = info.canonicalPath();
        if (ancestor.isEmpty()) {
            // Target path has no canonical form (doesn't exist and
            // neither does any ancestor). Fall back to the textual
            // absolute path — the climb still terminates because we
            // bound it below.
            ancestor = info.absolutePath();
        }
        // Bounded climb — defense in depth against pathological
        // symlink topologies where even canonicalPath() resolution
        // left residual loops (shouldn't happen on Linux / macOS
        // since canonicalPath already collapses symlinks, but
        // cheap insurance). 32 levels is deeper than any sane
        // filesystem layout.
        constexpr int kMaxClimb = 32;
        int climbed = 0;
        while (!ancestor.isEmpty() && !QDir(ancestor).exists()) {
            if (++climbed > kMaxClimb) {
                qCWarning(lcLoader) << "attachWatcherForDir: climb exceeded" << kMaxClimb << "levels for target"
                                    << directory << "— aborting watcher installation";
                return;
            }
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
