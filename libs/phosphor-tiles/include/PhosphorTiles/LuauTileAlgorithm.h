// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "TileScriptMetadata.h"
#include "TilingAlgorithm.h"

#include <phosphortiles_export.h>

#include <QString>
#include <QVariantMap>

#include <memory>

namespace PhosphorScripting {
class LuauEngine;
class LuauWatchdog;
}

namespace PhosphorTiles {

class SplitNode;

/**
 * @brief A tiling algorithm backed by a user-provided Luau script.
 *
 * The script's module is loaded into a PhosphorScripting::LuauEngine whose
 * `pluau` standard library has been injected and frozen. There are two engine
 * modes, chosen by which constructor is used:
 *
 *  - **Owned engine** (single-arg ctor): the instance creates and owns its own
 *    sandboxed VM. Used for *untrusted* user scripts so each gets its own
 *    fault-isolated heap and per-engine memory cap — a runaway script can only
 *    exhaust its own VM.
 *  - **Shared engine** (engine-arg ctor): the module is loaded into a VM shared
 *    across many *trusted* bundled scripts, so the ~per-VM baseline + 42 KB
 *    `pluau` prelude is paid once instead of per script. The shared engine is
 *    held by `shared_ptr` so a deferred-deleted algorithm keeps it alive until
 *    its own teardown (which releases just this algorithm's module handle).
 *
 * The script is a Luau chunk
 * that returns a module table, conventionally via `pluau.algorithm{...}`:
 *
 *     return pluau.algorithm {
 *         metadata = { id = "columns", name = "Columns" },
 *         tile = function(ctx) return ctx.area:columns(ctx.count, ctx.gap) end,
 *     }
 *
 * Params/state are marshalled to/from the script as nested QVariant structures;
 * no Lua types cross into this class. Runaway scripts are bounded by a shared
 * LuauWatchdog.
 *
 * Not thread-safe — all calls must occur on the owning (main) thread.
 */
class PHOSPHORTILES_EXPORT LuauTileAlgorithm : public TilingAlgorithm
{
    Q_OBJECT

public:
    /// Owned-engine ctor: creates an isolated, per-engine-capped VM for this
    /// script. Use for untrusted user scripts.
    explicit LuauTileAlgorithm(const QString& filePath,
                               std::shared_ptr<PhosphorScripting::LuauWatchdog> watchdog = nullptr,
                               QObject* parent = nullptr);
    /// Shared-engine ctor: loads this script's module into @p sharedEngine (an
    /// already init+prelude+sandbox'd VM) instead of creating its own. Use for
    /// trusted bundled scripts so the VM baseline + prelude are shared. The
    /// engine must be created via createSandboxedEngine().
    LuauTileAlgorithm(const QString& filePath, std::shared_ptr<PhosphorScripting::LuauEngine> sharedEngine,
                      std::shared_ptr<PhosphorScripting::LuauWatchdog> watchdog, QObject* parent = nullptr);
    ~LuauTileAlgorithm() override;

    /// Build a sandboxed VM with the `pluau` standard library installed and
    /// frozen, ready for loadModule(). Returns nullptr (and sets @p error) on
    /// failure. Shared by the owned-engine path and the shared-engine factory
    /// in ScriptedAlgorithmLoader so the prelude is loaded identically.
    static std::shared_ptr<PhosphorScripting::LuauEngine>
    createSandboxedEngine(std::shared_ptr<PhosphorScripting::LuauWatchdog> watchdog, QString* error = nullptr);

    /// Whether the script loaded and exposes a callable tile() function.
    bool isValid() const;

    QString filePath() const;

    /// Optional algorithm id declared in metadata (empty if unset).
    QString id() const;

    void setUserScript(bool isUser);

    // TilingAlgorithm interface
    QString name() const override;
    QString description() const override;
    QVector<QRect> calculateZones(const TilingParams& params) const override;
    int masterZoneIndex() const override;
    bool supportsMasterCount() const override;
    bool supportsSplitRatio() const override;
    qreal defaultSplitRatio() const override;
    int minimumWindows() const override;
    int defaultMaxWindows() const override;
    bool producesOverlappingZones() const override;
    bool supportsMinSizes() const noexcept override;
    bool supportsMemory() const noexcept override;
    QString zoneNumberDisplay() const noexcept override;
    bool centerLayout() const override;
    bool isScripted() const noexcept override;
    bool isUserScript() const noexcept override;
    void prepareTilingState(TilingState* state) const override;

    // Lifecycle hooks (v2)
    bool supportsLifecycleHooks() const noexcept override;
    void onWindowAdded(TilingState* state, int windowIndex) override;
    void onWindowRemoved(TilingState* state, int windowIndex) override;

    // Interactive-resize hook (v2) — for non-tree algorithms that persist their
    // own ctx.state (e.g. an aligned grid remembering column widths).
    bool supportsResizeHook() const noexcept override;
    void onWindowResized(TilingState* state, const ResizeEvent& resize) override;
    bool supportsScriptState() const noexcept override;

    // Custom parameters (v2)
    bool supportsCustomParams() const noexcept override;
    QVariantList customParamDefList() const override;
    bool hasCustomParam(const QString& name) const override;

private:
    bool loadScript(const QString& filePath);
    void cacheMetadataAndOverrides();

    /// Marshal TilingParams into the `ctx` table the script's tile() receives.
    QVariantMap buildContext(const TilingParams& params, const QRect& area) const;
    /// Marshal a TilingState into the `state` table lifecycle hooks receive.
    QVariantMap buildStateMap(const TilingState* state, bool includeCountAfterRemoval) const;

    // Engine ownership: exactly one of m_ownedEngine / m_sharedEngine is set,
    // and m_engine is a non-owning view of whichever is active. Both are
    // shared_ptr so the active engine survives this object's deferred-delete
    // teardown (see class docs); m_sharedEngine additionally keeps the
    // loader's shared VM alive until this algorithm releases its module.
    std::shared_ptr<PhosphorScripting::LuauEngine> m_ownedEngine;
    std::shared_ptr<PhosphorScripting::LuauEngine> m_sharedEngine;
    PhosphorScripting::LuauEngine* m_engine = nullptr;
    std::shared_ptr<PhosphorScripting::LuauWatchdog> m_watchdog;
    int m_module = -1;

    QString m_filePath;
    QString m_scriptId;
    bool m_valid = false;
    bool m_isUserScript = false;
    bool m_hasOnWindowAdded = false;
    bool m_hasOnWindowRemoved = false;
    bool m_hasOnWindowResized = false;

    ScriptedHelpers::ScriptMetadata m_metadata;

    // Accessor values resolved once at load (override fn → metadata → default).
    int m_cachedMasterZoneIndex = -1;
    int m_cachedMinimumWindows = 1;
    int m_cachedDefaultMaxWindows = 6;
    qreal m_cachedDefaultSplitRatio = AutotileDefaults::DefaultSplitRatio;
    bool m_cachedSupportsMasterCount = false;
    bool m_cachedSupportsSplitRatio = false;
    bool m_cachedProducesOverlappingZones = false;
    bool m_cachedCenterLayout = false;
};

} // namespace PhosphorTiles
