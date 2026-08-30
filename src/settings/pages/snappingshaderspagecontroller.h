// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorControl/PageController.h>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace PlasmaZones {
class ISettings;
class ShaderRegistry;
class ShaderPreviewController;
}

namespace PhosphorZones {
class IZoneLayoutRegistry;
}

namespace PlasmaZones {

/// Q_PROPERTY surface for the "Snapping → Overlay Shaders" pages: the
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
class SnappingShadersPageController : public PhosphorControl::PageController
{
    Q_OBJECT

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
    explicit SnappingShadersPageController(PlasmaZones::ShaderRegistry* shaderRegistry,
                                           PhosphorZones::IZoneLayoutRegistry* layoutRegistry, ISettings* settings,
                                           ShaderPreviewController* previewController, QObject* parent = nullptr);
    ~SnappingShadersPageController() override;

    /// The borrowed live-preview controller (see the previewController property).
    QObject* previewController() const;

    // ── Assignment surface ────────────────────────────────────────────────

    /// Every layout an override card can target: `{id, name}` rows from
    /// the layout registry, sorted by name. `id` is the UUID-with-braces.
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

    /// Engage @p effectId (with @p params) at @p path — the baseline for
    /// "", a per-layout override otherwise. An empty @p effectId on a
    /// layout path explicitly suppresses the baseline shader for that
    /// layout; on "" it clears the global default.
    Q_INVOKABLE void setShaderOverride(const QString& path, const QString& effectId, const QVariantMap& params);

    /// Replace just the parameters of the node at @p path, keeping its
    /// shader. No-op when the path has no direct node ("" always has one).
    Q_INVOKABLE void setShaderParameters(const QString& path, const QVariantMap& params);

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
    /// `ISettings::overlayShaderTreeChanged`) and when the layout
    /// catalogue changes (add / remove / rename). Cards and the
    /// browser's "Used in:" chips re-resolve on this tick. Always fires
    /// with an EMPTY path ("any assignment may have changed"); consumers
    /// treat it as a full refresh trigger.
    void shaderProfileChanged(const QString& path);

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

    PlasmaZones::ShaderRegistry* m_shaderRegistry = nullptr;
    PhosphorZones::IZoneLayoutRegistry* m_layoutRegistry = nullptr;
    ISettings* m_settings = nullptr;
    ShaderPreviewController* m_previewController = nullptr; // borrowed; owned by SettingsController
};

} // namespace PlasmaZones
