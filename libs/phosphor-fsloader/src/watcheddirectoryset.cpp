// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorFsLoader/IScanStrategy.h>
#include <PhosphorFsLoader/WatchedDirectorySet.h>

#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QThread>

namespace PhosphorFsLoader {

namespace {
Q_LOGGING_CATEGORY(lcWatcher, "phosphorfsloader.watcheddirset")

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
    if (cleaned.isEmpty()) {
        return true;
    }
    // Resolve symlinks. A target spelled `~/sym → $HOME` (or
    // `/tmp/foo → ~/.cache`) cleans to itself but its inode is the
    // forbidden root — without canonicalisation, QFileSystemWatcher
    // would follow the symlink at the OS layer and watch the forbidden
    // inode anyway. Empty when the path doesn't exist (canonical
    // resolution requires every chain segment to exist) — the input's
    // cleaned form remains the only check in that case.
    const QString canonical = QFileInfo(path).canonicalFilePath();

    // Compare a candidate forbidden root against `cleaned` AND `canonical`,
    // and also against the candidate's canonical form so that two
    // independent symlinks pointing at the same inode collapse onto the
    // same forbidden-root verdict.
    const auto matches = [&](const QString& candidate) {
        if (candidate.isEmpty()) {
            return false;
        }
        const QString cleanedCandidate = QDir::cleanPath(candidate);
        if (samePath(cleaned, cleanedCandidate)) {
            return true;
        }
        if (!canonical.isEmpty() && samePath(canonical, cleanedCandidate)) {
            return true;
        }
        const QString canonicalCandidate = QFileInfo(candidate).canonicalFilePath();
        if (!canonicalCandidate.isEmpty()) {
            if (samePath(cleaned, canonicalCandidate)) {
                return true;
            }
            if (!canonical.isEmpty() && samePath(canonical, canonicalCandidate)) {
                return true;
            }
        }
        return false;
    };

    if (matches(QDir::rootPath()) || matches(QDir::homePath())) {
        return true;
    }
    // Also refuse to watch high-churn shared roots — a rescan fires for every
    // unrelated app writing into those trees, effectively equivalent to
    // watching $HOME. GenericData/Config catch the obvious XDG roots; Temp,
    // Runtime, and Cache catch consumers whose target lives under
    // /tmp, /run/user/UID, or ~/.cache and where the climb would otherwise
    // stop at the high-traffic root.
    using QSP = QStandardPaths;
    static constexpr QSP::StandardLocation kForbidden[] = {
        QSP::GenericDataLocation, QSP::GenericConfigLocation, QSP::TempLocation,
        QSP::RuntimeLocation,     QSP::CacheLocation,         QSP::GenericCacheLocation,
    };
    for (const auto loc : kForbidden) {
        if (matches(QSP::writableLocation(loc))) {
            return true;
        }
    }
    return false;
}
} // namespace

WatchedDirectorySet::WatchedDirectorySet(IScanStrategy& strategy, QObject* parent)
    : QObject(parent)
    , m_strategy(&strategy)
{
    // Single-shot debounce coalesces the save-temp-rename storm most
    // editors produce (QSaveFile / most IDEs fire three events in the
    // same ~10 ms) into one rescan.
    m_debounceTimer.setSingleShot(true);
    m_debounceTimer.setInterval(50);
    connect(&m_debounceTimer, &QTimer::timeout, this, &WatchedDirectorySet::rescanAll);
}

WatchedDirectorySet::~WatchedDirectorySet() = default;

int WatchedDirectorySet::registerDirectory(const QString& directory, LiveReload liveReload)
{
    return registerDirectories(QStringList{directory}, liveReload);
}

int WatchedDirectorySet::registerDirectories(const QStringList& directories, LiveReload liveReload)
{
    Q_ASSERT_X(thread() == QThread::currentThread(), "WatchedDirectorySet::registerDirectories",
               "GUI-thread only — see class docs");
    // Empty input is a no-op — skip the rescan entirely. Callers that
    // want to force a rescan call `requestRescan()` / `rescanNow()`
    // directly; routing them here would just be an alias.
    if (directories.isEmpty()) {
        return m_directories.size();
    }

    // Register all directories first so the rescan sees the complete
    // scan order in one pass.
    //
    // Canonicalise on insertion so two registrations of the same
    // directory under different spellings (`/foo/bar/` vs `/foo/bar`)
    // collapse to one entry.
    for (const QString& dir : directories) {
        const QString canonical = QDir::cleanPath(dir);
        if (!m_directories.contains(canonical)) {
            m_directories.append(canonical);
        }
        if (liveReload == LiveReload::On) {
            m_liveReloadEnabled = true;
            installWatcherIfNeeded();
            attachWatcherForDir(canonical);
        }
    }

    rescanAll();
    return m_directories.size();
}

void WatchedDirectorySet::rescanNow()
{
    Q_ASSERT_X(thread() == QThread::currentThread(), "WatchedDirectorySet::rescanNow",
               "GUI-thread only — see class docs");
    // Cancel any pending debounce — its rescan would just re-do work
    // we're about to perform synchronously below.
    m_debounceTimer.stop();
    rescanAll();
}

void WatchedDirectorySet::requestRescan()
{
    Q_ASSERT_X(thread() == QThread::currentThread(), "WatchedDirectorySet::requestRescan",
               "GUI-thread only — see class docs");
    // A rescan may already be running on this thread (strategy's
    // commit step or a nested slot re-entered us via a signal). Starting
    // the debounce timer here would be a no-op — the timer is idle while
    // rescanAll is executing — and the event that triggered this call
    // would be silently dropped because the rescan has already captured
    // the pre-event filesystem state. Defer to the end of rescanAll,
    // which checks the flag and re-arms the timer for a follow-up scan.
    if (m_rescanInProgress) {
        m_rescanRequestedWhileRunning = true;
        return;
    }
    // Safe to call unconditionally — the single-shot timer coalesces
    // back-to-back calls into one rescanAll() on timeout.
    m_debounceTimer.start();
}

QStringList WatchedDirectorySet::directories() const
{
    return m_directories;
}

void WatchedDirectorySet::setDebounceIntervalForTest(int ms)
{
    m_debounceTimer.setInterval(ms);
}

QString WatchedDirectorySet::watchedAncestorForTest(const QString& target) const
{
    // Canonicalise the lookup key to match the insertion key used by
    // `attachWatcherForDir`, otherwise trailing-slash differences
    // between the caller's test path and the internal form would cause
    // the accessor to spuriously return an empty string.
    return m_parentWatchFor.value(QDir::cleanPath(target));
}

bool WatchedDirectorySet::hasParentWatchForTest(const QString& path) const
{
    return m_watchedParents.contains(QDir::cleanPath(path));
}

void WatchedDirectorySet::rescanAll()
{
    // Race guard — any `requestRescan` call delivered during this scan
    // (e.g. from within the strategy's commit step, or from a watcher
    // event that fires while we're iterating) is captured in
    // `m_rescanRequestedWhileRunning` and replayed at the end of this
    // function. Without it, the single-shot debounce timer is idle
    // while this slot runs, so any `m_debounceTimer.start()` call from
    // inside the nested stack is a no-op and the observed state can go
    // stale until the next external event.
    m_rescanInProgress = true;
    m_rescanRequestedWhileRunning = false;

    // If live-reload is enabled, the watcher needs a chance to promote
    // any parent-watched target that just materialised. Do this before
    // the strategy runs so a dir created between the last rescan and
    // this one is fully wired.
    if (m_liveReloadEnabled) {
        for (const QString& dir : m_directories) {
            attachWatcherForDir(dir);
        }
    }

    // Hand off to the strategy. It enumerates, parses, commits — and
    // returns the absolute paths we should install per-path watches on
    // after this rescan.
    const QStringList desiredFileWatches = m_strategy->performScan(m_directories);

    // Arm per-file watches AFTER commit so the watch set exactly
    // matches the strategy's view of "currently relevant" paths.
    // QFileSystemWatcher auto-drops entries on atomic-rename saves
    // (most editors), so we re-sync on every rescan — the add/remove
    // diff makes this cheap.
    if (m_liveReloadEnabled) {
        syncFileWatches(desiredFileWatches);
    }

    Q_EMIT rescanCompleted();

    // Race-guard replay. `Q_EMIT` is a synchronous call: every
    // `Qt::DirectConnection` slot wired to `rescanCompleted` runs
    // before the line below clears `m_rescanInProgress`, so any
    // `requestRescan()` call from those slots still takes the guarded
    // path (sets `m_rescanRequestedWhileRunning`). Slots wired with
    // `Qt::QueuedConnection` run later, after the flag is cleared, and
    // will arm the debounce timer normally — that's also fine.
    //
    // We clear `m_rescanInProgress` BEFORE re-arming the timer so the
    // single-shot timer's eventual `rescanAll` slot starts from a clean
    // state, rather than seeing a stale "in progress" flag from the
    // outer scan it's chained off.
    m_rescanInProgress = false;
    if (m_rescanRequestedWhileRunning) {
        m_rescanRequestedWhileRunning = false;
        m_debounceTimer.start();
    }
}

void WatchedDirectorySet::syncFileWatches(const QStringList& desiredPaths)
{
    if (!m_watcher) {
        return;
    }

    QSet<QString> desired;
    desired.reserve(desiredPaths.size());
    for (const QString& path : desiredPaths) {
        desired.insert(path);
    }

    // Drop watches for paths no longer relevant (deleted between rescans,
    // or no longer reported by the strategy).
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
    const QStringList alreadyWatchedDirs = m_watcher->directories();
    const QStringList alreadyWatchedFiles = m_watcher->files();
    for (const QString& path : desired) {
        if (m_watcher->addPath(path)) {
            m_watchedFiles.insert(path);
        } else if (!m_watchedFiles.contains(path)) {
            // addPath returned false AND we didn't already have it in
            // OUR file-watch set. Two cases:
            //
            //   1) Qt is already watching this path because the strategy
            //      reported one of the registered roots that
            //      `attachWatcherForDir` already added — silent dedupe
            //      (strategies are *advised* not to do this, but this
            //      keeps a misbehaving strategy from producing a
            //      misleading log spam every rescan).
            //   2) Genuinely unwatchable — permissions, OS-specific
            //      watch quota hit (Linux: inotify max_user_watches;
            //      macOS: kqueue file-descriptor limits; Windows:
            //      ReadDirectoryChangesW handle exhaustion), or the
            //      path vanished between stat and addPath. Warn —
            //      hot-reload silently degrades to "doesn't work" and
            //      users troubleshooting need this visible at default
            //      log levels.
            if (alreadyWatchedDirs.contains(path) || alreadyWatchedFiles.contains(path)) {
                continue; // case 1 — silent dedupe
            }
            qCWarning(lcWatcher) << "syncFileWatches: failed to add watch for" << path
                                 << "— check permissions or filesystem-watcher quota (inotify on Linux, "
                                    "kqueue/FSEvents on macOS, ReadDirectoryChangesW on Windows)";
        }
    }
}

void WatchedDirectorySet::installWatcherIfNeeded()
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

void WatchedDirectorySet::attachWatcherForDir(const QString& directory)
{
    if (!m_watcher) {
        return;
    }
    // Canonicalise the target key: `m_parentWatchFor` is keyed by the
    // target's canonical-path form so symlinked / trailing-slash
    // duplicates (e.g. the same dir registered as both
    // `/a/b/profiles/` and `/a/b/profiles`) hash to the same bucket.
    const QString targetKey = QDir::cleanPath(directory);
    // Refuse to watch the registered target itself if it resolves to a
    // forbidden root (e.g. `$HOME`, `/`, an XDG_*Location root). The
    // climb-onto-ancestor branch below has its own guard — duplicating
    // it here means a misconfigured caller passing `$HOME` directly
    // gets the same protection as one whose target's parent climb would
    // land there. A misconfiguration here is silent-but-warned rather
    // than a daemon-killing flood of rescan events.
    if (isForbiddenWatchRoot(targetKey)) {
        qCWarning(lcWatcher) << "Refusing to watch forbidden root" << directory
                             << "— registered target resolves to $HOME, /, or an XDG data/config root";
        return;
    }
    const QFileInfo info(directory);
    if (info.exists() && info.isDir()) {
        // Target exists — watch it directly. If we were watching an
        // ancestor as a proxy, we can stop (direct watch is strictly
        // better and avoids duplicate rescan events when a sibling
        // file elsewhere in the parent changes).
        if (!m_watcher->directories().contains(directory)) {
            m_watcher->addPath(directory);
        }
        // Promotion cleanup — look up the ancestor WE actually watched
        // for this target. A prior `attachWatcherForDir` invocation may
        // have climbed past the immediate parent (because the parent
        // didn't exist either at the time) and installed the watch on
        // a grandparent / further ancestor. Removing `info.absolutePath()`
        // here would miss that case and leak the ancestor watch for the
        // set's lifetime, firing a rescan for every unrelated file
        // event in that tree.
        const auto watchedAncestorIt = m_parentWatchFor.constFind(targetKey);
        if (watchedAncestorIt != m_parentWatchFor.constEnd()) {
            const QString watchedAncestor = watchedAncestorIt.value();
            m_parentWatchFor.erase(watchedAncestorIt);
            if (m_watchedParents.contains(watchedAncestor)) {
                // Only remove the ancestor watch if no OTHER configured
                // target still relies on it (multiple missing-sibling
                // targets can share one climbed ancestor). Iterate the
                // remaining `m_parentWatchFor` values — that map tracks
                // exactly which other targets still need this ancestor.
                bool ancestorStillNeeded = false;
                for (auto it = m_parentWatchFor.constBegin(); it != m_parentWatchFor.constEnd(); ++it) {
                    if (it.value() == watchedAncestor) {
                        ancestorStillNeeded = true;
                        break;
                    }
                }
                if (!ancestorStillNeeded) {
                    m_watcher->removePath(watchedAncestor);
                    m_watchedParents.remove(watchedAncestor);
                }
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
        // On Linux, `canonicalPath()` on a non-existent target returns
        // the platform-dependent degenerate form `"."` (current dir)
        // rather than an empty string when no ancestor in the chain
        // exists. Treat both forms as "no canonical resolution" —
        // otherwise the climb below terminates immediately on
        // `QDir(".").exists()` (the cwd trivially exists) and we end
        // up installing a watcher on the process's current directory,
        // which is both incorrect and a serious foot-gun.
        if (ancestor.isEmpty() || ancestor == QLatin1String(".")) {
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
                qCWarning(lcWatcher) << "attachWatcherForDir: climb exceeded" << kMaxClimb << "levels for target"
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
            qCDebug(lcWatcher) << "attachWatcherForDir: no existing ancestor for" << directory
                               << "— call requestRescan() after creating the directory tree.";
            return;
        }
        // Canonicalise the ancestor path — cleanPath collapses trailing
        // slashes and "/./" segments so the parent-set equality check
        // doesn't duplicate the same ancestor under two spellings. We
        // don't call canonicalPath() here to preserve symlink identity
        // at the ancestor level (callers may have deliberately pointed
        // at a symlinked location) — cleanPath is the right level of
        // normalisation.
        ancestor = QDir::cleanPath(ancestor);
        if (isForbiddenWatchRoot(ancestor)) {
            qCDebug(lcWatcher) << "Refusing to watch forbidden ancestor" << ancestor << "for target" << directory
                               << "— call requestRescan() after creating the directory tree.";
            return;
        }
        if (!m_watchedParents.contains(ancestor)) {
            if (!m_watcher->directories().contains(ancestor)) {
                m_watcher->addPath(ancestor);
            }
            m_watchedParents.insert(ancestor);
        }
        // Record the per-target ancestor back-reference — the promotion
        // branch above uses this to find the ACTUALLY-watched ancestor
        // (may be the grandparent or further up). Re-inserting is
        // idempotent: the same target path is guaranteed to resolve to
        // the same ancestor on repeated calls while the filesystem is
        // steady.
        m_parentWatchFor.insert(targetKey, ancestor);
    }
}

void WatchedDirectorySet::onWatchedPathChanged()
{
    // Debounce — a single save typically fires 2-3 inotify events in
    // the same ~10 ms window (create temp → rename → fsync). The
    // single-shot timer collapses that into one rescan.
    requestRescan();
}

} // namespace PhosphorFsLoader
