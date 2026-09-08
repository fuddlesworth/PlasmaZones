// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Not forward-declared: moc needs the complete type to register the
// ShaderSetStore* Q_PROPERTY below as a pointer meta-type.
#include "settings/stores/shadersetstore.h"

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

namespace PhosphorPointerShaders {
class PointerShaderRegistry;
}

namespace PlasmaZones {

class DecorationPreviewController;
class ISettings;
class PointerPreviewController;

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
/// category root cards ("window", "osd", "popup", "shell") instead. Decoration SETS
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

    /// Live-preview data source for the shader browser's detail dialog. The
    /// pack-agnostic ShaderBrowserDetailDialog reads `bridge.previewController`
    /// and shows a preview pane when it is non-null — all three shipped
    /// browsers (zone/overlay, decoration, animation) expose one, and a
    /// bridge that leaves it null degrades to the read-only parameter list.
    /// Typed as QObject* because the dialog is
    /// shared with the zone/overlay browser, whose controller is an unrelated
    /// class; `previewKind` below is what tells the dialog which pane to load.
    Q_PROPERTY(QObject* previewController READ previewController CONSTANT)

    /// Which preview pane the detail dialog should load for this bridge.
    /// "decoration" here; the animations bridge declares "animation".
    ///
    /// The zone/overlay bridge exposes a previewController and no
    /// previewKind, so it reads as undefined; the dialog's own fallback is
    /// what maps that to "zone", and a bridge with neither to an empty
    /// string and no pane. Adding the property to another bridge is
    /// therefore a change to that fallback, not a convention it already
    /// follows.
    Q_PROPERTY(QString previewKind READ previewKind CONSTANT)

    /// Live-preview data source for the `pointer` surface. The pointer packs
    /// render through a different host from the surface family (a cursor
    /// driven over a wallpaper, not a stand-in window card), so the pointer
    /// card and the browser's pointer rows take this one instead of
    /// `previewController`. Typed as QObject* for the same reason as its
    /// sibling above.
    Q_PROPERTY(QObject* pointerPreviewController READ pointerPreviewController CONSTANT)

public:
    /// @param registry Optional — when null, the `*ShaderEffects()`
    ///        Q_INVOKABLEs return empty results so unit tests can construct
    ///        the controller without a surface bootstrap.
    /// @param settings Optional — when null the controller reads an empty
    ///        profile tree and every mutator is a no-op. Note that "empty tree"
    ///        is not the same as "empty result": a RESOLVED read still returns a
    ///        fully populated map, because resolution fills the library defaults
    ///        in. Only the raw reads come back empty.
    /// @param pointerRegistry Optional — the pointer-pack registry backing the
    ///        `pointer` surface. When null the pointer surface simply offers no
    ///        packs, exactly as a null @p registry leaves the surface family
    ///        empty.
    explicit DecorationPageController(PhosphorSurfaceShaders::SurfaceShaderRegistry* registry = nullptr,
                                      ISettings* settings = nullptr,
                                      PhosphorPointerShaders::PointerShaderRegistry* pointerRegistry = nullptr,
                                      QObject* parent = nullptr);
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
    /// Each row, in the order effectToMap inserts them: id / name /
    /// description / author / version / category / isUserEffect /
    /// providesBorder / providesOpacityTint / parameters (a
    /// QVariantList of ParameterInfo maps).
    /// Rows carry two extra keys the per-surface list above does not need:
    /// `type` ("surface" or "pointer") and `appliesTo` (a one-element list
    /// holding that same token). The browser's existing type axis reads
    /// `appliesTo`, so tagging the rows is all it takes for the Type filter,
    /// group-by and card badge to appear once both families are present.
    Q_INVOKABLE QVariantList availableShaderEffects() const;

    /// The packs a chain at @p path may actually use: pointer packs for the
    /// `pointer` surface, surface packs everywhere else. The chain editor
    /// binds this rather than availableShaderEffects, so a cursor chain never
    /// offers a window pack (which would render nothing through the pointer
    /// pass) and a window chain never offers a cursor pack. Mirrors
    /// AnimationsPageController::availableShaderEffectsForPath.
    Q_INVOKABLE QVariantList availableShaderEffectsForPath(const QString& path) const;

    /// Which preview pane suits @p effectId: "pointer" when the pointer
    /// registry owns the id, else "decoration". The shared browser detail
    /// dialog calls this per selected pack, because one bridge now serves two
    /// families and the CONSTANT `previewKind` property can only name one.
    Q_INVOKABLE QString previewKindFor(const QString& effectId) const;

    /// The preview controller matching previewKindFor(@p effectId).
    Q_INVOKABLE QObject* previewControllerFor(const QString& effectId) const;

    // ── Surface taxonomy ──────────────────────────────────────────────────

    /// Inheritance chain from @p path up to (but excluding) the empty
    /// baseline. e.g. `parentChain("window.tiled")` == `["window.tiled",
    /// "window"]`. Drives the "inheriting from" breadcrumb.
    Q_INVOKABLE QStringList parentChain(const QString& path) const;

    /// Forwards PhosphorSurfaceShaders::decorationPathIsBaselineIsolated so
    /// the QML cards read the one SSOT instead of hand-mirroring the shell
    /// path prefixes. True for a path whose subtree never inherits the tree
    /// baseline (the `shell` family).
    Q_INVOKABLE bool isBaselineIsolated(const QString& path) const;

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
    /// setChain). At a SEEDED path (the shipped card chrome for the OSD and the
    /// PopupFrame popups) a bare clear would be undone by the read-side seed
    /// overlay, so this engages an explicit empty chain there instead and the
    /// surface ends up genuinely undecorated. @return true when the override
    /// was removed or replaced by that marker.
    Q_INVOKABLE bool clearOverride(const QString& path);

    /// True when @p path carries the "explicitly undecorated" marker
    /// clearOverride leaves on a seeded surface: a direct override whose chain
    /// is engaged and EMPTY, at a path the read-side seed overlay would
    /// otherwise inject into. The card reads it as OFF: an override exists,
    /// but it means "draws nothing", not "the user has a look here". An
    /// engaged empty chain at an UNSEEDED path is not this — there it is the
    /// documented way for a leaf to disable an ancestor's chain, a real user
    /// look, and it keeps reading as ON.
    Q_INVOKABLE bool isExplicitlyUndecorated(const QString& path) const;

    /// Remove that marker so @p path inherits (and the seed chain flows in)
    /// again. No-op unless isExplicitlyUndecorated(@p path). @return true when
    /// the marker was removed.
    Q_INVOKABLE bool clearUndecorated(const QString& path);

    /// Number of descendant surfaces under @p path that carry their own
    /// override — they SHADOW this parent node (the resolve walk stops at the
    /// descendant's own profile, so the parent's chain never reaches them).
    /// Drives the parent-node "Clear shadowing children" affordance. The same
    /// affordance as AnimationsPageController::shaderOverrideDescendantCount,
    /// but NOT the same rule, and the difference is deliberate on that side
    /// rather than an omission on this one: the animations walk skips
    /// leaf-isolated paths (which never consult an ancestor) and params-only
    /// overrides (which ride the ancestor's pack rather than pinning one).
    /// Neither exclusion has an analogue here — every decoration surface
    /// resolves through its ancestors, and this tree's `chain` is the pack
    /// choice itself — so this counts every descendant override the USER made.
    /// The one thing it skips is an untouched seed injection: the tree read
    /// here carries the shipped card chrome as an override at its seed paths,
    /// and counting those would warn about shadowing on a config nobody has
    /// edited, with a Clear action the next read undoes. A seeded descendant
    /// the user has actually edited counts.
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
    /// are not staged at all, so no Discard
    /// can undo a set write here. Saving over an existing set requires explicit
    /// consent regardless of domain (see ShaderSetStore::saveCurrentAsSet). The
    /// domain closures live in decorationpagecontroller_sets.cpp.
    ShaderSetStore* setsBridge() const
    {
        return m_sets;
    }

    QObject* previewController() const;

    QObject* pointerPreviewController() const;

    QString previewKind() const;

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
    PhosphorPointerShaders::PointerShaderRegistry* m_pointerRegistry = nullptr;
    ISettings* m_settings = nullptr;

    /// Owned via QObject parenting (this). Constructed eagerly: it is cheap
    /// until the pane actually asks for a chain, and CONSTANT Q_PROPERTYs must
    /// not change identity after first read.
    DecorationPreviewController* m_preview = nullptr;

    /// Same ownership and eager-construction rules as m_preview — it backs a
    /// CONSTANT Q_PROPERTY too.
    PointerPreviewController* m_pointerPreview = nullptr;

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
