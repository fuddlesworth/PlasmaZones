// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <PhosphorProtocol/DragMarshalling.h>
#include <PhosphorProtocol/ZoneMarshalling.h>
#include <QDBusAbstractAdaptor>
#include <QObject>
#include <QString>
#include <QRect>
#include <QUuid>
#include <QSet>
#include <QVector>
#include <memory>

class QScreen;

namespace PhosphorScreens {
class ScreenManager;
}

namespace PhosphorContext {
class IContextResolver;
} // namespace PhosphorContext

namespace PhosphorShortcutsIntegration {
class IAdhocRegistrar;
}

namespace PhosphorZones {
class IZoneDetector;
class Layout;
class Zone;
class LayoutRegistry;
}

namespace PhosphorEngine {
class IPlacementEngine;
}

namespace PlasmaZones {

class IOverlayService;

class ISettings;
class WindowTrackingAdaptor;

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
                               PhosphorZones::LayoutRegistry* layoutManager,
                               PhosphorScreens::ScreenManager* screenManager, ISettings* settings,
                               WindowTrackingAdaptor* windowTracking, QObject* parent = nullptr);
    ~WindowDragAdaptor() override = default;

    /**
     * @brief Set the autotile engine for per-screen autotile checks
     *
     * When set, dragStopped() rejects snaps on autotile screens and
     * prepareHandlerContext() skips overlay display on them.
     * Pass nullptr during shutdown to prevent dangling pointer access.
     */
    void setAutotileEngine(PhosphorEngine::IPlacementEngine* engine)
    {
        m_autotileEngine = engine;
    }

    /**
     * @brief Set the frozen-snapshot resolver used to gate snap/drag handlers
     *        on the per-screen disable + lock cascade.
     *
     * Late-bound because the resolver is constructed after both this adaptor
     * and the daemon's ScreenModeRouter exist. Daemon calls this after
     * `m_contextResolver` lands. Pass nullptr during shutdown.
     */
    void setContextResolver(PhosphorContext::IContextResolver* resolver)
    {
        m_contextResolver = resolver;
    }

    /**
     * @brief Set the shortcut registrar used to (un)register the Escape
     *        cancel-overlay shortcut for the snap-assist phase and layout
     *        picker (the drag itself uses the kwin-effect's keyboard grab).
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
    void setShortcutRegistrar(PhosphorShortcutsIntegration::IAdhocRegistrar* registrar)
    {
        m_shortcutRegistrar = registrar;
    }

    /**
     * Register / unregister the cancel-overlay Escape shortcut on demand.
     *
     * The snap-assist phase (start.cpp's snapAssistShown handler) and the
     * layout picker register / unregister this on demand. The drag itself
     * needs no binding: the
     * kwin-effect grabs the keyboard for the whole drag and routes Escape
     * to cancelSnap() directly, so a KGlobalAccel grab would never fire
     * during a drag (and binding one per drag fsynced kglobalshortcutsrc and
     * stuttered the compositor on slow disks, #167). The layout picker
     * re-uses the same id
     * (kCancelOverlayId) so KGlobalAccel never sees two distinct actions
     * competing for Escape — once a key is granted, KGlobalAccel routes
     * to a single action, and a second registration with a fresh id is
     * silently no-op'd. cancelSnap() dismisses whichever overlay is
     * visible (picker takes precedence over snap-assist over drag) so a
     * single shared binding works for all consumers.
     *
     * Idempotent: calling register twice in a row is a no-op (Registry
     * deduplicates same-id same-sequence binds).
     */
    void ensureCancelOverlayShortcutRegistered()
    {
        registerCancelOverlayShortcut();
    }

    /// Release the shared cancel-overlay Escape grab, but ONLY when no other
    /// consumer still needs it. kCancelOverlayId is bound on behalf of the
    /// layout picker (start.cpp, layoutPickerRequested) and the snap-assist
    /// phase (start.cpp, snapAssistShown); the drag itself never binds it (the
    /// kwin-effect's keyboard grab handles Escape during a drag). Every normal
    /// release site routes through here so one consumer's teardown cannot tear
    /// the grab out from under another consumer that is still showing. Two
    /// sites deliberately bypass it with an unconditional release: cancelSnap()
    /// (the explicit Escape-pressed teardown) and clearForCompositorReconnect()
    /// (force-release when the compositor that held the grab is already gone).
    void releaseCancelOverlayShortcutIfIdle();

    /// Register the layout-picker keyboard navigation accelerators
    /// (Left/Right/Up/Down/Return/Enter) as global shortcuts. Required
    /// because the unified PassiveOverlayShell is kbd-None — the
    /// picker content's QML Shortcuts don't fire. Match release on
    /// dismiss.
    ///
    /// Callbacks are owned by the caller and must outlive the
    /// registration. start.cpp passes lambdas that capture the
    /// long-lived OverlayService pointer.
    void ensureLayoutPickerNavShortcutsRegistered(std::function<void(int dx, int dy)> moveCb,
                                                  std::function<void()> confirmCb);
    void releaseLayoutPickerNavShortcuts();

public Q_SLOTS:
    /**
     * Begin a drag session — daemon-authoritative policy decision.
     *
     * Replaces dragStarted as the canonical drag-begin entry point. Compositor
     * plugin calls this synchronously at drag start and uses the returned
     * PhosphorProtocol::DragPolicy to decide whether to stream cursor updates, grab keyboard,
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
    PhosphorProtocol::DragPolicy beginDrag(const QString& windowId, int frameX, int frameY, int frameWidth,
                                           int frameHeight, const QString& startScreenId, int mouseButtons);

    /**
     * End a drag session — daemon-authoritative action.
     *
     * Replaces dragStopped as the canonical drag-end entry point. Returns a
     * PhosphorProtocol::DragOutcome that the compositor plugin applies verbatim — no further
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
    PhosphorProtocol::DragOutcome endDrag(const QString& windowId, int cursorX, int cursorY, int modifiers,
                                          int mouseButtons, bool cancelled);

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
     * Clear any drag state the daemon is still holding. Called when the
     * compositor bridge re-registers (e.g. KWin reloaded the effect, the
     * effect process restarted, or the daemon is being re-adopted by a fresh
     * effect instance). Any drag in flight from the prior effect is
     * abandoned: the new effect has no knowledge of it and the next
     * dragStarted from the fresh connection must land on a clean slate.
     * Also hides any leftover overlay so stale visuals don't linger.
     *
     * MUST stay under `public Q_SLOTS` — this is genuinely invoked
     * cross-process. The effect fires it on shutdown via
     * `PlasmaZonesEffect::clearDaemonCompositorState()`
     * (`ClientHelpers::sendOneWay(...WindowDrag, "clearForCompositorReconnect")`,
     * lifecycle.cpp) so the daemon drops stale drag/overlay state the
     * moment the effect tears down, not just on the next re-registration.
     * It is NOT listed in `org.plasmazones.WindowDrag.xml` (the XML is
     * hand-maintained doc, not adaptor-generating), so the bus surface for
     * this method comes solely from its `Q_SLOTS` placement: moving it to a
     * plain `public:` member would silently remove it from the wire and the
     * effect's fire-and-forget `sendOneWay` call would no-op without any
     * error. The daemon also calls it in-process from `Daemon::init` via the
     * `bridgeRegistered` signal, but that only covers re-registration, not
     * the effect's explicit shutdown-time clear. Contrast `handleWindowClosed`
     * below, which has no remote caller and is correctly a plain member.
     */
    void clearForCompositorReconnect();

public:
    /**
     * Called when a window is closed during or after a drag operation.
     * Connected to WindowTrackingAdaptor::windowClosedNotification — the
     * canonical close path also tears down drag state when the closing
     * window was in flight.
     *
     * Declared as a public plain member function (NOT under Q_SLOTS):
     * QDBusAbstractAdaptor's runtime introspection exposes every PUBLIC
     * scriptable slot on the bus regardless of what the hand-maintained
     * XML lists. Keeping this in `public Q_SLOTS` would re-expose
     * `WindowDrag.handleWindowClosed` on the wire even after removing
     * it from `org.plasmazones.WindowDrag.xml`. Plain member-function
     * placement keeps the in-process function-pointer-`connect()`
     * target reachable while the bus surface truly matches the XML.
     *
     * @param windowId Window ID that was closed
     */
    void handleWindowClosed(const QString& windowId);

    /**
     * True while a compositor drag session is in flight (between beginDrag
     * and endDrag/clear). Plain public member (NOT a Q_SLOT) for the same
     * reason as handleWindowClosed: it has no remote caller and must not
     * surface on the bus. The daemon's cheatsheet toggle consults it —
     * during a drag the kwin-effect holds a keyboard grab and routes
     * Escape to cancelSnap itself (see windowdragadaptor/drag.cpp), so a
     * cheatsheet shown mid-drag could never receive its own KGlobalAccel
     * dismiss grab.
     */
    bool isDragInFlight() const
    {
        return !m_draggedWindowId.isEmpty();
    }

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
    void dragPolicyChanged(const QString& windowId, const PhosphorProtocol::DragPolicy& newPolicy);

    /**
     * Emitted asynchronously after endDrag returns, when the drop requested
     * snap assist. Carries the list of empty zones on the release screen so
     * the effect can show a window picker without blocking the fast endDrag
     * reply path. The effect discards this if a new drag has already started.
     */
    void snapAssistReady(const QString& windowId, const QString& releaseScreenId,
                         const PhosphorProtocol::EmptyZoneList& emptyZones);

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
    // Overload using pre-parsed triggers (hot path during drag). Pass
    // @p excludeSentinel = true to skip entries whose modifier is the
    // AlwaysActive sentinel — those match every tick by definition, so they
    // are useless as a per-tick "user is holding the trigger" signal.
    // dragMoved uses this so the activation cache can carry both the master
    // always-active bit and user-configurable hold/toggle entries (#249).
    bool anyTriggerHeld(const QVector<ParsedTrigger>& triggers, Qt::KeyboardModifiers mods, int mouseButtons,
                        bool excludeSentinel = false) const;
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
    void dragStarted(const QString& windowId, double x, double y, double width, double height);
    void dragMoved(const QString& windowId, int cursorX, int cursorY, int modifiers, int mouseButtons);
    void dragStopped(const QString& windowId, int cursorX, int cursorY, int modifiers, int mouseButtons, int& snapX,
                     int& snapY, int& snapWidth, int& snapHeight, bool& shouldApplyGeometry,
                     QString& releaseScreenIdOut, bool& restoreSizeOnly, bool& snapAssistRequested,
                     PhosphorProtocol::EmptyZoneList& emptyZonesOut, QString& resolvedZoneIdOut);

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
     * @param resolver Frozen-snapshot context resolver — supplies the
     *        (desktop, activity, Snapping-mode) tuple used for the
     *        context-disabled check. nullptr disables the disable gate
     *        (matches the historical `settings == nullptr` fallback).
     */
    static PhosphorProtocol::DragPolicy computeDragPolicy(const ISettings* settings,
                                                          const PhosphorEngine::IPlacementEngine* autotileEngine,
                                                          const QString& windowId, const QString& screenId,
                                                          const PhosphorContext::IContextResolver* resolver,
                                                          bool reorderMode);

private:
    /// Whether reorder (drag-to-swap) mode is effective for @p screenId: a matched
    /// context SetDragBehavior rule wins, otherwise the global
    /// `autotileDragBehavior` setting. Resolves through m_layoutManager (which the
    /// static computeDragPolicy can't reach), so callers pass the result in.
    bool effectiveReorderMode(const QString& screenId) const;

    // Helper: Find screen containing a point (returns primary screen if not found)
    QScreen* screenAtPoint(int x, int y) const;

    // Helper: Returns the effective (virtual-aware) screen ID for a cursor position.
    // Prefers virtual screen resolution via PhosphorScreens::ScreenManager, falls back to physical screen.
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
    PhosphorZones::LayoutRegistry* m_layoutManager; // Concrete type for signal connections
    PhosphorScreens::ScreenManager* m_screenManager;
    ISettings* m_settings;
    WindowTrackingAdaptor* m_windowTracking;
    PhosphorEngine::IPlacementEngine* m_autotileEngine = nullptr; // Optional: per-screen autotile check
    PhosphorContext::IContextResolver* m_contextResolver =
        nullptr; // Non-owning; set via setContextResolver after Daemon builds it.
    PhosphorShortcutsIntegration::IAdhocRegistrar* m_shortcutRegistrar =
        nullptr; // Non-owning: owned by Daemon (ShortcutManager)

    // Snap-assist deferred compute state. Populated in dragStopped when snap
    // assist is requested; consumed by computeAndEmitSnapAssist() which runs
    // after the endDrag D-Bus reply has been sent, so the expensive
    // buildEmptyZoneList walk doesn't block the compositor waiting on the
    // reply.
    QString m_snapAssistPendingWindowId;
    QString m_snapAssistPendingScreenId;
    // Desktop snapshotted at drop time so the deferred snap-assist compute
    // describes "what zones were empty on the desktop the user dropped on"
    // rather than re-reading the live desktop at timer-fire time. If the
    // user changed virtual desktop between endDrag and the timer firing,
    // the live read would otherwise filter against the new desktop.
    // Cleared alongside the windowId/screenId pair in cancelSnap and the
    // pending-clear sites.
    int m_snapAssistPendingDesktop = 0;

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
    // snap-behavior differences. operator== on PhosphorProtocol::DragPolicy is a defaulted
    // structural compare, so new fields are picked up automatically.
    PhosphorProtocol::DragPolicy m_currentDragPolicy;
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
    bool m_pendingSnapDragWasSnapped = false;
    QString m_currentZoneId;
    QRect m_currentZoneGeometry;
    bool m_snapCancelled = false;
    bool m_triggerReleasedAfterCancel = false; // Tracks release→press cycle for retrigger after Escape
    bool m_activationToggled = false; // Current toggle state (on/off)
    bool m_prevTriggerHeld = false; // Previous frame's trigger state for edge detection
    bool m_autotileDragInsertToggled = false; // Current toggle state for autotile drag-insert
    bool m_prevAutotileDragInsertHeld = false; // Previous frame's autotile drag-insert trigger state
    bool m_zoneSpanToggled = false; // Current toggle state for zone span (toggle mode)
    bool m_prevZoneSpanTriggerHeld = false; // Previous frame's zone span trigger state for edge detection
    // Drag-to-reorder mode is active for the current autotile screen: cached so
    // per-tick dragMoved work (60+ Hz) doesn't have to re-query the settings +
    // engine on every cursor update. Seeded at beginDrag from the start screen
    // (requires (a) autotile-bypass path, (b) AutotileDragBehavior::Reorder,
    // (c) window tiled at drag-start) and RE-LATCHED to the cursor's current screen
    // on each policy flip in updateDragCursor under the SAME conditions (destination
    // bypassReason == AutotileScreen AND isWindowTiled), so a mid-drag crossing
    // between screens with divergent per-context SetDragBehavior rules applies the
    // destination screen's mode without ever adopting a floating window into the
    // stack or forcing a preview on a context-disabled screen. Cleared by endDrag,
    // clearPendingSnapDragState, cancelSnap, handleWindowClosed, and the shared
    // resetDragState teardown.
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
    // via the PhosphorShortcuts Registry for the snap-assist phase and layout
    // picker — no QAction member needed (the Registry owns everything).

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

    // Pre-snap geometry helper (reduces code duplication). Takes the
    // captured pre-snap geometry to prevent race conditions in
    // dragStopped() — the in-flight value may have already been
    // overwritten by the snap commit by the time this runs.
    void tryStorePreSnapGeometry(const QString& windowId, const QRect& originalGeometry);

private Q_SLOTS:
    /**
     * Called when the active layout changes mid-drag
     * Clears cached zone state to prevent stale geometry being used on snap
     */
    void onLayoutChanged();

    /**
     * Called when snap assist is dismissed (selection, timeout, click-away, etc.)
     * Unregisters the Escape shortcut that start.cpp's snapAssistShown handler
     * bound for snap assist
     */
    void onSnapAssistDismissed();
};

} // namespace PlasmaZones
