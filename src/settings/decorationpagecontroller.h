// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Not forward-declared: moc needs the complete type to register the
// ShaderSetStore* Q_PROPERTY below as a pointer meta-type.
#include "shadersetstore.h"

#include <PhosphorControl/PageController.h>
#include <PhosphorSurface/DecorationProfileTree.h>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

namespace PhosphorSurfaceShaders {
class SurfaceShaderRegistry;
}

namespace PlasmaZones {

class ISettings;

/// Q_INVOKABLE surface for the "Decoration" drill-down settings pages
/// (exposed to QML through SettingsController's `decorationPage` Q_PROPERTY;
/// this class declares invokables + signals, plus the one `setsBridge`
/// property that hands QML the decoration-set store).
///
/// ## Scope: PER-SURFACE chains with walk-up inheritance
///
/// Where the previous flat surface-shader page selected ONE decoration pack
/// for every window, this controller edits a
/// `PhosphorSurfaceShaders::DecorationProfileTree` — a hierarchical store
/// of `DecorationProfile`s keyed on the dot-path surface namespace
/// (`window.tiled`, `osd`, `popup.snapAssist`, …). Each profile carries an
/// ordered CHAIN of surface shader packs. A baseline (global default) is overlaid by per-surface
/// overrides via the tree's resolve() walk-up.
///
/// ## Baseline as path ""
///
/// The empty path "" addresses the baseline (global default): every mutator and
/// reader special-cases it to read or write the tree's `baseline()` rather than a
/// per-surface override. So `resolvedProfile("")` == baseline, `setChain("", ...)`
/// sets the baseline chain, and `clearOverride("")` is rejected (the baseline
/// cannot be "inherited away").
///
/// NOTE that NO settings page binds "": the Decoration nav has no General surface
/// page because there is no meaningful global default (borders and title bars are
/// window-only, and the daemon surfaces default to no decoration — see
/// settingscontroller_pageregistration.cpp). The baseline is reachable over D-Bus
/// and from tests, and the resolve walk-up honours it, but the UI edits the
/// category root cards ("window", "osd", "popup") instead. Decoration SETS
/// deliberately neither capture nor apply a baseline for the same reason: an
/// imported one could never be undone through the UI.
///
/// ## Dirty tracking
///
/// All mutators read the tree from `ISettings::decorationProfileTree()`,
/// mutate, and write back through `setDecorationProfileTree()`. That
/// setter's NOTIFY (`decorationProfileTreeChanged`) is routed by
/// `SettingsController`'s meta-object loop into the active page's dirty
/// flag — so this controller carries NO per-page staged state:
/// `isDirty()` / `apply()` / `discard()` are no-ops, exactly like
/// `GeneralPageController`. Apply /
/// Discard / Defaults are driven globally by `SettingsController`.
class DecorationPageController : public PhosphorControl::PageController
{
    Q_OBJECT

    /// The decoration-set store, bound by DecorationSetsPage as its `bridge`.
    Q_PROPERTY(PlasmaZones::ShaderSetStore* setsBridge READ setsBridge CONSTANT)

public:
    /// @param registry Optional — when null, the `*ShaderEffects()`
    ///        Q_INVOKABLEs return empty results so unit tests can construct
    ///        the controller without a surface bootstrap.
    /// @param settings Optional — when null, profile getters return empty
    ///        results and mutators are no-ops.
    explicit DecorationPageController(PhosphorSurfaceShaders::SurfaceShaderRegistry* registry = nullptr,
                                      ISettings* settings = nullptr, QObject* parent = nullptr);
    ~DecorationPageController() override;

    // ── PhosphorControl::StagingDomain contract ───────────────────────────
    // No per-page staged state — mutations write straight to Settings and
    // the global SettingsController dirty loop tracks them (see class doc).
    bool isDirty() const override
    {
        return false;
    }
    void apply() override
    {
    }
    void discard() override
    {
    }

    // ── Available packs ───────────────────────────────────────────────────

    /// Installed `SurfaceShaderEffect`s flattened to a QML-friendly list.
    /// Each row: id / name / description / author / version / category /
    /// isUserEffect / previewPath / parameters (QVariantList of
    /// ParameterInfo maps).
    Q_INVOKABLE QVariantList availableShaderEffects() const;

    // ── Surface taxonomy ──────────────────────────────────────────────────

    /// Inheritance chain from @p path up to (but excluding) the empty
    /// baseline. e.g. `parentChain("window.tiled")` == `["window.tiled",
    /// "window"]`. Drives the "inheriting from" breadcrumb.
    Q_INVOKABLE QStringList parentChain(const QString& path) const;

    // ── Profile readers ───────────────────────────────────────────────────

    /// Effective profile for @p path: walks the parent chain through the
    /// tree (deeper-wins overlay), filling unset fields with library
    /// defaults so the returned map is always fully populated. For path
    /// "" returns the baseline (resolved against itself). Keys mirror
    /// `DecorationProfile::toJson()` (chain / parameters) — border width /
    /// radius / colour are the "border" pack's PARAMETERS,
    /// carried inside `parameters`, not separate decoration fields.
    Q_INVOKABLE QVariantMap resolvedProfile(const QString& path) const;

    /// The DIRECT override at @p path (the tree's `directOverride`), as a
    /// sparse map carrying ONLY the fields engaged at this exact path. An
    /// engaged field appears in the map; an inherited (nullopt) field is
    /// absent. Lets the QML card show which fields are locally set vs
    /// inherited. For path "" returns the baseline's engaged fields.
    Q_INVOKABLE QVariantMap rawProfile(const QString& path) const;

    /// True iff @p path carries a direct per-surface override. Always
    /// false for "" (the baseline is not an "override"). Rejects any
    /// @p path that is not a supported surface path.
    Q_INVOKABLE bool hasOverride(const QString& path) const;

    // ── Chain mutators ────────────────────────────────────────────────────

    /// The effective (resolved) chain for @p path — the ordered pack-id
    /// list after walk-up. For "" this is the baseline chain.
    Q_INVOKABLE QStringList chainAt(const QString& path) const;

    /// Engage @p chain as the chain at @p path. For "" sets the baseline
    /// chain. An empty list engages "explicitly no packs" (blocks the
    /// parent's chain). Writes the whole tree back through Settings.
    Q_INVOKABLE void setChain(const QString& path, const QStringList& chain);

    /// Set one per-pack parameter override at @p path:
    /// parameters[packId][paramId] = value. For "" edits the baseline.
    Q_INVOKABLE void setChainParam(const QString& path, const QString& packId, const QString& paramId,
                                   const QVariant& value);

    /// Merge a whole map of per-pack parameter overrides at @p path in one
    /// write: parameters[packId][k] = v for every (k, v) in @p params. Used
    /// by the editor's Randomize action so a single roll persists as one
    /// tree write instead of one per parameter. For "" edits the baseline.
    Q_INVOKABLE void setChainParams(const QString& path, const QString& packId, const QVariantMap& params);

    /// The effective (resolved) set of chain packs toggled OFF at @p path.
    /// Pairs with chainAt(): the editor renders every declared pack and
    /// greys the ones listed here; the renderers exclude them.
    Q_INVOKABLE QStringList disabledPacksAt(const QString& path) const;

    /// Toggle one chain layer on/off at @p path without touching the chain
    /// order or the pack's parameters — the per-layer counterpart of a
    /// rule's setRuleEnabled. First direct edit at @p path seeds the set
    /// from the resolved (inherited) value, mirroring how the override
    /// editor seeds the chain. For "" edits the baseline.
    Q_INVOKABLE void setChainLayerEnabled(const QString& path, const QString& packId, bool enabled);

    // ── Whole-override mutator ─────────────────────────────────────────────

    /// Drop the entire per-surface override at @p path so the surface
    /// fully inherits its ancestors / baseline. Rejected for "" (the baseline
    /// is the root and has nothing to inherit from; edit its fields directly via
    /// setChain). @return true when an override was removed.
    Q_INVOKABLE bool clearOverride(const QString& path);

    /// Number of descendant surfaces under @p path that carry their own
    /// override — they SHADOW this parent node (the resolve walk stops at the
    /// descendant's own profile, so the parent's chain never reaches them).
    /// Drives the parent-node "Clear shadowing children" affordance, mirroring
    /// AnimationsPageController::shaderOverrideDescendantCount.
    Q_INVOKABLE int overrideDescendantCount(const QString& path) const;

    /// Clear every descendant override under @p path so the whole subtree
    /// inherits this node again. @return the number of overrides cleared.
    Q_INVOKABLE int clearOverrideDescendants(const QString& path);

    // ── Shader-browser bridge (ShaderBrowserPage contract) ──────────────
    // Same trio the animations / snapping shader pages implement, over the
    // surface-pack registry and the decoration profile tree.

    /// Copy a shader-pack folder into the user surface-pack directory
    /// (~/.local/share/plasmazones/surface) via the shared
    /// ShaderPackInstaller. The registry's file watcher rescans on its own.
    Q_INVOKABLE bool installShaderPack(const QString& sourceUrl);
    /// Open (creating if needed) the user surface-pack directory in the
    /// file manager.
    Q_INVOKABLE void openUserShaderDirectory();
    /// Every chain that contains @p effectId, as {path, label} entries sorted
    /// by label — the browser's "Used in" chips. That is every surface whose
    /// DIRECT override uses it, plus a "Global default" entry (empty path) when
    /// the baseline chain does: the baseline is a real chain the resolve walk
    /// falls back to, so a pack used only there is still in use.
    Q_INVOKABLE QVariantList shaderEffectUsages(const QString& effectId) const;

    /// The decoration-set store — the `bridge` ShaderSetsPage binds to.
    /// Named snapshots of the decoration profile tree, persisted as JSON
    /// under ~/.local/share/plasmazones/decorationsets/<slug>.json.
    /// Applying merges: every entry replaces the DIRECT profile at its
    /// path; surfaces the set
    /// does not cover keep their current overrides. APPLYING a set writes
    /// through ISettings::setDecorationProfileTree, so that write rides the
    /// normal dirty / apply / discard staging flow. The set FILES themselves
    /// are not staged (decoration wires no fileSnapshot hook), so no Discard
    /// can undo a set write here. Saving over an existing set requires explicit
    /// consent regardless of domain (see ShaderSetStore::saveCurrentAsSet). The
    /// domain closures live in decorationpagecontroller_sets.cpp.
    ShaderSetStore* setsBridge() const
    {
        return m_sets;
    }

    /// Test hook: redirect the sets directory to @p dir instead of the XDG
    /// default. Pass an empty string to restore the default. Mirrors
    /// AnimationsPageController::setUserProfilesDirOverride, and exists for the
    /// same reason: two test binaries resolving the same per-user qttest path
    /// wipe each other's files under parallel ctest. Not Q_INVOKABLE — QML
    /// callers must not redirect persistence.
    void setSetsDirOverride(const QString& dir);

Q_SIGNALS:
    /// Re-emit of `SurfaceShaderRegistry::effectsChanged` so QML can
    /// rebind without poking at the registry directly.
    void shaderEffectsChanged();

    /// Emitted whenever the tree mutates (any setter) AND on a full
    /// settings reload (`ISettings::decorationProfileTreeChanged`). Cards
    /// listen to this to imperatively refresh from the controller.
    void profilesChanged();

    /// Chrome-toast requests from the browser bridge (install / directory
    /// failures with the concrete reason). Routed by ShaderBrowserPage.
    void toastRequested(const QString& text);

private:
    /// Construct m_sets with the decoration domain closures. Called from the
    /// constructor; defined in decorationpagecontroller_sets.cpp.
    void initSetsStore();

    QString userShaderDirectoryPath() const;
    QString decorationSetsDirectoryPath() const;

    PhosphorSurfaceShaders::SurfaceShaderRegistry* m_registry = nullptr;
    ISettings* m_settings = nullptr;

    QString m_setsDirOverride; ///< Empty = use the XDG default

    /// The profile tree, parsed once per change rather than once per read.
    /// ISettings::decorationProfileTree() rebuilds it from the config store every
    /// call (QVariantMap to QJsonObject to fromJson), and refreshing one card
    /// costs several reads while a slider drag costs several per frame. Invalidated
    /// on decorationProfileTreeChanged, which is the only thing that can move it,
    /// including a write from D-Bus or a global reload.
    mutable std::optional<PhosphorSurfaceShaders::DecorationProfileTree> m_treeCache;

    /// The tree, from cache when it is warm. Empty tree when there are no settings.
    const PhosphorSurfaceShaders::DecorationProfileTree& tree() const;
    ShaderSetStore* m_sets = nullptr;
};

} // namespace PlasmaZones
