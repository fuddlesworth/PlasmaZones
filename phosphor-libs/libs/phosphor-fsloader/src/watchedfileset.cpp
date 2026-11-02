// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorFsLoader/WatchedFileSet.h>

#include "nearestexistingancestor_p.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QLoggingCategory>
#include <QThread>

#include <sys/stat.h>

namespace PhosphorFsLoader {

namespace {
Q_LOGGING_CATEGORY(lcFileSet, "phosphorfsloader.watchedfileset")
} // namespace

WatchedFileSet::WatchedFileSet(QObject* parent)
    : QObject(parent)
    , m_watcher(new QFileSystemWatcher(this))
{
    m_debounceTimer.setSingleShot(true);
    m_debounceTimer.setInterval(DefaultDebounceMs);
    connect(&m_debounceTimer, &QTimer::timeout, this, &WatchedFileSet::onDebounceElapsed);
    // Neither signal's path argument is used: a directory event says only that
    // SOMETHING beside a watched path moved, and the fingerprint pass is what
    // decides whether it was one of ours.
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this] {
        m_debounceTimer.start();
    });
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        m_debounceTimer.start();
    });
}

WatchedFileSet::~WatchedFileSet() = default;

void WatchedFileSet::setPaths(const QStringList& paths)
{
    // Asserted AND guarded, like WatchedDirectorySet's mutators: this installs
    // kernel watches and rewrites the fingerprint table, so a release build
    // must refuse an off-thread call rather than race the debounce slot.
    if (thread() != QThread::currentThread()) {
        Q_ASSERT_X(false, "WatchedFileSet::setPaths", "GUI-thread only — see class docs");
        qCWarning(lcFileSet) << "setPaths called off the owning thread; refusing";
        return;
    }

    QStringList normalised;
    normalised.reserve(paths.size());
    for (const QString& path : paths) {
        if (path.isEmpty()) {
            continue;
        }
        const QString clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
        if (!normalised.contains(clean)) {
            normalised.append(clean);
        }
    }
    m_paths = std::move(normalised);

    // Watches first, fingerprints second. A change landing after the watch is
    // armed raises an event and is caught by the next debounce; one landing
    // before it is already in the baseline. The reverse order leaves a window
    // in which a change is neither.
    reconcileWatches();

    QHash<QString, Fingerprint> fingerprints;
    fingerprints.reserve(m_paths.size());
    for (const QString& path : std::as_const(m_paths)) {
        const auto it = m_fingerprints.constFind(path);
        fingerprints.insert(path, it != m_fingerprints.constEnd() ? it.value() : fingerprintOf(path));
    }
    m_fingerprints = std::move(fingerprints);
}

QStringList WatchedFileSet::paths() const
{
    return m_paths;
}

void WatchedFileSet::setDebounceInterval(int ms)
{
    m_debounceTimer.setInterval(qMax(0, ms));
}

int WatchedFileSet::debounceInterval() const
{
    return m_debounceTimer.interval();
}

QStringList WatchedFileSet::watchedPathsForTest() const
{
    QStringList watched = m_watcher->files() + m_watcher->directories();
    watched.sort();
    return watched;
}

WatchedFileSet::Fingerprint WatchedFileSet::fingerprintOf(const QString& path)
{
    // stat(), not lstat(): a symlinked config is the common dotfiles layout and
    // its content is what the caller cares about. Retargeting the link changes
    // the inode seen through it, so that still reports.
    struct stat st{};
    if (::stat(QFile::encodeName(path).constData(), &st) != 0) {
        return {};
    }
    Fingerprint fp;
    fp.exists = true;
    fp.device = static_cast<quint64>(st.st_dev);
    fp.inode = static_cast<quint64>(st.st_ino);
    fp.size = static_cast<qint64>(st.st_size);
    fp.mtimeSec = static_cast<qint64>(st.st_mtim.tv_sec);
    fp.mtimeNsec = static_cast<qint64>(st.st_mtim.tv_nsec);
    return fp;
}

void WatchedFileSet::reconcileWatches()
{
    // Every path is watched itself when it exists (an in-place edit only shows
    // on the file's own watch) and through its nearest existing ancestor
    // always (creation, deletion and rename-replacement only show on the
    // directory). Recomputed from scratch each time, because both halves go
    // stale on their own: the kernel drops a file watch when an editor's
    // rename retires the inode, and a missing directory may have appeared
    // since the ancestor was chosen.
    QSet<QString> desired;
    desired.reserve(m_paths.size() * 2);
    for (const QString& path : std::as_const(m_paths)) {
        if (QFileInfo::exists(path)) {
            desired.insert(path);
        }
        const QString ancestor = detail::nearestExistingAncestor(path);
        if (!ancestor.isEmpty()) {
            desired.insert(ancestor);
        }
    }

    // Diff against the watcher's REAL state rather than a mirror of what was
    // added: Qt removes a path from files()/directories() when the kernel drops
    // its watch, and that is exactly the case that has to be re-armed.
    const QStringList watchedFiles = m_watcher->files();
    const QStringList watchedDirectories = m_watcher->directories();
    QSet<QString> current(watchedFiles.cbegin(), watchedFiles.cend());
    current.unite(QSet<QString>(watchedDirectories.cbegin(), watchedDirectories.cend()));

    const QSet<QString> stale = current - desired;
    if (!stale.isEmpty()) {
        m_watcher->removePaths(stale.values());
    }
    const QSet<QString> missing = desired - current;
    if (missing.isEmpty()) {
        return;
    }
    const QStringList failed = m_watcher->addPaths(missing.values());
    for (const QString& path : missing) {
        if (!failed.contains(path)) {
            m_warnedFailedWatchPaths.remove(path);
            continue;
        }
        // A path that vanished between the exists() above and addPaths() is a
        // race the next event resolves; only a path still present is a real
        // failure (permissions, inotify quota) worth a warning.
        if (QFileInfo::exists(path) && !m_warnedFailedWatchPaths.contains(path)) {
            m_warnedFailedWatchPaths.insert(path);
            qCWarning(lcFileSet) << "failed to watch" << path
                                 << "— check permissions or the filesystem-watcher quota (inotify on Linux)";
        }
    }
}

void WatchedFileSet::onDebounceElapsed()
{
    // Same order as setPaths: re-arm first, then fingerprint, so nothing that
    // lands in between is lost.
    reconcileWatches();

    QStringList changed;
    for (const QString& path : std::as_const(m_paths)) {
        Fingerprint now = fingerprintOf(path);
        Fingerprint& previous = m_fingerprints[path];
        if (now == previous) {
            continue;
        }
        // A new inode under the same name is a rename-replacement. Qt drops
        // the old watch only once the kernel frees the old inode, which waits
        // for every open descriptor on it to close, so the file watch can
        // still be pinned to the retired inode here. Re-target it, then take
        // the baseline AFTER the re-arm so an edit in between is not lost.
        if (now.exists && previous.exists && (now.device != previous.device || now.inode != previous.inode)) {
            m_watcher->removePath(path);
            m_watcher->addPath(path);
            now = fingerprintOf(path);
        }
        previous = now;
        changed.append(path);
    }
    if (!changed.isEmpty()) {
        Q_EMIT filesChanged(changed);
    }
}

} // namespace PhosphorFsLoader
