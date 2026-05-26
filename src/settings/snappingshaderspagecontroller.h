// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorSettingsUi/PageController.h>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace PlasmaZones {
class ShaderRegistry;
}

namespace PhosphorZones {
class IZoneLayoutRegistry;
}

namespace PlasmaZones {

/// Q_PROPERTY surface for the "Snapping → Shaders" settings page.
///
/// Read-only browser over the snapping overlay shader registry (the
/// `data/shaders/` family — cosmic-flow, neon-city, etc.) — the
/// counterpart to @ref AnimationsPageController's animation-shader
/// surface. Drives the pack-agnostic `ShaderBrowserPage.qml` via a
/// duck-typed bridge contract: `availableShaderEffects`,
/// `installShaderPack`, `openUserShaderDirectory`, `shaderEffectUsages`,
/// plus a `shaderEffectsChanged` signal forwarded from the registry.
///
/// ## "Used in:" reverse lookup
///
/// Snapping overlay shaders are assigned to layouts (one shader per
/// `PhosphorZones::Layout::shaderId`). `shaderEffectUsages(id)` walks
/// the borrowed @ref PhosphorZones::IZoneLayoutRegistry and returns
/// `[{path, label}]` for every layout that references @p id.
///
/// ## Settings-side mirror
///
/// Mirrors the `AnimationShaderRegistry` setup pattern in
/// `SettingsController` — both processes scan the same XDG dirs
/// independently, FS watching keeps each in sync. The settings-side
/// registry instance is borrowed (constructor parameter); composition
/// is owned by `SettingsController`.
class SnappingShadersPageController : public PhosphorSettingsUi::PageController
{
    Q_OBJECT

public:
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

    /// @param shaderRegistry Borrowed; lifetime managed by the caller.
    ///        Pass nullptr to make every Q_INVOKABLE return an empty
    ///        result (useful for unit tests). Takes the PlasmaZones
    ///        subclass (rather than the bare @c PhosphorShaders base) so
    ///        the user-shader directory + open-folder helpers can be
    ///        forwarded straight through, keeping the on-disk path in a
    ///        single source of truth on the registry.
    /// @param layoutRegistry Borrowed; consulted by `shaderEffectUsages`.
    ///        Pass nullptr to disable usage lookup (returns empty).
    explicit SnappingShadersPageController(PlasmaZones::ShaderRegistry* shaderRegistry,
                                           PhosphorZones::IZoneLayoutRegistry* layoutRegistry,
                                           QObject* parent = nullptr);
    ~SnappingShadersPageController() override;

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

    /// Reverse-lookup: list every layout whose `shaderId` matches @p
    /// effectId. Each entry: `{path, label}` where `path` is the
    /// layout's UUID-with-braces and `label` is the layout's display
    /// name (matching the `{path,label}` shape the animations bridge
    /// returns). Sorted by label for deterministic UI order.
    Q_INVOKABLE QVariantList shaderEffectUsages(const QString& effectId) const;

Q_SIGNALS:
    /// Forwarded from `PhosphorShaders::ShaderRegistry::shadersChanged`
    /// so QML can rebind without poking at the registry directly.
    void shaderEffectsChanged();

    /// Emitted when a layout's `shaderId` changes — the browser's
    /// "Used in:" chips re-resolve on this tick. Forwarded from every
    /// `PhosphorZones::Layout::shaderIdChanged` signal in the registry.
    ///
    /// @p path carries the layout's UUID-with-braces when the emit
    /// originates from a per-layout signal (the canonical case).
    /// The fan-out path that fires on `ILayoutSourceRegistry::contentsChanged`
    /// emits with an EMPTY path — QML treats that as "any layout may
    /// have changed, re-resolve everything." Consumers that key off
    /// `path` MUST guard for the empty case and treat it as a full
    /// refresh trigger, not a no-op.
    void shaderProfileChanged(const QString& path);

private Q_SLOTS:
    /// Slot wired (with `Qt::UniqueConnection`) to every layout's
    /// `shaderIdChanged`. A free-function lambda would defeat
    /// `UniqueConnection` — Qt cannot dedupe functor connections — and
    /// each `contentsChanged` fan-out would accumulate duplicate edges,
    /// causing N-fold signal multiplication on real edits. Using a
    /// member-function pointer makes `UniqueConnection` actually
    /// idempotent. Recovers the layout via @c sender().
    void onLayoutShaderIdChanged();

private:
    /// User-writable XDG directory the overlay-shader registry watches.
    /// Forwards to `PlasmaZones::ShaderRegistry::userShaderDirectory()`
    /// so the settings + daemon stay on one source of truth (which the
    /// registry resolves via `ConfigDefaults::userOverlayShadersSubdir`).
    QString userShaderDirectoryPath() const;

    /// Wire up `shaderIdChanged` for every layout currently in the
    /// registry plus any added later. Each fire re-emits
    /// `shaderProfileChanged` so the QML usage chips re-evaluate.
    void connectLayoutSignals();

    PlasmaZones::ShaderRegistry* m_shaderRegistry = nullptr;
    PhosphorZones::IZoneLayoutRegistry* m_layoutRegistry = nullptr;
};

} // namespace PlasmaZones
