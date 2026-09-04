// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <PhosphorProtocol/DragMarshalling.h>
#include <PhosphorProtocol/ZoneMarshalling.h>
#include <QDBusAbstractAdaptor>
#include <QElapsedTimer>
#include <QObject>
#include <QPoint>
#include <QString>
#include <QRect>
#include <QSize>
#include <QUuid>
#include <QSet>
#include <QVector>
#include <memory>

class QScreen;
class QTimer;

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
     * @brief Set the scrolling engine for per-screen drag policy.
     *
     * The scrolling engine owns placement on its screens exactly like
     * autotile does, so drags there take the engine bypass (drag-to-float)
     * instead of the snap pipeline. Pass nullptr during shutdown.
     */
    void setScrollEngine(PhosphorEngine::IPlacementEngine* engine)
    {
        m_scrollEngine = engine;
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
     * The snap-assist phase (shortcuts_wiring.cpp's snapAssistShown handler) and the
     * layout picker register / unregister this on demand. The drag itself
     * needs no binding: on the SNAP path the kwin-effect grabs the keyboard
     * (lifecycle_wiring.cpp's dragStarted) and routes Escape to cancelSnap()
     * from grabbedKeyboardEvent (plasmazoneseffect.cpp), so a KGlobalAccel
     * grab would never fire during such a drag (and binding one per drag fsynced kglobalshortcutsrc and
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
    /// layout picker (shortcuts_wiring.cpp, layoutPickerRequested) and the snap-assist
    /// phase (shortcuts_wiring.cpp, snapAssistShown); the drag itself never binds it (the
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
    /// registration. shortcuts_wiring.cpp passes lambdas that capture the
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
     *   - snapping_disabled / context_disabled / layout_suppressed bypass → NoOp
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
     * True while a drag this adaptor is ACTING ON is in flight. Plain public
     * member (NOT a Q_SLOT) for the same reason as handleWindowClosed: it has
     * no remote caller and must not surface on the bus. The daemon's
     * cheatsheet toggle consults it — while the adaptor is acting on a drag
     * the kwin-effect holds a keyboard grab and routes Escape to cancelSnap
     * itself (grabbedKeyboardEvent, kwin-effect/plasmazoneseffect.cpp — the
     * note in windowdragadaptor/drag.cpp only records why the daemon binds
     * nothing), so a cheatsheet shown then
     * could never receive its own KGlobalAccel dismiss grab.
     *
     * NOT the same as "between beginDrag and endDrag". beginDrag's SNAP path
     * deliberately defers filling m_draggedWindowId until the user first
     * holds an activation trigger (see the deferral note in
     * beginDrag/activateSnapDragIfNeeded), so a snap drag where the trigger is
     * never held reports false from beginning to end. That is the answer this
     * predicate's consumers want: with no activation there is no keyboard
     * grab, so the cheatsheet's own dismiss grab works normally. A caller that
     * genuinely needs "a compositor drag session exists" has to consult
     * m_pendingSnapDragWindowId as well.
     */
    bool isDragInFlight() const
    {
        return !m_draggedWindowId.isEmpty();
    }

    /**
     * True while a compositor drag session exists, whether or not the user
     * has held an activation trigger yet — the broader question
     * isDragInFlight() deliberately does not answer (see its note).
     *
     * The daemon's geometry-recompute debounce consults this. Plasma's
     * floating panels dock themselves when a dragged window touches them and
     * float again when it moves away, which is stock Plasma behaviour we
     * cannot and should not suppress. Each toggle changes the panel's
     * exclusive zone, so our layer-shell sensor reflows and the work area
     * moves. Recomputing zone rects from that mid-drag drags the zones out
     * from under the user's cursor, and the drop then resolves against a rect
     * that moved after the user aimed at it.
     */
    bool isDragSessionActive() const
    {
        return !m_draggedWindowId.isEmpty() || !m_pendingSnapDragWindowId.isEmpty();
    }

    /**
     * Cancel any live drag-insert preview on either engine. The daemon's
     * context-change handlers route through these instead of hand-inlining
     * the two-engine sweep (a third engine would otherwise need every call
     * site updated). The ForScreen form cancels only previews whose target
     * or prior screen shares @p screenId's physical output — a desktop
     * switch or output removal on monitor A must not snap monitor B's live
     * preview back.
     */
    void cancelDragInsertPreviews();
    void cancelDragInsertPreviewsForScreen(const QString& screenId);

Q_SIGNALS:
    /**
     * Emitted when the zone geometry under the cursor changes during drag.
     * NO in-tree subscriber exists today: the effect and DaemonClient
     * subscribe only to restoreSizeDuringDragChanged. The signal stays on
     * the published D-Bus surface (org.plasmazones.WindowDrag.xml) for a
     * snap-on-hover consumer; removing it from the wire contract is a
     * maintainer decision, not a doc fix.
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
                     QString& resolvedZoneIdOut);

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
     * Precedence: context_disabled → autotile_screen (either tiling-family
     * engine claiming the screen — autotile or scrolling) →
     * snapping_disabled → layout_suppressed → snap path (canonical). First
     * match wins so the
     * bypassReason string is stable across coincidental disables.
     *
     * @param settings Settings interface (snappingEnabled, zone-span triggers, etc.)
     * @param autotileEngine May be nullptr in tests that don't exercise autotile
     * @param scrollEngine May be nullptr in tests that don't exercise scrolling;
     *        a screen it claims takes the same engine bypass as autotile
     * @param windowId Dragged window (used for the isWindowTracked lookup
     *                 that decides immediateFloatOnStart)
     * @param screenId Virtual-screen-aware screen ID at drag start
     * @param resolver Frozen-snapshot context resolver — supplies the
     *        (desktop, activity, live-mode) tuple used for the
     *        context-disabled check. nullptr disables the disable gate
     *        (matches the historical `settings == nullptr` fallback).
     * @param reorderMode The caller's per-screen reorder verdict
     *        (effectiveDragReorderModeFor: autotile = the DragBehavior rule
     *        cascade, scrolling = the bare AlwaysActive sentinel in its
     *        trigger list). Resolved by the caller because the static can't
     *        reach the registry or the per-drag trigger caches. True on an
     *        engine-owned screen clears immediateFloatOnStart — the reorder
     *        pipeline, not a float, owns the drop.
     * @param activeLayoutSuppressed Whether the screen's context has NO active
     *        zone layout because the default assignment is suppressed
     *        (LayoutRegistry::isContextActiveLayoutSuppressed — resolved by the
     *        caller, like reorderMode, since the static can't reach the
     *        registry). True → dead drag: the drag path's layout resolution
     *        would otherwise fall back to the global default layout and snap
     *        windows into zones the screen was never assigned (#724).
     */
    static PhosphorProtocol::DragPolicy computeDragPolicy(const ISettings* settings,
                                                          const PhosphorEngine::IPlacementEngine* autotileEngine,
                                                          const PhosphorEngine::IPlacementEngine* scrollEngine,
                                                          const QString& windowId, const QString& screenId,
                                                          const PhosphorContext::IContextResolver* resolver,
                                                          bool reorderMode, bool activeLayoutSuppressed);

    /**
     * @brief Whether reorder (drag-to-swap) mode is effective for @p screenId
     *
     * A matched context SetDragBehavior rule wins over the global
     * `autotileDragBehavior` setting. Static for the same reason
     * computeDragPolicy is — the decision needs the layout registry, which
     * computeDragPolicy cannot reach, and a test must be able to pin the
     * precedence without standing up a whole adaptor.
     *
     * @param layoutManager Rule/assignment cascade source; nullptr falls back
     *        to the global setting
     * @param settings Global-setting source; nullptr means "not reorder"
     * @param screenId Screen the drag starts on; empty falls back to the
     *        global setting (no context to resolve a rule against)
     */
    static bool resolveReorderMode(const PhosphorZones::LayoutRegistry* layoutManager, const ISettings* settings,
                                   const QString& screenId);

private:
    /// resolveReorderMode bound to this adaptor's registry and settings.
    bool effectiveReorderMode(const QString& screenId) const;

    /// The engine that owns drag-insert on @p screenId: autotile when the
    /// screen is autotile-active, else the scroll engine when scrolling-
    /// active, else nullptr. Every drag-insert dispatch site resolves
    /// through this instead of hard-typing m_autotileEngine.
    PhosphorEngine::IPlacementEngine* dragInsertEngineFor(const QString& screenId) const;

    /// The engine currently holding a live drag-insert preview (at most one
    /// across both engines by construction), or nullptr.
    PhosphorEngine::IPlacementEngine* dragInsertPreviewEngine() const;

    /// The window's client-reported minimum size as known by ANY engine
    /// (scroll and autotile today; snap answers 0x0 until it grows a
    /// min-size model, its slot is future-proofing), or 0x0 when none
    /// tracks it. Queried
    /// BEFORE a drag-insert commit: a cross-engine adoption releases the
    /// source's tracking (windowFloatingStateSynced -> handoffRelease), so
    /// the value must be read while the source still holds it, then pushed
    /// into the committing engine via windowMinSizeUpdated. Without that
    /// push a fresh adoption seats the tile with min 0x0 and every
    /// respect-minimum-size clamp is inert for it.
    QSize dragInsertAdoptedMinSize(const QString& windowId) const;

    /// Reorder-mode resolve for whichever engine owns @p screenId: autotile
    /// keeps the SetDragBehavior-rule/global-setting cascade; scrolling has
    /// no DragBehavior enum, so "always re-insert" IS the AlwaysActive
    /// sentinel in its trigger list (read from the per-drag parsed cache).
    bool effectiveDragReorderModeFor(const QString& screenId) const;

    /// Whether @p screenId's context has no active zone layout because the
    /// default assignment is suppressed (the computeDragPolicy
    /// activeLayoutSuppressed input, resolved on the current desktop/activity
    /// like effectiveReorderMode). Also gates the per-tick drag paths, which a
    /// mid-drag monitor crossing reaches without a fresh beginDrag (#724).
    /// Memoized per drag on the last screen id (see the definition); beginDrag
    /// clears the memo.
    bool isActiveLayoutSuppressedForScreen(const QString& screenId) const;
    /// Uncached desktop-explicit form for snapshot-desktop callers (the
    /// deferred snap-assist build must judge the drop-time desktop, not a
    /// live read).
    bool isActiveLayoutSuppressedForScreen(const QString& screenId, int virtualDesktop) const;

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
    PhosphorEngine::IPlacementEngine* m_scrollEngine = nullptr; // Optional: per-screen scrolling check
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
    // Activity snapshot, same rationale as the desktop one: the suppression
    // gate and the layout resolve in the deferred compute would otherwise
    // read the LIVE activity and could pair the new activity's layout with
    // the drop activity's occupancy. Cleared wherever the desktop is.
    QString m_snapAssistPendingActivity;

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
    /// Set for the duration of one endDrag whose `cancelled` flag was true —
    /// KWin ended the interactive move itself (Escape, a fullscreen
    /// transition, another effect) and is restoring the window to where it
    /// started. dragStopped runs on that path only for its overlay/zone
    /// teardown; every branch that would COMMIT something must skip.
    ///
    /// Distinct from m_snapCancelled on purpose. That flag means "the user
    /// cancelled the snap", which additionally means the window is being
    /// dragged out, so it deliberately still runs the drag-out unsnap. An
    /// externally cancelled drag is not a drag-out: the window is going back
    /// where it was, and unsnapping it there would be a fresh defect rather
    /// than a fix. Cleared by resetDragState.
    bool m_dragExternallyCancelled = false;
    QString m_currentZoneId;
    /// Screen the zone in m_currentZoneId was resolved against. Part of the
    /// change-gate key, not decoration: the default layout assignment is
    /// GLOBAL, so two monitors routinely resolve the same Layout* and hand
    /// back the same zone UUID. Keyed on the id alone, a cursor crossing
    /// between them compares equal, skips the update, and leaves
    /// m_currentZoneGeometry holding the OLD monitor's absolute rect — which
    /// the drop's physical-screen guard then rejects, so the window
    /// float-drops instead of snapping.
    QString m_currentZoneScreenId;
    QRect m_currentZoneGeometry;
    bool m_snapCancelled = false;
    bool m_triggerReleasedAfterCancel = false; // Tracks release→press cycle for retrigger after Escape
    bool m_activationToggled = false; // Current toggle state (on/off)
    bool m_prevTriggerHeld = false; // Previous frame's trigger state for edge detection
    // Drag-insert toggle latch, shared across engines (the cursor is on one
    // screen at a time; the trigger LIST and toggle SETTING are selected per
    // tick by the engine owning the cursor screen).
    bool m_dragInsertToggled = false; // Current toggle state for drag-insert
    bool m_prevDragInsertHeld = false; // Previous frame's drag-insert trigger state
    // Journal-diagnostic dedup for the "drag-insert tick:" line: first tick
    // plus every raw-held transition logs once (a first-tick-only latch was
    // structurally blind to a mid-drag press, which once sent a whole
    // debugging session after the wrong fix).
    bool m_lastLoggedRawInsertHeld = false;
    bool m_zoneSpanToggled = false; // Current toggle state for zone span (toggle mode)
    bool m_prevZoneSpanTriggerHeld = false; // Previous frame's zone span trigger state for edge detection
    // Hold-mode release grace (resolveHoldGrace in dragactivation.h). The
    // timestamps sit on m_dragClock, started at beginDrag; -1 means the
    // trigger has not been physically held during this drag. Activation, zone
    // span and drag-insert each keep their own, since the three lists may bind
    // different buttons and are released at different moments.
    QElapsedTimer m_dragClock;
    qint64 m_activationLastHeldMs = -1;
    qint64 m_dragInsertLastHeldMs = -1;
    qint64 m_zoneSpanLastHeldMs = -1;
    // Snap assist's stamp is fed per tick like the others but CONSUMED at the
    // drop rather than on a tick, so it needs no expiry replay: endDrag reads
    // it once and the drag is over. That also makes it the arm most exposed to
    // the race, since the drop is the moment the lifting hand has let go.
    qint64 m_snapAssistLastHeldMs = -1;
    // A release followed by no pointer motion delivers no tick once the
    // grace has run out, so the zone state the release tick preserved would
    // stand until the drop and a drop long after the grace would still snap.
    // This single-shot replays the last tick's arguments at expiry so the
    // clear happens on time. Created lazily like m_dragScrollTimer.
    QTimer* m_graceExpiryTimer = nullptr;
    int m_lastTickCursorX = 0;
    int m_lastTickCursorY = 0;
    int m_lastTickModifiers = 0;
    int m_lastTickMouseButtons = 0;
    // Drag-to-reorder mode is active for the cursor's current ENGINE screen
    // (autotile or scrolling): cached so per-tick dragMoved work (60+ Hz)
    // doesn't re-query settings + engine per cursor update. Seeded at
    // beginDrag from the start screen (requires (a) engine-bypass path,
    // (b) the engine's reorder verdict — autotile's DragBehavior rule
    // cascade or scrolling's AlwaysActive sentinel, via
    // effectiveDragReorderModeFor, (c) window tiled at drag-start) and
    // RE-LATCHED to the cursor's screen on each policy flip in
    // updateDragCursor under the same conditions, with the tiled test
    // widened to "tiled OR a preview is live for this drag" (detach-once
    // un-tiles the dragged window while its preview lives). Cleared by
    // endDrag, clearPendingSnapDragState, cancelSnap, handleWindowClosed,
    // and the shared resetDragState teardown.
    bool m_dragReorderActive = false;
    // beginDrag's reorder fallback (preview begin refused → float-on-start
    // restored) is a PER-DRAG decision: while set, the policy-flip re-latch
    // must not re-arm reorder for this drag, or the fallback survives
    // exactly one tick. Cleared at beginDrag and in resetDragState.
    bool m_dragReorderAbandoned = false;
    // The dragged window matches an Exclude / ExcludePlacement rule (or the
    // minimum-window-size floor). Evaluated ONCE per drag (dragStarted, and
    // beginDrag's bypass and pending twins) through
    // SnapEngine::isWindowExcluded — the shared placement Exclude rules
    // (mode-neutral; the scroll side enforces the same rules effect-side
    // before a strip ever adopts such a window) plus the snap minimum-size
    // floor. So on either variant, a window whose placement would be
    // refused never sees the popup (the pre-existing gap where the trigger
    // check had only cursor coords). Cleared in resetDragState.
    bool m_dragWindowExcludedFromSelector = false;
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
    bool m_wasSnapped = false; // True if window was snapped to a zone when drag started

    // Per-drag memo for isActiveLayoutSuppressedForScreen (see its
    // definition), keyed on (screen, desktop). mutable: the predicate is
    // logically const and sits on the ~30 Hz dragMoved path. beginDrag and
    // onLayoutChanged clear it.
    mutable QString m_suppressMemoScreenId;
    mutable int m_suppressMemoDesktop = 0;
    mutable QString m_suppressMemoActivity;
    mutable bool m_suppressMemoValue = false;

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

    // Pre-parsed trigger caches (populated on beginDrag for every drag,
    // bypass or snap; used on every dragMoved tick)
    QVector<ParsedTrigger> m_cachedActivationTriggers;
    QVector<ParsedTrigger> m_cachedZoneSpanTriggers;
    QVector<ParsedTrigger> m_cachedSnapAssistTriggers;
    QVector<ParsedTrigger> m_cachedAutotileDragInsertTriggers;
    QVector<ParsedTrigger> m_cachedScrollingDragInsertTriggers;

    // Drag-insert preview state lives on the owning engine
    // (hasDragInsertPreview(), dragInsertPreviewScreenId()). The adaptor
    // queries the engines directly to avoid drift between caches.

    /// One-per-drag latch for the drag-insert tick diagnostic (the block's
    /// silent failure modes are indistinguishable from missing ticks in the
    /// journal without it). Reset by beginDrag.
    bool m_dragInsertTickLogged = false;

    // ── Edge auto-scroll driver (niri's dnd-edge-view-scroll) ───────────
    //
    // A repeating timer, not the cursor ticks: dragMoved only fires on
    // pointer MOTION (DragTracker emits on slotMouseChanged, throttled to
    // 32 ms), and the whole point of the feature is that a cursor PARKED in
    // the edge band keeps scrolling. The engine does the ramp; this side
    // only supplies a heartbeat, the last known cursor and the real elapsed
    // time between ticks.

    /// ~60 Hz while a drag-insert preview is live on the CURSOR's own engine
    /// screen (the arm site sits inside dragMoved's same-screen branch), on
    /// any engine including one that cannot auto-scroll — the arm does not
    /// test the engine, because the tick's own interface default answers
    /// "not me" for free. The one exception is the strip selector popup:
    /// while it is up on the preview's screen it owns aiming, so the arm is
    /// skipped and the engine disarmed. Created lazily. Most preview-end
    /// paths stop it explicitly; the per-output cancel stops it only once no
    /// preview is left anywhere, since it must not disturb a scroll running
    /// on another monitor. The tick itself is the backstop and stops when it
    /// finds no preview.
    QTimer* m_dragScrollTimer = nullptr;
    /// Wall time since the previous tick, so the engine's speed ramp is
    /// frame-rate independent and a late timer cannot lurch the strip.
    QElapsedTimer m_dragScrollElapsed;
    /// Cursor position from the last dragMoved that had a live drag-insert
    /// preview on the cursor's own engine screen, in screen pixels. NOT every
    /// pointer move: the single write sits inside that branch, immediately
    /// before the only site that arms the timer. That ordering is what makes
    /// it safe to leave uncleared — no tick can read a previous drag's parked
    /// cursor, because arming always re-writes it first. Do not "fix" it with
    /// a clear: a default QPoint is (0, 0), which is inside the leading band
    /// on most work areas, and the timer reads this.
    QPoint m_lastDragCursorPos;
    /// The drag the timer was armed for. A tick is at most 16 ms from
    /// firing when a drag ends, which is long enough for the next drag's
    /// eager preview to begin — and that tick would then nudge the NEW
    /// strip with the OLD drag's parked cursor.
    QString m_dragScrollWindowId;
    void ensureDragScrollTimerRunning(const QString& windowId);
    void stopDragScrollTimer();
    /// Arm / cancel the hold-mode release grace expiry replay. Arming keeps the
    /// earlier of any pending deadline, so the three trigger families can share
    /// the one timer.
    void armGraceExpiry(qint64 remainingMs);
    void stopGraceExpiry();
    /// One heartbeat. Named as a verb rather than `onDragScrollTick`: the
    /// `on*` prefix in this class marks a Qt slot (onLayoutChanged,
    /// onSnapAssistDismissed, both under `private Q_SLOTS:`), and this is a
    /// plain member reached through the pointer-to-member connect overload,
    /// which needs no slot marking. Keeping it off the slot surface also
    /// keeps it off the adaptor's introspected bus interface.
    void advanceDragScroll();

    // DRY helper: cancel any active drag-insert preview on either engine.
    void cancelDragInsertIfActive();

    /// DRY helper: mark @p windowId as under a compositor interactive move on
    /// BOTH placement engines (empty clears). Every set/clear site routes
    /// through here — a site marking only one engine is exactly how the
    /// autotile arm went missing (discussion #1028: any retile landing during
    /// a drag resized the window in the user's hand).
    void setEngineInteractiveDragWindow(const QString& windowId);

    /// DRY helper for teardown paths where the prior session is DEAD by
    /// construction (a fresh beginDrag, a compositor reconnect, a pending
    /// drag's exit): clear the interactive-drag mark FIRST, then cancel any
    /// leftover preview with an explicit dragStillActive=false so its
    /// snap-back geometry is actually emitted. cancelDragInsertIfActive is
    /// wrong on these paths twice over — its session-liveness derivation can
    /// read a dead session's ids as a live drag, and a mark still standing
    /// during the cancel suppresses the snap-back through applyTiling /
    /// applyLayout even when the flag is false, parking the window at its
    /// mid-drag geometry.
    void cancelStaleDragInsertPreviews();

    /// Screen the drop indicator was last pushed to, empty when none is
    /// showing. Tracked rather than re-derived because the clear has to reach
    /// the screen the indicator is ON, which after a cross-screen drag is no
    /// longer the screen under the cursor.
    QString m_dropIndicatorScreenId;
    /// Push the drop-target indicator for a live preview on @p screenId,
    /// hiding the previous screen's indicator first when the drag crossed
    /// screens so a cross-screen drag cannot strand one behind it. Hides
    /// ride the overlay's animate=false path directly — see
    /// clearScrollDropIndicator.
    ///
    /// Animated by default: the cursor-tick caller follows a cursor move, so
    /// a rect change there is the user aiming somewhere new (the overlay
    /// change-gates, so an unchanged rect animates nothing). Pass
    /// @p animate false for the edge auto-scroll's own pushes: those land
    /// every ~16 ms against a ~100 ms transition, so animating them
    /// retargets the rect six times before it can settle and it stretches
    /// instead of sliding.
    void pushScrollDropIndicator(const QString& screenId, const QRect& rect, bool animate = true);
    /// Preview-end teardown for the drop indicator. Safe to call with none
    /// showing.
    ///
    /// A stranded indicator sits on the desktop with no drag left to dismiss
    /// it, so every preview-end path calls this. The list is longer than it
    /// looks, because an engine can drop its own preview WITHOUT telling the
    /// adaptor — several engine-side self-cancel sites do exactly that — so the
    /// daemon cannot rely on being notified and repairs wherever it notices:
    ///   - settleDragInsertPreviewAt, on all three arms including the one
    ///     that finds no engine at all (a drag is still ending there);
    ///   - cancelDragInsertIfActive, the shared cancel;
    ///   - cancelDragInsertPreviewsForScreen, keyed on the DEPARTING screen
    ///     rather than on whether it cancelled anything, since a prune may
    ///     have self-cancelled first;
    ///   - dragMoved's failed-begin arm, its trigger-release cancel arm, and
    ///     its cross-engine / cross-screen preview-cancel arm, none of which
    ///     route through the shared teardown;
    ///   - endDrag's disabled/suppressed arm (SnappingDisabled,
    ///     ContextDisabled, LayoutSuppressed), which ends the drag with the
    ///     preview alive.
    /// Adding a new preview-end path means adding a call here; the earlier
    /// version of this comment asserted the rule without the code keeping it.
    void clearScrollDropIndicator();

    /// Drop-path settle for a live drag-insert preview (either engine).
    /// Commits it and returns true when the preview belongs to the screen
    /// under (@p cursorX, @p cursorY); otherwise cancels it (or does nothing
    /// when no preview is live) and returns false. Shared by the two drop
    /// entry points (drop.cpp's dragStopped and drag_protocol.cpp's
    /// engine-owned bypass), which differ only in how they finalize.
    ///
    /// A valid strip-popup selection for the release screen outranks the
    /// cursor-derived target at commit: the last hit-test the user saw on
    /// the popup is the drop they meant. When no preview is live at all
    /// (hold-mode trigger never held) but the popup holds a valid target,
    /// the settle runs the full begin → update → commit itself; @p windowId
    /// is only consumed by that arm.
    bool settleDragInsertPreviewAt(int cursorX, int cursorY, const QString& windowId = QString());

    /// Whether the scroll engine owns @p screenId AND asks the drag popup
    /// to render its drag-insert vocabulary there (the strip-mode selector).
    /// The one capability read every selector gate in this class shares.
    bool scrollSelectorScreen(const QString& screenId) const;

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
     * Unregisters the Escape shortcut that shortcuts_wiring.cpp's snapAssistShown
     * handler bound for snap assist
     */
    void onSnapAssistDismissed();
};

} // namespace PlasmaZones
