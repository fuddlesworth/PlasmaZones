// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <PhosphorJsonLoader/DirectoryLoader.h>
#include <PhosphorJsonLoader/IDirectoryLoaderSink.h>

#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <memory>

namespace PhosphorAnimation {

class CurveRegistry;

/**
 * @brief Opt-in policy for directory-scanning loaders.
 *
 * Thin re-export of `PhosphorJsonLoader::LiveReload` so the existing
 * `PhosphorAnimation::LiveReload::On` call-sites keep compiling after
 * the directory-watching scaffolding moved into `phosphor-jsonloader`.
 */
using LiveReload = PhosphorJsonLoader::LiveReload;

/**
 * @brief Scans JSON curve-definition files and registers them with `CurveRegistry`.
 *
 * Phase 4 decisions U + V + W + X. Consumer-agnostic: the loader takes
 * an absolute directory path and a `CurveRegistry` reference; callers
 * (PlasmaZones daemon, Wayfire plugin, Quickshell shell) pick their
 * own XDG namespace via `QStandardPaths::locateAll(GenericDataLocation,
 * "<consumer>/curves", LocateDirectory)` and hand the results here.
 *
 * The directory-walking, watching, debouncing, and user-wins-collision
 * bookkeeping is delegated to `PhosphorJsonLoader::DirectoryLoader`;
 * this class is the curve-specific sink on top of that.
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
 * - `displayName` — optional. Settings-UI label; stored but not acted on.
 * - `typeId` — required. Must match an existing `CurveRegistry`
 *   factory (`"spring"`, `"cubic-bezier"`, `"elastic-in"`, etc.). No
 *   new curve classes can be defined through JSON (decision V).
 * - `parameters` — object whose shape depends on `typeId`.
 *
 * ## Collision / live reload
 *
 * Inherited from `DirectoryLoader`: later-scanned directories override
 * earlier on name collision (pass dirs in system-first, user-last
 * order); `LiveReload::On` installs a `QFileSystemWatcher` with 50 ms
 * debounce, including parent-directory watching when the target
 * doesn't exist yet. Deleted files are purged from `CurveRegistry`
 * via `unregisterFactory`.
 */
class PHOSPHORANIMATION_EXPORT CurveLoader : public QObject
{
    Q_OBJECT

public:
    explicit CurveLoader(QObject* parent = nullptr);
    ~CurveLoader() override;

    /// Scan @p directory for `*.json` curve definitions and register
    /// each into @p registry.
    int loadFromDirectory(const QString& directory, CurveRegistry& registry, LiveReload liveReload = LiveReload::Off);

    /// Scan multiple directories in order.
    int loadFromDirectories(const QStringList& directories, CurveRegistry& registry,
                            LiveReload liveReload = LiveReload::Off);

    /// Load curves bundled at the library's install-relative
    /// `data/curves/`. Returns zero when no bundled curves ship.
    int loadLibraryBuiltins(CurveRegistry& registry);

    /// Manual rescan request (cross-process D-Bus signal → this).
    void requestRescan();

    /// Count of currently-registered curves under this loader's management.
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
    /// Fired after a rescan when the set of registered curves changed.
    void curvesChanged();

private:
    class Sink;
    std::unique_ptr<Sink> m_sink;
    std::unique_ptr<PhosphorJsonLoader::DirectoryLoader> m_loader;
};

} // namespace PhosphorAnimation
