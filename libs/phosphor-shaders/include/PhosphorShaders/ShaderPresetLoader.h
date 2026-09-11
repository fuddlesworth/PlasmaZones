// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShaders/ShaderPreset.h>
#include <PhosphorShaders/phosphorshaders_export.h>

#include <PhosphorFsLoader/DirectoryLoader.h>
#include <PhosphorFsLoader/WatchedDirectorySet.h>

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

namespace PhosphorShaders {

class ShaderPresetRegistry;

using LiveReload = PhosphorFsLoader::LiveReload;

/// The standard root for user-saved shader presets:
/// `<XDG_DATA_HOME>/plasmazones/shader-presets`. One subdirectory per family,
/// named by `shaderFamilyToken`.
///
/// Does NOT create the directory. `WatchedDirectorySet` promotes its watch to
/// the parent when the target does not exist yet, so a preset saved into a
/// fresh install is picked up without a restart and without every reader having
/// to mkdir a directory it only wants to read.
PHOSPHORSHADERS_EXPORT QString standardUserPresetRoot();

/// The directory holding @p family's user presets under @p root.
PHOSPHORSHADERS_EXPORT QString userPresetDirectory(const QString& root, ShaderFamily family);

/**
 * @brief Import pre-existing overlay preset files into the family layout.
 *
 * Before presets became assignable, the zone shader browser saved them through
 * a plain file dialog into the ROOT of the preset directory, with no family
 * level and the pack id under a different key:
 *
 * ```
 * <root>/whatever-the-user-typed.json
 * { "name": "...", "shaderId": "aurora", "shaderParams": { ... } }
 * ```
 *
 * Those files are the only ones that ever existed, since overlay was the only
 * family with presets. This moves each into `<root>/overlay/` under the current
 * shape, renaming `shaderId` -> `packId` and `shaderParams` -> `params`.
 *
 * SAFE TO CALL FROM ANY PROCESS, AND AS OFTEN AS YOU LIKE. The new id is a
 * UUID v5 derived from the source filename rather than a fresh random one, so
 * two processes racing produce the same target path and the second write is a
 * no-op instead of a duplicate preset. Once a file has moved the root holds no
 * `*.json`, so later runs do nothing.
 *
 * A root-level file that carries no `shaderId` is not a legacy preset. It is
 * left alone rather than moved or deleted — this owns the shape it wrote, not
 * the directory.
 *
 * @return the number of presets imported (0 when there was nothing to do).
 */
PHOSPHORSHADERS_EXPORT int migrateLegacyOverlayPresets(const QString& root);

/**
 * @brief Scans one shader family's user preset files into a `ShaderPresetRegistry`.
 *
 * The preset counterpart of `PhosphorAnimation::CurveLoader`, and built on the
 * same `PhosphorFsLoader::DirectoryLoader`, which owns the directory walk, the
 * `QFileSystemWatcher`, the 50 ms debounce, parent-watch promotion for a
 * directory that does not exist yet, re-arming after an atomic-rename save,
 * user-wins layering, the file-size and entry-count caps, and the stale-key
 * purge. Nothing here re-implements any of that.
 *
 * ## On-disk shape
 *
 * `<root>/<family>/<presetId>.json`, flat within the family directory:
 *
 * ```
 * { "id": "{uuid}", "name": "Neon Pulse", "packId": "dissolve",
 *   "params": { "speed": 1.4, "glow": 0.8 } }
 * ```
 *
 * Flat rather than nested under a pack directory because `DirectoryLoader`
 * scans top-level `*.json` and a subdirectory layout would mean writing a
 * bespoke `IScanStrategy`. The pack a preset belongs to is a field in the file
 * instead of a directory level, which also makes re-homing a preset to another
 * pack an edit rather than a move. The FAMILY is still the directory, because
 * the loader has to know which family it is committing before it reads
 * anything.
 *
 * The filename stem is the fallback id, so a hand-written `my-preset.json` with
 * no `id` field loads with a stable identity an assignment can point at.
 *
 * ## Thread safety
 *
 * GUI-thread only. Inherits the constraint from `DirectoryLoader`.
 */
class PHOSPHORSHADERS_EXPORT ShaderPresetLoader : public QObject
{
    Q_OBJECT

public:
    /// @p registry must outlive the loader.
    ShaderPresetLoader(ShaderPresetRegistry& registry, ShaderFamily family, QObject* parent = nullptr);
    ~ShaderPresetLoader() override;

    ShaderPresetLoader(const ShaderPresetLoader&) = delete;
    ShaderPresetLoader& operator=(const ShaderPresetLoader&) = delete;

    /// Scan @p directory for `*.json` presets and commit them to the registry.
    /// @return the number of presets registered.
    int loadFromDirectory(const QString& directory, LiveReload liveReload = LiveReload::On);

    /// Scan several directories in caller-declared priority order, for a
    /// consumer that layers a system preset directory under the user's.
    int loadFromDirectories(
        const QStringList& directories, LiveReload liveReload = LiveReload::On,
        PhosphorFsLoader::RegistrationOrder order = PhosphorFsLoader::RegistrationOrder::LowestPriorityFirst);

    /// Ask for a rescan, coalesced through the 50 ms debounce. Use this when a
    /// rescan is merely desirable soon; back-to-back calls collapse into one.
    void requestRescan();

    /// Rescan every registered directory synchronously, so the registry
    /// reflects the new state before this returns.
    ///
    /// This is what the WRITE side wants: after saving a preset file, the
    /// picker has to show it in the same turn rather than a debounce later,
    /// and the caller usually wants to select what it just saved. GUI-thread
    /// only, like everything else here.
    void rescanNow();

    ShaderFamily family() const;

private:
    class Sink;
    std::unique_ptr<Sink> m_sink;
    std::unique_ptr<PhosphorFsLoader::DirectoryLoader> m_loader;
};

} // namespace PhosphorShaders
