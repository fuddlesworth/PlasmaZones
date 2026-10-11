// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/// @file effect_state.h
/// Small value/helper types that PlasmaZonesEffect holds by value or in maps.
/// Extracted from plasmazoneseffect.h to keep that header focused on the class
/// surface. These were nested types of PlasmaZonesEffect; none needs private
/// access to the class, so they live at namespace scope. Every reference in the
/// .cpp files is unqualified (inside PlasmaZonesEffect member functions), so it
/// resolves here via ordinary namespace lookup. Included by plasmazoneseffect.h.

#include <PhosphorCompositor/DecorationDefaults.h> // WindowAppearanceScope

#include <QColor>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QSet>
#include <QSizeF>
#include <QString>
#include <QVariantList>
#include <QtGlobal>

#include <cstdint>
#include <memory>
#include <type_traits>

namespace KWin {
class EffectWindow;
class LogicalOutput;
}

namespace PlasmaZones {

struct CompiledSurfacePack; // surface_types.h

/// Smoothed focus value per window driving the uSurfaceFocused ramp. `value < 0`
/// is the uninitialised sentinel; `lastMs` dedupes the per-frame advance. See
/// PlasmaZonesEffect::m_focusFade.
struct FocusFadeState
{
    float value = -1.0f;
    qint64 lastMs = -1;
};

/// Config-backed window appearance default: the decoration slots plus the
/// per-mode keep-floating-above window-layer slot, filling the slots a
/// window's per-window rules leave unset (rules win per slot). See
/// PlasmaZonesEffect::m_windowAppearanceDefault.
struct WindowAppearanceDefault
{
    // Seeded from the shared decoration defaults rather than hand-written, so
    // the values rendered before the async settings load lands are the ones the
    // daemon would persist. ShowBorder is false, so this changes nothing in the
    // normal path; it matters when a settings reply never arrives for the width
    // and radius keys (an older daemon), where the loaders below deliberately
    // keep the seed rather than clobbering it with an empty reply.
    bool showBorder = PhosphorCompositor::DecorationDefaults::ShowBorder;
    QString borderScope = QString(PhosphorCompositor::WindowAppearanceScope::Tiled);
    int borderWidth = PhosphorCompositor::DecorationDefaults::BorderWidth;
    int borderRadius = PhosphorCompositor::DecorationDefaults::BorderRadius;
    QString activeColor;
    QString inactiveColor;
    bool hideTitleBar = false;
    QString titleBarScope = QString(PhosphorCompositor::WindowAppearanceScope::Tiled);
    // Plain opacity+tint layer (Windows.* ShowOpacityTint/Opacity/Tint*),
    // rendered by the built-in "opacity-tint" pack in easy mode. The tint
    // colour carries concrete hex like the border colours (a current daemon
    // resolves the follow-the-theme sentinel before D-Bus; only an older
    // daemon still marshals the legacy "accent" token).
    bool showOpacityTint = false;
    QString opacityTintScope = QString(PhosphorCompositor::WindowAppearanceScope::Tiled);
    double opacity = 1.0;
    double tintStrength = 0.0;
    QString tintColor;
    // Keep-floating-above per placement mode (Snapping.Behavior.WindowHandling /
    // Tiling.Behavior / Scrolling.Behavior KeepFloatingAbove). Fills the window
    // LAYER slot with "above" for a floated window on a screen running that
    // mode when no SetWindowLayer rule owns it; see reconcileRuleWindowLayer.
    bool keepFloatingAboveSnapping = false;
    bool keepFloatingAboveTiling = false;
    bool keepFloatingAboveScrolling = false;

    bool anyKeepFloatingAbove() const
    {
        return keepFloatingAboveSnapping || keepFloatingAboveTiling || keepFloatingAboveScrolling;
    }
};

/// Debounced frame-geometry shadow push state per window. The window pointer
/// rides along so the flush runs the exclusion gate once per flush. See
/// PlasmaZonesEffect::m_pendingFrameGeometry.
struct PendingFrameGeometry
{
    QRect geometry;
    QPointer<KWin::EffectWindow> window;
};

/// Resolves a pack id to its compiled program, compiling on a cache miss. The fold
/// memoises the decoration-profile lookup behind this, so the input side takes it
/// as a callable rather than resolving the tree a second time.
///
/// A NON-OWNING reference to the caller's lambda, not a std::function. The fold's
/// resolver captures three pointers (24 bytes), which is past libstdc++'s 16-byte
/// small-object buffer — so every conversion to std::function HEAP-ALLOCATED, and the
/// fold converts twice. At eight decorated windows across two outputs at 60Hz that is
/// some four thousand malloc/free pairs a second, added by a refactor that shipped
/// inside a performance PR. The callee only ever invokes it during the call, so a
/// borrowed reference is all it ever needed.
class CompiledPackResolver
{
public:
    template<typename F, typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, CompiledPackResolver>>>
    CompiledPackResolver(F&& fn) // NOLINT(google-explicit-constructor): behaves as a callable
        : m_ctx(static_cast<void*>(std::addressof(fn)))
        , m_invoke([](void* ctx, const QString& packId) -> CompiledSurfacePack* {
            return (*static_cast<std::remove_reference_t<F>*>(ctx))(packId);
        })
    {
    }
    CompiledSurfacePack* operator()(const QString& packId) const
    {
        return m_invoke(m_ctx, packId);
    }

private:
    void* m_ctx;
    CompiledSurfacePack* (*m_invoke)(void*, const QString&);
};

/// Pre-write keepAbove/keepBelow pair captured the first time a SetWindowLayer
/// rule or the keep-floating-above default is applied to a window. See
/// PlasmaZonesEffect::m_ruleWindowLayerSnapshots.
struct WindowLayerSnapshot
{
    bool keepAbove = false;
    bool keepBelow = false;
};

/// The rect a scrolling batch last commanded for an X11 window, plus the
/// counter-assert burst budget. See PlasmaZonesEffect::m_scrollCommandedRects.
struct ScrollCommandedRect
{
    QRect rect;
    qint64 burstStartMs = 0;
    int burstCount = 0;
};

/// Minimize-shader stamp: the time and generation of the transition a minimize
/// event installed, so a spurious minimize→unminimize pair can cancel the exact
/// reverse leg. See PlasmaZonesEffect::m_minimizeShaderStamp.
struct MinimizeShaderStamp
{
    qint64 timeMs = 0;
    quint64 generation = 0;
};

/// Per-drag activation / float tracking. Grouped from PlasmaZonesEffect's
/// trailing member block; see PlasmaZonesEffect::m_dragActivation. Set/read from
/// the drag-tracker and async begin/endDrag reply lambdas.
struct DragActivationState
{
    // Per-drag activation tracking: set once any activation trigger is detected
    // during the current drag. Stays true for the remainder of the drag so
    // the daemon receives all subsequent cursor updates (needed for hold/release
    // cycles and overlay hide/show).
    bool detected = false;

    /// Monotonic per-drag generation. Bumped on every drag start. The async
    /// beginDrag reply lambda captures the generation at dispatch time and
    /// checks against the live value at reply time — if the drag has ended
    /// (or a new one started) before the reply arrives, the captured policy
    /// would otherwise be written into m_currentDragPolicy and bleed into
    /// the next drag's state. Generation-mismatched replies are discarded.
    quint64 generation = 0;

    // Windows floated by drag on autotile screens. The daemon emits
    // applyGeometryRequested to restore pre-autotile geometry on float,
    // but drag-to-float should keep the window where the user dropped it.
    // Entries are consumed (removed) when slotApplyGeometryRequested skips
    // the geometry restore for a drag-floated window.
    QSet<QString> floatedWindowIds;

    // Whether the window being dragged was ALREADY floating when the drag
    // began. Written in the DragTracker::dragStarted handler before any float
    // transition runs, then snapshotted into a local at callEndDrag dispatch
    // (the async endDrag reply may land after the next dragStarted has already
    // overwritten this member, so the reply lambda must not read it directly).
    // The drag-stop ApplyFloat path consults that snapshot: a window that was
    // already floating is just being moved, so its current (user-chosen) size
    // must be preserved. Re-applying the stale pre-autotile size would clobber
    // any resize the user made while floating. Only the tiled→float transition
    // wants the pre-autotile size restore.
    bool startedFloating = false;

    // A snapping excursion suspended the dragged tile mid-drag
    // (TilingHandler::suspendTileForSnapDrag); callEndDrag settles it once the
    // drop has an answer.
    bool tileSuspended = false;
};

/// Per-window geometry command stamp: the one supersession model every deferred
/// apply shares. Every geometry command for a window (applyWindowGeometry, a
/// drag-end dispatch, an effect-side float, the untrack funnel, and each entry a
/// cascade schedules) bumps the window's stamp from one monotonic counter. A
/// deferred apply (the snap and tile cascades, the screen-change cascade, the
/// mid-drag replay) captures the stamp it was scheduled under and drops itself
/// at fire time when a newer command has bumped it since.
///
/// Per WINDOW, not per screen: a per-screen epoch let a newer batch that does
/// not carry a window void that window's pending entry (over-cancel), and let a
/// single-window command leave an older pending entry standing (under-cancel).
/// Keyed by raw pointer and erased when the window is deleted; the counter is
/// session-monotonic, so a recycled address never matches a captured stamp.
struct WindowCommandStamps
{
    quint64 bump(const KWin::EffectWindow* w)
    {
        return w ? (byWindow[w] = ++seq) : 0;
    }
    /// True when @p stamp is still @p w's latest command. A zero stamp (the
    /// entry had no live window when it was scheduled) is never superseded.
    bool isCurrent(const KWin::EffectWindow* w, quint64 stamp) const
    {
        return stamp == 0 || byWindow.value(w) == stamp;
    }
    quint64 current(const KWin::EffectWindow* w) const
    {
        return byWindow.value(w);
    }
    /// Put back a stamp read before an apply that only RE-ASSERTS a command
    /// already issued (the scroll counter-assert): that apply is not a new
    /// command, so it must not void an entry scheduled after the one it
    /// re-asserts.
    void reinstate(const KWin::EffectWindow* w, quint64 stamp)
    {
        if (w && stamp != 0) {
            byWindow[w] = stamp;
        }
    }
    /// A drop is the user's own placement, so it gets a mark of its own: a
    /// reply to a request sent before the window's latest drop answers a
    /// placement the user has since replaced (F884). Only drops count. A
    /// daemon apply between a request and its reply (the open path's
    /// size-only restore) is part of the answer, not a newer one.
    void noteDrop(const KWin::EffectWindow* w)
    {
        if (w) {
            dropsByWindow[w] = ++seq;
        }
    }
    quint64 lastDrop(const KWin::EffectWindow* w) const
    {
        return dropsByWindow.value(w);
    }
    void forget(const KWin::EffectWindow* w)
    {
        byWindow.remove(w);
        dropsByWindow.remove(w);
        staleAcks.remove(w);
    }
    /// Supersede every pending deferred apply at once (daemon loss, F405):
    /// each window's stamp moves on, so no captured stamp is current any more.
    void supersedeAll()
    {
        for (auto it = byWindow.begin(); it != byWindow.end(); ++it) {
            it.value() = ++seq;
        }
        staleAcks.clear();
    }

    /// A command KWin applied WITHOUT a configure (it asked for the size the
    /// client already has) while an older configure for a different size was
    /// still unacked. That older configure stays outstanding, and when the
    /// client acks it the stale size lands. The frame-change hook answers that
    /// ack once by re-issuing @c target, valid only while @c stamp is current.
    struct StaleAck
    {
        QRect target;
        quint64 stamp = 0;
    };
    QHash<const KWin::EffectWindow*, StaleAck> staleAcks;

    QHash<const KWin::EffectWindow*, quint64> byWindow;
    QHash<const KWin::EffectWindow*, quint64> dropsByWindow;
    quint64 seq = 0;
};

/// The desktop and activity fields of a metadata push, in the form the wire
/// carries them: the first desktop's x11 number (0: every desktop or
/// unknown) and the full list only for a window on several, the first
/// activity (empty: every activity) and the full list only for several.
struct WindowContextFields
{
    int virtualDesktop = 0;
    QVariantList desktops;
    QString activity;
    QVariantList activities;
};

/// The interactive RESIZE in progress, from windowStartUserMovedResized until
/// its end is settled (F486, F701): while it holds, the window's screen
/// crossings and its desktop and activity edges wait, and
/// PlasmaZonesEffect::drainResizeHold resolves them once. One at a time, since
/// KWin runs one interactive move or resize. Call sites compare
/// `m_resizeHold.window == w`.
struct ResizeHold
{
    enum ContextAxis : quint8 {
        DesktopAxis = 1,
        ActivityAxis = 2
    };
    QPointer<KWin::EffectWindow> window;
    QRect startGeometry; ///< the pre-resize frame, the #652 report's baseline
    /// The context the window had when the resize began: every metadata push
    /// reports it while an edit is held, so the daemon does not carry the
    /// window into a context mid-gesture (F665).
    WindowContextFields context;
    bool finished = false; ///< KWin ended the gesture; the drain waits for the client
    QSizeF requestedSize; ///< moveResizeGeometry().size() at the finish
    quint64 commandStampAtFinish = 0;
    quint8 heldAxes = 0; ///< ContextAxis bits whose handler waits for the drain
    QMetaObject::Connection ackWatch;
    quint64 generation = 0; ///< bumped per hold, read by the deadline timer
};

/// Daemon readiness / virtual-screen fetch gate state. Grouped from
/// PlasmaZonesEffect's trailing member block; see PlasmaZonesEffect::m_daemonGate.
/// Cached daemon D-Bus service registration state, updated via QDBusServiceWatcher
/// signals to avoid synchronous isServiceRegistered() calls that block the
/// compositor thread.
struct DaemonGateState
{
    bool serviceRegistered = false;
    /// True between sending registerBridge and receiving its reply. Prevents
    /// the Introspect probe + daemonReady signal racing into two concurrent
    /// registrations before the first reply sets serviceRegistered.
    /// Reset on every reply path (success / error / rejection / version-
    /// mismatch) so a future retry can re-arm. ALSO reset in the
    /// serviceUnregistered handler so a daemon restart with an in-flight
    /// stale call doesn't leave the gate stuck and silently swallow the
    /// new daemon's daemonReady signal.
    bool bridgeRegistrationInFlight = false;
    /// Monotonic id of the registration attempt the gate above belongs to.
    /// Bumped when a call is sent AND when the daemon vanishes, and captured
    /// by each reply lambda. Without it a dead daemon's reply cleared the gate
    /// belonging to a LIVE registration: the daemon dies with a call in
    /// flight, serviceUnregistered clears the gate, the new daemon's
    /// daemonReady starts a second call, and then the first call's error reply
    /// lands and clears the gate out from under it — leaving a third
    /// daemonReady free to start a concurrent third registration, which is
    /// exactly the duplicate-state-push the gate exists to prevent. Never
    /// restarted; a monotonic counter is the whole mechanism.
    quint64 bridgeRegistrationGeneration = 0;
    bool readyRestoresDone = false; ///< set after slotDaemonReady snap restores dispatched

    bool virtualScreensReady = false; ///< set after all fetchVirtualScreenConfig replies arrive
    /// True while a daemon-driven geometry apply (slotApplyGeometriesBatch / slotWindowsTileRequested)
    /// is moving a window. Suppresses the windowFrameGeometryChanged crossing-detection paths so a
    /// VS swap/rotate does not produce spurious "window moved between monitors" events. The daemon
    /// emits virtualScreensChanged and the geometry batch in the same handler chain, but on the
    /// effect side those D-Bus messages can race: the geometry change fires while m_virtualScreenDefs
    /// still holds the pre-rotation regions, so the crossing comparison computes newScreenId from
    /// stale config + new position and falsely concludes the window crossed VSes. The daemon is the
    /// authoritative source of the window's intended VS during these applies, so the crossing check
    /// is unsafe and must be skipped.
    bool inGeometryApply = false;
    /// The window whose commit applyWindowGeometry's ANIMATED arm is making
    /// right now (null otherwise). That arm commits before it retargets its
    /// own leg, and the frame-change hook's strip retarget must not answer
    /// the synchronous re-entry, or it zeroes the leg's velocity first.
    const KWin::EffectWindow* animatedApplyCommit = nullptr;
    /// See WindowCommandStamps.
    WindowCommandStamps commandStamps;
    int pendingVsConfigReplies = 0; ///< countdown for fetchAllVirtualScreenConfigs async replies
    /// Countdown for the LIVE (generation 0) fetchVirtualScreenConfig replies, which the counter
    /// above deliberately does not cover — it is the startup batch's own tally. Both exist because
    /// virtualScreensReady is ONE flag for the whole effect while onVirtualScreensChanged is a
    /// PER-SCREEN signal, so a virtual-screen reconfigure touching several monitors closes the gate
    /// once and issues one fetch per monitor. Without this, the first reply to land reopened the gate
    /// for all the others, and the window-crossing detector then ran against a half-updated
    /// m_virtualScreenDefs — the same phantom crossing the gate exists to prevent. A superseded
    /// reply for one screen did it too. Every live fetch increments this exactly once and every
    /// reply path discharges it exactly once, through restoreReadyIfLive, which is already
    /// contractually reached by all of them.
    int pendingLiveVsConfigReplies = 0;
    uint64_t vsConfigGeneration = 0; ///< generation counter for fetchAllVirtualScreenConfigs
    /// Per-physId fetchVirtualScreenConfig sequence. Every fetch bumps its
    /// physId's entry; the async reply applies to m_virtualScreenDefs only if
    /// it is still the latest. Without this, two live changes in quick
    /// succession (e.g. remove-then-readd a VS) race: replies can land
    /// out-of-order and a stale payload clobbers the fresh one, leaving
    /// resolveEffectiveScreenId tagging windows with dead "physId/vs:N" ids.
    QHash<QString, uint64_t> vsFetchSeqPerPhysId;
    bool readyWindowStateProcessed = false; ///< re-entrancy guard for processDaemonReadyWindowState
    /// One-shot guard for the Rules rulesChanged D-Bus subscription.
    /// QDBusConnection::connect silently accepts duplicate subscriptions, so without
    /// this flag the subscription set would grow unbounded across every
    /// slotSettingsChanged broadcast (which re-runs loadCachedSettings()). Set true
    /// after the first successful connect from continueDaemonReadySetup().
    bool rulesSubscribed = false;
};

/// Screen/window id caches. Every field is populated lazily from const accessors
/// (getWindowId, screen-id resolution), so the whole struct is held by a mutable
/// member — see PlasmaZonesEffect::m_idCaches. m_trackedScreenPerWindow is
/// deliberately NOT grouped here: it is a non-mutable member.
struct IdCacheState
{
    // Screen ID cache: connector name → EDID screen ID (manufacturer:model:serial).
    // Avoids repeated QScreen iteration and sysfs reads during drag (~30Hz).
    // Cleared on screen geometry changes (add/remove/reconfigure).
    QHash<QString, QString> screenIdCache;

    // Connected physical screen ids (outputScreenId per KWin output), rebuilt
    // EAGERLY by both screen handlers and lazily after the other invalidation
    // points (virtual-screen change, geometry change, the bridge). Lets the
    // scroll-override path (getWindowScreenId — a per-candidate call inside
    // both focus-follows-mouse stacking walks) test output liveness with a
    // set lookup instead of an O(outputs) string-building scan per call.
    QSet<QString> connectedPhysicalIds;
    bool connectedPhysicalIdsValid = false;

    // Screen id RECORDED PER OUTPUT, so a removal reads the spelling its state was published
    // under instead of recomputing one. The recompute is unreliable on a MULTI-output removal of
    // identical monitors: KWin prunes every removed output from its list before emitting the
    // first screenRemoved, so once the cache is dropped mid-handler the duplicate scan can no
    // longer see the departed twin, and a suffixed "baseId/connector" key collapses to a bare
    // baseId. That defeats the teardown clear keyed on it. Deliberately NOT cleared by
    // clearScreenIdCache, which is the whole point: it must survive the mid-handler drop.
    // Erased when its output goes, and on add, so a raw pointer cannot outlive the output or be
    // read after a later hotplug lands at the same address.
    QHash<const KWin::LogicalOutput*, QString> screenIdByOutput;

    // Window ID cache: EffectWindow* → "appId|uuid" (populated on first getWindowId call,
    // cleared in slotWindowClosed/windowDeleted). Eliminates 3-5 QString allocations per
    // getWindowId call across all hot paths (~1000-3000 allocs/sec during drag).
    QHash<KWin::EffectWindow*, QString> windowIdCache;
    // Reverse lookup: windowId → EffectWindow* (for O(1) findWindowById)
    QHash<QString, KWin::EffectWindow*> windowIdReverse;
};

} // namespace PlasmaZones
