// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <dbus_types.h>
#include <QDBusAbstractAdaptor>
#include <QObject>
#include <QString>
#include <QRect>
#include <QUuid>
#include <QSet>
#include <QVector>
#include <memory>

class QScreen;

namespace Phosphor::Shortcuts::Integration {
class IAdhocRegistrar;
}

namespace PhosphorZones {
class IZoneDetector;
class Layout;
class Zone;
}

namespace PlasmaZones {

class IOverlayService;
class LayoutManager; // Concrete type needed for signal connections
class ISettings;
class WindowTrackingAdaptor;
class AutotileEngine;

/**
 * @brief D-Bus adaptor for window drag handling
 *
 * Provides D-Bus interface: org.plasmazones.WindowDrag
 *
 * Receives drag events from KWin script and handles:
 * - Modifier key detection (works on Wayland via QGuiApplication)
 * - PhosphorZones::Zone detection and highlighting
 * - Overlay visibility based on modifiers
 * - Window snapping via KWin D-Bus
 */
class PLASMAZONES_EXPORT WindowDragAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.WindowDrag")

public:
    explicit WindowDragAdaptor(IOverlayService* overlay, PhosphorZones::IZoneDetector* detector,
                               LayoutManager* layoutManager, ISettings* settings, WindowTrackingAdaptor* windowTracking,
                               QObject* parent = nullptr);
    ~WindowDragAdaptor() override = default;

    /**
     * @brief Set the autotile engine for per-screen autotile checks
     *
     * When set, dragStopped() rejects snaps on autotile screens and
     * prepareHandlerContext() skips overlay display on them.
     * Pass nullptr during shutdown to prevent dangling pointer access.
     */
    void setAutotileEngine(AutotileEngine* engine)
    {
        m_autotileEngine = engine;
    }

    /**
     * @brief Set the shortcut registrar used to (un)register the Escape
     *        cancel-overlay shortcut around active drag sessions.
     *
     * Must be called after construction, before any drag operations.
     * The registrar is owned by Daemon — this is a non-owning pointer. Routing
     * through the interface keeps the underlying Registry private, so the
     * drag adaptor can't accidentally iterate or flush other consumers'
     * shortcuts.
     *
     * Passing nullptr detaches the adaptor from the registrar; any subsequent
     * (un)register call becomes a no-op. Daemon::stop() uses this to prevent
     * late callbacks from touching a destroyed ShortcutManager during
     * shutdown (member destruction order: unique_ptr members destruct before
     * ~QObject runs, so ShortcutManager dies before this adaptor does).
     */
    void setShortcutRegistrar(Phosphor::Shortcuts::Integration::IAdhocRegistrar* registrar)
    {
        m_shortcutRegistrar = registrar;
    }

public Q_SLOTS:
    /**
     * Begin a drag session — daemon-authoritative policy decision.
     *
     * Replaces dragStarted as the canonical drag-begin entry point. Compositor
     * plugin calls this synchronously at drag start and uses the returned
     * DragPolicy to decide whether to stream cursor updates, grab keyboard,
     * apply an immediate float transition (autotile), etc. Single source of
     * truth replaces the effect-side m_dragBypassedForAutotile cache that
     * went stale after every settings reload.
     *
     * Internally, for snap-path drags, this also performs the same drag-start
     * setup as the legacy dragStarted method (original geometry, pre-parsed
     * triggers, was-snapped check). For autotile-bypass or
     * snapping-disabled drags, it only stores m_draggedWindowId so later
     * updateDragCursor / endDrag calls match.
     *
     */
    PlasmaZones::DragPolicy beginDrag(const QString& windowId, int frameX, int frameY, int frameWidth, int frameHeight,
                                      const QString& startScreenId, int mouseButtons);

    /**
     * End a drag session — daemon-authoritative action.
     *
     * Replaces dragStopped as the canonical drag-end entry point. Returns a
     * DragOutcome that the compositor plugin applies verbatim — no further
     * decisions on the plugin side. Covers the full dispatch matrix:
     *
     *   - autotile_screen bypass → ApplyFloat at the release cursor
     *   - snapping_disabled / context_disabled bypass → NoOp
     *   - snap path → delegates to legacy dragStopped and packages its
     *     out-params into the outcome (ApplySnap / RestoreSize / NoOp /
     *     with snap-assist empty zones if requested)
     *
     * Must be called after beginDrag for the same windowId. If beginDrag
     * was never called (or the ids mismatch), returns NoOp.
     */
    PlasmaZones::DragOutcome endDrag(const QString& windowId, int cursorX, int cursorY, int modifiers, int mouseButtons,
                                     bool cancelled);

    /**
     * Update drag cursor position — fire-and-forget counterpart to
     * beginDrag / endDrag. Replaces dragMoved as the canonical hot-path
     * entry point during a drag. Throttled 30Hz by the compositor plugin.
     *
     * For snap-path drags: delegates to legacy dragMoved internally to
     * keep overlay/zone-detection state current.
     *
     * For bypass drags: no-op, but still consulted for cursor screen
     * detection — if the cursor crosses a virtual-screen boundary that
     * would change the policy (autotile↔snap), the daemon emits
     * dragPolicyChanged and the plugin reacts by switching its local
     * drag mode. This replaces the effect-side cross-VS flip logic.
     */
    void updateDragCursor(const QString& windowId, int cursorX, int cursorY, int modifiers, int mouseButtons);

    /** Forward mouse wheel delta to zone selector for scrolling during drag. */
    void selectorScrollWheel(int angleDeltaY);

    /**
     * Cancel current snap operation (Escape key)
     */
    void cancelSnap();

    /**
     * Called when a window is closed during or after a drag operation
     * @param windowId Window ID that was closed
     * @note Cleans up any drag state associated with this window
     */
    void handleWindowClosed(const QString& windowId);

    /**
     * Clear any drag state the daemon is still holding. Called when the
     * compositor bridge re-registers (e.g. KWin reloaded the effect, the
     * effect process restarted, or the daemon is being re-adopted by a fresh
     * effect instance). Any drag in flight from the prior effect is
     * abandoned: the new effect has no knowledge of it and the next
     * dragStarted from the fresh connection must land on a clean slate.
     * Also hides any leftover overlay so stale visuals don't linger.
     */
    void clearForCompositorReconnect();

Q_SIGNALS:
    /**
     * Emitted when the zone geometry under the cursor changes during drag.
     * KWin effect subscribes and applies the geometry immediately for snap-on-hover behavior.
     */
    void zoneGeometryDuringDragChanged(const QString& windowId, int x, int y, int width, int height);

    /**
     * Emitted when the cursor leaves all zones during drag and the window was snapped.
     * KWin effect applies pre-snap size immediately (restore-size-only at current position).
     */
    void restoreSizeDuringDragChanged(const QString& windowId, int width, int height);

    /**
     * Daemon has detected a policy change for an active drag
     * (typically because the cursor crossed a virtual-screen
     * boundary that flips autotile↔snap mode). Plugin reacts by applying
     * the transition: entering/exiting autotile bypass, canceling snap
     * overlay, calling handleDragToFloat, etc.
     *
     * Replaces the effect-side cross-VS flip logic that used a local cache
     * and could go stale after settings reloads.
     */
    void dragPolicyChanged(const QString& windowId, const PlasmaZones::DragPolicy& newPolicy);

    /**
     * Emitted asynchronously after endDrag returns, when the drop requested
     * snap assist. Carries the list of empty zones on the release screen so
     * the effect can show a window picker without blocking the fast endDrag
     * reply path. The effect discards this if a new drag has already started.
     */
    void snapAssistReady(const QString& windowId, const QString& releaseScreenId,
                         const PlasmaZones::EmptyZoneList& emptyZones);

private:
    // Tolerance constants for geometry matching (fallback detection)
    // Position tolerance is generous due to KWin window decoration/shadow offsets
    static constexpr int PositionTolerance = 100;
    // Size tolerance is stricter - snapped windows should match zone size closely
    static constexpr int SizeTolerance = 20;

    /// Pre-parsed trigger (avoids QVariantMap unboxing on every dragMoved tick)
    struct ParsedTrigger
    {
        int modifier = 0;
        int mouseButton = 0;
    };

    // Check if modifier matches setting
    bool checkModifier(int modifierSetting, Qt::KeyboardModifiers mods) const;
    // Check if any trigger in a list matches current modifiers/mouse buttons
    bool anyTriggerHeld(const QVariantList& triggers, Qt::KeyboardModifiers mods, int mouseButtons) const;
    // Overload using pre-parsed triggers (hot path during drag)
    bool anyTriggerHeld(const QVector<ParsedTrigger>& triggers, Qt::KeyboardModifiers mods, int mouseButtons) const;
    // Parse QVariantList triggers into POD structs for repeated use
    static QVector<ParsedTrigger> parseTriggers(const QVariantList& triggers);

    // ═══════════════════════════════════════════════════════════════════════
    // Legacy drag state-machine helpers (formerly public D-Bus slots). Now
    // called internally by beginDrag / updateDragCursor / endDrag in
    // drag_protocol.cpp. They stay as regular C++ member functions to
    // preserve the intricate snap-path overlay/zone logic without having
    // to rewrite it into the new protocol wrappers. The D-Bus surface no
    // longer exposes them — external clients go through the new protocol.
    // ═══════════════════════════════════════════════════════════════════════
    void dragStarted(const QString& windowId, double x, double y, double width, double height, int mouseButtons);
    void dragMoved(const QString& windowId, int cursorX, int cursorY, int modifiers, int mouseButtons);
    void dragStopped(const QString& windowId, int cursorX, int cursorY, int modifiers, int mouseButtons, int& snapX,
                     int& snapY, int& snapWidth, int& snapHeight, bool& shouldApplyGeometry,
                     QString& releaseScreenIdOut, bool& restoreSizeOnly, bool& snapAssistRequested,
                     PlasmaZones::EmptyZoneList& emptyZonesOut, QString& resolvedZoneIdOut);

    // Promote the pending snap-path drag (stashed by beginDrag) to an
    // active drag by running the legacy dragStarted setup. Called from
    // updateDragCursor once the activation trigger is first held OR the
    // cursor enters a zone-selector edge region (edge-hover is an
    // alternative "user wants to snap" commitment).
    // Returns true if promotion happened or the drag was already active.
    bool activateSnapDragIfNeeded(int modifiers, int mouseButtons, int cursorX, int cursorY);

    // Discard any pending snap-path drag state. Called from endDrag and
    // handleWindowClosed to prevent leftover pending state leaking into
    // the next drag.
    void clearPendingSnapDragState();

public:
    /**
     * @brief Pure policy decision — no side effects, static so tests can
     *        invoke it without constructing a full adaptor.
     *
     * Consulted from daemon-authoritative state. The result is what
     * beginDrag returns to the compositor plugin and what is emitted on
     * dragPolicyChanged during cross-VS cursor crossings.
     *
     * Precedence: context_disabled → autotile_screen → snapping_disabled →
     * snap path (canonical). First match wins so the bypassReason string is
     * stable across coincidental disables.
     *
     * @param settings Settings interface (snappingEnabled, zone-span triggers, etc.)
     * @param autotileEngine May be nullptr in tests that don't exercise autotile
     * @param windowId Dragged window (used for the isWindowTracked lookup
     *                 that decides immediateFloatOnStart)
     * @param screenId Virtual-screen-aware screen ID at drag start
     * @param curDesktop Current virtual desktop (for context-disabled check)
     * @param curActivity Current activity (for context-disabled check)
     */
    static PlasmaZones::DragPolicy computeDragPolicy(const ISettings* settings, const AutotileEngine* autotileEngine,
                                                     const QString& windowId, const QString& screenId, int curDesktop,
                                                     const QString& curActivity);

private:
    // Helper: Find screen containing a point (returns primary screen if not found)
    QScreen* screenAtPoint(int x, int y) const;

    // Helper: Returns the effective (virtual-aware) screen ID for a cursor position.
    // Prefers virtual screen resolution via Phosphor::Screens::ScreenManager, falls back to physical screen.
    QString effectiveScreenIdAt(int x, int y) const;

    // Shared preamble for drag handler methods (DRY extraction)
    // Returns layout for the screen at (x,y), or nullptr if screen disabled/no layout.
    // Shows overlay if not visible. Sets outScreen to the resolved physical screen
    // and outScreenId to the virtual-aware screen identifier.
    PhosphorZones::Layout* prepareHandlerContext(int x, int y, QScreen*& outScreen, QString& outScreenId);

    // Compute bounding rectangle of multiple zones with gaps applied
    // screenId is the virtual-aware screen identifier for gap/padding lookups.
    QRectF computeCombinedZoneGeometry(const QVector<PhosphorZones::Zone*>& zones, QScreen* screen,
                                       PhosphorZones::Layout* layout, const QString& screenId) const;

    // Convert zone UUIDs to string list (for overlay service)
    static QStringList zoneIdsToStringList(const QVector<QUuid>& ids);

    // Refactored dragMoved helpers
    void handleZoneSpanModifier(int x, int y);
    void handleMultiZoneModifier(int x, int y);
    void hideOverlayAndClearZoneState();

    // Mid-drag trigger release: clear zone state and blank the overlay's shader
    // output WITHOUT destroying overlay windows. See the call site in dragMoved
    // and the rationale comment in IOverlayService::setIdleForDragPause().
    void clearOverlayForTriggerRelease();

    IOverlayService* m_overlayService;
    PhosphorZones::IZoneDetector* m_zoneDetector;
    LayoutManager* m_layoutManager; // Concrete type for signal connections
    ISettings* m_settings;
    WindowTrackingAdaptor* m_windowTracking;
    AutotileEngine* m_autotileEngine = nullptr; // Optional: per-screen autotile check
    Phosphor::Shortcuts::Integration::IAdhocRegistrar* m_shortcutRegistrar =
        nullptr; // Non-owning: owned by Daemon (ShortcutManager)

    // Snap-assist deferred compute state. Populated in dragStopped when snap
    // assist is requested; consumed by computeAndEmitSnapAssist() which runs
    // after the endDrag D-Bus reply has been sent, so the expensive
    // buildEmptyZoneList walk doesn't block the compositor waiting on the
    // reply.
    QString m_snapAssistPendingWindowId;
    QString m_snapAssistPendingScreenId;

    // Current drag state
    QString m_draggedWindowId;
    // Policy returned from the last beginDrag call, updated in place by
    // updateDragCursor when the cursor crosses a screen whose policy
    // differs. Read in endDrag (via bypassReason) to decide which branch
    // to take — autotile bypass gets a synthesized ApplyFloat outcome,
    // context/snap disabled gets NoOp, snap path delegates to the legacy
    // dragStopped. Reset to default by endDrag / handleWindowClosed so
    // the next drag starts clean.
    //
    // Storing the full policy (not just bypassReason) lets the cross-VS
    // comparator in updateDragCursor re-emit dragPolicyChanged on any
    // policy-relevant field change, including same-bypass-reason
    // variations like autotile→autotile cross-VS or (future) per-screen
    // snap-behavior differences. operator== on DragPolicy is a defaulted
    // structural compare, so new fields are picked up automatically.
    DragPolicy m_currentDragPolicy;
    QRect m_originalGeometry;

    // Pending snap-path drag awaiting first activation. Populated by
    // beginDrag on the snap path instead of immediately running the full
    // legacy dragStarted setup — that way updateDragCursor ticks before the
    // user holds the activation trigger are cheap no-ops (no overlay
    // show/hide cycle). Promoted to m_draggedWindowId on the first tick
    // where the activation trigger is held, via activateSnapDragIfNeeded().
    // Restores the lazy drag-state semantics from before the drag-protocol
    // refactor — there used to be a sendDeferredDragStarted() latch in the
    // kwin-effect; the refactor made beginDrag unconditional, so the
    // laziness now lives on the daemon side.
    QString m_pendingSnapDragWindowId;
    QRect m_pendingSnapDragGeometry;
    int m_pendingSnapDragMouseButtons = 0;
    bool m_pendingSnapDragWasSnapped = false;
    QString m_currentZoneId;
    QRect m_currentZoneGeometry;
    bool m_snapCancelled = false;
    bool m_triggerReleasedAfterCancel = false; // Tracks release→press cycle for retrigger after Escape
    bool m_activationToggled = false; // Current toggle state (on/off)
    bool m_prevTriggerHeld = false; // Previous frame's trigger state for edge detection
    bool m_autotileDragInsertToggled = false; // Current toggle state for autotile drag-insert
    bool m_prevAutotileDragInsertHeld = false; // Previous frame's autotile drag-insert trigger state
    // Drag-to-reorder mode is active for the current drag: cached at beginDrag
    // time so per-tick dragMoved work (60+ Hz) doesn't have to re-query the
    // settings + engine on every cursor update. Requires (a) autotile-bypass
    // path, (b) AutotileDragBehavior::Reorder, and (c) window was tracked+tiled
    // at drag-start. Cleared by endDrag / clearPendingSnapDragState.
    bool m_dragReorderActive = false;
    bool m_overlayShown = false;
    // Overlay was blanked mid-drag via IOverlayService::setIdleForDragPause()
    // (trigger released, but the drag is still live). Windows stay alive;
    // only the shader output is cleared. Cleared when the overlay shows zones
    // again (refreshFromIdle) or when the drag ends.
    bool m_overlayIdled = false;
    bool m_zoneSelectorShown = false;
    // Tracks which (virtual) screen the selector is currently shown on, so we
    // can detect cursor-crosses-VS-while-still-near-edge and re-show on the
    // new VS instead of leaving the old one stuck visible.
    QString m_zoneSelectorShownOn;
    int m_lastCursorX = 0;
    int m_lastCursorY = 0;
    bool m_wasSnapped = false; // True if window was snapped to a zone when drag started

    // Multi-zone state
    QVector<QUuid>
        m_currentAdjacentZoneIds; // PhosphorZones::Zone IDs (not pointers - zones owned by PhosphorZones::Layout)
    bool m_isMultiZoneMode = false;
    QRect m_currentMultiZoneGeometry; // Combined geometry for multi-zone

    // Paint-to-span state (zone span modifier)
    QSet<QUuid> m_paintedZoneIds; // Accumulates zones during paint-to-span drag
    bool m_modifierConflictWarned = false; // Logged once per drag, reset on next dragStarted

    // Escape cancel-overlay shortcut is registered/unregistered dynamically
    // via the PhosphorShortcuts Registry around drag sessions — no QAction
    // member needed (the Registry owns everything).

    // Pre-parsed trigger caches (populated on dragStarted, used on every dragMoved tick)
    QVector<ParsedTrigger> m_cachedActivationTriggers;
    QVector<ParsedTrigger> m_cachedZoneSpanTriggers;
    QVector<ParsedTrigger> m_cachedAutotileDragInsertTriggers;

    // Autotile drag-insert preview state lives on AutotileEngine
    // (hasDragInsertPreview(), dragInsertPreviewScreenId()). The adaptor
    // queries the engine directly to avoid drift between the two caches.

    // DRY helper: cancel any active autotile drag-insert preview.
    void cancelDragInsertIfActive();

    // Last emitted zone geometry (emit only when changed)
    QRect m_lastEmittedZoneGeometry;
    bool m_restoreSizeEmittedDuringDrag = false;

    // Last logged activationActive value — used to emit a log entry only on
    // true transitions so the drag-overlay churn source can be traced from
    // journalctl without spamming every tick.
    bool m_lastLoggedActivationActive = false;

    void registerCancelOverlayShortcut();
    void unregisterCancelOverlayShortcut();

    // PhosphorZones::Zone selector methods
    void checkZoneSelectorTrigger(int cursorX, int cursorY);
    bool isNearTriggerEdge(QScreen* screen, int cursorX, int cursorY, const QString& screenId = QString()) const;

    // Screen resolution helper (DRY: used by prepareHandlerContext, dragStopped, checkZoneSelectorTrigger)
    struct ScreenResolution
    {
        QString screenId; // effective (possibly virtual) screen ID
        QString physicalId; // physical screen ID
        QScreen* qscreen; // physical QScreen pointer
    };
    ScreenResolution resolveScreenAt(const QPointF& globalPos) const;

    // dragStopped() helpers
    void hideOverlayAndSelector();
    void resetDragState(bool keepEscapeShortcut = false);

    /**
     * Compute the empty-zone list for snap assist and emit the snapAssistReady
     * signal. Runs AFTER endDrag has already returned its reply to the
     * compositor, so the expensive buildEmptyZoneList walk doesn't block the
     * D-Bus reply path. Scheduled from endDrag via QTimer::singleShot(0) when
     * dragStopped set m_snapAssistPendingWindowId / m_snapAssistPendingScreenId.
     */
    void computeAndEmitSnapAssist();

    // Pre-snap geometry helper (reduces code duplication)
    // Overload with captured values to prevent race conditions in dragStopped()
    void tryStorePreSnapGeometry(const QString& windowId);
    void tryStorePreSnapGeometry(const QString& windowId, bool wasSnapped, const QRect& originalGeometry);

private Q_SLOTS:
    /**
     * Called when the active layout changes mid-drag
     * Clears cached zone state to prevent stale geometry being used on snap
     */
    void onLayoutChanged();

    /**
     * Called when snap assist is dismissed (selection, timeout, click-away, etc.)
     * Unregisters the Escape shortcut that was kept alive for snap assist
     */
    void onSnapAssistDismissed();
};

} // namespace PlasmaZones
