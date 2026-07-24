// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════════════
// WindowTrackingAdaptor — window lifecycle
//
// Placement capture, window screen/desktop changes, open/close/activate, metadata
// upserts, frame-geometry tracking, and stale-window pruning.
// ═══════════════════════════════════════════════════════════════════════════════

#include "windowtrackingadaptor.h"
#include "core/resolve/daemongeometryresolver.h"
#include <PhosphorPlacement/PlacementConfig.h>
#include <PhosphorSnapEngine/snapnavigationtargets.h>
#include "persistenceworker.h"
#include "dbus/zonedetectionadaptor.h"
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include "config/configbackends.h"
#include "core/interfaces/interfaces.h"
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorWorkspaces/ActivityManager.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include "core/platform/logging.h"
#include "core/resolve/screenmoderouter.h"
#include "core/utils/utils.h"
#include <PhosphorScreens/VirtualScreen.h>
#include "core/types/types.h"
#include <PhosphorEngine/WindowRegistry.h>
// Complete type required where ~WindowTrackingAdaptor destroys the
// unique_ptr<RuleEvaluator> member (m_ruleEvaluator).
#include <PhosphorRules/RuleEvaluator.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

void WindowTrackingAdaptor::captureWindowPlacement(const QString& windowId, const QString& authoritativeScreen)
{
    if (windowId.isEmpty() || !m_service) {
        return;
    }
    // Capture from the engine that CURRENTLY OWNS this window, not merely the first
    // engine that returns a placement. The engines keep INDEPENDENT state (snap:
    // snapped / floated; autotile: tiled / floated), so a window now managed by
    // autotile can still carry stale snap state — e.g. leftover unmanaged geometry
    // from a prior snap session — which SnapEngine::capturePlacement would claim as
    // a floated record before autotile is ever asked.
    //
    // Owning engine = the engine matching the window's CURRENT screen mode, via the
    // SAME predicate that routes the float resolver / writer / float-back geometry
    // (WTS::isWindowInAutotileMode → the daemon's screenModeForWindow). Using one
    // definition everywhere avoids the mid-mode-flip divergence an isWindowTracked()
    // check would introduce (it goes false at autotile teardown a beat before the
    // assignment flips). Both engines are still tried (first non-null wins), so the
    // mode pick is just ordering insurance — SnapEngine::capturePlacement is itself
    // gated to return nullopt on autotile-mode screens.
    PhosphorEngine::PlacementEngineBase* primary = m_snapEngine.data();
    PhosphorEngine::PlacementEngineBase* secondary = m_autotileEngine.data();
    if (m_autotileEngine && m_service->isWindowInAutotileMode(windowId)) {
        std::swap(primary, secondary);
    }
    PhosphorEngine::PlacementEngineBase* engines[] = {primary, secondary};
    for (PhosphorEngine::PlacementEngineBase* e : engines) {
        if (!e) {
            continue;
        }
        std::optional<PhosphorEngine::WindowPlacement> p = e->capturePlacement(windowId);
        if (p) {
            // The engine's capturePlacement fills ONLY its own slot (state + zone IDs
            // / tile order) — never a rectangle. Here is the SINGLE point that writes
            // the shared free/float geometry, and it does so ONLY when the window is
            // floated in this engine. For a snapped/tiled window
            // the live frame IS the zone/tile rect, so writing it would poison the
            // float-back — exactly the per-mode geometry leak this model removes. By
            // gating the write on the slot state (not a fragile frame-vs-zone compare),
            // a managed rect can never become the shared free geometry. The merge in
            // record() leaves any other screen's free geometry and the other engine's
            // slot intact.
            const PhosphorEngine::EngineSlot slot = p->slotFor(e->engineId());
            // Floated windows carry a shared free-geometry rect; snapped/tiled windows
            // don't (the live frame IS the zone/tile rect). Snapping now produces only
            // snapped/floating (the `free` state is retired) and autotile produces
            // tiled/floating — so the owning-engine check is simply `floating`.
            //
            // The owning-engine slot state is NOT sufficient on its own across a
            // mode flip. The free geometry is SHARED between both engines, but
            // m_frameGeometry is just the last frame the effect reported — it is not
            // re-validated against the slot. When a window TILED by autotile flips to
            // a snapping-mode screen (or straddles screens), isWindowInAutotileMode
            // goes false, snap becomes the owning engine and reports `floating`, yet
            // m_frameGeometry still holds the autotile TILE rect (the window has not
            // been repositioned yet). Writing it would poison the float-back with a
            // tile rect — which a later snap reopen then restores as the floated
            // position (the "tiled geometry restored to floated in snapping mode"
            // bug). The other engine still owning the window as actively tiled means
            // the live frame is its managed rect, not a genuine free frame, so refuse
            // the write — exactly as recordFreeGeometry refuses tiled frames. The
            // window's prior, genuine free geometry stays intact for the float-back.
            const bool unmanagedState = (slot.state == PhosphorEngine::WindowPlacement::stateFloating())
                && !m_service->isWindowAutotileTiled(windowId);
            if (unmanagedState) {
                const QRect frame = m_frameGeometry.value(windowId);
                if (frame.isValid()) {
                    // Screen key for the shared free/float geometry. The owning engine
                    // normally reports the window's screen, but a FLOATING window whose
                    // engine lost its screen assignment comes back with an empty
                    // screenId. The previous `!p->screenId.isEmpty()` gate then silently
                    // DROPPED its live float geometry, so it never persisted — and a
                    // later re-float (or logout→login) had no freeGeometry to restore,
                    // snapping the window back to a default instead of the user's last
                    // floated size/position. Fall back to the screen the live frame
                    // actually sits on (resolved from its centre) so a free/floating
                    // window's geometry is ALWAYS captured. Stamp it back onto
                    // p->screenId so the merged record carries a real managed screen too
                    // (the capture's engine slot makes record() adopt p->screenId).
                    QString screenKey = p->screenId;
                    if (screenKey.isEmpty()) {
                        screenKey = Utils::effectiveScreenIdAt(m_service->screenManager(), frame.center());
                    }
                    if (!screenKey.isEmpty()) {
                        // Float-back poison guard. A window floated FROM a snap (its slot
                        // carries the pre-float zones) that has NOT yet moved is still
                        // sitting on its snap rect — recording that as the free/float
                        // geometry would make a later float return to the zone, not a
                        // genuine free position. This bites windows that opened directly
                        // snapped (e.g. a SnapToZone rule) and were then floated without
                        // ever being moved: the live frame is still the zone rect. Skip
                        // until the frame differs from the pre-float zones' geometry — the
                        // user's next move while floating captures the real free spot.
                        const bool stillOnSnapRect =
                            !slot.zoneIds.isEmpty() && m_service->resolveZoneGeometry(slot.zoneIds, screenKey) == frame;
                        // Tiled analogue of the same poison guard (see the
                        // helper doc). The isWindowAutotileTiled gate above
                        // cannot catch the float-toggle edge:
                        // AutotileEngine::performToggleFloat clears the tiled
                        // bit BEFORE the daemon's sync slot reaches this
                        // capture, while the live frame is still the tile rect
                        // (KWin has not applied the float-back yet —
                        // applyGeometryForFloat runs AFTER this capture and
                        // reads what it writes). Recording that frame
                        // overwrote the genuine float-back with the tile rect,
                        // so every float "restored" the window onto its own
                        // tile. Skip until the frame moves off it — the next
                        // move while floating captures the real free spot,
                        // exactly like the snap case.
                        if (!stillOnSnapRect && !isFrameStillOnTileRect(windowId, frame)) {
                            p->screenId = screenKey;
                            p->freeGeometryByScreen.insert(screenKey, frame);
                        }
                    }
                }
            }
            // A capture that yields no restorable content — a bare {floating} slot
            // with no frame geometry (the window has no reported frame: closing, or
            // never mapped) and no zones — carries nothing to restore. Recording it
            // would only append FIFO noise that, at MaxPerApp entries per app, starves
            // and eventually evicts (removeFirst) the window's REAL placement, silently
            // breaking float/free geometry restore on the next open. Skip it: any
            // existing record for this window keeps its last meaningful state (a genuine
            // float/free transition always captures a valid frame, so it is never
            // contentless — only a frame-less capture lands here).
            if (!p->hasRestorableContent()) {
                return;
            }
            // Only mark dirty when the store actually changed. A content-identical
            // re-capture (the common case — refreshOpenWindowPlacements re-captures
            // every open window on each save) returns false, so an idle window does
            // NOT re-arm the save timer. Without this the save→refresh→record→
            // markDirty→reschedule cycle never settles (a ~2 Hz save/capture storm).
            if (m_service->placementStore().record(*p)) {
                m_service->markDirty(PhosphorPlacement::WindowTrackingService::DirtyWindowPlacements);
            }
            // Close-capture convergence (see WindowPlacementStore::collapsePureFloatSiblings).
            // Only on the close path (authoritativeScreen supplied): a window closing
            // floating supersedes stale pure-float duplicates of the same app on the
            // same screen, so a reopen restores to one consistent spot instead of
            // rotating between leftover records. Live captures (refresh / float-change)
            // pass no screen and never prune live siblings.
            if (!authoritativeScreen.isEmpty()
                && m_service->placementStore().collapsePureFloatSiblings(p->appId, p->windowId)) {
                // The prune mutated the store; flag dirty in case the record()
                // above was a content-identical no-op (then this is the only change).
                m_service->markDirty(PhosphorPlacement::WindowTrackingService::DirtyWindowPlacements);
            }
            return;
        }
    }
    // No engine actively manages the window right now → do NOT clear its records.
    // The single per-window record is per-mode MEMORY: a window on an autotile screen
    // keeps its frozen snap slot (its last snap-mode placement) and vice versa, and a
    // transient gap where neither engine tracks a just-opened window must not wipe
    // that memory. A record is only ever merge-updated by an engine recording newer
    // state (record()), consumed on restore (take), or removed by an explicit
    // exclude-rule prune (removeIf) — never by a capture miss. (Stale records are
    // bounded by MaxPerApp and consumed on reopen.)
    //
    // Authoritative close-screen fallback. A window dragged cross-screen and then
    // closed reaches here with NEITHER engine tracking it: the source engine was
    // cleared when the window left its screen (onWindowRemoved) and the destination
    // engine never adopted it (a same-engine-type cross-screen move skips the
    // handoff, and a floated window is not inserted into the destination tile
    // layout). With both capturePlacement calls declined, the record's screen would
    // keep the stale source value and a reopen would restore to the wrong monitor.
    // The caller (windowClosed) supplies the window's true current screen from KWin;
    // record the float-back there so the next open restores to the right monitor.
    // Scoped to the engine-miss path so a normally-tracked close (an engine captured
    // above and returned) is never second-guessed.
    if (!authoritativeScreen.isEmpty() && m_service && !m_service->isWindowAutotileTiled(windowId)) {
        const QRect frame = m_frameGeometry.value(windowId);
        // Same tile-rect poison guard as the primary capture path (see the
        // helper doc): a window tiled by autotile, handed off, and closed
        // before ever being repositioned still sits on its tile rect —
        // recording that as the reopen float-back would restore it onto the
        // tile, not a free spot. The same holds for a tiled close on the
        // autotile screen itself: the effect's autotile-close relay has
        // already untracked the window by the time this capture runs (relay
        // before WindowTracking, in-order), so it lands here with its frame
        // still the tile rect — only the engine's retained memory (kept
        // through its windowClosed exactly for this read) lets the guard
        // refuse it. Skipping deliberately forfeits this close's
        // OTHER effects too (the screen adoption and pure-float sibling
        // collapse recordFloatingClose bundles): the record keeps its prior
        // screen and geometry, which is strictly better than adopting a
        // poisoned one — recordFloatingClose has no geometry-less mode, and
        // a genuine free frame at the next close records normally.
        if (frame.isValid() && !isFrameStillOnTileRect(windowId, frame)) {
            m_service->recordFloatingClose(windowId, authoritativeScreen, frame);
        }
    }
}

bool WindowTrackingAdaptor::isFrameStillOnTileRect(const QString& windowId, const QRect& frame) const
{
    return m_autotileEngine && m_autotileEngine->lastManagedRect(windowId) == frame;
}

void WindowTrackingAdaptor::refreshOpenWindowPlacements()
{
    // Re-capture EVERY open window into the unified store at save time. This is the
    // generic, engine-agnostic snapshot: captureWindowPlacement asks each engine's
    // capturePlacement() for the window's current state (snapped float-back, floated
    // live geometry, autotiled position) and records it; when no engine manages a
    // window its existing record is left intact — never cleared there (see the
    // declaration doc). Saves are debounced and shutdown is guaranteed to run, so
    // this is the single point that makes open-window state (floating drag geometry,
    // autotile tile order) survive a daemon restart without any per-window capture
    // hook in the engines. m_frameGeometry holds every window the effect has
    // reported, i.e. every open window.
    if (!m_service) {
        return;
    }
    for (auto it = m_frameGeometry.constBegin(); it != m_frameGeometry.constEnd(); ++it) {
        if (it.value().isValid()) {
            captureWindowPlacement(it.key());
        }
    }
}

void WindowTrackingAdaptor::pruneExcludedPendingRestores(const QStringList& patterns)
{
    if (patterns.isEmpty() || !m_service) {
        return;
    }

    // Drop unified placement records for any app the user just added an Exclude
    // rule for, across both engines (snap and autotile records alike). One store,
    // one prune — no per-engine pending-restore queue to walk.
    const int removed = m_service->placementStore().removeIf([&patterns](const PhosphorEngine::WindowPlacement& p) {
        for (const QString& pattern : patterns) {
            if (!pattern.isEmpty() && PhosphorIdentity::WindowId::appIdMatches(p.appId, pattern)) {
                return true;
            }
        }
        return false;
    });

    if (removed > 0) {
        m_service->markDirty(PhosphorPlacement::WindowTrackingService::DirtyWindowPlacements);
        qCInfo(lcDbusWindow) << "Pruned" << removed << "placement records for excluded apps";
    }
}

void WindowTrackingAdaptor::windowScreenChanged(const QString& windowId, const QString& newScreenId)
{
    if (!m_service)
        return;
    if (!validateWindowId(windowId, QStringLiteral("screen changed"))) {
        return;
    }
    // An empty newScreenId would propagate through the cross-engine
    // handoff below and store an empty toScreenId in the engine's
    // tracking. Bail early — every downstream consumer treats an empty
    // screen id as "no tracking", and re-running with the live screen
    // would arrive via the next windowScreenChanged callback anyway.
    if (newScreenId.isEmpty()) {
        return;
    }

    // Floating window with a tracked screen: refresh the engine's
    // screen-tracking via the cross-engine handoff contract so subsequent
    // shortcut routing (lastActiveScreenName) finds the new screen instead of
    // the stale one. Without this, a snap-floated window dragged to another
    // screen leaves screenAssignments pointing at the source forever — the
    // float toggle then unfloats it back to the source-screen zone.
    QString currentZoneId = m_service->zoneForWindow(windowId);
    if (currentZoneId.isEmpty()) {
        if (!m_service->isWindowFloating(windowId)) {
            return;
        }
        const QString trackedSnap = m_snapEngine ? m_snapEngine->screenForTrackedWindow(windowId) : QString();
        const QString trackedAutotile =
            m_autotileEngine ? m_autotileEngine->screenForTrackedWindow(windowId) : QString();
        const QString trackedScreen = !trackedSnap.isEmpty() ? trackedSnap : trackedAutotile;
        if (trackedScreen.isEmpty() || PhosphorScreens::ScreenIdentity::screensMatch(trackedScreen, newScreenId)) {
            return;
        }
        PhosphorEngine::PlacementEngineBase* source =
            !trackedSnap.isEmpty() ? m_snapEngine.data() : m_autotileEngine.data();
        PhosphorEngine::PlacementEngineBase* dest = nullptr;
        if (m_autotileEngine && m_autotileEngine->isActiveOnScreen(newScreenId)) {
            dest = m_autotileEngine.data();
        } else if (m_snapEngine) {
            dest = m_snapEngine.data();
        }
        if (!dest) {
            return;
        }
        PhosphorEngine::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = windowId;
        ctx.toScreenId = newScreenId;
        ctx.fromEngineId = source ? source->engineId() : QString();
        ctx.wasFloating = true;
        ctx.sourceGeometry = m_frameGeometry.value(windowId);
        if (source && source != dest) {
            source->handoffRelease(windowId);
        }
        dest->handoffReceive(ctx);
        qCInfo(lcDbusWindow) << "windowScreenChanged: floating window" << windowId << "moved from" << trackedScreen
                             << "to" << newScreenId << "- handoff complete";
        return;
    }

    // Compare the stored screen assignment with the new screen.
    // If they match (format-agnostic), the window was moved programmatically
    // to its assigned zone's screen (restore, resnap, snap assist) — keep snapped.
    // If they differ, the user moved the window away — unsnap it.
    QString storedScreen = m_service->screenForWindow(windowId);

    // KWin reports physical screen names in outputChanged. When the stored screen
    // is a virtual screen (e.g. "HDMI-1/vs:0"), comparing against the physical name
    // ("HDMI-1") via screensMatch returns false → spurious unsnap.
    //
    // If the stored screen is virtual and its physical parent matches the reported
    // physical screen, keep the stored virtual screen ID — it's already correct.
    // Only re-resolve via geometry when the physical screens actually differ,
    // which avoids the zone-center ambiguity when a zone straddles a VS boundary.
    QString resolvedNewScreen = newScreenId;
    if (!PhosphorIdentity::VirtualScreenId::isVirtual(newScreenId)
        && PhosphorIdentity::VirtualScreenId::isVirtual(storedScreen)) {
        QString storedPhysical = PhosphorIdentity::VirtualScreenId::extractPhysicalId(storedScreen);
        if (PhosphorScreens::ScreenIdentity::screensMatch(storedPhysical, newScreenId)) {
            // Physical parent matches — the window is still on the same monitor,
            // so the stored virtual screen ID is still valid.
            resolvedNewScreen = storedScreen;
        } else {
            // Different physical screen — resolve via zone geometry as fallback.
            // NOTE: Ideally we'd use the window's actual position, but window
            // geometry is not available in this context (KWin only reports the
            // physical screen name). Using the zone center is imprecise when the
            // zone straddles a virtual screen boundary; however, the resolved ID
            // will still differ from storedScreen (different physical parent), so
            // the window correctly unsnaps regardless.
            QRect zoneGeo = m_service->zoneGeometry(currentZoneId, storedScreen);
            if (zoneGeo.isValid()) {
                QString vsId = Utils::effectiveScreenIdAt(m_service->screenManager(), zoneGeo.center());
                if (!vsId.isEmpty()) {
                    resolvedNewScreen = vsId;
                }
            }
        }
    }

    if (PhosphorScreens::ScreenIdentity::screensMatch(storedScreen, resolvedNewScreen)) {
        qCDebug(lcDbusWindow) << "windowScreenChanged:" << windowId << "moved to assigned screen, keeping snap";
        return;
    }

    qCInfo(lcDbusWindow) << "windowScreenChanged:" << windowId << "moved from" << storedScreen << "to"
                         << resolvedNewScreen << "- unsnapping";
    m_service->consumePendingAssignment(windowId);
    m_service->unassignWindow(windowId);

    // Emit unified state change for screen-change-triggered unsnap. Report the
    // resolved (effective) screen — the same value the decision + log above use —
    // not the raw newScreenId.
    Q_EMIT windowStateChanged(windowId,
                              PhosphorProtocol::WindowStateEntry{windowId, QString(), resolvedNewScreen, false,
                                                                 QStringLiteral("screen_changed"), QStringList{},
                                                                 false});
}

void WindowTrackingAdaptor::setWindowSticky(const QString& windowId, bool sticky)
{
    if (windowId.isEmpty()) {
        return;
    }
    // Delegate to service
    m_service->setWindowSticky(windowId, sticky);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window Lifecycle - Delegate to Service
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingAdaptor::windowClosed(const QString& windowId, int windowKind, const QString& screenId)
{
    if (!validateWindowId(windowId, QStringLiteral("clean up closed window"))) {
        return;
    }

    // Clear active window tracking if the closed window was the active one.
    // Without this, navigation shortcuts after closing the active window would
    // operate on a stale ID, producing confusing OSD failure messages.
    if (m_lastActiveWindowId == windowId) {
        m_lastActiveWindowId.clear();
    }

    const PhosphorEngine::WindowKind kind = PhosphorEngine::clampWindowKindFromWire(windowKind);

    // Capture the window's final live placement before teardown drops the
    // frame-geometry shadow + per-engine state below. For a FLOATING window this
    // records a floated WindowPlacement at its live geometry (the single source
    // of truth a future reopen restores from); for a snapped window it records the
    // corresponding state. Runs while the window is still floating
    // (m_service->windowClosed below tears that down) and before m_frameGeometry
    // is dropped, so the live floated geometry is captured.
    //
    // Pass the effect's authoritative close screen: when a cross-screen move has
    // orphaned the window from both engines' tracking by close time, both engine
    // capturePlacement calls miss/decline and the real screen would be lost —
    // captureWindowPlacement falls back to this screen to record the float-back.
    captureWindowPlacement(windowId, screenId);

    // Drop frame-geometry shadow entry for this window.
    m_frameGeometry.remove(windowId);
    // Drop the last-broadcast floating state for this window.
    m_broadcastFloating.remove(windowId);

    m_service->windowClosed(windowId, kind);

    // Drop registry state last: consumers subscribed to windowDisappeared may
    // rely on other WTS state still being present during their cleanup. The
    // canonical release MUST happen after remove() because WindowRegistry's
    // disappear signal fires synchronously from remove() and subscribers may
    // still call canonicalizeForLookup on their way out.
    if (m_windowRegistry) {
        const QString instanceId = PhosphorIdentity::WindowId::extractInstanceId(windowId);
        m_windowRegistry->remove(instanceId);
        m_windowRegistry->releaseCanonical(instanceId);
    }

    // Drive in-process sibling-adaptor cleanup (WindowDragAdaptor) without
    // re-introducing a D-Bus surface that nothing outside the daemon was
    // calling. Emitted after the canonical WTS teardown above so listeners
    // see consistent post-close state.
    Q_EMIT windowClosedNotification(windowId);

    qCDebug(lcDbusWindow) << "Cleaned up tracking data for closed window" << windowId;
}

void WindowTrackingAdaptor::setWindowMetadata(const QString& instanceId, const QString& appId,
                                              const QString& desktopFile, const QString& title,
                                              const QString& windowRole, int pid, int virtualDesktop,
                                              const QString& activity, int windowType, const QVariantMap& extended)
{
    if (!m_windowRegistry) {
        // Registry not wired yet — during daemon startup the kwin-effect may
        // fire before setWindowRegistry runs. Drop silently; the effect re-emits
        // on every class change so state converges.
        return;
    }
    if (instanceId.isEmpty()) {
        qCWarning(lcDbusWindow) << "setWindowMetadata: rejecting empty instance id";
        return;
    }

    PhosphorEngine::WindowMetadata meta;
    meta.appId = appId;
    meta.desktopFile = desktopFile;
    meta.title = title;
    meta.windowRole = windowRole;
    // pid / virtualDesktop crossed D-Bus as plain ints. The contract is
    // "0 = unknown" — negative values are malformed input (version skew,
    // a buggy caller) that would otherwise propagate into WindowMetadata
    // and WindowQuery. Clamp them to 0 at the boundary.
    if (pid < 0) {
        // Negative pid is now clamped to 0 at the effect-side source
        // (see window_identity.cpp pushWindowMetadata) so this path is the
        // defensive belt-and-braces for a malformed external caller — not a
        // KWin -1 leaking through. Logged at debug so the routine
        // session-restore "-1" no longer spams the warning log.
        qCDebug(lcDbusWindow) << "setWindowMetadata: negative pid" << pid << "for instance" << instanceId
                              << "— treating as 0 (unknown)";
    }
    if (virtualDesktop < 0) {
        qCWarning(lcDbusWindow) << "setWindowMetadata: negative virtualDesktop" << virtualDesktop << "for instance"
                                << instanceId << "— treating as 0 (unknown)";
    }
    meta.pid = pid < 0 ? 0 : pid;
    meta.virtualDesktop = virtualDesktop < 0 ? 0 : virtualDesktop;
    meta.activity = activity;
    // windowType crossed D-Bus as a plain int — clamp out-of-range values
    // (version skew, a malformed caller) to Unknown rather than casting blind.
    if (!PhosphorProtocol::isValidWindowType(windowType)) {
        qCWarning(lcDbusWindow) << "setWindowMetadata: out-of-range windowType" << windowType << "for instance"
                                << instanceId << "— treating as Unknown";
    }
    meta.windowType = PhosphorProtocol::windowTypeFromInt(windowType);

    // Extended window-property snapshot (the trailing a{sv}). An EMPTY map is a
    // caption-only refresh (the effect skips the snapshot on chatty title ticks):
    // carry forward the registry's existing extended fields so a per-frame title
    // update does not wipe geometry/state. A non-empty map fully replaces them —
    // each key present only when the effect could observe the value, so an absent
    // key disengages the optional (and its derived WindowQuery field), mirroring the
    // effect-side engage-only-when-known contract in window_query.cpp. Lenient
    // QVariant conversions are the boundary policy here, matching the pid /
    // windowType clamping above (a malformed caller cannot corrupt placement).
    if (extended.isEmpty()) {
        if (const std::optional<PhosphorEngine::WindowMetadata> existing = m_windowRegistry->metadata(instanceId)) {
            meta.isMinimized = existing->isMinimized;
            meta.isFullscreen = existing->isFullscreen;
            meta.isSticky = existing->isSticky;
            meta.isMaximized = existing->isMaximized;
            meta.isFocused = existing->isFocused;
            meta.isTransient = existing->isTransient;
            meta.isNotification = existing->isNotification;
            meta.keepAbove = existing->keepAbove;
            meta.keepBelow = existing->keepBelow;
            meta.skipTaskbar = existing->skipTaskbar;
            meta.skipPager = existing->skipPager;
            meta.skipSwitcher = existing->skipSwitcher;
            meta.isModal = existing->isModal;
            meta.hasDecoration = existing->hasDecoration;
            meta.isResizable = existing->isResizable;
            meta.isMovable = existing->isMovable;
            meta.isMaximizable = existing->isMaximizable;
            meta.width = existing->width;
            meta.height = existing->height;
            meta.positionX = existing->positionX;
            meta.positionY = existing->positionY;
            meta.captionNormal = existing->captionNormal;
            meta.virtualDesktops = existing->virtualDesktops;
        }
    } else {
        namespace Key = PhosphorProtocol::Service::WindowMetadataKey;
        const auto optBool = [&extended](QLatin1String key) -> std::optional<bool> {
            const auto it = extended.constFind(QString(key));
            return it != extended.constEnd() ? std::optional<bool>(it.value().toBool()) : std::nullopt;
        };
        const auto optInt = [&extended](QLatin1String key) -> std::optional<int> {
            const auto it = extended.constFind(QString(key));
            return it != extended.constEnd() ? std::optional<int>(it.value().toInt()) : std::nullopt;
        };
        const auto optString = [&extended](QLatin1String key) -> std::optional<QString> {
            const auto it = extended.constFind(QString(key));
            return it != extended.constEnd() ? std::optional<QString>(it.value().toString()) : std::nullopt;
        };
        meta.isMinimized = optBool(Key::IsMinimized);
        meta.isFullscreen = optBool(Key::IsFullscreen);
        meta.isSticky = optBool(Key::IsSticky);
        meta.isMaximized = optBool(Key::IsMaximized);
        meta.isFocused = optBool(Key::IsFocused);
        meta.isTransient = optBool(Key::IsTransient);
        meta.isNotification = optBool(Key::IsNotification);
        meta.keepAbove = optBool(Key::KeepAbove);
        meta.keepBelow = optBool(Key::KeepBelow);
        meta.skipTaskbar = optBool(Key::SkipTaskbar);
        meta.skipPager = optBool(Key::SkipPager);
        meta.skipSwitcher = optBool(Key::SkipSwitcher);
        meta.isModal = optBool(Key::IsModal);
        meta.hasDecoration = optBool(Key::HasDecoration);
        meta.isResizable = optBool(Key::IsResizable);
        meta.isMovable = optBool(Key::IsMovable);
        meta.isMaximizable = optBool(Key::IsMaximizable);
        meta.width = optInt(Key::Width);
        meta.height = optInt(Key::Height);
        meta.positionX = optInt(Key::PositionX);
        meta.positionY = optInt(Key::PositionY);
        meta.captionNormal = optString(Key::CaptionNormal);
        // Multi-desktop span list (absent for single-desktop / sticky windows,
        // so an absent key correctly clears a previous span). Same lenient
        // QVariant conversion policy as the fields above.
        if (const auto it = extended.constFind(QString(Key::VirtualDesktops)); it != extended.constEnd()) {
            const QVariantList list = it.value().toList();
            meta.virtualDesktops.reserve(list.size());
            for (const QVariant& v : list) {
                const int d = v.toInt();
                if (d > 0) {
                    meta.virtualDesktops.append(d);
                }
            }
        }
    }

    // Universal canonical seed. setWindowMetadata is the per-window choke point —
    // the effect pushes it for every window it tracks, ahead of the other
    // per-window notifications, snap-mode included. Freezing the first-seen
    // composite (appId|instanceId) here gives EVERY window a canonical entry from
    // first contact, so the snap stores (which canonicalize their keys) resolve a
    // window even after the effect restarts and re-derives a mutated-class
    // composite for it. Idempotent: the instance id is stable, so a later push
    // carrying a mutated appId returns the original composite rather than
    // re-seeding (issue #628). Cleaned up by releaseCanonical on windowClosed.
    m_windowRegistry->canonicalizeWindowId(PhosphorIdentity::WindowId::buildCompositeId(appId, instanceId));

    m_windowRegistry->upsert(instanceId, meta);
}

void WindowTrackingAdaptor::cursorScreenChanged(const QString& screenId)
{
    if (screenId.isEmpty()) {
        return;
    }

    // The KWin effect may send a physical screen ID when virtual screen configs
    // haven't loaded yet.  Resolve to the correct virtual screen using the
    // focused window's daemon-tracked screen assignment as the best hint.
    QString resolvedId = screenId;
    if (!PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        auto* mgr = m_service->screenManager();
        if (mgr && mgr->hasVirtualScreens(screenId)) {
            // Use focused window's tracked screen as hint
            if (m_service && !m_lastActiveWindowId.isEmpty()) {
                const QString trackedScreen = m_service->screenForWindow(m_lastActiveWindowId);
                if (PhosphorIdentity::VirtualScreenId::isVirtual(trackedScreen)
                    && PhosphorIdentity::VirtualScreenId::extractPhysicalId(trackedScreen) == screenId) {
                    resolvedId = trackedScreen;
                }
            }
            // If no window hint, fall back to first virtual screen
            if (!PhosphorIdentity::VirtualScreenId::isVirtual(resolvedId)) {
                QStringList vsIds = mgr->virtualScreenIdsFor(screenId);
                if (!vsIds.isEmpty()) {
                    resolvedId = vsIds.first();
                }
            }
        }
    }

    m_lastCursorScreenId = resolvedId;
    qCDebug(lcDbusWindow) << "Cursor screen changed to" << resolvedId;
}

void WindowTrackingAdaptor::screenDesktopChanged(const QString& screenId, int desktop)
{
    if (screenId.isEmpty() || desktop < 1 || !m_virtualDesktopManager) {
        return;
    }
    // The effect reports the PHYSICAL screen id; VirtualDesktopManager keys its
    // per-screen map on the same id the daemon uses everywhere. updateScreenDesktop
    // emits screenDesktopChanged only on a real change (emit-on-change).
    m_virtualDesktopManager->updateScreenDesktop(screenId, desktop);
}

void WindowTrackingAdaptor::setFrameGeometry(const QString& windowId, int x, int y, int width, int height)
{
    if (windowId.isEmpty() || width <= 0 || height <= 0) {
        return;
    }
    m_frameGeometry[windowId] = QRect(x, y, width, height);
}

void WindowTrackingAdaptor::notifyWindowResized(const QString& windowId, int oldX, int oldY, int oldWidth,
                                                int oldHeight, int newX, int newY, int newWidth, int newHeight)
{
    if (!validateWindowId(windowId, QStringLiteral("reflow after interactive resize"))) {
        return;
    }
    // Validate both frames at this untrusted D-Bus boundary. The engine also
    // requires a valid old frame (it derives the resize delta from it), so a
    // non-positive old dimension would be rejected one layer down regardless —
    // reject it here so the boundary's contract is symmetric and explicit.
    if (oldWidth <= 0 || oldHeight <= 0 || newWidth <= 0 || newHeight <= 0) {
        return;
    }

    const QRect newFrame(newX, newY, newWidth, newHeight);
    // Keep the frame shadow in sync with the committed geometry.
    m_frameGeometry[windowId] = newFrame;

    if (!m_autotileEngine) {
        return;
    }
    // Empty screen ⇒ the window isn't autotile-tracked (floating, snap-mode, or
    // unmanaged) — nothing to reflow. The engine re-validates regardless.
    const QString screenId = m_autotileEngine->screenForTrackedWindow(windowId);
    if (screenId.isEmpty()) {
        return;
    }

    const QRect oldFrame(oldX, oldY, oldWidth, oldHeight);
    m_autotileEngine->onWindowResized(windowId, oldFrame, newFrame, screenId);
}

void WindowTrackingAdaptor::windowActivated(const QString& windowId, const QString& screenId)
{
    if (!validateWindowId(windowId, QStringLiteral("process windowActivated"))) {
        return;
    }

    // Track the active window for daemon-driven navigation (move/focus/swap/etc.)
    m_lastActiveWindowId = windowId;

    // Track the active window's screen as fallback for shortcut screen detection.
    // The primary source is now cursorScreenChanged (from KWin effect's mouseChanged).
    // Prefer the daemon-tracked screen assignment (set at snap time) over what the
    // effect reports, since the effect may send a physical ID before VS configs load.
    QString resolvedScreen = screenId;
    if (!screenId.isEmpty()) {
        if (!PhosphorIdentity::VirtualScreenId::isVirtual(screenId) && m_service) {
            const QString trackedScreen = m_service->screenForWindow(windowId);
            if (PhosphorIdentity::VirtualScreenId::isVirtual(trackedScreen)
                && PhosphorIdentity::VirtualScreenId::extractPhysicalId(trackedScreen)
                    == PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId)) {
                resolvedScreen = trackedScreen;
            }
        }
        m_lastActiveScreenId = resolvedScreen;
    }

    // Cross-monitor re-home backstop (the analogue of autotile's windowFocused
    // migration): if the snap engine tracks this window but its owning per-key
    // store names a DIFFERENT monitor than the one it activated on, migrate its
    // snap state onto the activation screen so screenForTrackedWindow and the
    // unfloat fallback-screen resolution report the real monitor (#724). Guarded
    // by screensMatch so a mere virtual/physical id-form difference on the same
    // monitor never churns the stores. windowScreenChanged handles the primary
    // drift path; this catches activations that arrive without a screen-change report.
    if (PhosphorSnapEngine::SnapEngine* snap = snapEngine(); snap && !resolvedScreen.isEmpty()) {
        const QString owning = snap->screenForTrackedWindow(windowId);
        if (!owning.isEmpty() && !PhosphorScreens::ScreenIdentity::screensMatch(owning, resolvedScreen)) {
            snap->migrateWindowToScreen(windowId, resolvedScreen);
        }
    }

    qCDebug(lcDbusWindow) << "Window activated:" << windowId << "on screen" << screenId;

    // Update last-used zone when focusing a snapped window
    // Skip auto-snapped windows - only user-focused windows should update the tracking
    QString zoneId = m_service->zoneForWindow(windowId);
    if (!zoneId.isEmpty() && m_settings && m_settings->moveNewWindowsToLastZone()
        && !m_service->isAutoSnapped(windowId)) {
        QString windowClass = m_service->currentAppIdFor(windowId);
        m_service->updateLastUsedZone(zoneId, resolvedScreen, windowClass, currentDesktopForScreen(resolvedScreen));
    }
}

void WindowTrackingAdaptor::pruneStaleWindows(const QStringList& aliveWindowIds)
{
    const QSet<QString> alive(aliveWindowIds.begin(), aliveWindowIds.end());
    int persistedPruned = m_service->pruneStaleAssignments(alive);
    if (m_autotileEngine) {
        // The autotile engine keys every internal map (m_states reverse map,
        // m_windowMinSizes, m_autotileFloatedWindows, TilingState membership)
        // on each window's CANONICAL id — its FIRST-seen composite, frozen by
        // the daemon-side WindowRegistry. The alive list, by contrast, carries
        // the effect's CURRENT composites: on an effect reload the effect
        // rebuilds its id cache from the windows' present WM_CLASS, so for an
        // app that mutated its class mid-session (Electron/CEF — the exact
        // class the canonicalization machinery exists for) the current
        // composite differs from the canonical. A raw comparison would then
        // miss the live window in the alive set and FORCE-REMOVE it from the
        // layout. Canonicalize each alive id back to its registry identity so
        // the engine compares canonical-to-canonical (passthrough for ids the
        // registry never saw — never worse than the raw set).
        QSet<QString> canonicalAlive;
        canonicalAlive.reserve(aliveWindowIds.size());
        for (const QString& id : aliveWindowIds) {
            canonicalAlive.insert(m_service->canonicalizeForLookup(id));
        }
        persistedPruned += m_autotileEngine->pruneStaleWindows(canonicalAlive);
    }
    // Defensive sweep of the frame-geometry shadow store. The primary
    // cleanup path is `windowClosed`, but if a window dies without a
    // matching close signal reaching the adaptor (effect bug, compositor
    // crash, lost D-Bus call), the entry would otherwise leak forever.
    // The effect calls pruneStaleWindows precisely for this defensive
    // case — extend the same alive-set filter to m_frameGeometry.
    int frameGeoPruned = 0;
    for (auto it = m_frameGeometry.begin(); it != m_frameGeometry.end();) {
        if (!alive.contains(it.key())) {
            it = m_frameGeometry.erase(it);
            ++frameGeoPruned;
        } else {
            ++it;
        }
    }
    // Same defensive sweep for the last-broadcast floating shadow: an entry
    // would otherwise leak if the window died without a windowClosed signal.
    // Not persisted, so it does not feed the save-scheduling decision below.
    for (auto it = m_broadcastFloating.begin(); it != m_broadcastFloating.end();) {
        if (!alive.contains(it.key())) {
            it = m_broadcastFloating.erase(it);
        } else {
            ++it;
        }
    }
    // And the WindowRegistry's metadata records + canonical-id translations:
    // windowClosed releases these per-window, but a window that died without a
    // close signal (the case this whole method backstops) would leak its
    // record + canonical entry for the session. The registry keys on instance
    // ids (uuid components), so build the alive set in that form.
    if (m_windowRegistry) {
        QSet<QString> aliveInstances;
        aliveInstances.reserve(aliveWindowIds.size());
        for (const QString& id : aliveWindowIds) {
            aliveInstances.insert(PhosphorIdentity::WindowId::extractInstanceId(id));
        }
        m_windowRegistry->pruneStaleInstances(aliveInstances);
    }
    const int totalPruned = persistedPruned + frameGeoPruned;
    if (totalPruned > 0) {
        qCInfo(lcDbusWindow) << "Pruned" << totalPruned << "stale window assignments (not in KWin)";
    }
    // Only schedule a save when something PERSISTED was pruned. Frame-
    // geometry is the compositor-layer shadow store (not on disk), so a
    // frame-geo-only prune would fire scheduleSaveState → takeDirty →
    // DirtyNone-early-return — pointless debounced wake-up. Mirrors the
    // narrow-dirty-mask discipline the rest of the file follows.
    if (persistedPruned > 0) {
        scheduleSaveState();
    }
}

} // namespace PlasmaZones
