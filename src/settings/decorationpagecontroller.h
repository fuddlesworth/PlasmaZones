// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorControl/PageController.h>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

namespace PhosphorSurfaceShaders {
class SurfaceShaderRegistry;
}

namespace PlasmaZones {

class ISettings;

/// Q_INVOKABLE surface for the "Decoration" drill-down settings pages
/// (exposed to QML through SettingsController's `decorationPage` Q_PROPERTY;
/// this class declares invokables + signals, no properties of its own).
///
/// ## Scope: PER-SURFACE chains with walk-up inheritance
///
/// Where the previous flat surface-shader page selected ONE decoration pack
/// for every window, this controller edits a
/// `PhosphorSurfaceShaders::DecorationProfileTree` — a hierarchical store
/// of `DecorationProfile`s keyed on the dot-path surface namespace
/// (`window.tiled`, `osd`, `popup.snapAssist`, …). Each profile carries an
/// ordered CHAIN of surface shader packs plus the border/titlebar
/// appearance. A baseline (global default) is overlaid by per-surface
/// overrides via the tree's resolve() walk-up.
///
/// ## Baseline as path ""
///
/// The QML side addresses the baseline (global default) with the empty
/// path "". Every mutator and reader special-cases "" to read/write the
/// tree's `baseline()` rather than a per-surface override. So
/// `resolvedProfile("")` == baseline; `setChain("", ...)` sets the
/// baseline chain; `clearOverride("")` is rejected (the baseline can't be
/// "inherited away").
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

public:
    /// @param registry Optional — when null, all `*ShaderEffects()` /
    ///        `shaderParameters()` Q_INVOKABLEs return empty results so
    ///        unit tests can construct the controller without a surface
    ///        bootstrap.
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

    /// Just the parameters list for @p effectId — convenience for the
    /// per-pack parameter editor.
    Q_INVOKABLE QVariantList shaderParameters(const QString& effectId) const;

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
    /// Every surface path whose DIRECT override's chain contains
    /// @p effectId, as {path, label} entries sorted by label — the
    /// browser's "Used in" chips.
    Q_INVOKABLE QVariantList shaderEffectUsages(const QString& effectId) const;

    // ── Decoration sets (the Motion Sets twin) ──────────────────────────
    // Named snapshots of the decoration profile tree, persisted as JSON
    // under ~/.local/share/plasmazones/decorationsets/<slug>.json.
    // Applying merges: the baseline (when the set captured one) and every
    // entry replace the DIRECT profile at their path; surfaces the set
    // does not cover keep their current overrides. All writes go through
    // ISettings::setDecorationProfileTree, so dirty / apply / discard ride
    // the normal staging flow.

    /// Saved sets as {name, description, slug, overrideCount} rows.
    Q_INVOKABLE QVariantList availableDecorationSets() const;
    Q_INVOKABLE bool applyDecorationSet(const QString& name);
    Q_INVOKABLE bool saveCurrentAsDecorationSet(const QString& name, const QString& description);
    Q_INVOKABLE bool removeDecorationSet(const QString& name);

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

    /// Emitted on any successful save/removeDecorationSet so the Sets page
    /// refreshes its Q_INVOKABLE-loaded list.
    void decorationSetsChanged();

private:
    QString userShaderDirectoryPath() const;
    QString decorationSetsDirectoryPath() const;
    QString decorationSetFilePath(const QString& setName) const;

    PhosphorSurfaceShaders::SurfaceShaderRegistry* m_registry = nullptr;
    ISettings* m_settings = nullptr;
};

} // namespace PlasmaZones
