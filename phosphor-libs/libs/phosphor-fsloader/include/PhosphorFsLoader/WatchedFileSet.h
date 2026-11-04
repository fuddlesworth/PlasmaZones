// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorFsLoader/phosphorfsloader_export.h>

#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QTimer>

QT_BEGIN_NAMESPACE
class QFileSystemWatcher;
QT_END_NAMESPACE

namespace PhosphorFsLoader {

/**
 * @brief Watch an explicit set of files and report only the ones that really changed.
 *
 * The file-level sibling of `WatchedDirectorySet`. Where that class rescans a
 * directory tree through a strategy, this one watches a caller-chosen list of
 * individual paths (a config file and the theme it names, a shader's include
 * and asset dependencies, one user file a script asked about) and tells the
 * caller which of them changed.
 *
 *   • Any path may not exist yet. Each existing path is watched directly, and
 *     the nearest existing ancestor directory of every path is watched too, so
 *     creation, deletion and atomic-rename replacement are all noticed, even
 *     when the file's parent directory is itself created later.
 *   • Watches are re-resolved after every event: an ancestor proxy moves down
 *     as missing directories appear, and a file watch the kernel dropped when
 *     an editor replaced the inode by rename is re-armed.
 *   • Each path carries a fingerprint (existence, device, inode, size and
 *     modification time to the nanosecond). After the debounce, one
 *     `filesChanged` carries exactly the paths whose fingerprint moved. A write
 *     to an unrelated file beside a watched one wakes the watcher but emits
 *     nothing.
 *
 * Unlike `WatchedDirectorySet`, an ancestor watch here is never refused for
 * being a high-churn root such as `$HOME`: a file can legitimately live
 * directly in one, and the fingerprint filter turns unrelated churn there into
 * a few `stat()` calls per debounce rather than a signal.
 *
 * Paths are normalised to clean absolute form (`QFileInfo::absoluteFilePath`
 * then `QDir::cleanPath`), and that is the spelling `paths()` returns and
 * `filesChanged` reports. Symlinks are followed for the fingerprint, so
 * retargeting a link counts as a change of the path.
 *
 * POSIX only: the fingerprint is read with `stat()`.
 *
 * ## Thread safety
 *
 * GUI-thread only, like `WatchedDirectorySet`. The watcher and the debounce
 * timer live on the thread the set was constructed on.
 */
class PHOSPHORFSLOADER_EXPORT WatchedFileSet : public QObject
{
    Q_OBJECT

public:
    /// Default debounce, long enough to coalesce an editor's
    /// write-temp-then-rename save into one report.
    static constexpr int DefaultDebounceMs = 100;

    explicit WatchedFileSet(QObject* parent = nullptr);
    ~WatchedFileSet() override;

    WatchedFileSet(const WatchedFileSet&) = delete;
    WatchedFileSet& operator=(const WatchedFileSet&) = delete;

    /**
     * @brief Replace the watched set with @p paths.
     *
     * Reconciles rather than accumulates: watches for paths no longer listed
     * are removed, new ones are added, and the underlying watcher ends up
     * holding exactly what the new set needs. Empty entries and duplicates
     * (after normalisation) are dropped.
     *
     * A newly listed path takes its current fingerprint as the baseline and
     * is not reported by this call. A path that stays listed keeps its
     * baseline, so a change still pending in the debounce window is reported
     * as usual.
     */
    void setPaths(const QStringList& paths);

    /// The watched paths in normalised form, in the order given to `setPaths`.
    QStringList paths() const;

    /// Change the debounce window. Values below 0 are clamped to 0. Takes
    /// effect from the next event; a pending window keeps its interval.
    void setDebounceInterval(int ms);
    int debounceInterval() const;

    /// Test-only: every path the underlying watcher currently holds (files
    /// and directories together), sorted. Lets a test pin that `setPaths`
    /// reconciles instead of accumulating.
    QStringList watchedPathsForTest() const;

Q_SIGNALS:
    /**
     * @brief Emitted once per debounce window that changed something.
     *
     * @p paths lists, in `paths()` order, every watched path whose
     * fingerprint differs from the last one observed: created, deleted,
     * replaced, or edited in place. Never emitted with an empty list.
     */
    void filesChanged(const QStringList& paths);

private:
    /// Identity and content stamp for one path. `exists == false` compares
    /// equal to any other missing state, so a path that stays absent never
    /// reports.
    struct Fingerprint
    {
        bool exists = false;
        quint64 device = 0;
        quint64 inode = 0;
        qint64 size = 0;
        qint64 mtimeSec = 0;
        qint64 mtimeNsec = 0;

        bool operator==(const Fingerprint&) const = default;
    };

    static Fingerprint fingerprintOf(const QString& path);
    void reconcileWatches();
    void onDebounceElapsed();

    QFileSystemWatcher* m_watcher = nullptr;
    QStringList m_paths; ///< normalised, in caller order
    QHash<QString, Fingerprint> m_fingerprints;
    /// Warn-once latch for watch failures. `reconcileWatches` runs on every
    /// event, and the causes (inotify quota, permissions) persist, so an
    /// unconditional warning would repeat at the debounce rate. Cleared for a
    /// path once a later add of it succeeds.
    QSet<QString> m_warnedFailedWatchPaths;
    QTimer m_debounceTimer;
};

} // namespace PhosphorFsLoader
