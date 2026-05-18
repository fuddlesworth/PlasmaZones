// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorScrollEngine/ScrollLayout.h>
#include <PhosphorScrollEngine/ScrollScreenState.h>
#include <phosphorscrollengine_export.h>

#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorEngine/IScrollNavigation.h>
#include <PhosphorEngine/PlacementEngineBase.h>

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>
#include <QVector>

#include <cstddef>
#include <unordered_map>

namespace PhosphorScrollEngine {

/// Hash adaptor so TilingStateKey can key a std::unordered_map. unordered_map
/// (unlike QHash) keeps element pointers stable across insert/erase — required
/// because stateForScreen() hands callers long-lived state pointers.
struct TilingStateKeyHash
{
    std::size_t operator()(const PhosphorEngine::TilingStateKey& key) const noexcept
    {
        return qHash(key);
    }
};

/// niri-style scrollable-tiling placement engine.
///
/// Owns one ScrollScreenState strip per (screen, desktop, activity) context
/// and translates the IPlacementEngine lifecycle/navigation contract onto the
/// strip model. It is KWin-agnostic: it mutates strip state and emits
/// `placementChanged` — resolving the strip to pixel geometry (via
/// resolveScrollLayout) and applying it is the daemon's job, since only the
/// daemon knows each screen's working area.
class PHOSPHORSCROLLENGINE_EXPORT ScrollEngine final : public PhosphorEngine::PlacementEngineBase,
                                                       public PhosphorEngine::IScrollNavigation
{
    Q_OBJECT

public:
    explicit ScrollEngine(QObject* parent = nullptr);
    ~ScrollEngine() override = default;

    QString engineId() const override
    {
        return QStringLiteral("scroll");
    }

    // ── Screen ownership ────────────────────────────────────────────────
    bool isActiveOnScreen(const QString& screenId) const override;
    QSet<QString> activeScreens() const override;
    void setActiveScreens(const QSet<QString>& screens) override;
    bool isEnabled() const noexcept override;
    QString activeScreen() const override;
    void setActiveScreenHint(const QString& screenId) override;

    // ── Desktop / activity context ──────────────────────────────────────
    void setCurrentDesktop(int desktop) override;
    void setCurrentActivity(const QString& activity) override;
    QSet<int> desktopsWithActiveState() const override;
    void pruneStatesForDesktop(int removedDesktop) override;
    void pruneStatesForActivities(const QStringList& validActivities) override;

    // ── Window lifecycle ────────────────────────────────────────────────
    using IPlacementEngine::windowOpened;
    void windowOpened(const QString& windowId, const QString& screenId, int minWidth, int minHeight) override;
    void windowClosed(const QString& windowId) override;
    void windowFocused(const QString& windowId, const QString& screenId) override;
    /// React to a tracked window being minimized or restored. A minimized
    /// window keeps its slot in the strip but drops out of the resolved
    /// layout (Karousel's TiledMinimized). Scroll-specific — minimize has no
    /// IPlacementEngine equivalent (autotile routes minimize through float).
    void windowMinimizedChanged(const QString& windowId, bool minimized);
    /// Reorder the column holding @p draggedWindowId next to the column
    /// holding @p anchorWindowId (before it, or after when @p placeAfter) and
    /// focus the dragged window. Drives drag-to-reorder ("reorder on drop");
    /// scroll-specific — has no IPlacementEngine equivalent.
    void windowDropped(const QString& draggedWindowId, const QString& anchorWindowId, bool placeAfter);

    // ── Float ───────────────────────────────────────────────────────────
    void toggleWindowFloat(const QString& windowId, const QString& screenId) override;
    void setWindowFloat(const QString& windowId, bool shouldFloat) override;

    // ── Navigation ──────────────────────────────────────────────────────
    void focusInDirection(const QString& direction, const PhosphorEngine::NavigationContext& ctx) override;
    void moveFocusedInDirection(const QString& direction, const PhosphorEngine::NavigationContext& ctx) override;
    void swapFocusedInDirection(const QString& direction, const PhosphorEngine::NavigationContext& ctx) override;
    void moveFocusedToPosition(int position, const PhosphorEngine::NavigationContext& ctx) override;
    void rotateWindows(bool clockwise, const PhosphorEngine::NavigationContext& ctx) override;
    void reapplyLayout(const PhosphorEngine::NavigationContext& ctx) override;
    void snapAllWindows(const PhosphorEngine::NavigationContext& ctx) override;
    void cycleFocus(bool forward, const PhosphorEngine::NavigationContext& ctx) override;
    void pushToEmptyZone(const PhosphorEngine::NavigationContext& ctx) override;
    void restoreFocusedWindow(const PhosphorEngine::NavigationContext& ctx) override;
    void toggleFocusedFloat(const PhosphorEngine::NavigationContext& ctx) override;

    // ── niri scrollable-tiling operations ───────────────────────────────
    void consumeWindowIntoColumn(const PhosphorEngine::NavigationContext& ctx) override;
    void expelWindowFromColumn(const PhosphorEngine::NavigationContext& ctx) override;
    void cyclePresetColumnWidth(const PhosphorEngine::NavigationContext& ctx) override;
    void cyclePresetWindowHeight(const PhosphorEngine::NavigationContext& ctx) override;
    void toggleColumnFullWidth(const PhosphorEngine::NavigationContext& ctx) override;
    void adjustColumnWidth(qreal deltaFraction, const PhosphorEngine::NavigationContext& ctx) override;

    // ── Tracking queries ────────────────────────────────────────────────
    bool isWindowTracked(const QString& windowId) const override;
    bool isWindowTiled(const QString& windowId) const override;
    bool isWindowManaged(const QString& windowId) const override;
    QString screenForTrackedWindow(const QString& windowId) const override;
    QStringList managedWindowOrder(const QString& screenId) const override;

    // ── Preset lists (defaults match niri; settings override later) ─────
    void setPresetColumnWidths(const QVector<qreal>& fractions);
    void setPresetWindowHeights(const QVector<qreal>& fractions);
    /// Coerce a QVariantList of numbers (e.g. a persisted preset list) into the
    /// engine's typed fraction vector. Shared with the daemon, which pushes the
    /// settings-backed preset lists through setPreset*().
    static QVector<qreal> toFractionVector(const QVariantList& list);
    QVector<qreal> presetColumnWidths() const
    {
        return m_presetColumnWidths;
    }
    QVector<qreal> presetWindowHeights() const
    {
        return m_presetWindowHeights;
    }

    // ── Default column width ────────────────────────────────────────────
    /// Width a freshly-opened column is created with — a fraction [0..1] of
    /// the working area. Pushed from settings by the daemon; defaults to
    /// niri's middle preset (one half) until then.
    void setDefaultColumnWidth(qreal fraction);
    qreal defaultColumnWidth() const
    {
        return m_defaultColumnWidth;
    }

    // ── Viewport mode ───────────────────────────────────────────────────
    /// How the viewport scrolls to keep the focused column on-screen. The
    /// daemon reads this when resolving the strip. Engine-global runtime
    /// state — not serialized; see serializeEngineState().
    ScrollViewportMode viewportMode() const
    {
        return m_viewportMode;
    }
    void setViewportMode(ScrollViewportMode mode)
    {
        m_viewportMode = mode;
    }

    // ── Global gap config ───────────────────────────────────────────────
    /// Strip gaps in logical pixels — the daemon pushes these from settings.
    /// The engine is geometry-agnostic and never uses them itself; it holds
    /// them only so effectiveInnerGap()/effectiveOuterGap() can resolve a
    /// per-screen override over the global value for the daemon's resolver.
    void setInnerGap(int gap)
    {
        m_innerGap = gap;
    }
    void setOuterGap(int gap)
    {
        m_outerGap = gap;
    }

    // ── Per-screen config overrides ─────────────────────────────────────
    /// Apply a per-screen override map (screen-only key, mirroring autotile's
    /// applyPerScreenConfig). Recognised keys — unrecognised ones are ignored:
    ///   "InnerGap" / "OuterGap"            int, logical px
    ///   "DefaultColumnWidth"               qreal, fraction [0..1]
    ///   "CenterFocusedColumn"              bool (true → Centered viewport)
    ///   "PresetColumnWidths" / "PresetWindowHeights"
    ///                                      QVariantList of qreal fractions
    /// An empty @p overrides clears the screen's overrides. The effective*()
    /// accessors resolve a screen's value as override → global default.
    void applyPerScreenConfig(const QString& screenId, const QVariantMap& overrides) override;
    void clearPerScreenConfig(const QString& screenId) override;
    QVariantMap perScreenOverrides(const QString& screenId) const override;

    // ── Effective config (per-screen override → global default) ─────────
    QVector<qreal> effectivePresetColumnWidths(const QString& screenId) const;
    QVector<qreal> effectivePresetWindowHeights(const QString& screenId) const;
    qreal effectiveDefaultColumnWidth(const QString& screenId) const;
    ScrollViewportMode effectiveViewportMode(const QString& screenId) const;
    int effectiveInnerGap(const QString& screenId) const;
    int effectiveOuterGap(const QString& screenId) const;

    // ── State access ────────────────────────────────────────────────────
    PhosphorEngine::IPlacementState* stateForScreen(const QString& screenId) override;
    const PhosphorEngine::IPlacementState* stateForScreen(const QString& screenId) const override;

    // ── Persistence ─────────────────────────────────────────────────────
    void saveState() override;
    void loadState() override;
    QJsonObject serializeEngineState() const override;
    void deserializeEngineState(const QJsonObject& state) override;

    /// Reconcile a freshly restored strip against the live window set.
    ///
    /// deserializeEngineState() rebuilds the strip structurally from disk, so a
    /// window closed while the daemon was down would survive as a phantom
    /// column. The daemon calls this once, with the complete window set from
    /// the effect's initial windowsOpenedBatch after a restore: any restored
    /// window absent from @p liveWindowIds never came back and is pruned (its
    /// column dropped when it empties). This makes the persisted strip a hint
    /// over the live set rather than authoritative — mirroring autotile, whose
    /// restored window order is likewise only ever reconciled against the
    /// windows the effect actually reports. A no-op unless a restore is
    /// pending, so the routine windowsOpenedBatch path stays unaffected.
    void reconcileRestoredWindows(const QSet<QString>& liveWindowIds);

protected:
    void onWindowClaimed(const QString& windowId) override;
    void onWindowReleased(const QString& windowId) override;
    void onWindowFloated(const QString& windowId) override;
    void onWindowUnfloated(const QString& windowId) override;

private:
    PhosphorEngine::TilingStateKey keyForScreen(const QString& screenId) const;
    ScrollScreenState* stateForKey(const PhosphorEngine::TilingStateKey& key, bool create);
    const ScrollScreenState* stateForWindowConst(const QString& windowId) const;
    /// Resolve which strip a navigation intent acts on, from the context's
    /// screen (or the last active screen). Returns nullptr if none.
    ScrollScreenState* resolveNavTarget(const PhosphorEngine::NavigationContext& ctx, QString* outScreenId);
    void emitChanged(const QString& screenId);
    void reportNav(bool success, const QString& action, const QString& screenId);
    /// Per-screen override for @p key, or an invalid QVariant when the screen
    /// has no override map or the map lacks @p key.
    QVariant perScreenValue(const QString& screenId, QLatin1String key) const;
    /// toFractionVector() with each entry clamped into [kMinSizeFraction,
    /// kMaxSizeFraction]; used by the effectivePreset*() resolvers to
    /// range-check a per-screen override list.
    static QVector<qreal> clampedFractionVector(const QVariantList& list);

    std::unordered_map<PhosphorEngine::TilingStateKey, ScrollScreenState, TilingStateKeyHash> m_states;
    QHash<QString, PhosphorEngine::TilingStateKey> m_windowToKey;
    QSet<QString> m_activeScreens;
    int m_currentDesktop = 1;
    QString m_currentActivity;
    QString m_activeScreen;
    QVector<qreal> m_presetColumnWidths;
    QVector<qreal> m_presetWindowHeights;
    qreal m_defaultColumnWidth = kDefaultColumnWidthFraction;
    int m_innerGap = 8;
    int m_outerGap = 8;
    ScrollViewportMode m_viewportMode = ScrollViewportMode::Fit;
    /// Per-screen config overrides, keyed by screenId (screen-only, like
    /// autotile). Resolved over the globals above by the effective*() helpers.
    QHash<QString, QVariantMap> m_perScreenConfig;
    /// True from a non-empty deserializeEngineState() until the first
    /// reconcileRestoredWindows() prunes the restored strip against the live
    /// window set. One-shot — see reconcileRestoredWindows().
    bool m_pendingRestoreReconcile = false;
};

} // namespace PhosphorScrollEngine
