// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include "core/types/types.h"
#include <PhosphorProtocol/NavigationMarshalling.h>
#include <PhosphorProtocol/WindowMarshalling.h>
#include <QDBusAbstractAdaptor>
#include <QHash>
#include <QObject>
#include <QRect>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <functional>

namespace PhosphorContext {
class IContextResolver;
} // namespace PhosphorContext

namespace PhosphorSnapEngine {
class SnapEngine;
}

namespace PlasmaZones {

class WindowTrackingAdaptor;
class ISettings;

/**
 * @brief D-Bus adaptor for snap-mode window placement
 *
 * Provides D-Bus interface: org.plasmazones.Snap
 *
 * Owns the snap-specific D-Bus surface: commit/uncommit, snap-restore
 * (placement rule / emptyZone / lastZone / resolveWindowRestore),
 * resnap, calculateSnapAllWindows, windowsSnappedBatch, snap-mode navigation
 * (move/focus/swap/push/snap-by-number/rotate/cycle/restore),
 * snap-mode convenience (moveWindowToZone, swapWindowsById), and
 * snap-mode float (toggleFloatForWindow, setWindowFloat, calculateUnfloatRestore,
 * windowUnsnappedForFloat).
 *
 * Signal relay from SnapEngine to WindowTrackingAdaptor is also wired
 * here (navigationFeedback, windowFloatingChanged, applyGeometryRequested,
 * fullscreenHandBackRequested, restatementGeometryRequested,
 * snapAllWindowsRequested, applyGeometriesBatch, activateWindowRequested).
 * SnapEngine::resnapToNewLayoutRequested routes to this adaptor's
 * applyEngineResnap (bookkeeping + applyGeometriesBatch emission), not
 * directly to WTA; the bus slot handleBatchedResnap checks external batches.
 * The batches and the re-statements go out through the announce sequencer
 * (setAnnounceSequencer).
 *
 * @see SnapEngine, WindowTrackingAdaptor
 */
class PLASMAZONES_EXPORT SnapAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.Snap")

public:
    /**
     * @brief Construct a SnapAdaptor
     *
     * Connects the SnapEngine signals listed in the class doc to the
     * WindowTrackingAdaptor for D-Bus relay; windowSnapStateChanged and
     * windowFloatingClearedForSnap are wired in the WTA's enginewiring.cpp.
     *
     * @param engine SnapEngine to relay signals from (not owned)
     * @param adaptor WindowTrackingAdaptor to relay signals to (not owned)
     * @param settings ISettings for the snapping master switch (snappingEnabled)
     *        the restore and bus gates read; not owned
     * @param parent Parent QObject (must be the D-Bus-registered daemon)
     */
    explicit SnapAdaptor(PhosphorSnapEngine::SnapEngine* engine, WindowTrackingAdaptor* adaptor, ISettings* settings,
                         QObject* parent = nullptr);
    ~SnapAdaptor() override = default;

    /**
     * @brief Clear the engine pointer during shutdown
     *
     * Drops the signal relays its constructor made and every borrow.
     * Called by Daemon::stop() before the SnapEngine unique_ptr is reset.
     */
    void clearEngine();

    /// Re-apply the zones the windows hold on the screens snapping runs on in
    /// an enabled context (only @p screens when not empty), for a settings
    /// reflow. A re-statement batch ("restate": no Snap Assist) by default;
    /// never an OSD. Not a D-Bus slot.
    void reapplySnapZones(const QStringList& screens = {}, bool restatement = true);

    /**
     * @brief Set the frozen-snapshot resolver used by snaprestore's disable
     *        gate. Set right after construction by
     *        Daemon::initEnginesAndWiring (the resolver already exists then);
     *        a setter so stop() and clearEngine() can null it.
     *
     * @param resolver IContextResolver instance (not owned; Daemon::stop() clears it after clearEngine())
     */
    void setContextResolver(PhosphorContext::IContextResolver* resolver)
    {
        m_contextResolver = resolver;
    }

    /// Runs the work it is handed behind any screens announce still queued,
    /// so the effect judges a placement against the managed set it was
    /// computed for (TilingAdaptor::runBehindScreensAnnounce, #1151). Every
    /// zone batch this adaptor emits goes through it (the engine's own, the
    /// settings reflows, the bus resnaps and the virtual-screen resnap), so
    /// they reach the effect in the order they were committed, and so does
    /// every re-statement of one window (restatementGeometryRequested). A held
    /// placement of a window is dropped when another placement of that window
    /// goes out before it (a user verb in the same turn). Unset: emitted at
    /// once.
    using AnnounceSequencer = std::function<void(std::function<void()>)>;
    void setAnnounceSequencer(AnnounceSequencer sequencer)
    {
        m_announceSequencer = std::move(sequencer);
    }

public Q_SLOTS:
    // ═══════════════════════════════════════════════════════════════════════════
    // Snap-commit D-Bus slots
    // ═══════════════════════════════════════════════════════════════════════════

    // The confirmations, recordSnapIntent, the snap-restore slots,
    // calculateSnapAllWindows and windowUnsnappedForFloat are compositor-only
    // (DbusHelpers::admitCompositorReport, #1155): any bus caller but the
    // registered bridge is answered with AccessDenied and nothing runs.

    /**
     * @brief Confirm a single-zone snap from the KWin effect (snap assist).
     *        Commits only a live window, a known screen, a window on the
     *        landing desktop and the current activity, and a zone that
     *        screen's layout holds (validBusSnapTarget).
     */
    void windowSnapped(const QString& windowId, const QString& zoneId, const QString& screenId);

    /**
     * @brief Confirm a multi-zone snap, checked like windowSnapped. No
     *        in-tree caller; a compositor bridge's contract surface.
     */
    void windowSnappedMultiZone(const QString& windowId, const QStringList& zoneIds, const QString& screenId);

    /**
     * @brief Confirm an unsnap. No in-tree caller; a compositor bridge's
     *        contract surface. A window left with no zone and no float also
     *        loses its snap slot.
     */
    void windowUnsnapped(const QString& windowId);

    /**
     * @brief The effect's snap-all confirmation
     */
    void windowsSnappedBatch(const PhosphorProtocol::SnapConfirmationList& entries);

    /**
     * @brief Record that a window class was USER-snapped (not auto-snapped)
     */
    void recordSnapIntent(const QString& windowId, bool wasUserInitiated);

    // ═══════════════════════════════════════════════════════════════════════════
    // Snap-restore D-Bus slots
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Snap a new window to the last used zone. No in-tree caller (the
     *        effect restores through resolveWindowRestore); a compositor
     *        bridge's contract surface.
     */
    void snapToLastZone(const QString& windowId, const QString& windowScreenId, bool sticky, int& snapX, int& snapY,
                        int& snapWidth, int& snapHeight, bool& shouldSnap);

    /**
     * @brief Snap a window to its SnapToZone-rule-defined zone(s). No in-tree
     *        caller; a compositor bridge's contract surface like snapToLastZone.
     */
    void snapToAppRule(const QString& windowId, const QString& windowScreenId, bool sticky, int& snapX, int& snapY,
                       int& snapWidth, int& snapHeight, bool& shouldSnap);

    /**
     * @brief Snap a window to the first empty zone
     */
    void snapToEmptyZone(const QString& windowId, const QString& windowScreenId, bool sticky, int& snapX, int& snapY,
                         int& snapWidth, int& snapHeight, bool& shouldSnap);

    /**
     * @brief Run the full snap-restore resolution (WindowPlacementStore restore +
     *        placement-rule / empty-zone / last-zone fallback chain) in one call.
     *        A screen id the daemon does not know (resolveBusScreen answers empty)
     *        gets the no-snap answer and runs nothing.
     * @param windowKind Structural kind of the opening window (0=Unknown, 1=Normal, 2=Transient).
     *                   Accepted for wire compatibility and ignored: nothing sets a
     *                   placement record's kind from a live window, so none carries one.
     * @param restoreReason Why this resolve is running — see
     *                   PhosphorEngine::RestoreReason. Clamped from the wire, so an
     *                   unrecognised value reads as Open. Gates the open claim,
     *                   the RouteToDesktop / RouteToScreen routing and the
     *                   DesktopArrival held-zone re-apply, and is forwarded to
     *                   the engine, where it gates the FIFO, a re-entry re-snap,
     *                   a leaver's rule and the free-size restore of a floated
     *                   open (#1106).
     * @param minWidth, minHeight Pinned by the v9 signature, accepted and ignored.
     */
    void resolveWindowRestore(const QString& windowId, const QString& screenId, bool sticky, int windowKind,
                              int restoreReason, int minWidth, int minHeight, int& snapX, int& snapY, int& snapWidth,
                              int& snapHeight, bool& shouldSnap);

    // ═══════════════════════════════════════════════════════════════════════════
    // Resnap / snap-all D-Bus slots
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Resnap all windows from the previous layout to the current layout
     */
    void resnapToNewLayout();

    /**
     * @brief Resnap windows to their current zone assignments, on screens
     *        snapping runs and the user has not disabled
     * @param screenFilter When non-empty, only resnap windows on this screen
     *        (a monitor's id covers all its virtual screens)
     */
    void resnapCurrentAssignments(const QString& screenFilter = QString());

    /**
     * @brief Resnap windows from autotile to manual zones using explicit window
     *        order, on a screen snapping runs, for live windows it does not exclude
     */
    void resnapFromAutotileOrder(const QStringList& autotileWindowOrder, const QString& screenId);

    /**
     * @brief Calculate snap assignments for all provided windows. It records
     *        each candidate's free frame, so it is compositor-only.
     */
    PhosphorProtocol::SnapAllResultList calculateSnapAllWindows(const QStringList& windowIds, const QString& screenId);

    /**
     * @brief Ask the effect to snap every window on a screen: a screen the
     *        daemon knows, which snapping runs, switched on and not disabled
     *        in the context in view
     */
    void snapAllWindows(const QString& screenId);

    /**
     * @brief Commit an external batch of resnap entries, each checked as a bus
     *        snap is (admitBusSnap) and dropped when it fails. A restore entry
     *        (unsnap and put back at a caller's rect) is taken only from the
     *        registered compositor bridge or a direct call
     */
    void handleBatchedResnap(const QString& resnapData);

    // ═══════════════════════════════════════════════════════════════════════════
    // Snap-mode navigation D-Bus slots
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Move the focused window to an adjacent zone (daemon-driven)
     * @param direction Direction to move ("left", "right", "up", "down")
     */
    void moveWindowToAdjacentZone(const QString& direction);

    /**
     * @brief Focus a window in an adjacent zone (daemon-driven)
     * @param direction Direction to look for windows ("left", "right", "up", "down")
     */
    void focusAdjacentZone(const QString& direction);

    /**
     * @brief Push the focused window to the first empty zone (daemon-driven)
     * @param screenId A named screen is the target; empty acts like the shortcut
     *        (the focused window's own screen when snapped, else the cursor's)
     */
    void pushToEmptyZone(const QString& screenId = QString());

    /**
     * @brief Take the focused window out of its zone and float it at its
     *        float-back position and size (daemon-driven)
     */
    void restoreWindowSize();

    /**
     * @brief Swap the focused window with the window in an adjacent zone (daemon-driven)
     * @param direction Direction to swap ("left", "right", "up", "down")
     */
    void swapWindowWithAdjacentZone(const QString& direction);

    /**
     * @brief Snap the focused window to a zone by its number (daemon-driven)
     * @param zoneNumber Zone number, 1-based; 1-9 when no screen is named
     * @param screenId A named screen is the target (its layout for the desktop it
     *        shows); empty acts like the shortcut
     */
    void snapToZoneByNumber(int zoneNumber, const QString& screenId = QString());

    /**
     * @brief Rotate windows in the layout for a specific screen (daemon-driven)
     * @param clockwise true for clockwise rotation, false for counterclockwise
     * @param screenId Screen to rotate on (empty = all screens); one snapping
     *        does not run rotates nothing
     */
    void rotateWindowsInLayout(bool clockwise, const QString& screenId = QString());

    /**
     * @brief Cycle focus between windows stacked in the same zone (daemon-driven)
     * @param forward true to cycle to next window, false to cycle to previous
     */
    void cycleWindowsInZone(bool forward);

    // ═══════════════════════════════════════════════════════════════════════════
    // Snap-mode convenience D-Bus slots
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Convenience: snap a window to a specific zone by ID
     * @param windowId Window to snap
     * @param zoneId Target zone UUID
     */
    void moveWindowToZone(const QString& windowId, const QString& zoneId);

    /**
     * @brief Convenience: swap two specific windows by ID
     * @param windowId1 First window
     * @param windowId2 Second window
     */
    void swapWindowsById(const QString& windowId1, const QString& windowId2);

    // ═══════════════════════════════════════════════════════════════════════════
    // Snap-mode float D-Bus slots
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Float toggle for snap-mode screens: the shell placement map's,
     *        gated as the keyboard toggle is (admitBusFloat)
     * @param windowId Window identifier
     * @param screenId The screen the window is on (empty: its live screen);
     *        a screen it is not on is refused, with a log line (F1138)
     */
    void toggleFloatForWindow(const QString& windowId, const QString& screenId);

    /**
     * @brief Set a window's floating state on its live screen (snap-mode
     *        direct, gated as the toggle is). Like calculateUnfloatRestore it
     *        has no in-tree caller and stays as external contract surface.
     * @param windowId Window identifier
     * @param floating true to float, false to unfloat
     */
    void setWindowFloat(const QString& windowId, bool floating);

    /**
     * @brief Calculate unfloat restore geometry and zone IDs in a single call
     * @param windowId Window identifier
     * @param screenId Screen for geometry calculation
     * @return PhosphorProtocol::UnfloatRestoreResult with found, zoneIds, screenName, x, y, width, height
     */
    PhosphorProtocol::UnfloatRestoreResult calculateUnfloatRestore(const QString& windowId, const QString& screenId);

    /**
     * @brief First half of a two-call float: unsnap the window and keep its
     *        zone as the pre-float home. It does not float the window, so
     *        setWindowFloat(true) follows it. Compositor-only: from another
     *        caller the gated second half could refuse and leave the window
     *        in neither state.
     * @param windowId Window identifier
     */
    void windowUnsnappedForFloat(const QString& windowId);

public:
    // ═══════════════════════════════════════════════════════════════════════════
    // Internal — plain `public:` (NOT Q_SLOTS, no Q_INVOKABLE) so
    // QDBusAbstractAdaptor's introspection does not expose them on the bus.
    // Every caller is in-process and reaches these via direct C++ invocation
    // through the daemon (same pattern as
    // WindowDragAdaptor::handleWindowClosed).
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Resnap for virtual screen reconfigure (silent, no snap-assist)
     *
     * Not a D-Bus method. Called from Daemon when VS config changes.
     */
    void resnapForVirtualScreenReconfigure(const QString& physicalScreenId);

    /// Resolve a resnap filter into the concrete list of snap-mode screens.
    QStringList resolveSnapModeScreensForResnap(const QString& screenFilter) const;

    /// moveWindowToZone on a named screen. @p screenHint names the screen (a
    /// split monitor's connector name resolves to the virtual screen the window
    /// is in); empty means the window's own screen when its layout holds the
    /// zone, else the screen whose layout does. Refused unless a keyboard snap
    /// could make the same move (admitBusSnap).
    void moveWindowToZoneOnScreen(const QString& windowId, const QString& zoneId, const QString& screenHint);
    /// The zone numbered @p zoneNumber in the layout of @p screenHint (empty: the
    /// window's own screen), for Control.snapWindowToZone. A missing number is
    /// reported as zone_not_found.
    void moveWindowToZoneNumberOnScreen(const QString& windowId, int zoneNumber, const QString& screenHint);

private:
    // ═══════════════════════════════════════════════════════════════════════════
    // Private helpers
    // ═══════════════════════════════════════════════════════════════════════════

    bool validateWindowId(const QString& windowId, const QString& operation) const;

    /**
     * @brief Apply a successful SnapResult: assign outputs, mark auto-snapped,
     *        clear floating state, and track the zone assignment.
     *
     * Returns false (and leaves the out-params at 0 / false) when the snap is
     * refused: missing dependencies, the global `snappingEnabled()` kill-switch
     * is off, or the target context is disabled by the cascade. A false return
     * means no commit happened; callers must skip any post-snap work (e.g.
     * success logging). @p intent is AutoRestored for a placement that may
     * take focus (a genuine open, a drop's auto-fill) and AutoReplaced for a
     * re-placement of a window that did not just open.
     */
    bool applySnapResult(const SnapResult& result, const QString& windowId, int& snapX, int& snapY, int& snapWidth,
                         int& snapHeight, bool& shouldSnap,
                         PhosphorEngine::SnapIntent intent = PhosphorEngine::SnapIntent::AutoRestored);

    /**
     * @brief The two user-facing refusals every auto-snap answer this facade
     *        gives has to pass.
     *
     * The global `snappingEnabled()` kill-switch (discussion #461 item 2) and
     * the disabled-context gate for the context the window will land in.
     * Extracted so applySnapResult is not the only place they live: an answer
     * that writes its geometry out-params directly — the desktop-arrival
     * re-apply in resolveWindowRestore — never reaches applySnapResult and
     * would otherwise slip both, snapping a window on a monitor, desktop or
     * activity the user has turned PlasmaZones off for.
     *
     * @p virtualDesktop is the desktop the window lands on, or 0 for "the one
     * that context is showing now".
     */
    bool snapPermittedForContext(const QString& windowId, const QString& screenId, int virtualDesktop) const;

    /// Where a bus-requested snap of one window lands. desktop 0 = the one the
    /// screen shows.
    struct BusSnapTarget
    {
        QString screenId;
        int desktop = 0;
    };
    /// The validity checks every bus snap passes (busgate.cpp): a live window
    /// (F142); a screen the daemon knows, from @p screenHint, else the window's
    /// own when its layout holds @p zoneIds (F21), else the zone's; the window on
    /// the landing desktop and activity (F179); and that context's layout holding
    /// every zone, so no unknown id, sentinel or ghost-layout zone commits.
    std::optional<BusSnapTarget> validBusSnapTarget(const QString& windowId, const QStringList& zoneIds,
                                                    const QString& screenHint, int pinnedDesktop = 0) const;
    /// validBusSnapTarget plus what the keyboard twin applies to a user verb:
    /// the window is not being dragged (F773), snapping runs the screen live, is
    /// switched on and not disabled in the landing context, and the window is
    /// not excluded (F20). The one bus-verb gate.
    std::optional<BusSnapTarget> admitBusSnap(const QString& windowId, const QStringList& zoneIds,
                                              const QString& screenHint, int pinnedDesktop = 0) const;
    /// The desktop @p windowId lands on for @p screenId: 0 when it is on the one
    /// the screen shows or on all, its one desktop when on a single hidden one,
    /// nullopt when on several hidden ones or not on @p pinnedDesktop.
    std::optional<int> landingDesktop(const QString& windowId, const QString& screenId, int pinnedDesktop) const;
    /// The keyboard gates for a bus verb on the focused window: snapping on, a
    /// named screen known, and the screen the verb acts on running snapping and,
    /// when it @p placesWindow, not disabled in the context in view and the
    /// focused window not being dragged (F773).
    bool focusedVerbPermitted(const QString& screenHint, bool placesWindow) const;
    /// The screen a bus float or unfloat of @p windowId acts on, always its
    /// live screen, or empty when @p screenHint resolves to another one
    /// (F1138), a keyboard toggle could not act there, or the window is being
    /// dragged (F773).
    QString admitBusFloat(const QString& windowId, const QString& screenHint) const;
    /// Release the record's snap slot once no store holds a zone or the float
    /// bit for @p windowId (an unsnap confirmation leaves it free).
    void releaseSnapSlotIfFree(const QString& windowId);
    /// A snap confirmation (windowSnapped, the batch): checked by
    /// validBusSnapTarget, then committed. True when committed.
    bool confirmSnap(const QString& windowId, const QString& zoneId, const QString& screenId);
    /// The engine's own resnap batches (resnapToNewLayoutRequested), committed
    /// as they are, their emission sequenced like every batch (commitBatch).
    void applyEngineResnap(const QString& resnapData);
    /// Commit @p entries through SnapEngine::applyBatchAssignments at once and
    /// emit the resulting applyGeometriesBatch through emitPlacementBatch.
    /// True when anything was committed.
    bool commitBatch(const QVector<ZoneAssignmentEntry>& entries, const QString& action);
    /// commitBatch, relayed as "restate" (no Snap Assist) when every
    /// entry re-states a placement, else as "resnap".
    void commitResnapBatch(const QVector<ZoneAssignmentEntry>& entries);
    /// Emit @p geometries as applyGeometriesBatch through the announce
    /// sequencer (see setAnnounceSequencer), or at once without one.
    void emitPlacementBatch(PhosphorProtocol::WindowGeometryList geometries, const QString& action);
    /// A re-statement of one window's placement as applyGeometryRequested,
    /// sequenced like the batches.
    void emitRestatement(const QString& windowId, const QRect& geometry, const QString& zoneId,
                         const QString& screenId);
    /// Hold a placement of @p windowIds: the serial it is held under, which is
    /// from now on the newest held placement of each of them.
    quint64 holdPlacement(const QStringList& windowIds);
    /// Of @p windowIds, the ones whose newest held placement is @p serial,
    /// released from the hold.
    QSet<QString> releaseHeldPlacement(const QStringList& windowIds, quint64 serial);
    /// Another placement of these windows went out: their held ones are stale.
    void supersedeHeldPlacements(const QStringList& windowIds);

    PhosphorSnapEngine::SnapEngine* m_engine = nullptr;
    WindowTrackingAdaptor* m_adaptor = nullptr;
    ISettings* m_settings = nullptr;
    /// One-shot latch for the "called before panel geometry ready" warning, so
    /// the four snap-restore slots log it once between them and then fall back
    /// to debug. Per-adaptor rather than a function-local static: a static is
    /// process-wide, and a ctest binary running several fixtures would see the
    /// warning only for whichever fixture happened to hit the path first.
    bool m_snapNotReadyWarned = false;
    /// Late-bound by Daemon via setContextResolver — replaces the inline
    /// `(modeFor → isContextDisabled)` cascade in snaprestore.cpp.
    PhosphorContext::IContextResolver* m_contextResolver = nullptr;
    /// See setAnnounceSequencer.
    AnnounceSequencer m_announceSequencer;
    /// The serial of the newest placement of each window still held behind
    /// the announce, and the last serial handed out. A window leaves the map
    /// when its held placement goes out or another placement of it does.
    QHash<QString, quint64> m_heldPlacementSerial;
    quint64 m_lastHeldSerial = 0;

    // Stored handles for the signal relays wired in the constructor so
    // clearEngine() can disconnect exactly the connections this class
    // made. A broad disconnect(m_engine, nullptr, m_adaptor, nullptr)
    // would also remove any connection another class happens to make
    // between the same sender/receiver pair — a latent footgun the
    // targeted approach avoids.
    QVector<QMetaObject::Connection> m_connections;
};

} // namespace PlasmaZones
