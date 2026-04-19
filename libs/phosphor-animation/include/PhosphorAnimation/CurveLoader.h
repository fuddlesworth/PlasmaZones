// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QTimer>

#include <memory>

QT_BEGIN_NAMESPACE
class QFileSystemWatcher;
QT_END_NAMESPACE

namespace PhosphorAnimation {

class CurveRegistry;

/**
 * @brief Opt-in policy for directory-scanning loaders — pair with
 *        `CurveLoader` / `ProfileLoader`.
 *
 * Phase 4 decision W. `LiveReload::On` installs a
 * `QFileSystemWatcher` on the scanned directory with 50 ms debounce;
 * edits trigger a rescan + `curvesChanged` / `profilesChanged` signal.
 * `LiveReload::Off` is the fire-and-forget mode — tests, batch imports,
 * and consumers that prefer explicit refresh semantics.
 */
enum class LiveReload : quint8 {
    Off,
    On,
};

/**
 * @brief Scans JSON curve-definition files and registers them with `CurveRegistry`.
 *
 * Phase 4 decisions U + V + W + X. Consumer-agnostic: the loader takes
 * an absolute directory path and a `CurveRegistry` reference; callers
 * (PlasmaZones daemon, Wayfire plugin, Quickshell shell) pick their
 * own XDG namespace via `QStandardPaths::locateAll(GenericDataLocation,
 * "<consumer>/curves", LocateDirectory)` and hand the results here.
 *
 * ## File format (schema v1)
 *
 * One curve per file, UTF-8 JSON object:
 *
 * ```json
 * {
 *   "name":         "smooth-overshoot",
 *   "displayName":  "Smooth Overshoot",
 *   "typeId":       "spring",
 *   "parameters":   { "omega": 14.0, "zeta": 0.6 }
 * }
 * ```
 *
 * - `name` — required. Registry key under which the curve is registered.
 *   Callers reference it as `"smooth-overshoot"` when constructing
 *   Profiles or Curves.
 * - `displayName` — optional. Settings-UI label; the loader stores it
 *   but does not act on it.
 * - `typeId` — required. Must match an existing `CurveRegistry`
 *   factory (`"spring"`, `"cubic-bezier"`, `"elastic-in"`, etc.). No
 *   new curve classes can be defined through JSON (decision V —
 *   user curves parameterise existing types).
 * - `parameters` — object whose shape depends on `typeId`:
 *     * `spring`: `omega` (qreal), `zeta` (qreal)
 *     * `cubic-bezier`: `x1`, `y1`, `x2`, `y2` (all qreal)
 *     * `elastic-in` / `-out` / `-in-out`: `amplitude` (qreal), `period` (qreal)
 *     * `bounce-in` / `-out` / `-in-out`: `amplitude` (qreal), `bounces` (int)
 *
 * ## Collision policy (decision X)
 *
 * `loadFromDirectories` scans in the order supplied. Later entries
 * override earlier — callers that want the usual XDG "user overrides
 * system" shape pass directories in system-first / user-last order
 * (matches the reverse of `QStandardPaths::locateAll`'s natural output;
 * see `LayoutManager::loadLayouts` for the in-tree precedent).
 *
 * Per-curve bookkeeping tracks the `sourcePath` plus `systemSourcePath`
 * (the shadowed system entry's file, if any). Unregistering via
 * `unregisterUserCurves(dir)` restores the shadowed system curves from
 * the tracked paths — mirrors `LayoutManager`'s user-copy-deletion
 * semantics.
 *
 * ## Live reload (decision W)
 *
 * When constructed with `LiveReload::On`, the loader installs a
 * `QFileSystemWatcher` on every scanned directory and debounces rescan
 * requests through a 50 ms single-shot `QTimer` — matches the
 * `LayoutManager` / `EditorController` pattern.
 *
 * `QFileSystemWatcher` has known misses on atomic-rename writes (e.g.,
 * `QSaveFile`). For cross-process robustness, consumers should wire a
 * D-Bus notification stream for their writer path through the same
 * rescan entry (`loader.requestRescan()` — public so consumer signals
 * can tie in as belt-and-suspenders).
 *
 * ## Thread safety
 *
 * GUI-thread only. `QFileSystemWatcher` / `QTimer` live on the thread
 * the loader was constructed on; call `loadFromDirectory` from the
 * same thread.
 */
class PHOSPHORANIMATION_EXPORT CurveLoader : public QObject
{
    Q_OBJECT

public:
    explicit CurveLoader(QObject* parent = nullptr);
    ~CurveLoader() override;

    /// Scan @p directory for `*.json` curve definitions and register
    /// each into @p registry. Returns the count successfully registered
    /// (may be less than the file count if some files failed to parse —
    /// failures are logged but do not abort the rest of the scan).
    ///
    /// @p liveReload installs a `QFileSystemWatcher` on the directory
    /// and re-registers on change. Multiple calls with the same
    /// directory are idempotent — the watcher is installed once.
    int loadFromDirectory(const QString& directory, CurveRegistry& registry, LiveReload liveReload = LiveReload::Off);

    /// Scan multiple directories in order. Later entries override
    /// earlier on name collision (standard user-wins-over-system
    /// layering). See class doc.
    int loadFromDirectories(const QStringList& directories, CurveRegistry& registry,
                            LiveReload liveReload = LiveReload::Off);

    /// Load curves bundled at the library's install-relative
    /// `data/curves/` (discovered via `QStandardPaths` against the
    /// library's own org/app). Returns the count registered. Returns
    /// zero when no bundled curves ship with the library — currently
    /// a stub that pays the scan cost but produces no output, for
    /// consumers to call regardless of whether builtins exist yet.
    int loadLibraryBuiltins(CurveRegistry& registry);

    /// Request a manual rescan of every watched directory. Callers
    /// wire this to D-Bus notifications (cross-process safe — see
    /// class doc) to cover `QFileSystemWatcher`'s atomic-rename
    /// blind spot.
    void requestRescan();

    /// Count of currently-registered curves under this loader's
    /// management. Excludes whatever CurveRegistry factories were
    /// present before the loader was instantiated.
    int registeredCount() const;

    /// Tests / debug — access the tracked entries.
    struct Entry
    {
        QString name;
        QString displayName;
        QString sourcePath; ///< Where this copy was loaded from.
        QString systemSourcePath; ///< Non-empty when this copy shadows a system entry.
    };
    QList<Entry> entries() const;

Q_SIGNALS:
    /// Fired after a rescan when the set of registered curves changed
    /// (addition, replacement, or removal). Consumers re-resolve any
    /// named curves they care about. Debounced to coalesce multiple
    /// filesystem events in the same 50 ms window.
    void curvesChanged();

private:
    /// Parse a single file, returning the registered entry on success
    /// or `std::nullopt` on parse failure. Does not mutate the
    /// registry — the caller installs the resulting curve.
    std::optional<std::pair<Entry, std::shared_ptr<const Curve>>> parseFile(const QString& filePath) const;

    void rescanAll();
    void rescanDirectory(const QString& directory);
    void installWatcherIfNeeded();

    // Raw pointer — CurveRegistry is a process-wide singleton that
    // outlives any loader, so QPointer's QObject-tracking is neither
    // needed nor applicable (CurveRegistry isn't a QObject).
    CurveRegistry* m_registry = nullptr;
    QStringList m_directories; ///< Scanned directories in caller-supplied order.
    QHash<QString, Entry> m_entries; ///< name → entry
    QFileSystemWatcher* m_watcher = nullptr;
    QTimer m_debounceTimer;
    bool m_liveReloadEnabled = false;
};

} // namespace PhosphorAnimation
