// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// FILE-SIZE EXCEPTION (sanctioned): TilingHandler is one class declaration,
// and the implementation is already partitioned across this directory
// (tiling.cpp, state.cpp, wiring.cpp, windowedfullscreen.cpp,
// pretilegeometry.cpp, floatcleanup.cpp) — every one of those TUs calls back
// through this single declaration, which C++ requires to be whole. Most of the
// length is the per-member invariant prose the split files depend on: the
// per-session daemon state (managed/scrolling/axis sets and their teardown
// pairings), the generation guards, and the inline predicates those TUs share.
// Splitting the class itself would mean a second handler type with the same
// state, which is the coupling this file exists to avoid.

#pragma once

#include "compositor/deferredwindowcommits.h"

#include <PhosphorCompositor/TilingState.h>
#include <PhosphorProtocol/AutotileMarshalling.h>
#include <PhosphorProtocol/ScrollAxisEnum.h>

#include <QHash>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QRect>
#include <QRectF>
#include <QSet>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include <cstdint>

class QAction;
class QTimer;

namespace KWin {
class EffectWindow;
class Window;
}

namespace PlasmaZones {

// Targeted using-declarations, not a namespace-wide directive: headers must
// not leak the whole PhosphorCompositor namespace into every includer.
using PhosphorCompositor::BorderState;
namespace TilingStateHelpers = PhosphorCompositor::TilingStateHelpers;

class PlasmaZonesEffect;

/**
 * @brief Handles tiling-family engine integration (autotile + scrolling) for PlasmaZones
 *
 * Manages the autotile D-Bus interface, screen tracking, window tiling,
 * monocle mode, tiled-tracking for border rendering, and pre-autotile
 * geometry preservation. Title-bar (borderless) state is owned by the
 * effect's DecorationManager and driven by rules — this handler does
 * not touch decorations.
 *
 * Delegates window lookups, geometry application, and animation back to the effect.
 */
class TilingHandler : public QObject
{
    Q_OBJECT

public:
    explicit TilingHandler(PlasmaZonesEffect* effect, QObject* parent = nullptr);

    // ═══════════════════════════════════════════════════════════════════
    // Integration points (called by PlasmaZonesEffect)
    // ═══════════════════════════════════════════════════════════════════

    /// Returns true when the window was on an autotile screen and a
    /// `windowOpened` D-Bus call was issued (i.e. the daemon is expected
    /// to tile it, producing a moveResize). Returns false when the call
    /// was filtered out locally (ineligible window, non-autotile screen,
    /// already-notified) — callers that depend on a follow-up tile must
    /// not wait for one in that case (used by the first-frame open
    /// suppression path to release suppression immediately on a no-op).
    ///
    /// @p knownFreeFloating governs the pre-autotile geometry capture: the
    /// genuine window-opened/spawn path passes `true` (the frame is KWin's
    /// spawn geometry and the FloatingCache is not yet populated, so the
    /// isWindowFloating() guard must be bypassed). RE-ADD callers (a window
    /// already known to the engine being re-announced — cross-screen
    /// transfer, desktop-return catch-scan) pass `false`: the frame may be
    /// a tiled zone rect, so the floating guard MUST run and reject it,
    /// otherwise the tiled rect would be persisted as the window's
    /// free-floating geometry and clobber the daemon's real float-back.
    bool notifyWindowAdded(KWin::EffectWindow* w, bool knownFreeFloating);

    /**
     * @brief Batch-notify windows added to engine-managed screens
     *
     * Filters windows the same way as notifyWindowAdded, then sends one
     * windowsOpenedBatch D-Bus call instead of per-window windowOpened calls.
     * Used on daemon startup/restart and autotile toggle-on.
     *
     * @param windows List of candidate windows to process
     * @param screenFilter If non-empty, only process windows on these screens
     * @param resetNotified If true, remove windowId from m_notifiedWindows before processing
     *        (for re-announce on daemon restart / screen change)
     * @param enteringAutotile True when the screen has just changed from snap
     *        mode. Already-minimized windows then need an immediate first
     *        autotile placement when they become visible.
     * @param screenOverrides Per-window screen ids resolved by the caller,
     *        used in place of getWindowScreenId for the windows it names. The
     *        engine-flip caller (setScrollingScreens) MUST supply these: the
     *        engine-authoritative screen override is gated on the scrolling
     *        set, so once the flip has been written a parked strip column
     *        resolves positionally onto the neighbouring output and the
     *        screenFilter drops it. The value chosen here is the one used for
     *        the filter, the m_notifiedWindowScreens stamp, the pre-tile
     *        capture and the wire entry alike, so an override cannot desync
     *        them.
     */
    void notifyWindowsAddedBatch(const QList<KWin::EffectWindow*>& windows, const QSet<QString>& screenFilter = {},
                                 bool resetNotified = false, bool enteringAutotile = false,
                                 const QHash<KWin::EffectWindow*, QString>& screenOverrides = {});

    /// Remove a window from this handler's autotile tracking and notify the daemon.
    /// INTENT NOTE: an earlier m_pendingCloses guard (deduping a close racing
    /// its own re-announce) was deliberately deleted — window ids are
    /// uuid-based, so a closed id can never be re-announced, and the
    /// regression the guard defended against is unreachable. Do not
    /// reintroduce it as "missing hardening".
    void onWindowClosed(const QString& windowId, const QString& screenId);

    /// Drop a LIVE window from engine tracking without a close (the
    /// drag-bypass revert). Same effect-side cleanup as onWindowClosed, but
    /// the daemon relay is Tiling.releaseWindowTracking, which runs NO
    /// placement capture — the window is mid-drag and its frame must never
    /// be recorded as a float-back.
    void releaseWindowTracking(const QString& windowId, const QString& screenId);
    /// Tear down all effect-side autotile tracking for @p windowId (shared +
    /// KWin-specific state, incl. the pending cross-screen-restore connection)
    /// WITHOUT notifying the daemon. Shared by onWindowClosed (which adds the
    /// daemon windowClosed call) and the cross-mode-move marker path (where the
    /// daemon already relinquished the window via handoffRelease).
    /// No screenId parameter: every container this touches is either keyed by
    /// windowId or swept across all screens. The saved stacking orders used to
    /// be pruned only for a caller-supplied screen, which left the id behind on
    /// every other screen's order — including the DESTINATION's on a
    /// cross-output transfer, where the caller passes the source.
    void cleanupAutotileTracking(const QString& windowId);
    /// Drop @p windowId from the minimize-float set and cancel EITHER
    /// deferred edge — the minimize debounce and the unminimize commit —
    /// mirroring SnapHandler::removeMinimizeFloated, plus the untiled-marker
    /// drop (snap has no untiled-marker counterpart). Returns true if
    /// the window was tracked. Callers: close cleanup, the effect's
    /// authoritative visible unfloat (slotWindowFloatingChanged, including
    /// its dual-hold repair), and the cross-mode adoption hops (snap's
    /// adoption paths and countermand hand-offs).
    bool removeMinimizeFloated(const QString& windowId)
    {
        cancelPendingMinimizeFloat(windowId);
        cancelPendingUnminimizeUnfloat(windowId);
        m_untiledMinimizeFloats.remove(windowId);
        m_unfloatRetryAttempts.remove(windowId);
        const bool owned = m_minimizeFloatedWindows.remove(windowId);
        return m_unfloatInFlight.remove(windowId) > 0 || owned;
    }

    /// Whether this handler currently owns @p windowId's minimize-float
    /// marker (mirrors SnapHandler::isMinimizeFloated). Includes the
    /// in-flight unfloat interval: dispatchUnminimizeUnfloat moves the id
    /// from the marker set into m_unfloatInFlight, and during that window
    /// the handler still owns the float — the dual-hold repair and the
    /// capture guards must not treat it as unowned.
    bool isMinimizeFloated(const QString& windowId) const
    {
        return m_minimizeFloatedWindows.contains(windowId) || m_unfloatInFlight.contains(windowId);
    }

    /// Take ownership of a minimize-float relinquished by the snap handler
    /// (cross-mode countermand / hand-off on a screen autotile now owns).
    /// @p untiled marks the window's rect as belonging to the prior mode so
    /// its unminimize commits immediately instead of through the animation
    /// grace.
    void adoptMinimizeFloated(const QString& windowId, bool untiled)
    {
        m_minimizeFloatedWindows.insert(windowId);
        if (untiled) {
            m_untiledMinimizeFloats.insert(windowId);
        }
    }

    /// Retry-budget hand-off for cross-mode adoptions: the budget is a
    /// per-WINDOW cap on daemon unfloat attempts, and it must survive the
    /// ownership hop or a persistently-refused unfloat ping-ponging between
    /// handlers resets to a fresh budget on every bounce.
    int unfloatRetryBudgetUsed(const QString& windowId) const
    {
        return m_unfloatRetryAttempts.value(windowId);
    }
    void seedUnfloatRetryBudget(const QString& windowId, int attemptsUsed)
    {
        if (attemptsUsed > m_unfloatRetryAttempts.value(windowId)) {
            m_unfloatRetryAttempts.insert(windowId, attemptsUsed);
        }
    }
    bool hasPendingUnminimizeUnfloat(const QString& windowId) const
    {
        return m_pendingUnminimizeUnfloat.contains(windowId);
    }
    bool hasUnfloatInFlight(const QString& windowId) const
    {
        return m_unfloatInFlight.contains(windowId);
    }
    /// Drop a destroyed window's desktop-move geometry stash. Separate from
    /// onWindowClosed because the desktop-MOVE path calls onWindowClosed
    /// right after creating the stash (the window must look "closed" to this
    /// desktop's tiling) — only genuine destruction may clear it.
    void clearDesktopMoveStash(const QString& windowId)
    {
        m_savedPreTileForDesktopMove.remove(windowId);
    }
    bool isScreenQueryPending() const
    {
        return m_initialScreenQueryPending;
    }
    void deferWindowRouting(KWin::EffectWindow* window, bool canSnapRestore);
    void onDaemonReady();
    /// Drop every map whose contents belong to ONE daemon session.
    ///
    /// Called by onDaemonReady, which is the only edge guaranteed to run: a
    /// straight old→new daemon owner handover produces no serviceUnregistered
    /// edge at all, so the teardown in lifecycle_wiring_daemon.cpp cannot be the
    /// sole drain site. That teardown drains a DIFFERENT, deliberately smaller
    /// set (the rule-visible inputs plus the compositor restores), because the
    /// maps here must keep answering for the daemon-down interval — a window's
    /// notified screen, its minimize-float ownership and its deferred routes all
    /// still describe live windows while the daemon is merely absent.
    ///
    /// Paired invariant, and the reason this is one function rather than an
    /// inline list: anything added to a per-session map must be dropped here, or
    /// it survives a daemon restart and answers for a session that no longer
    /// exists. Adding to the teardown list is a separate decision — see the
    /// serviceUnregistered handler for what belongs there and why.
    void clearPerSessionDaemonState();

    /**
     * @brief Handle autotile drag-to-float: restore border and pre-autotile size
     *
     * Called synchronously at drag-stop time. Restores the KWin border (title bar),
     * clears tiling state, and defers a size-only restore to the next event loop
     * tick (after KWin finishes the interactive move).
     *
     * When @p immediate is true (drag-start path), the size restore is applied
     * synchronously with allowDuringDrag=true so the user sees the window return
     * to its free-floating size as soon as they start dragging a tile — matching
     * snap-mode behavior. When false (drag-stop path), the restore is deferred
     * via QTimer::singleShot so it runs after KWin has finished the interactive
     * move and the window's frame geometry reflects the actual drop position.
     *
     * @param w The window being floated (may be null for cross-screen drops)
     * @param windowId Stable window identifier
     * @param immediate Apply size restore synchronously during the interactive move
     */
    void handleDragToFloat(KWin::EffectWindow* w, const QString& windowId, bool immediate = false);
    void savePreTileForDesktopMove(const QString& windowId);
    void handleWindowOutputChanged(KWin::EffectWindow* w);

    // D-Bus signal connections and settings
    void connectSignals();
    void loadSettings();

    // Cleanup: unmaximize all monocle-maximized windows (called on daemon loss / effect teardown)
    void restoreAllMonocleMaximized();
    /// Membership half of the windowed-fullscreen release (hash removal
    /// only, no compositor call). Split from the state half because the
    /// demote path needs them on opposite sides of the managed-set write.
    void forgetWindowedFullscreen(const QString& windowId);
    /// Compositor half: drop KWin fullscreen state under the suppression
    /// counter and an own inGeometryApply bracket. Membership-independent.
    void releaseWindowedFullscreenState(const QString& windowId);
    /// Write @p kw's fullscreen state with slotWindowFullScreenChanged
    /// suppressed for the duration of the call.
    ///
    /// For effect-owned fullscreen flips made OUTSIDE this handler — today the
    /// OpenFullscreen rule's one-shot at windowAdded. On XWayland
    /// setFullScreen emits the state signal SYNCHRONOUSLY, so an unbracketed
    /// call re-enters slotWindowFullScreenChanged from inside
    /// slotWindowAdded, where its never-tracked exit arm announces a window
    /// that is still mid-open and releases the first-frame restore
    /// suppression that handler is about to arm. The counter is private, and
    /// the friendship runs the other way (the effect befriends this class, not
    /// the reverse), so the bracketed write lives here rather than as a
    /// counter accessor. No-op for a null @p kw.
    void applyFullScreenSuppressed(KWin::Window* kw, bool fullScreen);
    /// Hold keep-below on a flagged window (snapshot-once) so KWin's
    /// active-fullscreen layer promotion cannot stack it above its strip.
    void applyWindowedFullscreenLayerDemotion(const QString& windowId, KWin::Window* kw);
    /// Drain the keep-flag snapshot; @p kw may be null for a gone window
    /// (the snapshot is dropped either way).
    void restoreWindowedFullscreenLayerDemotion(const QString& windowId, KWin::Window* kw);
    /// Bulk restore (daemon loss, effect unload, engine disable, and daemon
    /// BRING-UP — a straight old-to-new daemon handover emits no
    /// serviceUnregistered edge, so onDaemonReady drains the dead session's
    /// holds too) — snapshot-and-clear then release each, the
    /// restoreAllMonocleMaximized shape.
    void restoreAllWindowedFullscreen();
    /// Arm the clear-in-flight marker and dispatch Scrolling.
    /// clearWindowedFullscreen reply-gated: the error arm drops the marker
    /// so a lost clear (whose flag-off echo will never arrive) cannot latch
    /// the adopt guard for the session.
    void dispatchWindowedFullscreenClear(const QString& windowId);
    /// Shed half of applyFloatCleanup for the WindowTracking float channel
    /// (PlasmaZonesEffect::slotWindowFloatingChanged), whose floats never
    /// reach this handler's slot and so never run applyFloatCleanup. Drops
    /// the windowed-fullscreen hold, the clear-in-flight marker, the
    /// counter-assert rect, the centering targets and the parked paint hint
    /// (with damage), and re-drives decorations.
    void applyPassiveFloatShed(const QString& windowId);

    /// Cleanup: drop all autotile tiled-tracking bookkeeping. Physical
    /// title-bar restores are the DecorationManager's job — teardown callers
    /// pair this with DecorationManager::restoreAll().
    void clearTiledTracking();

    // Focus follows mouse: focus the managed window under the cursor.
    // Autotile and scrolling screens carry independent flags (per-mode
    // settings); handleCursorMoved routes per screen.
    void setFocusFollowsMouse(bool enabled);
    void setScrollingFocusFollowsMouse(bool enabled);
    void handleCursorMoved(const QPointF& pos, const QString& screenId);

    // Meta+wheel column focus configuration. Disabling genuinely releases
    // the axis chords back to the compositor (updateScrollWheelShortcuts'
    // want predicate); inverting flips the wheel direction.
    void setWheelFocusEnabled(bool enabled);
    void setWheelFocusInverted(bool inverted);

    // Screen accessors (for gating drag/snap/overlay behavior per-screen)
    bool isManagedScreen(const QString& screenId) const;

    /// The engine-authoritative screen for a window the scrolling engine is
    /// ACTIVELY TILING, or empty. Parked scroll frames sit inside a
    /// neighbouring output's geometry by design, so position-derived screen
    /// resolution must yield to this for strip-placed windows. Gated on
    /// tiled membership: a FLOATED window on a scrolling screen is never
    /// parked — its frame is its real position — and answering for it would
    /// pin every consumer (drag drop, rule Mode stamp, minimize routing) to
    /// the screen it floated away from.
    /// Second half of the invariant, folded in (not left to call sites):
    /// the answer only holds while the tracked screen's physical output is
    /// still CONNECTED — on unplug/DPMS-off KWin reassigns the window and
    /// the daemon's scrollingScreensChanged lags, and until it lands every
    /// id-keyed consumer would keep resolving to the dead output.
    QString scrollTrackedScreenFor(const QString& windowId) const;

    /// True when @p w's live frame lies entirely off the union of every
    /// connected output — i.e. it is sitting at a strip park right now.
    ///
    /// Answers about the CURRENT frame, so it is the arrival test the batch
    /// apply needs ("was this window parked until this batch?"), not a
    /// statement about where it is heading. Fails closed (false) on a null
    /// window, degenerate geometry, or no resolvable outputs.
    bool atScrollPark(KWin::EffectWindow* w) const;

    /// Cheap gate for callers that want to skip scroll-specific work in a
    /// session with no scrolling screens at all. RAW set, deliberately NOT
    /// the isScrollingScreen intersection — the clip / input-filter /
    /// screen-override consumers want the conservative answer, and their
    /// inner predicates re-derive per-window truth anyway (see the rationale
    /// on scrollTrackedScreenFor in tilinghandler.cpp).
    bool hasScrollingScreens() const
    {
        return !m_scrollingScreens.isEmpty();
    }

    /// True when at least one scrolling screen resolves to cropping its
    /// straddlers. The direct-scanout gate needs a whole-session answer (the
    /// hazard is any un-cropped overhang reaching a hardware plane), so it
    /// asks this rather than per-screen membership. The per-screen
    /// distinction never reaches the paint CLIP: on a screen that does not
    /// crop the engine clamps the geometry so no rect straddles, leaving the
    /// clip nothing to cut.
    bool anyScreenCropsStraddlers() const
    {
        return !m_scrollCropStraddlerScreens.isEmpty();
    }

    /// Which way @p screenId's strip runs. NEVER derive this from the output's
    /// aspect ratio: the axis is resolved by the engine against the work area
    /// and may be an explicit user choice, so a guess here can disagree with
    /// the geometry the daemon actually committed — and the disagreement shows
    /// up as a full-strip shear for the length of every leg, on exactly the
    /// near-square topologies where a guess is least reliable.
    PhosphorProtocol::ScrollAxis scrollAxisForScreen(const QString& screenId) const
    {
        return m_scrollVerticalAxisScreens.contains(screenId) ? PhosphorProtocol::ScrollAxis::Vertical
                                                              : PhosphorProtocol::ScrollAxis::Horizontal;
    }

    /// True once the daemon's scrollEffectBehaviour map has landed (live
    /// signal or the bring-up Properties.Get reply), false again after either
    /// teardown path clears it.
    ///
    /// The direct-scanout gate is the reason this flag exists rather than the
    /// sets answering for themselves: an EMPTY crop set is ambiguous without
    /// it — "the daemon resolved no screen to cropping" and "no reply yet"
    /// look identical — and blocksDirectScanout has to fall back to the global
    /// setting in the second case while treating the first as authoritative.
    /// Without the distinction, a per-context SetScrollCropStraddlers=false
    /// could never hand direct scanout back while the global setting was on.
    bool scrollEffectBehaviourSeeded() const
    {
        return m_scrollEffectBehaviourSeeded;
    }

    /// Daemon-loss teardown: drop the dead session's scrolling snapshot.
    /// Deliberately NOT the live chokepoint — setScrollingScreens schedules
    /// a border sweep, which on this path would re-create rule-matched
    /// decorations one event-loop turn AFTER the handler's
    /// clearAllDecorations, undoing the teardown. The generation bump still
    /// voids in-flight property replies.
    ///
    /// CALL-SITE CONTRACT: the ONE sanctioned caller is the effect's
    /// serviceUnregistered teardown (lifecycle_wiring_daemon.cpp), which
    /// runs invalidateAllRuleCaches immediately after — this clear changes
    /// the Mode discriminator, and skipping the invalidate would leave rule
    /// verdicts memoised against the dead session's stamp. A second caller
    /// must carry the same pairing or use setScrollingScreens.
    void clearScrollingScreensForTeardown()
    {
        ++m_scrollingScreensGeneration;
        m_scrollingScreens.clear();
        // The per-screen behaviour sets belong to the same dead session, and
        // ALL THREE outlive the screen set they are keyed by if they are not
        // cleared here: a stale crop entry keeps forcing composition
        // session-wide, a stale focus-follows-mouse entry answers for a
        // screen the new daemon may not run the scrolling engine on at all,
        // and a stale vertical-axis entry answers Vertical for a screen the
        // new daemon may lay out horizontally.
        clearScrollEffectBehaviourForTeardown();
        // Release Meta+wheel with the dead session (no repaint interplay,
        // unlike the border sweep this path deliberately skips): a consumed
        // axis chord with no daemon to serve it would just eat input.
        updateScrollWheelShortcuts();
    }

    /// Drop the dead session's resolved scroll-behaviour map (all three
    /// per-screen sets plus the seeded flag). Shared by the serviceUnregistered teardown
    /// (via clearScrollingScreensForTeardown) and by onDaemonReady, which a
    /// straight old→new owner handover reaches WITHOUT any unregistered edge
    /// having fired. Takes the crop set's repaint bookend itself — that set is
    /// painted state, so dropping it changes what the clip cuts. The axis set
    /// needs no bookend of its own: the paint path reads StripViewAnimator's
    /// per-output copy, which its own reset() drops on the same teardown.
    void clearScrollEffectBehaviourForTeardown();

    /// True when @p screenId runs the SCROLLING engine. A subset of the
    /// engine-managed set the daemon publishes as managedScreens (which is
    /// the union of both tiling-family engines); tracked separately so
    /// window rule queries can stamp Mode "scrolling" instead of "tiling".
    ///
    /// Intersected with the union at READ time. The union and the
    /// discriminator arrive as two independent signals with no ordering
    /// guarantee, so between them m_scrollingScreens can transiently name a
    /// screen the union has already dropped; answering true there stamped
    /// Mode "scrolling" for an unmanaged screen and forwarded focusColumn to
    /// an engine that no longer owns it. Note it does NOT un-consume
    /// Meta+wheel: updateScrollWheelShortcuts keys registration on the RAW
    /// m_scrollingScreens, so KWin still swallows the chord in that window;
    /// the wheel handler simply no-ops instead of acting.
    bool isScrollingScreen(const QString& screenId) const
    {
        return m_scrollingScreens.contains(screenId) && m_managedScreens.contains(screenId);
    }

    /// The RULES-VISIBLE active layout id the daemon pushed for @p screenId
    /// (snapping UUID / "autotile:<algo>" / "scrolling:<templateUuid>" /
    /// bare sentinel), stamped onto Field::ActiveLayout in ruleQuery.
    /// Empty for a screen the daemon did not name. Callers must gate on
    /// activeLayoutsSeeded(): before the first map lands, EVERY screen reads
    /// empty here, and an empty answer is not inert (see the seeded flag).
    QString activeLayoutForScreen(const QString& screenId) const
    {
        return m_activeLayouts.value(screenId);
    }

    /// True once the daemon's first activeLayouts map has landed (live signal
    /// or the bring-up Properties.Get reply), false again after the
    /// daemon-loss teardown clear.
    ///
    /// Read by the effect's rule-admission filter
    /// (effectNeverStampedFields in shader_config_dbus.cpp), which is the ONE
    /// consumer: while this is false, ActiveLayout-referencing rules are not
    /// admitted to any effect-bound rule set at all. That is the only shape
    /// that is inert in BOTH polarities. Not stamping the field would not be:
    /// WindowQuery::activeLayout is a plain QString context field, so
    /// valueForField returns an ENGAGED empty string either way, and a
    /// `None{ActiveLayout Equals X}` leaf then matches every window.
    ///
    /// The false→true edge re-drives loadRuleAnimationsFromDbus so the rules
    /// held out during bring-up are admitted as soon as the map is real. The
    /// re-drive is GATED on the effect's m_activeLayoutRulesWithheld marker,
    /// which records whether the last admission pass actually dropped a rule
    /// for referencing ActiveLayout: a seeded pass withholds nothing and needs
    /// no re-fetch. The edge consumes the marker, so a later unseed→seed cycle
    /// re-drives only on its own evidence.
    ///
    /// The marker is NOT cleared by either unseeding path — those paths SET
    /// it instead, from the re-slice clearActiveLayoutsForTeardown performs.
    /// Every getAllRules reply that PARSES recomputes it outright; the
    /// malformed-payload and over-cap arms RE-ARM it to true instead, because
    /// this edge consumed it before dispatching and those arms admit nothing —
    /// leaving it false there would disarm the next unseed→seed cycle while
    /// the rules are still withheld. Clearing it on teardown or bring-up would
    /// disarm this edge for the session if the following getAllRules never
    /// lands; a stale-TRUE marker only costs one redundant re-drive.
    bool activeLayoutsSeeded() const
    {
        return m_activeLayoutsSeeded;
    }

    /// Daemon-loss teardown: drop the dead session's active-layout map. Same
    /// shape and the same reasoning as clearScrollingScreensForTeardown — a
    /// stale map would otherwise keep baking into rule verdicts resolved
    /// while the daemon is down, and the live chokepoint's border sweep would
    /// re-create rule-matched decorations one event-loop turn AFTER the
    /// handler's clearAllDecorations.
    ///
    /// CALL-SITE CONTRACT: two sanctioned callers, both of which must supply
    /// the invalidate this function deliberately omits, plus their own answer
    /// for the decorations those verdicts baked in. The only repaint it takes
    /// is the SetOpacity bookend inside the re-slice described below.
    ///  - the effect's serviceUnregistered teardown
    ///    (lifecycle_wiring_daemon.cpp), which runs invalidateAllRuleCaches
    ///    immediately after and then clearAllDecorations — it tears the
    ///    decorations down outright rather than sweeping them, which is why
    ///    the live chokepoint's SCHEDULED sweep would be wrong there (it would
    ///    re-create rule-matched decorations one turn after the teardown);
    ///  - TilingHandler::onDaemonReady, whose own tail runs
    ///    invalidateAllRuleCaches and scheduleBorderSweep. Bring-up needs the
    ///    clear because a straight old→new owner handover emits no
    ///    serviceUnregistered edge at all, so without it the seeded flag stays
    ///    true over the dead daemon's map and the seeding edge can never fire
    ///    again for the session.
    ///
    /// Clearing the seeded flag does NOT re-drive the rule admission: no
    /// getAllRules reply is coming with the daemon down, and the re-drive's
    /// updateAllDecorations would fight the teardown's clearAllDecorations.
    /// Instead the clear re-slices the rule sets in place, through
    /// PlasmaZonesEffect::sliceActiveLayoutRulesForUnseededMap: every rule
    /// whose match references Field::ActiveLayout comes back OUT of the five
    /// effect-bound sets (the three exclusion slices and the shader manager's
    /// effect-rule set). Leaving them in was the defect — the rule sets
    /// survive daemon loss on purpose, but they were filled while the map was
    /// seeded, and an unstamped ActiveLayout reads as an ENGAGED empty string,
    /// so a negated leaf over-matches EVERY window for the whole daemon-down
    /// interval. Re-slicing restores the both-polarities-inert shape a cold
    /// start has.
    ///
    /// Neither caller clears the effect's m_activeLayoutRulesWithheld marker
    /// that gates the seed edge, and neither should. The re-slice SETS it
    /// whenever it removed a rule, so on this path the marker's correctness is
    /// by construction: the same call that withholds the rules records that it
    /// did, and the next seeding edge re-drives loadRuleAnimationsFromDbus to
    /// restore them from the live store. A marker left true from an earlier
    /// admission pass is still true and must not be cleared here — that would
    /// strand the withheld rules disarmed for the session whenever the
    /// following getAllRules errors or times out. A stale-TRUE marker costs
    /// one redundant re-drive, the safe direction.
    ///
    /// Out-of-line (state.cpp) because the re-slice reaches into effect
    /// internals that are incomplete at this point in the header.
    void clearActiveLayoutsForTeardown();

    /// The set this discriminator actually answers over.
    ///
    /// Because the answer is an INTERSECTION, it can change when EITHER input
    /// moves. setScrollingScreens invalidates the rule caches on its own
    /// writes, but m_managedScreens is assigned raw by slotScreensChanged and
    /// by loadSettings' reply, and those two arrive independently of the
    /// scrolling-screens signal. Every writer of m_managedScreens must
    /// therefore snapshot this before and compare after — otherwise a screen
    /// that enters the union AFTER being marked scrolling leaves every
    /// `Mode == "scrolling"` verdict memoised as non-matching for the session.
    ///
    /// ONE exemption, by name: clearTiledTracking. It is a teardown-only writer
    /// (daemon loss and effect destruction), and neither caller has a session
    /// left to memoise a verdict for — the daemon-loss caller invalidates the
    /// caches itself moments later, and the destructor must not run a rule
    /// sweep against half-destroyed members. Any NEW writer takes the pair.
    QSet<QString> scrollingScreenIntersection() const
    {
        return m_scrollingScreens & m_managedScreens;
    }

    /// Check if a window is tracked by the autotile handler (in m_notifiedWindows).
    bool isTrackedWindow(const QString& windowId) const
    {
        return m_notifiedWindows.contains(windowId);
    }

    /**
     * @brief Update the notified screen ID for a tracked window.
     *
     * Called after virtual screen config changes re-resolve window screen IDs,
     * so that slotWindowFrameGeometryChanged does not compare against stale
     * screen IDs and trigger spurious cross-VS transfers.
     *
     * No-op if the window is not in m_notifiedWindowScreens.
     */
    void updateNotifiedScreen(const QString& windowId, const QString& newScreenId)
    {
        auto it = m_notifiedWindowScreens.find(windowId);
        if (it != m_notifiedWindowScreens.end()) {
            it.value() = newScreenId;
        }
    }

    // Tiled-membership accessor — delegates to shared TilingStateHelpers. The
    // membership set feeds the IsTiled rule field; per-window border appearance
    // and title-bar hiding are resolved from rules, not from this state.
    bool isTiledWindow(const QString& windowId) const
    {
        return TilingStateHelpers::isTiledWindow(m_border, windowId);
    }

    // Tiled-set mutators that re-resolve the window's rules whenever its overall
    // tiled status flips (IsTiled is a match field). Welding the invalidation to
    // the write mirrors NavigationHandler's snap/float setters, so a caller can't
    // change tiling membership and forget to re-resolve a tiled-scoped border /
    // title-bar / opacity rule. markWindowTiled only invalidates on a genuine
    // not-tiled→tiled transition (its own single-owner sweep runs after the
    // wasTiled read, so a cross-screen transfer of an already-tiled window
    // does NOT invalidate here — IsTiled never flipped, and the per-batch
    // invalidation in slotWindowsTileRequested covers the ScreenId field
    // change); a same-screen re-assert is likewise a no-op.
    void markWindowTiled(const QString& screenId, const QString& windowId);
    void clearWindowTiledAllScreens(const QString& windowId);
    void clearWindowTiledOnScreen(const QString& screenId, const QString& windowId);

    // Set a window to re-activate after the next autotile raise loop completes.
    // Used by slotDaemonReady() to preserve focus of non-tiled windows (e.g. KCM).
    void setPendingReactivateWindow(KWin::EffectWindow* w)
    {
        m_pendingReactivateWindow = w;
    }

    /// Record a daemon-initiated cross-output move so the next outputChanged
    /// for @p windowId to @p targetScreenId updates bookkeeping only. See
    /// m_expectedOutputMove.
    ///
    /// @p sourceScreenId is the screen the daemon moved the window off. It is
    /// authoritative and must be used whenever it is non-empty: a daemon arm
    /// site that runs after the handoff has already pushed the destination's
    /// tiles, so by now m_notifiedWindowScreens names the DESTINATION.
    ///
    /// An empty source falls back to the notified screen. That is correct for
    /// the arm sites that fire ahead of any placement work, and it is what an
    /// older daemon that does not put the source on the wire produces — in
    /// which case the fallback is blind for a post-handoff arm exactly as
    /// described above, and stillTiledOnSource degrades to always-false.
    void markExpectedOutputMove(const QString& windowId, const QString& targetScreenId, const QString& sourceScreenId)
    {
        const QString source = sourceScreenId.isEmpty() ? m_notifiedWindowScreens.value(windowId) : sourceScreenId;
        m_expectedOutputMove.insert(windowId, ExpectedOutputMove{targetScreenId, source});
    }

public Q_SLOTS:
    // Autotile D-Bus signal handlers
    void slotWindowsTileRequested(const PhosphorProtocol::TileRequestList& tileRequests);
    void slotFocusWindowRequested(const QString& windowId);
    void slotEnabledChanged(bool enabled);
    void slotScreensChanged(const QStringList& screenIds, bool isDesktopSwitch);
    /// The two halves of slotScreensChanged's removed-screens pass, split out
    /// because they reach opposite conclusions about the same event and were
    /// the reason the slot ran to 775 lines. A screen leaves the managed set
    /// either because the DESKTOP changed (the windows stay owned by that
    /// desktop's live session, so demote and remember) or because autotile was
    /// genuinely turned off for it (nothing owns them now, so untrack and
    /// restore). Both take the caller's `windows` snapshot and append to its
    /// deferred windowed-fullscreen release list rather than releasing inline,
    /// because that release must land AFTER the managed-set write.
    void demoteWindowsForDesktopSwitch(const QSet<QString>& removed, const QList<KWin::EffectWindow*>& windows,
                                       QStringList& windowedFsToRelease,
                                       QHash<QString, QRectF>& windowedFsPreTileRestore);
    void untrackWindowsForDisabledScreens(const QSet<QString>& removed, const QSet<QString>& newScreens,
                                          const QList<KWin::EffectWindow*>& windows, QStringList& windowedFsToRelease);
    void slotScrollingScreensChanged(const QStringList& screenIds);
    void slotScrollEffectBehaviourChanged(const QVariantMap& behaviour);
    void slotActiveLayoutsChanged(const QVariantMap& activeLayouts);
    void slotWindowFloatingChanged(const QString& windowId, bool isFloating, const QString& screenId);

    // Window state change handlers (connected per-window in setupWindowConnections)
    void slotWindowMinimizedChanged(KWin::EffectWindow* w);
    /// Cross-mode transfer entry point over slotWindowMinimizedChanged.
    /// Returns false when the ENTRY GATES would refuse the window
    /// (unhandleable / not on an autotile screen) WITHOUT forwarding the
    /// edge; true after forwarding. SnapHandler's became-autotile hand-offs
    /// must use this instead of the raw slot: a silently refused transfer
    /// left ownership with the sender while the edge was already spent, so
    /// the suspension stranded until the next minimize cycle.
    bool offerMinimizeEdge(KWin::EffectWindow* w);
    void slotWindowMaximizedStateChanged(KWin::EffectWindow* w, bool horizontal, bool vertical);
    void slotWindowFullScreenChanged(KWin::EffectWindow* w);
    void slotWindowFrameGeometryChanged(KWin::EffectWindow* w, const QRectF& oldGeometry);

private:
    // ═══════════════════════════════════════════════════════════════════
    // Utility methods
    // ═══════════════════════════════════════════════════════════════════

    /// Announce, once per window per episode, that a window on a tracked
    /// scrolling screen lost its clip because the screen's physical output is
    /// not connected. The routine negatives (non-member, non-scrolling screen)
    /// never report — see scrollTrackedScreenFor.
    void reportScrollClipLoss(const QString& windowId, const QString& reason) const;

    void unmaximizeMonocleWindow(const QString& windowId);

    /**
     * @brief Shared float-state cleanup for a window being floated
     *
     * Updates the float cache, clears tiled tracking, removes the border
     * overlay, and unmaximizes monocle (title-bar restores flow through the
     * rule path). Used by the per-window D-Bus signal, batch float, and
     * drag-to-float paths.
     */
    void applyFloatCleanup(const QString& windowId);

    /**
     * @brief Check if a window is eligible for autotile notification
     *
     * Shared predicate used by both notifyWindowAdded and notifyWindowsAddedBatch
     * to keep filtering logic in sync.
     *
     * @param rejectedOnlyBecauseMinimized When non-null, set to true if the
     *        window passed every other gate and was rejected solely for being
     *        minimized — the batch announce uses this to claim the window as
     *        minimize-floated instead of silently dropping it (a daemon-side
     *        mode-transition seed would otherwise tile it).
     * @return true if the window should be notified to the autotile daemon
     */
    bool isEligibleForTilingNotify(KWin::EffectWindow* w, bool* rejectedOnlyBecauseMinimized = nullptr) const;

    /**
     * @brief Claim a window that was already minimized at batch-announce time
     *        as minimize-floated.
     *
     * A window minimized BEFORE a batch announcement never gets a
     * minimizedChanged edge for that transition. Marking it minimize-floated
     * keeps it out of the live tiling geometry until its later unminimize.
     * Mode-entry batches commit that first placement immediately, while daemon
     * re-announcements retain the normal animation grace for windows already
     * parked at an autotile rect.
     *
     * Skips windows the daemon already tracks as floating (a user float or
     * another mode's minimize-float record) — mirroring the runtime minimize
     * path, we never claim ownership of a float we did not create.
     *
     * @p resolvedScreenId, when non-empty, replaces the positional resolve.
     * The batch caller passes the id it already resolved for the window so
     * this arm's screen-filter test cannot disagree with the batch's — see
     * notifyWindowsAddedBatch's screenOverrides.
     */
    void claimAlreadyMinimizedAsFloated(KWin::EffectWindow* w, const QString& windowId,
                                        const QSet<QString>& screenFilter, bool enteringAutotile,
                                        const QString& resolvedScreenId = {});

    /**
     * @brief Cancel a pending debounced minimize→float commit.
     *
     * No-op if no timer is pending for the window. Called from the
     * unminimize path (to coalesce spurious cycles), from
     * removeMinimizeFloated, and from cleanupAutotileTracking's per-window
     * teardown (so pending timers never fire against destroyed windows);
     * bulk teardown goes through the helper's cancelAll in
     * clearAllPendingMinimizeFloats instead.
     */
    void cancelPendingMinimizeFloat(const QString& windowId);

    /**
     * @brief Cancel a pending deferred unminimize→unfloat commit.
     *
     * No-op if no timer is pending for the window. Called from the minimize
     * path (a re-minimize during the grace must leave the window
     * minimize-floated), from cleanupAutotileTracking (window closed), and
     * from removeMinimizeFloated (authoritative external unfloat via the
     * daemon's windowFloatingChanged echo); bulk teardown goes through the
     * helper's cancelAll in clearAllPendingMinimizeFloats instead.
     */
    void cancelPendingUnminimizeUnfloat(const QString& windowId);

    /**
     * @brief Cancel every pending debounced minimize→float commit.
     *
     * Called when autotile is disabled — in-flight timers should not
     * commit floats against a now-disabled engine.
     */
    void clearAllPendingMinimizeFloats();

    /// Move active minimize ownership into the asynchronous unfloat interval.
    /// Returns whether the daemon request may be dispatched.
    bool beginUnminimizeUnfloat(const QString& windowId);
    /// Returns whether the daemon unfloat was actually dispatched. False
    /// (window not ours, or daemon unregistered) means the daemon still
    /// considers the window floating — callers must not announce it as
    /// tileable via notifyWindowAdded on that branch.
    bool dispatchUnminimizeUnfloat(const QString& windowId, const QString& screenId);
    void scheduleUnminimizeUnfloatRetry(const QString& windowId);
    QSet<QString> completeDeferredWindowRoutes();

    /**
     * @brief Save the pre-autotile free-float geometry for @p windowId.
     *
     * The caller passes the window's current frame in @p frameIn; it is corrected
     * for a maximized/fullscreen window via PlasmaZonesEffect::freeGeometryForCapture
     * (the pre-maximize restore rect) so a full-monitor frame is never stored as the
     * free-float geometry. The default safety guard skips the save when the window is
     * not currently floating — snapped/tiled windows have zone dimensions in
     * frameGeometry() and capturing them would poison the pre-tile entry. Invalid
     * input and deliberately-skipped saves are both silent no-ops (logged at debug);
     * no caller distinguishes them.
     *
     * @param w The window, used to read its maximize/fullscreen restore rect.
     * @param knownFreeFloating Bypass the isWindowFloating guard when the
     *        caller knows the frame is authoritatively a free-float rect.
     *        Use true only for a genuine window-open event. Fresh windows are
     *        not tracked in the FloatingCache yet, so isWindowFloating()
     *        returns false and would incorrectly reject the initial capture.
     */
    void saveAndRecordPreTileGeometry(const QString& windowId, const QString& screenId, KWin::EffectWindow* w,
                                      const QRectF& frameIn, bool knownFreeFloating = false);

    /**
     * @brief All-bucket pre-autotile geometry lookup.
     *
     * Returns the first VALID rect found for @p windowId across every
     * screen's bucket in m_preTileGeometries, or an invalid QRectF if
     * none holds one. Readers must scan all buckets (not just the window's
     * current screen): a VS config change can re-resolve the window's
     * screen without moving its geometry bucket. Shared by the batch-float,
     * drag-to-float, cross-monitor-snapshot, desktop-move-stash, and
     * desktop-switch restore paths.
     *
     * @param bucketScreenId Optional out — receives the screen key of the
     *        bucket the rect was found under (unchanged when not found).
     */
    QRectF findPreTileGeometry(const QString& windowId, QString* bucketScreenId = nullptr) const;

    /**
     * @brief Async daemon-side pre-tile geometry restore for a desktop-switch
     *        orphan with no local pre-autotile bucket entry.
     *
     * Fires getValidatedPreTileGeometry and applies the returned rect once
     * the reply lands, unless ANY owner took the window back during the
     * round-trip (re-notified, autotile screen, snap commit, float, user
     * move/resize, desktop switched again). Callers must only invoke this
     * for windows that were verifiably autotile-managed (tracked in
     * m_notifiedWindows at demotion time) — the daemon store is mode-shared,
     * appId-fuzzy and session-persisted, so an ungated call can teleport a
     * never-autotiled window.
     */
    /// `capturedScreenId` is the caller's screen resolution taken while the
    /// engine-authoritative override was still live; the reply guard tests it
    /// instead of re-resolving, because by reply time the window's tracking
    /// has been demoted and a positional re-resolve of a parked (off-canvas)
    /// frame can name a neighbouring output.
    void requestDaemonPreTileRestore(KWin::EffectWindow* w, const QString& windowId, const QString& capturedScreenId);

    /**
     * @brief Declared compositor min-size for @p w, rounded up to ints.
     *
     * Returns 0×0 when the window declares none. Internal windows (our own
     * overlays) are skipped entirely — KWin's InternalWindow::minSize()
     * segfaults when the backing QWindow is null (discussion #511).
     */
    // Public because the SNAP handler needs the same value: a cross-screen
    // reclaim driven off resolveWindowRestore adopts the window into a
    // tiling engine, which evaluates its oversized/float verdict once from
    // the declared minimum — so both channels must report the same number
    // from the same reader. Stateless static; no TilingHandler instance is
    // needed or implied.
public:
    static QSize declaredMinSize(KWin::EffectWindow* w);

private:
    void reportDiscoveredMinSize(const QString& windowId, int minWidth, int minHeight);

    // ═══════════════════════════════════════════════════════════════════
    // Member variables
    // ═══════════════════════════════════════════════════════════════════

    PlasmaZonesEffect* m_effect;

    QSet<QString> m_managedScreens;
    /// Screens running the scrolling engine — a pure mode discriminator
    /// (see isScrollingScreen); all lifecycle gating keys on
    /// m_managedScreens, which carries the union.
    QSet<QString> m_scrollingScreens;
    /// Windows already reported as having lost their scroll clip via the
    /// connected-output gate. Every exit from scrollTrackedScreenFor fails
    /// OPEN at both consumers (the paint clip and the overhang input filter
    /// read an invalid rect as "not a straddler"), but only that gate is
    /// anomalous enough to announce — the membership negatives are the
    /// predicate's routine answer for every non-strip window. This set keeps
    /// the report to once per window per episode (the predicate runs per
    /// window, per output, per frame), is cleared when the window's answer
    /// comes back so a later failure reports again, and entries die with the
    /// window in cleanupAutotileTracking and on daemon bring-up.
    mutable QSet<QString> m_scrollClipLossReported;
    /// Authoritative write for the scrolling discriminator. A screen that
    /// flips engine WITHIN the managed union transits no managedScreensChanged,
    /// so this re-announces the flipped screens' windows to hand them to the
    /// new engine. Pass @p announceFlipped false only for BRING-UP clears
    /// (onDaemonReady), where the tracking maps are about to be reset and
    /// loadSettings owns the re-announce — announcing there desyncs the
    /// daemon's view from the effect's until that batch lands.
    void setScrollingScreens(const QSet<QString>& newSet, bool announceFlipped = true);
    /// The three bring-up property Gets loadSettings dispatches (scrolling
    /// screens, active layouts, scroll effect behaviour), factored out
    /// so their bounded failure retries can re-dispatch exactly one fetch.
    /// Every dispatch bumps the matching per-query generation, so a stale
    /// retry reply loses to any newer query or live-signal write.
    ///
    /// PRIVATE deliberately: the retry budgets are granted by loadSettings
    /// alone, so an outside caller invoking either of these directly would
    /// re-drive a fetch under whatever budget the last loadSettings left.
    /// loadSettings is the budget-granting entry point, the same shape
    /// loadRuleAnimationsFromDbus has over fetchAllRulesOnce.
    void fetchScrollingScreens();
    /// Bring-up fetch of the daemon-resolved scrolling behaviour the
    /// compositor owns (focus-follows-mouse, straddler crop, strip axis).
    /// Bounded retry, the fetchScrollingScreens pattern.
    void fetchScrollEffectBehaviour();
    /// Shared apply for the fetch reply and the live signal: replaces all
    /// three sets from the wire map. Repaints when the CROP set or the AXIS
    /// set moved, because both are painted state the compositor will not
    /// otherwise revisit; focus-follows-mouse is consulted per pointer move
    /// and needs nothing.
    void applyScrollEffectBehaviour(const QVariantMap& behaviour);
    void fetchActiveLayouts();
    /// Meta+wheel axis shortcuts for column focus (niri's Mod+wheel).
    /// Registered while ANY screen runs the scrolling engine, unregistered
    /// (by destroying the QActions — KWin drops an axis shortcut with its
    /// action) when none does, so Meta+wheel is only consumed in sessions
    /// that can actually use it. The trigger itself re-gates on the
    /// CURSOR's screen being a scrolling screen.
    void updateScrollWheelShortcuts();
    void wheelFocusColumn(int delta);
    QList<QAction*> m_scrollWheelActions;
    /// Pause the effect's own focus-follows-mouse after an ENGINE-driven
    /// strip movement on a scrolling screen (tile batch or activation).
    /// Scrolling slides other columns under a stationary pointer, and the
    /// next incidental cursor twitch would re-focus whatever landed there,
    /// yanking focus straight off the column the user just navigated to.
    /// Cleared once the cursor moves deliberately (beyond the radius) from
    /// the position it held when the strip moved.
    void suppressFfmUntilCursorMoves();
    QPointF m_ffmSuppressAnchor;
    bool m_ffmSuppressPending = false;
    /// Bumped on every managedScreensChanged signal. loadSettings' async
    /// Properties.Get reply captures the value at dispatch and discards
    /// itself if a signal landed in between — the signal carried a newer
    /// set AND ran the full per-screen transition handling the raw reply
    /// assignment lacks.
    quint64 m_screensSignalGeneration = 0;
    quint64 m_screenQueryGeneration = 0;
    bool m_initialScreenQueryPending = false;
    QSet<QString> m_pendingFreshWindows;
    /// Monotonic stamp source for windowOpened / windowsOpenedBatch announces,
    /// session-global for the same reason m_crossScreenRestoreSeq is: a stamp is
    /// never reused, so an ERASED map entry reads back as 0 and can never match a
    /// captured stamp.
    quint64 m_announceSeq = 0;
    /// Per-window stamp of the newest in-flight announce. Both error arms capture
    /// their stamp and roll back only while it still matches, so an error for
    /// announce N-1 cannot erase the tracking announce N established, and a
    /// corpse's error arm (the entry erased by cleanupAutotileTracking) reads
    /// back 0, mismatches, and no-ops instead of re-inserting a spawn-provenance
    /// marker for a dead window whose appId-derived id is reusable.
    QHash<QString, quint64> m_announceGen;
    struct DeferredWindowRoute
    {
        QPointer<KWin::EffectWindow> window;
        bool canSnapRestore = false;
    };
    QHash<QString, DeferredWindowRoute> m_deferredWindowRoutes;
    /// Per-screen rules-visible active layout ids, pushed by the daemon
    /// (see activeLayoutForScreen). A pure ruleQuery input like
    /// m_scrollingScreens — no lifecycle transitions key on it.
    QHash<QString, QString> m_activeLayouts;
    /// Authoritative write for m_activeLayouts: generation bump (voids
    /// in-flight property replies), change gate, then rule-cache invalidate
    /// + border sweep on a genuine change — the setScrollingScreens pattern.
    void setActiveLayouts(const QHash<QString, QString>& activeLayouts);
    /// Set by setActiveLayouts BEFORE its change gate (an identical map is
    /// still a real map, and the gate would otherwise leave the effect
    /// permanently unseeded whenever the daemon's first push is empty),
    /// cleared by clearActiveLayoutsForTeardown. See activeLayoutsSeeded()
    /// for what reads it and why an unstamped field is not inert.
    bool m_activeLayoutsSeeded = false;
    /// Stale-reply guard for the activeLayouts property fetch, same contract
    /// as m_scrollingScreensGeneration.
    quint64 m_activeLayoutsGeneration = 0;
    /// Per-DISPATCH guard for the activeLayouts property fetch, the twin of
    /// m_screenQueryGeneration. The generation guard above only voids a reply
    /// an authoritative WRITE overtook; two Gets in flight across a daemon
    /// restart carry no write between them, so without this the first reply
    /// to arrive wins and the second (newer) one is discarded by its own
    /// generation check. Bumped at dispatch so only the newest query applies.
    quint64 m_activeLayoutsQueryGeneration = 0;
    /// Same per-dispatch guard for the scrolling-screens property fetch.
    quint64 m_scrollingScreensQueryGeneration = 0;
    /// Remaining bounded-retry attempts for the three bring-up fetches.
    /// Reset to the cap by each loadSettings run, consumed only by the
    /// failure arms' own re-dispatches.
    int m_activeLayoutsFetchRetriesLeft = 0;
    int m_scrollingScreensFetchRetriesLeft = 0;
    int m_scrollEffectBehaviourFetchRetriesLeft = 0;
    /// Per-dispatch guard for the scroll-effect-behaviour fetch. No write
    /// generation twin: nothing writes these two sets except the daemon, so
    /// there is no local authoritative writer for a reply to race.
    quint64 m_scrollEffectBehaviourQueryGeneration = 0;
    /// Screens whose RESOLVED scrolling focus-follows-mouse is on, and whose
    /// RESOLVED straddler crop is on (`rule ?? config`, decided daemon-side).
    /// Membership is the whole answer — the effect holds no config fallback
    /// for either, so an empty set reads as "off everywhere", which is what
    /// bring-up before the daemon's first reply looks like.
    QSet<QString> m_scrollFocusFollowsMouseScreens;
    QSet<QString> m_scrollCropStraddlerScreens;
    /// Scrolling screens whose strip runs VERTICALLY. Membership, so an absent
    /// key or an empty list means horizontal everywhere — which is exactly
    /// what a session with no vertical strip publishes. It is NOT a
    /// compatibility fallback: MinPeerApiVersion rejects any daemon old enough
    /// to omit the key at the handshake, so an absent key is a live daemon
    /// saying "none", never an old one saying nothing. That is why a MALFORMED
    /// value keeps the current membership instead of emptying it (see
    /// applyScrollEffectBehaviour) — empty is a positive claim here, not a
    /// safe default.
    ///
    /// Held CONTINUOUSLY rather than read off a tile batch, because the paint
    /// path and the tab-indicator surface both need it at moments when no
    /// batch is in hand, and because the axis outlives any one batch.
    QSet<QString> m_scrollVerticalAxisScreens;
    /// Set by applyScrollEffectBehaviour on every apply (before its change
    /// gate, the m_activeLayoutsSeeded shape: an identical map is still a real
    /// map, and the daemon's first publish is legitimately all-empty on a
    /// session with no scrolling screen). Cleared by
    /// clearScrollEffectBehaviourForTeardown. See scrollEffectBehaviourSeeded()
    /// for the one consumer and why an empty set alone cannot answer for it.
    bool m_scrollEffectBehaviourSeeded = false;
    /// Same stale-reply guard for the scrolling-screens property fetch.
    /// Bumped by setScrollingScreens on EVERY authoritative write (live
    /// signal, property reply, daemon-restart clear) — even an identical
    /// set voids in-flight replies dispatched earlier, since the writer is
    /// always newer than the reply. All writes to m_scrollingScreens go
    /// through setScrollingScreens so the rule-cache invalidate + border
    /// sweep on a genuine change cannot be bypassed.
    quint64 m_scrollingScreensGeneration = 0;
    /// Pre-autotile frame geometry, keyed [screenId][windowId].
    ///
    /// Ownership: this is a local cache. The daemon's unified
    /// `WindowPlacementStore` record is authoritative and survives daemon
    /// restart. The effect populates this map on
    /// the first autotile transition for a window so it can restore the
    /// frame instantly when the window leaves autotile mode (untile, mode
    /// switch, screen change) without waiting on a D-Bus round-trip.
    ///
    /// Layout: per-screen bucket mirrors `BorderState` so swap/rotate can
    /// drop a screen's records wholesale. Readers that need a window's rect
    /// regardless of which screen it was captured under (a VS config change
    /// can re-resolve the notified screen without moving the bucket) scan
    /// ALL buckets — see the desktop-switch Pass-2 scan in signals.cpp and
    /// the cross-monitor snapshot in handleWindowOutputChanged.
    QHash<QString, QHash<QString, QRectF>> m_preTileGeometries;
    QHash<QString, QStringList> m_savedAutotileStackingOrder; ///< autotile stacking order, restored on snap→autotile
    QSet<QString> m_notifiedWindows;
    QHash<QString, QString> m_notifiedWindowScreens; ///< windowId → screen ID at time of notification
    /// Daemon-initiated cross-output moves: windowId → expected destination
    /// screen. Set when the daemon emits windowOutputMoveExpected (it has
    /// already migrated its tiling state and reflowed both outputs). Consumed
    /// one-shot by handleWindowOutputChanged on the matching outputChanged so
    /// that transfer only updates bookkeeping + decoration, never re-issues
    /// windowClosed/windowOpened. A stale entry (no outputChanged ever arrives,
    /// or a different destination) is cleared on the next outputChanged for the
    /// window and on close.
    struct ExpectedOutputMove
    {
        QString targetScreenId;
        /// Screen the window left. Comes from the daemon when it knows it, else
        /// falls back to the notified screen at arm time. Read live membership
        /// against this id to tell a genuine handoff (source bucket already
        /// emptied) from a strip parking hop (still tiled on the source) — see
        /// markExpectedOutputMove.
        QString sourceScreenId;
    };
    QHash<QString, ExpectedOutputMove> m_expectedOutputMove;
    QSet<QString> m_savedNotifiedForDesktopReturn; ///< windows removed from m_notifiedWindows on desktop switch
    /// Pre-autotile geometry preserved when a window is moved to another
    /// desktop. Keyed by windowId; value holds (sourceScreenId, frameRect)
    /// so a cross-desktop + cross-screen move can detect the screen change
    /// at restore time and skip the saved geometry (the rect is in the
    /// source screen's coordinate space and would land off-target on a
    /// different monitor).
    QHash<QString, QPair<QString, QRectF>> m_savedPreTileForDesktopMove;
    /// Re-entrancy guard for handleWindowOutputChanged, PER WINDOW.
    ///
    /// A single global bool refused every nested call, including one for a
    /// DIFFERENT window, and that refusal was permanent: window_connections.cpp
    /// pre-writes m_trackedScreenPerWindow before calling in, so the detector
    /// will not re-fire for the same physical hop, and the only other re-entry
    /// path triggers solely on a virtual-screen crossing. A dropped physical hop
    /// therefore had no re-detect path anywhere and the window kept the old
    /// screen's tracking for the session. Keyed by windowId, re-entry is refused
    /// only for the window already being handled, which is the case the guard was
    /// written for (the transfer's own releaseWindowTracking / notifyWindowAdded
    /// can move the window across screens again).
    QSet<QString> m_outputChangeInFlight;
    QHash<QString, QMetaObject::Connection>
        m_pendingCrossScreenRestore; ///< windowId → deferred size-restore connection
    /// Monotonic stamp source for the cross-screen size-restore continuation.
    /// Session-global rather than per-window so a stamp is never reused, which
    /// is what lets the map below be ERASED on teardown: a removed entry reads
    /// back as 0 and the first stamp is 1, so no captured generation can ever
    /// match a dropped one.
    quint64 m_crossScreenRestoreSeq = 0;
    /// Generation for the cross-screen size-restore continuation, stamped every
    /// time that path arms a new D-Bus watcher for a window. The in-flight
    /// watcher is not cancellable (every cancel site disconnects the STORED
    /// connection, and a watcher that has not yet replied has not stored one),
    /// so a second hop arming while the first is still in flight leaves both
    /// live. Both continuations capture their stamp and bail on a mismatch,
    /// which keeps an older hop's pre-tile size from winning and stops its
    /// lambda consuming the newer hop's map entry.
    QHash<QString, quint64> m_crossScreenRestoreGen;
    QSet<QString> m_minimizeFloatedWindows;
    /// Ownership after an unfloat dispatch and before its authoritative echo.
    /// The generation rejects completions from a countermanded older request.
    /// A re-minimize countermand moves the window back to the active set.
    QHash<QString, quint64> m_unfloatInFlight;
    /// Windows whose clearWindowedFullscreen is dispatched and unechoed —
    /// the m_unfloatInFlight idiom for the fullscreen-exit reconcile. A batch
    /// emitted before the daemon processed the clear can still carry
    /// flag=true; the adopt arm skips members so it cannot re-fullscreen the
    /// window against the user's exit, and the first flag-off batch entry
    /// consumes the marker (a lost clear therefore cannot latch it).
    QSet<QString> m_windowedFsClearInFlight;
    quint64 m_unfloatRequestGeneration = 0;
    QHash<QString, int> m_unfloatRetryAttempts;
    /// Subset of m_minimizeFloatedWindows claimed at batch-announce time
    /// (already minimized when the screen entered autotile). These windows
    /// still carry geometry from the prior mode, so their unminimize commits
    /// immediately instead of through the deferred animation grace.
    QSet<QString> m_untiledMinimizeFloats;
    // NOTE: title-bar (borderless) state is owned by the effect's
    // DecorationManager; this handler only tracks tiled membership for
    // border RENDERING via m_border.tiledWindowsByScreen.
    /// Pending debounced minimize→float commits, keyed by windowId. An entry
    /// is created when slotWindowMinimizedChanged sees minimized=true; if the
    /// matching unminimize arrives before the timer fires, the timer is
    /// cancelled and no D-Bus float is issued. This absorbs spurious
    /// minimize/unminimize cycles that KWin emits on tiled windows when
    /// plasmashell notification popups transiently change stacking.
    DeferredWindowCommits m_pendingMinimizeFloat{this};
    /// Pending deferred unminimize→unfloat commits, keyed by windowId — the
    /// restore-side mirror of m_pendingMinimizeFloat, with a different reason:
    /// the unfloat triggers a retile whose applyWindowGeometry moveResize,
    /// landing mid-flight, CANCELS KWin's own unminimize animation (magic
    /// lamp / squash; kwin's maximize script likewise self-cancels on a
    /// mid-flight resize) — the discussion #816 restore stutter. The commit
    /// is deferred past the stock animation (Effect::animationTime-scaled
    /// grace) and revalidated at fire time; a re-minimize during the grace
    /// cancels it, leaving the window minimize-floated as before.
    DeferredWindowCommits m_pendingUnminimizeUnfloat{this};
    /// Global stagger epoch, bumped ONLY on a desktop switch (slotScreensChanged)
    /// to cancel EVERY in-flight staggered apply — geometry computed for the old
    /// desktop must never land in the new one. Plain managed-set changes must
    /// not bump it: a mode toggle's own managedScreensChanged lands in the same
    /// burst as its tile batch, and a blanket bump voided that batch after its
    /// first synchronous entry (screen sat half-tiled until a manual
    /// float/unfloat). Non-desktop invalidation is per-screen: removed screens
    /// in slotScreensChanged, flipped screens in setScrollingScreens.
    uint64_t m_tileStaggerGeneration = 0;
    /// Per-screen stagger generation. A retile bumps only its own screen(s), so a
    /// newer batch for the SAME screen supersedes an earlier one while a batch for
    /// a DIFFERENT screen leaves it untouched. Without this, the destination batch
    /// of a cross-output move (emitted microseconds after the source reflow)
    /// cancelled the source's still-staggered windows via the single global
    /// generation — they never moved, leaving a hole on the source monitor.
    QHash<QString, uint64_t> m_tileStaggerGenByScreen;
    QHash<QString, QRect> m_tileTargetZones;
    QHash<QString, QRect> m_centeredWaylandZones; ///< zones where Wayland windows were last centered
    QString m_pendingAutotileFocusWindowId;
    QPointer<KWin::EffectWindow> m_pendingReactivateWindow; ///< re-activate after raise loop (daemon restart)
    QSet<QString> m_monocleMaximizedWindows;
    int m_suppressMaximizeChanged = 0;
    /// Suppresses slotWindowFullScreenChanged for the effect's OWN
    /// setFullScreen calls (windowed fullscreen), mirroring
    /// m_suppressMaximizeChanged. Load-bearing on the X11/XWayland path,
    /// where the signal fires synchronously inside setFullScreen. On the
    /// Wayland path the committed signal arrives a client round-trip later
    /// with this counter back at 0 — there the hash-membership branches in
    /// the slot are what protect the tiling state, not this counter.
    int m_suppressFullScreenChanged = 0;
    // ── Focus follows mouse ──
    // Per-mode pair of GLOBAL SETTING mirrors: m_focusFollowsMouse is the
    // autotile flag (autotileFocusFollowsMouse), m_scrollingFocusFollowsMouse
    // the scrolling one (scrollingFocusFollowsMouse).
    //
    // Only the autotile half is still the per-screen AUTHORITY. On a scrolling
    // screen the authority is m_scrollFocusFollowsMouseScreens, the daemon's
    // already-resolved `rule ?? config` membership — a SetScrollFocusFollowsMouse
    // context rule can turn the behaviour on for one monitor while the global
    // setting is off, so m_scrollingFocusFollowsMouse alone would answer "off"
    // for a screen the rule turned on. handleCursorMoved therefore routes per
    // screen (isScrollingScreen → membership, else the autotile flag), and
    // every "is FFM off everywhere" gate goes through ffmOffEverywhere() so
    // the scrolling half's real authority is a term in all of them.
    bool m_focusFollowsMouse = false;
    bool m_scrollingFocusFollowsMouse = false;
    /// True when NO screen can focus-follow-mouse: both global settings off
    /// AND the daemon's resolved scrolling set empty.
    ///
    /// The ONE spelling of that condition. It has three consumers — the
    /// handleCursorMoved bail and the two setting-writer latch clears — plus
    /// the applyScrollEffectBehaviour tail, and while they were spelled
    /// separately the bail read only the two globals, so a rule that turned
    /// FFM on for a screen was defeated before the per-screen read ever ran
    /// (the rule could turn the behaviour OFF but never ON).
    ///
    /// The two globals stay as terms rather than being replaced by the set:
    /// the autotile half has no per-screen set at all, and the scrolling
    /// global still governs the pre-seed window before the daemon's first
    /// scrollEffectBehaviour reply lands.
    bool ffmOffEverywhere() const
    {
        return !m_focusFollowsMouse && !m_scrollingFocusFollowsMouse && m_scrollFocusFollowsMouseScreens.isEmpty();
    }
    // ── Meta+wheel column focus ──
    bool m_wheelFocusEnabled = true;
    bool m_wheelFocusInverted = false;
    // ── Border state — uses shared BorderState from compositor-common ──
    BorderState m_border;
};

} // namespace PlasmaZones
