// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngineApi/IPlacementState.h>
#include <PhosphorEngineApi/NavigationContext.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QPoint>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <functional>

namespace PlasmaZones {
class ISettings;
class WindowRegistry;
} // namespace PlasmaZones

namespace PhosphorEngineApi {

/// Unified placement engine interface.
///
/// Both snap-mode (manual zone layouts) and autotile-mode (automatic
/// tiling algorithms) implement this so the daemon can dispatch all
/// window lifecycle events and user navigation intents through a single
/// polymorphic call — zero mode branches.
///
/// Each method represents a USER INTENT, not a mode-specific
/// implementation step. "Move focused window left" has different internal
/// meaning in tile-swap mode vs. zone-snap mode, but the user's request
/// is the same — the interface names the request and each engine fulfills
/// it in its own terms.
///
/// All methods are idempotent with respect to "no focused window" — each
/// implementation emits navigation feedback with a sensible reason code
/// when there's nothing to act on, rather than erroring out.
class IPlacementEngine
{
public:
    virtual ~IPlacementEngine() = default;

    // ═══════════════════════════════════════════════════════════════════════════
    // Screen ownership
    // ═══════════════════════════════════════════════════════════════════════════

    /// Whether this engine is active on the given screen.
    virtual bool isActiveOnScreen(const QString& screenId) const = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Window lifecycle
    // ═══════════════════════════════════════════════════════════════════════════

    /// A new window appeared on this engine's screen.
    virtual void windowOpened(const QString& windowId, const QString& screenId, int minWidth = 0,
                              int minHeight = 0) = 0;

    /// Convenience overload — equivalent to windowOpened(id, screen, 0, 0).
    void windowOpened(const QString& windowId, const QString& screenId)
    {
        windowOpened(windowId, screenId, 0, 0);
    }

    /// A window was closed.
    virtual void windowClosed(const QString& windowId) = 0;

    /// A window gained focus.
    virtual void windowFocused(const QString& windowId, const QString& screenId) = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Float management (explicit window ID)
    //
    // These take a concrete window ID — used by the D-Bus adaptor and
    // engine-internal paths that already know which window to act on.
    // toggleFocusedFloat() in the Navigation section is the user-intent
    // entry point that resolves the focused window from NavigationContext.
    // ═══════════════════════════════════════════════════════════════════════════

    /// Toggle between managed and floating.
    virtual void toggleWindowFloat(const QString& windowId, const QString& screenId) = 0;

    /// Set floating state explicitly (directional, not toggle).
    virtual void setWindowFloat(const QString& windowId, bool shouldFloat) = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Navigation (user intents)
    // ═══════════════════════════════════════════════════════════════════════════

    /// Move keyboard focus to the adjacent window.
    virtual void focusInDirection(const QString& direction, const NavigationContext& ctx) = 0;

    /// Move the focused window to the adjacent slot.
    virtual void moveFocusedInDirection(const QString& direction, const NavigationContext& ctx) = 0;

    /// Swap the focused window with the adjacent window.
    virtual void swapFocusedInDirection(const QString& direction, const NavigationContext& ctx) = 0;

    /// Move the focused window to the Nth position.
    virtual void moveFocusedToPosition(int position, const NavigationContext& ctx) = 0;

    /// Rotate all managed windows on the screen.
    virtual void rotateWindows(bool clockwise, const NavigationContext& ctx) = 0;

    /// Re-apply the current layout to all managed windows.
    virtual void reapplyLayout(const NavigationContext& ctx) = 0;

    /// Snap every unmanaged window on the screen to the current layout.
    virtual void snapAllWindows(const NavigationContext& ctx) = 0;

    /// Cycle keyboard focus through managed windows.
    virtual void cycleFocus(bool forward, const NavigationContext& ctx) = 0;

    /// Move the focused window to the first empty slot.
    virtual void pushToEmptyZone(const NavigationContext& ctx) = 0;

    /// Restore the focused window out of its managed state.
    virtual void restoreFocusedWindow(const NavigationContext& ctx) = 0;

    /// Toggle the focused window between managed and floating.
    virtual void toggleFocusedFloat(const NavigationContext& ctx) = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // Screen management
    // ═══════════════════════════════════════════════════════════════════════════

    virtual QSet<QString> activeScreens() const
    {
        return {};
    }
    virtual void setActiveScreens(const QSet<QString>& screens)
    {
        Q_UNUSED(screens)
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Window ordering
    // ═══════════════════════════════════════════════════════════════════════════

    virtual QStringList managedWindowOrder(const QString& screenId) const
    {
        Q_UNUSED(screenId)
        return {};
    }
    virtual void setInitialWindowOrder(const QString& screenId, const QStringList& windowIds)
    {
        Q_UNUSED(screenId)
        Q_UNUSED(windowIds)
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Per-screen config
    // ═══════════════════════════════════════════════════════════════════════════

    virtual void applyPerScreenConfig(const QString& screenId, const QVariantMap& overrides)
    {
        Q_UNUSED(screenId)
        Q_UNUSED(overrides)
    }
    virtual void clearPerScreenConfig(const QString& screenId)
    {
        Q_UNUSED(screenId)
    }
    virtual QVariantMap perScreenOverrides(const QString& screenId) const
    {
        Q_UNUSED(screenId)
        return {};
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Mode-specific float tracking
    // ═══════════════════════════════════════════════════════════════════════════

    virtual bool isModeSpecificFloated(const QString& windowId) const
    {
        Q_UNUSED(windowId)
        return false;
    }
    virtual void clearModeSpecificFloatMarker(const QString& windowId)
    {
        Q_UNUSED(windowId)
    }
    virtual bool restoreSavedModeFloat(const QString& windowId)
    {
        Q_UNUSED(windowId)
        return false;
    }
    virtual void clearSavedFloatingForWindows(const QStringList& windowIds)
    {
        Q_UNUSED(windowIds)
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Drag insert preview
    // ═══════════════════════════════════════════════════════════════════════════

    virtual bool hasDragInsertPreview() const
    {
        return false;
    }
    virtual bool beginDragInsertPreview(const QString& windowId, const QString& screenId)
    {
        Q_UNUSED(windowId)
        Q_UNUSED(screenId)
        return false;
    }
    virtual void commitDragInsertPreview()
    {
    }
    virtual void cancelDragInsertPreview()
    {
    }
    virtual QString dragInsertPreviewScreenId() const
    {
        return {};
    }
    virtual bool isWindowTracked(const QString& windowId) const
    {
        Q_UNUSED(windowId)
        return false;
    }
    virtual bool isWindowManaged(const QString& windowId) const
    {
        Q_UNUSED(windowId)
        return false;
    }

    /// Whether the window is actively tiled (engine-owned, non-floating).
    /// Distinct from isWindowTracked (which includes floating windows).
    virtual bool isWindowTiled(const QString& windowId) const
    {
        Q_UNUSED(windowId)
        return false;
    }

    /// Adopt an untracked window as floating on this engine's screen.
    /// Used when a window is dragged from one engine's screen to another.
    virtual void adoptWindowAsFloating(const QString& windowId, const QString& screenId)
    {
        Q_UNUSED(windowId)
        Q_UNUSED(screenId)
    }

    /// Compute the insert index for a cursor position on a managed screen.
    /// Returns -1 if the screen has no active state.
    virtual int computeDragInsertIndexAtPoint(const QString& screenId, const QPoint& cursorPos) const
    {
        Q_UNUSED(screenId)
        Q_UNUSED(cursorPos)
        return -1;
    }

    /// Update the target insert index for an active drag-insert preview.
    virtual void updateDragInsertPreview(int insertIndex)
    {
        Q_UNUSED(insertIndex)
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Algorithm / mode identity
    // ═══════════════════════════════════════════════════════════════════════════

    virtual QString algorithmId() const
    {
        return {};
    }
    virtual void setAlgorithm(const QString& algorithmId)
    {
        Q_UNUSED(algorithmId)
    }
    virtual bool isEnabled() const
    {
        return false;
    }
    virtual QString activeScreen() const
    {
        return {};
    }
    virtual void setActiveScreenHint(const QString& screenId)
    {
        Q_UNUSED(screenId)
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Desktop/activity context
    // ═══════════════════════════════════════════════════════════════════════════

    virtual void setCurrentDesktop(int desktop)
    {
        Q_UNUSED(desktop)
    }
    virtual void setCurrentActivity(const QString& activity)
    {
        Q_UNUSED(activity)
    }
    virtual void updateStickyScreenPins(const std::function<bool(const QString&)>& isWindowSticky)
    {
        Q_UNUSED(isWindowSticky)
    }
    virtual QSet<int> desktopsWithActiveState() const
    {
        return {};
    }
    virtual void pruneStatesForDesktop(int removedDesktop)
    {
        Q_UNUSED(removedDesktop)
    }
    virtual void pruneStatesForActivities(const QStringList& validActivities)
    {
        Q_UNUSED(validActivities)
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Settings synchronization
    // ═══════════════════════════════════════════════════════════════════════════

    virtual void syncFromSettings(PlasmaZones::ISettings* settings)
    {
        Q_UNUSED(settings)
    }
    virtual void connectToSettings(PlasmaZones::ISettings* settings)
    {
        Q_UNUSED(settings)
    }
    virtual qreal effectiveSplitRatioStep(const QString& screenId) const
    {
        Q_UNUSED(screenId)
        return 0.05;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Retile / refresh
    // ═══════════════════════════════════════════════════════════════════════════

    virtual void retile(const QString& screenId = QString())
    {
        Q_UNUSED(screenId)
    }
    virtual void scheduleRetileForScreen(const QString& screenId)
    {
        Q_UNUSED(screenId)
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Float origin
    // ═══════════════════════════════════════════════════════════════════════════

    virtual void markModeSpecificFloated(const QString& windowId)
    {
        Q_UNUSED(windowId)
    }
    virtual void clearAllSavedFloating()
    {
    }
    virtual void saveModeFloat(const QString& windowId)
    {
        Q_UNUSED(windowId)
    }
    virtual void clearSavedModeFloating()
    {
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Serialization delegates
    // ═══════════════════════════════════════════════════════════════════════════

    virtual QJsonArray serializeWindowOrders() const
    {
        return {};
    }
    virtual void deserializeWindowOrders(const QJsonArray& orders)
    {
        Q_UNUSED(orders)
    }
    virtual QJsonObject serializePendingRestores() const
    {
        return {};
    }
    virtual void deserializePendingRestores(const QJsonObject& obj)
    {
        Q_UNUSED(obj)
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Init hooks
    // ═══════════════════════════════════════════════════════════════════════════

    virtual void setWindowRegistry(PlasmaZones::WindowRegistry* registry)
    {
        Q_UNUSED(registry)
    }
    virtual void setIsWindowFloatingFn(std::function<bool(const QString&)> fn)
    {
        Q_UNUSED(fn)
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Master operations (autotile-specific, no-op on snap)
    // ═══════════════════════════════════════════════════════════════════════════

    virtual void increaseMasterRatio(qreal delta = 0.05)
    {
        Q_UNUSED(delta)
    }
    virtual void decreaseMasterRatio(qreal delta = 0.05)
    {
        Q_UNUSED(delta)
    }
    virtual void increaseMasterCount()
    {
    }
    virtual void decreaseMasterCount()
    {
    }
    virtual void focusMaster()
    {
    }
    virtual void swapFocusedWithMaster()
    {
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Engine state serialization
    // ═══════════════════════════════════════════════════════════════════════════

    virtual QJsonObject serializeEngineState() const
    {
        return {};
    }
    virtual void deserializeEngineState(const QJsonObject& state)
    {
        Q_UNUSED(state)
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Persistence
    // ═══════════════════════════════════════════════════════════════════════════

    virtual void saveState() = 0;
    virtual void loadState() = 0;

    // ═══════════════════════════════════════════════════════════════════════════
    // State access
    // ═══════════════════════════════════════════════════════════════════════════

    /// Per-screen state object for the given screen. May return nullptr
    /// if the engine does not manage the screen OR if per-screen state
    /// ownership has not yet been wired (e.g., SnapEngine before PR 2).
    /// Callers must not use a non-null return as a proxy for "engine
    /// manages this screen" — use isActiveOnScreen() for that check.
    virtual IPlacementState* stateForScreen(const QString& screenId) = 0;
    virtual const IPlacementState* stateForScreen(const QString& screenId) const = 0;
};

} // namespace PhosphorEngineApi
