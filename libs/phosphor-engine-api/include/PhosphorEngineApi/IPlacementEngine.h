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

class QObject;

namespace PhosphorEngineApi {

/// Unified placement engine interface.
///
/// NOTE: This interface carries methods for both snap and autotile engines.
/// Methods documented as engine-specific (e.g., master operations) are no-ops
/// on engines that don't implement them. A future pass may split this into
/// focused facets (IScreenManagement, IMasterOperations, IDragPreview, etc.)
/// once a third engine arrives and the real seams become clearer.
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
    /// Remove saved floating state for the given windows (per-window, not bulk clear).
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
    /// Whether the engine considers the window "managed" (eligible for
    /// layout operations). Semantics are engine-specific:
    /// - Autotile: equivalent to isWindowTiled (floating windows excluded).
    /// - Snap: a window assigned to a zone (including floated-in-zone).
    /// Callers that need a consistent cross-engine check for "engine owns
    /// this window at all" should use isWindowTracked instead.
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

    // ═══════════════════════════════════════════════════════════════════════════
    // Cross-engine handoff
    //
    // When a window crosses screens whose owning engines differ (e.g. a snap
    // screen → an autotile screen, via drag drop), ownership has to transfer
    // between two engines. Without an explicit contract, each engine makes
    // local guesses (autotile's "is this a floating window I should adopt?"
    // branch, snap's "do I know this window?" early-return) and the daemon
    // misroutes user intents during the transition window.
    //
    // The handoff is a two-step transaction the daemon orchestrates:
    //   1. fromEngine->handoffRelease(windowId)
    //   2. toEngine->handoffReceive(ctx)
    //
    // Each engine's release is a tracking-only clear (no geometry mutation —
    // the receiving engine places the window). Each engine's receive applies
    // its own placement policy (autotile picks an insert slot or floats;
    // snap picks the nearest zone or floats) using the context fields.
    //
    // Engines that don't currently distinguish ownership across screens can
    // leave both as no-ops; the defaults are safe.
    //
    // The verbs are prefixed `handoff*` to keep them distinct from
    // PlacementEngineBase::releaseWindow (which is part of the base FSM
    // lifecycle and means "this window is no longer engine-managed at all"
    // — a different concept from "transferring ownership to another engine").
    // ═══════════════════════════════════════════════════════════════════════════

    /// Context for a cross-engine window handoff. Populated by the daemon
    /// from the source engine's state and the drop event before invoking
    /// handoffReceive on the destination engine.
    struct HandoffContext
    {
        QString windowId;
        QString fromEngineId; ///< source engine identity ("snap" / "autotile" / "")
        QString toScreenId; ///< destination screen (must be owned by `to` engine)
        QPoint dropPos; ///< cursor position at drop, or invalid for non-drag handoffs
        QRect sourceGeometry; ///< window's frame at handoff time (for size preservation)
        QStringList sourceZoneIds; ///< zones the window held at source (empty if not snapped)
        bool wasFloating = false; ///< window was floating in source engine
    };

    /// Receive ownership of a window from another engine.
    ///
    /// Implementations should:
    /// - Add the window to their own tracking (per-screen/per-state).
    /// - Decide placement (snap to zone / tile / float) using the context
    ///   and engine-local policy. Drag drops typically place at dropPos;
    ///   non-drag handoffs (cross-engine focus changes, programmatic moves)
    ///   typically respect wasFloating + sourceGeometry.
    /// - Emit any `windowFloatingChanged` / placement signals their normal
    ///   placement paths emit, so downstream state stays consistent.
    ///
    /// Default is a no-op so engines that don't yet implement the handoff
    /// don't reject the call — the orchestrator falls back to its legacy path.
    virtual void handoffReceive(const HandoffContext& ctx)
    {
        Q_UNUSED(ctx)
    }

    /// Release ownership of a window WITHOUT modifying its geometry.
    ///
    /// Implementations should:
    /// - Remove the window from per-screen/per-state tracking.
    /// - Clear zone assignments (if any) WITHOUT triggering a resnap of
    ///   neighbours — that's the receiving engine's job once it places the
    ///   window in its layout.
    /// - Preserve any pre-tile / pre-float captured geometry that should
    ///   survive the cross-engine move (the receiving engine may consult
    ///   it via the HandoffContext for size preservation).
    ///
    /// Default is a no-op for the same reason as handoffReceive.
    virtual void handoffRelease(const QString& windowId)
    {
        Q_UNUSED(windowId)
    }

    /// Stable engine identity for HandoffContext.fromEngineId. Conventional
    /// values: "snap" / "autotile". Empty string means "unidentified" and
    /// disables receive-side reasoning that depends on the source mode.
    virtual QString engineId() const
    {
        return {};
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
    virtual bool isEnabled() const noexcept
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

    /// Re-read all tuning values from the engine's settings interface.
    /// Called by the daemon after any settings change. Engines that cache
    /// config values (e.g. AutotileEngine) override this to repopulate
    /// their config struct. Engines that read on demand (e.g. SnapEngine)
    /// leave this as a no-op.
    virtual void refreshConfigFromSettings()
    {
    }
    virtual qreal effectiveSplitRatioStep(const QString& screenId) const
    {
        Q_UNUSED(screenId)
        return 0.05;
    }
    /// Runtime max-windows limit. Returns -1 (unlimited sentinel) by default;
    /// engines that enforce a cap override with the actual value.
    /// Callers must treat -1 as "no limit" — never use as a divisor.
    virtual int runtimeMaxWindows() const
    {
        return -1;
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

    /// Attach a window-class registry (QObject carrying WindowRegistry).
    /// Engines qobject_cast to their concrete type internally.
    virtual void setWindowRegistry(QObject* registry)
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
