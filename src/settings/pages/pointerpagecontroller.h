// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Not forward-declared: moc needs the complete type to register the
// ShaderSetStore* Q_PROPERTY below as a pointer meta-type.
#include "settings/stores/shadersetstore.h"

#include <PhosphorControl/PageController.h>

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

namespace PhosphorPointerShaders {
class PointerShaderRegistry;
}

namespace PlasmaZones {

class ISettings;
class PointerPreviewController;

/// Q_INVOKABLE surface for the "Pointer" settings pages (exposed to QML
/// through SettingsController's `pointerPage` Q_PROPERTY).
///
/// ## Scope: ONE flat chain, its own config domain
///
/// A pointer pack decorates the cursor, not a surface, so there is no path
/// taxonomy and no walk-up inheritance to resolve: the user has exactly one
/// ordered chain of packs, plus a master switch. That chain persists as the
/// `Pointer.Chain` JSON blob and the switch as `Pointer.Enabled`, both owned
/// by this page through the ordinary pageOwnedConfigKeys manifest. The page is
/// deliberately NOT part of the decoration domain (isDecorationPage), because
/// riding that domain would make its Reset and Discard act on the whole
/// DecorationProfileTree — every window, OSD and popup override — for a user
/// who only wanted to undo a cursor trail.
///
/// ## Layers are addressed by INDEX
///
/// Unlike the decoration chain, which is keyed by pack id, a pointer chain may
/// legitimately stack the same pack twice (two trails at different widths and
/// colours is a real look), so a pack id is not a key here. Every mutator
/// takes the layer's position in the list instead, and an out-of-range index
/// is a no-op rather than an error.
///
/// ## Dirty tracking
///
/// All mutators read the profile from `ISettings::pointerChain()`, mutate, and
/// write it back through `setPointerChain()`. That setter's NOTIFY
/// (`pointerChainChanged`) is routed by `SettingsController`'s meta-object
/// loop into the active page's dirty flag, so this controller carries NO
/// per-page staged state: `isDirty()` / `apply()` / `discard()` are no-ops,
/// exactly like DecorationPageController. Apply / Discard / Defaults are
/// driven globally by `SettingsController`.
class PointerPageController : public PhosphorControl::PageController
{
    Q_OBJECT

    /// The master switch, `Pointer.Enabled`. A read/write property rather than
    /// an invokable pair so the page's header toggle is an ordinary binding.
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)

    /// Live-preview data source for the shader browser's detail dialog. Typed
    /// as QObject* because the dialog is shared with browsers whose
    /// controllers are unrelated classes; `previewKind` is what tells it which
    /// pane to load.
    Q_PROPERTY(QObject* previewController READ previewController CONSTANT)

    /// Which preview pane the detail dialog should load for this bridge.
    Q_PROPERTY(QString previewKind READ previewKind CONSTANT)

    /// The pointer-set store — the `bridge` ShaderSetsPage binds to.
    Q_PROPERTY(PlasmaZones::ShaderSetStore* setsBridge READ setsBridge CONSTANT)

public:
    /// @param registry Optional — when null, `availableShaderEffects()` and
    ///        `shaderParameters()` return empty results so unit tests can
    ///        construct the controller without a pointer bootstrap.
    /// @param settings Optional — when null the controller reads an empty
    ///        chain and every mutator is a no-op.
    explicit PointerPageController(PhosphorPointerShaders::PointerShaderRegistry* registry = nullptr,
                                   ISettings* settings = nullptr, QObject* parent = nullptr);
    ~PointerPageController() override;

    // ── PhosphorControl::StagingDomain contract ───────────────────────────
    // No per-page staged state — mutations write straight to Settings and the
    // global SettingsController dirty loop tracks them (see class doc).
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

    bool enabled() const;
    void setEnabled(bool value);

    // ── Available packs ───────────────────────────────────────────────────

    /// Installed `PointerShaderEffect`s flattened to a QML-friendly list, in
    /// the shape ShaderBrowserPage's card grid consumes: id / name /
    /// description / author / version / category / isUserEffect / previewPath
    /// / layer / parameters (a QVariantList of ParameterInfo maps).
    ///
    /// Pointer packs declare no `appliesTo`, so the browser's type axis stays
    /// dormant (`_hasTypeAxis` false) and the catalogue reads as a flat set of
    /// categories, the same way the decoration browser does.
    Q_INVOKABLE QVariantList availableShaderEffects() const;

    /// Parameter schema rows for one pack, in the shape ParameterEditor
    /// consumes (id / name / type / description / group / default / min / max
    /// / step). Empty list for an unknown pack.
    Q_INVOKABLE QVariantList shaderParameters(const QString& effectId) const;

    // ── Chain readers ─────────────────────────────────────────────────────

    /// The user's chain, one map per layer in paint order: `effectId`, `name`
    /// (the pack's display name, falling back to the id for a pack that is not
    /// installed), `enabled`, `parameters` (the layer's friendly overrides
    /// merged over the pack's declared defaults, so the editor never renders a
    /// blank control for a parameter the user has not touched) and `missing`
    /// (true when the referenced pack is not installed, which the card shows
    /// as a warning rather than silently dropping the layer).
    Q_INVOKABLE QVariantList chain() const;

    // ── Chain mutators ────────────────────────────────────────────────────

    /// Replace the whole chain. Each entry is a map with the `effectId`,
    /// `enabled` and `parameters` keys `chain()` returns; entries with an
    /// empty effectId are dropped.
    Q_INVOKABLE void setChain(const QVariantList& layers);

    /// Append a layer for @p effectId, enabled, with no parameter overrides
    /// (the pack's declared defaults apply). No-op for an empty id.
    Q_INVOKABLE void addLayer(const QString& effectId);

    /// Drop the layer at @p index.
    Q_INVOKABLE void removeLayer(int index);

    /// Move the layer at @p from to position @p to, shifting the rest. The
    /// card's up / down buttons are the only callers, so @p to is always an
    /// adjacent slot, but the general move is what the list model wants.
    Q_INVOKABLE void moveLayer(int from, int to);

    /// Toggle one layer without touching its order or its parameters.
    Q_INVOKABLE void setLayerEnabled(int index, bool enabled);

    /// Set one friendly parameter override on the layer at @p index.
    Q_INVOKABLE void setLayerParam(int index, const QString& paramId, const QVariant& value);

    /// Merge a whole map of friendly parameter overrides onto the layer at
    /// @p index in one write. The editor's Randomize action is the caller, so
    /// a single roll persists as one chain write instead of one per parameter.
    Q_INVOKABLE void setLayerParams(int index, const QVariantMap& params);

    /// Drop every parameter override on the layer at @p index so the pack's
    /// declared defaults apply again.
    Q_INVOKABLE void resetLayerParams(int index);

    // ── Shader-browser bridge (ShaderBrowserPage contract) ────────────────

    /// Copy a shader-pack folder into the user pointer-pack directory
    /// (~/.local/share/plasmazones/pointer) via the shared ShaderPackInstaller.
    /// The registry's file watcher rescans on its own.
    Q_INVOKABLE bool installShaderPack(const QString& sourceUrl);

    /// Open (creating if needed) the user pointer-pack directory in the file
    /// manager.
    Q_INVOKABLE void openUserShaderDirectory();

    /// Every chain layer that uses @p effectId, as {path, label} entries — the
    /// browser's "Used in" chips. There is one chain, so `path` is the layer's
    /// index as a string and the label names its position, which is what makes
    /// two layers of the same pack distinguishable in the list.
    Q_INVOKABLE QVariantList shaderEffectUsages(const QString& effectId) const;

    QObject* previewController() const;

    QString previewKind() const;

    /// The pointer-set store — the `bridge` ShaderSetsPage binds to. Named
    /// snapshots of the pointer chain, persisted as JSON under
    /// ~/.local/share/plasmazones/pointersets/<slug>.json. Applying REPLACES
    /// the whole chain: there is only one value here, so there is nothing to
    /// merge into. The write goes through ISettings::setPointerChain, so it
    /// rides the normal dirty / apply / discard staging flow. The set FILES
    /// themselves are not staged, so no Discard undoes a set write. Saving
    /// over an existing set requires explicit consent (see
    /// ShaderSetStore::saveCurrentAsSet). The domain closures live in
    /// pointerpagecontroller_sets.cpp.
    ShaderSetStore* setsBridge() const
    {
        return m_sets;
    }

    /// Test hook: redirect the sets directory to @p dir instead of the XDG
    /// default. Pass an empty string to restore the default. Mirrors
    /// DecorationPageController::setSetsDirOverride, and exists for the same
    /// reason: two test binaries resolving the same per-user qttest path wipe
    /// each other's files under parallel ctest. Not Q_INVOKABLE — QML callers
    /// must not redirect persistence.
    void setSetsDirOverride(const QString& dir);

Q_SIGNALS:
    /// Re-emit of `PointerShaderRegistry::effectsChanged` so QML can rebind
    /// without poking at the registry directly.
    void shaderEffectsChanged();

    /// Emitted whenever the chain mutates (any setter) AND on a full settings
    /// reload (`ISettings::pointerChainChanged`). The page listens to this to
    /// imperatively refresh from the controller.
    void chainChanged();

    void enabledChanged();

    /// Chrome-toast requests from the browser bridge (install / directory
    /// failures with the concrete reason). Routed by ShaderBrowserPage.
    void toastRequested(const QString& text);

private:
    /// Construct m_sets with the pointer domain closures. Called from the
    /// constructor; defined in pointerpagecontroller_sets.cpp.
    void initSetsStore();

    QString userShaderDirectoryPath() const;
    QString pointerSetsDirectoryPath() const;

    PhosphorPointerShaders::PointerShaderRegistry* m_registry = nullptr;
    ISettings* m_settings = nullptr;

    /// Owned via QObject parenting (this). Constructed eagerly: it is cheap
    /// until a pane actually asks for a pack, and CONSTANT Q_PROPERTYs must not
    /// change identity after first read.
    PointerPreviewController* m_preview = nullptr;

    QString m_setsDirOverride; ///< Empty = use the XDG default

    ShaderSetStore* m_sets = nullptr;
};

} // namespace PlasmaZones
