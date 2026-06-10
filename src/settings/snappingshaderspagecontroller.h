// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorControl/PageController.h>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace PlasmaZones {
class ShaderRegistry;
class ShaderPreviewController;
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
class SnappingShadersPageController : public PhosphorControl::PageController
{
    Q_OBJECT

    /// The shared zone-shader preview feed for this (zone/overlay) browser, or
    /// null. Present only on the zone-shader bridge — the animation bridge has
    /// no equivalent, so ShaderBrowserDetailDialog gates its live preview pane
    /// on `bridge.previewController` being set.
    Q_PROPERTY(QObject* previewController READ previewController CONSTANT)

public:
    bool isDirty() const override
    {
        return false;
    }
    // The read-only browser never owns staged edits, so apply()/discard()
    // are no-ops at the storage layer. We still emit the inherited
    // applyResult / discardResult so the framework's wait-counter ticks
    // down — without it, a future per-page dirty flag accidentally
    // wired here would deadlock the chrome footer ("0 of 1 pages
    // saved" with no signal in sight). Q_ASSERT documents that we
    // never expect a dirty-state caller to reach these bodies.
    void apply() override
    {
        Q_ASSERT(!isDirty());
        Q_EMIT applyResult(true, QString());
    }
    void discard() override
    {
        Q_ASSERT(!isDirty());
        Q_EMIT discardResult(true, QString());
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
                                           ShaderPreviewController* previewController, QObject* parent = nullptr);
    ~SnappingShadersPageController() override;

    /// The borrowed live-preview controller (see the previewController property).
    QObject* previewController() const;

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

    /// User-facing transient notification request. QML chrome wires
    /// this to `window.showToast()` so a failed shader-pack install
    /// surfaces the underlying reason instead of returning false
    /// silently. Mirrors the same-named signal on
    /// AnimationsPageController.
    void toastRequested(const QString& text);

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
    /// Layouts already in @c m_wiredLayouts are skipped to keep the
    /// per-refresh cost proportional to NEW layouts rather than the
    /// full registry size — Qt::UniqueConnection still guarantees
    /// idempotence on the rare path where the set drifts.
    void connectLayoutSignals();

    /// Evict an entry from @c m_wiredLayouts when its layout is
    /// destroyed (QObject::destroyed). Without this, the set retains
    /// stale dangling pointers, and the next reconnect would skip a
    /// reused address that happens to match an old entry.
    void onWiredLayoutDestroyed(QObject* layout);

    PlasmaZones::ShaderRegistry* m_shaderRegistry = nullptr;
    PhosphorZones::IZoneLayoutRegistry* m_layoutRegistry = nullptr;
    ShaderPreviewController* m_previewController = nullptr; // borrowed; owned by SettingsController
    /// Layouts already wired via @c connectLayoutSignals — tracked so
    /// the O(N) walk on every @c contentsChanged is replaced by an
    /// O(new) walk. Entries are evicted on the layout's destroyed()
    /// signal (see @c onWiredLayoutDestroyed). connectLayoutSignals
    /// itself only ever calls `disconnect()` on pointers it has
    /// already confirmed are present in the current live registry
    /// snapshot (`live` QSet), so the set never dereferences a
    /// dangling raw pointer.
    QSet<QObject*> m_wiredLayouts;
};

} // namespace PlasmaZones
