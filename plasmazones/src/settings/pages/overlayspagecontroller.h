// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorControl/PageController.h>

// Complete type, not a forward declaration: moc must see it to register the
// pointer meta-type for the setsBridge Q_PROPERTY.
#include "settings/stores/shadersetstore.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

namespace PlasmaZones {
class ISettings;
class OverlayShaderTree;
class ShaderRegistry;
class ShaderPreviewController;
}

namespace PhosphorZones {
class IZoneLayoutRegistry;
}

namespace PlasmaZones {

/// Q_PROPERTY surface for the "Appearance → Overlays" pages: the
/// assignment page (global default + per-layout overrides) and the
/// pack browser.
///
/// ## Scope: an OverlayShaderTree keyed on layout UUIDs
///
/// Overlay shader assignments live in the config as an
/// `OverlayShaderTree` (`ISettings::overlayShaderTree()`): one global
/// baseline plus per-layout overrides, each node a `{shaderId,
/// parameters}` pair. The resolve step is flat — a layout's override
/// wins, otherwise the baseline applies. Paths in this API are layout
/// UUIDs (with braces); the empty path "" addresses the baseline,
/// mirroring the DecorationPageController convention.
///
/// ## Dirty tracking
///
/// All mutators read the tree from `ISettings::overlayShaderTree()`,
/// mutate, and write back through `setOverlayShaderTree()`. That
/// setter's NOTIFY (`overlayShaderTreeChanged`) rides the
/// SettingsController meta-object dirty loop, so this controller
/// carries NO per-page staged state: `isDirty()` / `apply()` /
/// `discard()` are no-ops, exactly like `DecorationPageController`.
///
/// ## Browser bridge
///
/// Also implements the pack-agnostic `ShaderBrowserPage.qml` duck-typed
/// bridge contract (`availableShaderEffects`, `installShaderPack`,
/// `openUserShaderDirectory`, `shaderEffectUsages`,
/// `shaderEffectsChanged`) over the overlay registry (`data/overlays/`
/// family).
class OverlaysPageController : public PhosphorControl::PageController
{
    Q_OBJECT

    /// The overlay-set store, bound by OverlaySetsPage as its `bridge`.
    Q_PROPERTY(PlasmaZones::ShaderSetStore* setsBridge READ setsBridge CONSTANT)

    /// The shared zone-shader preview feed for this (zone/overlay) browser, or
    /// null. Present only on the zone-shader bridge — the animation bridge has
    /// no equivalent, so ShaderBrowserDetailDialog gates its live preview pane
    /// on `bridge.previewController` being set.
    Q_PROPERTY(QObject* previewController READ previewController CONSTANT)

public:
    // ── PhosphorControl::StagingDomain contract ───────────────────────────
    // No per-page staged state — mutations write straight to Settings and
    // the global SettingsController dirty loop tracks them (see class doc).
    // The inherited applyResult / discardResult still fire so the
    // framework's wait-counter ticks down.
    bool isDirty() const override
    {
        return false;
    }
    void apply() override
    {
        Q_EMIT applyResult(true, QString());
    }
    void discard() override
    {
        Q_EMIT discardResult(true, QString());
    }

    /// @param shaderRegistry Borrowed; lifetime managed by the caller.
    ///        Pass nullptr to make every Q_INVOKABLE return an empty
    ///        result (useful for unit tests). Takes the PlasmaZones
    ///        subclass (rather than the bare @c PhosphorShaders base) so
    ///        the user-shader directory + open-folder helpers can be
    ///        forwarded straight through, keeping the on-disk path in a
    ///        single source of truth on the registry.
    /// @param layoutRegistry Borrowed; drives layout enumeration for the
    ///        assignment cards and label resolution for usages. Pass
    ///        nullptr to disable both (returns empty).
    /// @param settings Borrowed; the assignment store. Pass nullptr to
    ///        make every mutator a no-op and every read return empty.
    explicit OverlaysPageController(PlasmaZones::ShaderRegistry* shaderRegistry,
                                    PhosphorZones::IZoneLayoutRegistry* layoutRegistry, ISettings* settings,
                                    ShaderPreviewController* previewController, QObject* parent = nullptr);
    ~OverlaysPageController() override;

    /// The borrowed live-preview controller (see the previewController property).
    QObject* previewController() const;

    ShaderSetStore* setsBridge() const
    {
        return m_sets;
    }

    /// Point the set store at @p dir instead of the XDG data location.
    ///
    /// Deliberately NOT Q_INVOKABLE — a test seam, not UI. ctest runs suites in
    /// parallel and they share one qttest data dir, so two set-owning suites
    /// would otherwise wipe each other's files mid-run.
    void setSetsDirOverride(const QString& dir);

    // ── Assignment surface ────────────────────────────────────────────────

    /// Every layout an override card can target: `{id, name}` rows from
    /// the layout registry, sorted by name (case-insensitively). `id` is
    /// the UUID-with-braces. Overrides whose layout no longer exists in
    /// the registry are appended as `{id, name: "", missing: true}` rows
    /// so a stale assignment stays visible and clearable.
    Q_INVOKABLE QVariantList assignableLayouts() const;

    /// True iff @p path (a layout UUID) carries a direct override.
    /// Always false for "" (the baseline is not an override).
    Q_INVOKABLE bool hasOverride(const QString& path) const;

    /// The DIRECT node at @p path as `{shaderId, parameters}` — the
    /// baseline for "", the layout's own override otherwise (empty map
    /// values when none).
    Q_INVOKABLE QVariantMap rawShaderProfile(const QString& path) const;

    /// The EFFECTIVE node at @p path: the layout's override when one
    /// exists, else the baseline. For "" this is the baseline itself.
    /// An empty shaderId in the result means "no shader".
    Q_INVOKABLE QVariantMap resolvedShaderProfile(const QString& path) const;

    /// The three reads above in one call: `{hasOverride, raw, resolved}`.
    ///
    /// A card needs all three together on every refresh, and each of the
    /// separate getters re-reads the config key and re-parses the whole tree,
    /// so calling them in sequence parsed it three times per card. This parses
    /// once, and it is what the QML actually calls.
    ///
    /// The three separate getters above have no QML caller. They are kept for
    /// the unit tests, which read each one on its own to pin what the three
    /// answers mean apart — an absent override, an override whose shaderId is
    /// empty, and the baseline showing through — and that is what makes this
    /// merged read checkable against them rather than self-consistent.
    Q_INVOKABLE QVariantMap nodeState(const QString& path) const;

    /// Engage @p effectId (with @p params) at @p path — the baseline for
    /// "", a per-layout override otherwise. An empty @p effectId on a
    /// layout path explicitly suppresses the baseline shader for that
    /// layout; on "" it clears the global default.
    Q_INVOKABLE void setShaderOverride(const QString& path, const QString& effectId, const QVariantMap& params);

    /// Point the node at @p path at preset @p presetId, leaving its pack and
    /// its own parameter edits alone. Those edits become DELTAS on top of the
    /// preset, so a value the user set stays set while everything they never
    /// touched follows the preset.
    ///
    /// An empty @p presetId clears the reference, which leaves the node
    /// rendering exactly the values it already holds.
    Q_INVOKABLE void setShaderPreset(const QString& path, const QString& presetId);

    /// Drop the override at @p path so the layout inherits the baseline
    /// again. Rejected for "" (clear the baseline via setShaderOverride
    /// with an empty effectId). @return true when an override was removed.
    Q_INVOKABLE bool clearOverride(const QString& path);

    /// Parameter declarations for @p effectId (the registry's
    /// ParameterInfo maps), for the assignment page's parameter editor.
    Q_INVOKABLE QVariantList shaderParameters(const QString& effectId) const;

    // ── Shader-browser bridge (ShaderBrowserPage contract) ────────────────

    /// Installed overlay shader packs flattened to a QML-friendly list.
    /// Each row carries the same shape as the animations bridge so
    /// `ShaderBrowserPage.qml` can consume both transparently — the
    /// registry's native `availableShadersVariant()` uses `isUserShader`,
    /// this method renames it to `isUserEffect` for parity.
    Q_INVOKABLE QVariantList availableShaderEffects() const;

    /// Open the user shader directory in the system file manager,
    /// creating it first if missing. Forwarded to the registry, which
    /// already owns the create-and-open primitive.
    Q_INVOKABLE void openUserShaderDirectory();

    /// Install a shader pack from a dropped folder. Mirrors
    /// `AnimationsPageController::installShaderPack` validation +
    /// recursive-copy semantics; only the destination directory differs
    /// (overlay shader subdir instead of the animation subdir).
    /// @return true on success.
    Q_INVOKABLE bool installShaderPack(const QString& sourceUrl);

    /// A readable label for one of a set's coverage tokens.
    ///
    /// The store derives coverage from each entry's path, which here is either
    /// the reserved global-default token or a layout UUID. A UUID means nothing
    /// on screen, and the QML page has no handle on the layout registry, so the
    /// mapping lives here: the global token names itself, a live layout gets
    /// its name, and a layout this machine does not have says so rather than
    /// printing 36 characters of hex.
    Q_INVOKABLE QString setCoverageLabel(const QString& token) const;

    /// How a layout this machine does not have is named on screen: the id's
    /// leading group, braces stripped, so two absent layouts are still
    /// distinguishable without printing all 36 characters. One implementation
    /// for every surface that has to render this state — the assignments page,
    /// the set coverage chip and the shader browser's usage list previously
    /// each had their own, and the two that computed the short id did it in
    /// two languages.
    Q_INVOKABLE QString absentLayoutLabel(const QString& layoutId) const;

    /// Reverse-lookup over the assignment tree: a "Global default" entry
    /// (empty path) when the baseline uses @p effectId, plus `{path,
    /// label}` for every layout whose override does — `path` the layout
    /// UUID-with-braces, `label` its display name resolved through the
    /// layout registry (falls back to the bare UUID for a stale entry).
    /// Sorted by label for deterministic UI order.
    Q_INVOKABLE QVariantList shaderEffectUsages(const QString& effectId) const;

Q_SIGNALS:
    /// Forwarded from `PhosphorShaders::ShaderRegistry::shadersChanged`
    /// so QML can rebind without poking at the registry directly.
    void shaderEffectsChanged();

    /// Emitted whenever the assignment tree mutates (any setter, a D-Bus
    /// write, a global reload — forwarded from
    /// `ISettings::overlayShaderTreeChanged`) and when the layout catalogue
    /// changes (add / remove / rename). Cards and the browser's "Used in:"
    /// chips re-resolve on this tick.
    ///
    /// @p wholeTree true means "any assignment may have changed" and every
    /// consumer must re-read; @p path is empty and carries no meaning. That is
    /// the honest answer for a write this controller did not make (a D-Bus
    /// write, a profile apply, a page reset), since the tree write does not
    /// say which node moved.
    ///
    /// @p wholeTree false means exactly one node changed, and @p path names
    /// it — "" for the baseline, a layout UUID otherwise. Only this
    /// controller's own setters can say that, so only they emit it.
    ///
    /// The distinction matters because a parameter edit commits per drag
    /// pause: without it every visible card re-ran its full refresh, and each
    /// of those re-parsed the whole tree and re-flattened the pack registry.
    /// A card filters on @p path, but must still refresh when the BASELINE
    /// changed and it has no override of its own, because that moves what it
    /// resolves to.
    void shaderProfileChanged(const QString& path, bool wholeTree);

    /// User-facing transient notification request. QML chrome wires
    /// this to `window.showToast()` so a failed shader-pack install
    /// surfaces the underlying reason instead of returning false
    /// silently. Mirrors the same-named signal on
    /// AnimationsPageController.
    void toastRequested(const QString& text);

private:
    /// User-writable XDG directory the overlay-shader registry watches.
    /// Forwards to `PlasmaZones::ShaderRegistry::userShaderDirectory()`
    /// so the settings + daemon stay on one source of truth (which the
    /// registry resolves via `ConfigDefaults::userOverlayShadersSubdir`).
    QString userShaderDirectoryPath() const;

    /// Layout display name for @p layoutId (UUID-with-braces), or an
    /// empty string when the registry has no such layout.
    QString layoutNameFor(const QString& layoutId) const;

    /// Write @p tree and announce it as a single-node change at @p path.
    ///
    /// The settings NOTIFY that carries the write back cannot say which node
    /// moved, so the two setters park the path here for the duration of their
    /// own write and the NOTIFY handler reports it. Set only across a write
    /// this controller makes; empty at every other time, which is what makes
    /// a foreign write announce itself as a whole-tree change.
    void writeTreeAnnouncing(const OverlayShaderTree& tree, const QString& path);

    std::optional<QString> m_writingPath;

    /// Build the overlay-set store and wire its live-state signal. Defined in
    /// the sibling overlayspagecontroller_sets.cpp with the domain closures.
    void initSetsStore();

    /// Directory the set files live in — the XDG data location, or
    /// m_setsDirOverride when a test set one.
    QString overlaySetsDirectoryPath() const;

    /// Whether @p effectId may be persisted into the tree. Bounds the length
    /// and refuses path-shaped ids and ids no installed pack answers to, the
    /// same gate AnimationsPageController applies. An empty id is the "None"
    /// sentinel and is always acceptable; membership is skipped while the
    /// registry is still empty so a pack mid-scan is not eaten.
    bool acceptableShaderEffectId(const QString& effectId) const;

    ShaderSetStore* m_sets = nullptr;
    QString m_setsDirOverride;

    PlasmaZones::ShaderRegistry* m_shaderRegistry = nullptr;
    PhosphorZones::IZoneLayoutRegistry* m_layoutRegistry = nullptr;
    ISettings* m_settings = nullptr;
    ShaderPreviewController* m_previewController = nullptr; // borrowed; owned by SettingsController
};

} // namespace PlasmaZones
