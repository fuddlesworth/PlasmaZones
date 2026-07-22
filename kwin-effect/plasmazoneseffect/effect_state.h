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
#include <QPointer>
#include <QRect>
#include <QSet>
#include <QString>
#include <QtGlobal>

#include <cstdint>
#include <memory>
#include <type_traits>

namespace KWin {
class EffectWindow;
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

/// Config-backed window-decoration appearance default, filling the slots a
/// window's per-window rules leave unset (rules win per slot). See
/// PlasmaZonesEffect::m_windowAppearanceDefault.
struct WindowAppearanceDefault
{
    bool showBorder = false;
    QString borderScope = QString(PhosphorCompositor::WindowAppearanceScope::Tiled);
    int borderWidth = 0;
    int borderRadius = 0;
    QString activeColor;
    QString inactiveColor;
    bool hideTitleBar = false;
    QString titleBarScope = QString(PhosphorCompositor::WindowAppearanceScope::Tiled);
    // Plain opacity+tint layer (Windows.* ShowOpacityTint/Opacity/Tint*),
    // rendered by the built-in "opacity-tint" pack in easy mode. The tint
    // colour carries hex or the accent sentinel like the border colours.
    bool showOpacityTint = false;
    QString opacityTintScope = QString(PhosphorCompositor::WindowAppearanceScope::Tiled);
    double opacity = 1.0;
    double tintStrength = 0.0;
    QString tintColor;
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

/// Pre-rule keepAbove/keepBelow pair captured the first time a SetWindowLayer rule
/// is applied to a window. See PlasmaZonesEffect::m_ruleWindowLayerSnapshots.
struct WindowLayerSnapshot
{
    bool keepAbove = false;
    bool keepBelow = false;
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
    /// Per-screen supersession epoch for slotApplyGeometriesBatch cascades.
    /// When cascade stagger is enabled, a daemon geometry batch spreads its
    /// per-window moves across QTimer::singleShot ticks. A rapid second batch
    /// (e.g. holding the rotate shortcut) starts its own cascade while the
    /// first one's ticks are still queued; the older batch's later-firing
    /// timers would then clobber the newer batch's positions, leaving windows
    /// in stale zones. Each batch bumps and captures the epoch for every screen
    /// it targets. A staggered apply drops itself when its screen's epoch has
    /// advanced, and the z-order restore drops only when every screen it
    /// targeted has advanced. Per-screen, not global, so a batch on
    /// one output never strands an in-flight cascade on another — mirrors the
    /// autotile cascade guard (m_autotileStaggerGenByScreen).
    QHash<QString, uint64_t> batchGenByScreen;
    int pendingVsConfigReplies = 0; ///< countdown for fetchAllVirtualScreenConfigs async replies
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

    // Window ID cache: EffectWindow* → "appId|uuid" (populated on first getWindowId call,
    // cleared in slotWindowClosed/windowDeleted). Eliminates 3-5 QString allocations per
    // getWindowId call across all hot paths (~1000-3000 allocs/sec during drag).
    QHash<KWin::EffectWindow*, QString> windowIdCache;
    // Reverse lookup: windowId → EffectWindow* (for O(1) findWindowById)
    QHash<QString, KWin::EffectWindow*> windowIdReverse;
};

} // namespace PlasmaZones
