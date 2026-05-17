// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorScrollEngine/ScrollScreenState.h>
#include <phosphorscrollengine_export.h>

#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorEngine/PlacementEngineBase.h>

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
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
class PHOSPHORSCROLLENGINE_EXPORT ScrollEngine final : public PhosphorEngine::PlacementEngineBase
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

    // ── Tracking queries ────────────────────────────────────────────────
    bool isWindowTracked(const QString& windowId) const override;
    bool isWindowTiled(const QString& windowId) const override;
    bool isWindowManaged(const QString& windowId) const override;
    QString screenForTrackedWindow(const QString& windowId) const override;
    QStringList managedWindowOrder(const QString& screenId) const override;

    // ── Preset lists (defaults match niri; settings override later) ─────
    void setPresetColumnWidths(const QVector<qreal>& fractions);
    void setPresetWindowHeights(const QVector<qreal>& fractions);
    QVector<qreal> presetColumnWidths() const
    {
        return m_presetColumnWidths;
    }
    QVector<qreal> presetWindowHeights() const
    {
        return m_presetWindowHeights;
    }

    // ── State access ────────────────────────────────────────────────────
    PhosphorEngine::IPlacementState* stateForScreen(const QString& screenId) override;
    const PhosphorEngine::IPlacementState* stateForScreen(const QString& screenId) const override;

    // ── Persistence ─────────────────────────────────────────────────────
    void saveState() override;
    void loadState() override;
    QJsonObject serializeEngineState() const override;
    void deserializeEngineState(const QJsonObject& state) override;

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

    std::unordered_map<PhosphorEngine::TilingStateKey, ScrollScreenState, TilingStateKeyHash> m_states;
    QHash<QString, PhosphorEngine::TilingStateKey> m_windowToKey;
    QSet<QString> m_activeScreens;
    int m_currentDesktop = 1;
    QString m_currentActivity;
    QString m_activeScreen;
    QVector<qreal> m_presetColumnWidths;
    QVector<qreal> m_presetWindowHeights;
};

} // namespace PhosphorScrollEngine
