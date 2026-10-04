// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════════════
// WindowTrackingAdaptor — window lifecycle
//
// Placement capture, window screen changes, close, metadata upserts and
// frame-geometry tracking. The focus and activation reports live in
// activation.cpp and the stale-window prune in prune.cpp.
// ═══════════════════════════════════════════════════════════════════════════════

#include "windowtrackingadaptor.h"
#include "internal.h"
#include "core/resolve/daemongeometryresolver.h"
#include <PhosphorPlacement/PlacementConfig.h>
#include <PhosphorSnapEngine/snapnavigationtargets.h>
#include "persistenceworker.h"
#include "dbus/zonedetectionadaptor.h"
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include "config/configbackends.h"
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorWorkspaces/ActivityManager.h>
#include "core/platform/logging.h"
#include "core/resolve/screenmoderouter.h"
#include "core/utils/utils.h"
#include "core/utils/dbusvariantutils.h"
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
#include <utility>

namespace PlasmaZones {

void WindowTrackingAdaptor::captureWindowPlacement(const QString& windowId, const QString& authoritativeScreen,
                                                   bool fromStateChange)
{
    if (windowId.isEmpty() || !m_service) {
        return;
    }
    // Minimize uses floating as a live suspension so the hidden window stops
    // occupying a snap zone or autotile slot. It is not placement intent, so the
    // record is preserved as it was before minimization rather than persisting
    // that float through passive, presave, periodic, close or mode-transition
    // captures. A user-floated window was captured when it floated, so preserving
    // its prior record is also correct while it is minimized.
    //
    // Tri-state, not the collapsed bool: a REGISTERED window whose minimize state
    // was never delivered cannot be proven visible, so it takes the guard too. An
    // UNREGISTERED window (already pruned, or a registry-less test service) keeps
    // the plain capture path. An authoritative ENGINE STATE CHANGE bypasses the
    // guard entirely: the engine reports the freshly committed state (a resnap
    // can re-zone a minimized window). See the fromStateChange doc.
    bool treatAsMinimized = false;
    if (m_windowRegistry) {
        // Per-capture hot path: only pay the contains() lookup when the
        // optional is actually disengaged.
        const std::optional<bool> minimized = m_windowRegistry->minimizedState(windowId);
        treatAsMinimized = minimized.has_value()
            ? *minimized
            : m_windowRegistry->contains(PhosphorIdentity::WindowId::extractInstanceId(windowId));
    }
    // The suspension-float classification outlives the live minimize bit (the
    // unfloat commits after the animation grace), so a capture landing there
    // still preserves. A scroll tile held out for its OWN fullscreen is the same
    // kind of suspension: its live frame is the output rect, never free geometry
    // (m_cachedScrollEngine: isFullscreenFloated is ScrollEngine-only API).
    treatAsMinimized = treatAsMinimized || m_service->isSuspensionFloat(windowId)
        || (m_cachedScrollEngine && m_cachedScrollEngine->isFullscreenFloated(windowId));
    if (treatAsMinimized && !fromStateChange) {
        if (authoritativeScreen.isEmpty()) {
            qCDebug(lcDbusWindow) << "Skipping placement capture for minimized window" << windowId;
            return;
        }
        // AutotileEngine::capturePlacement has its own minimize-preserve branch
        // (facade.cpp) for engine-internal sweeps; the two coexist on purpose.
        // No engine fallback: with nothing to preserve, the engines could only
        // report a bare suspension float, the record this branch keeps out.
        std::optional<PhosphorEngine::WindowPlacement> preserved = m_service->placementStore().peekExact(windowId);
        if (!preserved) {
            return;
        }

        preserved->windowId = shadowWindowId(windowId);
        preserved->appId = m_service->currentAppIdFor(windowId);
        if (preserved->appId.isEmpty()) {
            // record() rejects an appId-less record, so the preserve would be
            // a silent no-op — say so instead of vanishing.
            qCDebug(lcDbusWindow) << "captureWindowPlacement: empty appId for minimized preserve of" << windowId
                                  << "— skipping";
            return;
        }
        // What is preserved is the PLACEMENT (engine slots + free geometry).
        // The CONTEXT is deliberately refreshed: authoritativeScreen is where
        // the window actually closed, and windowContext() is the window's OWN
        // desktop/activity from its live registry metadata (kept current by
        // the effect's desktopsChanged/activitiesChanged pushes) — a window
        // moved to another desktop while minimized must reopen there, not on
        // the desktop frozen into a pre-minimize record.
        // Managed slots recorded against a DIFFERENT screen must not be
        // re-filed verbatim under the close screen — the shared downgrade
        // (recordFloatingClose's rule) flips them to floating BEFORE the
        // screen stamp below erases the evidence of the mismatch.
        PhosphorPlacement::WindowTrackingService::downgradeMismatchedManagedSlots(*preserved, preserved->screenId,
                                                                                  authoritativeScreen);
        preserved->screenId = authoritativeScreen;
        // Registry-guarded like the minimize probe at the top of this function:
        // a registry-less service is a supported state (tests call
        // setWindowRegistry(nullptr) on a live adaptor), and this branch is
        // reachable with a null registry because isSuspensionFloat above needs
        // no registry to set treatAsMinimized. Without a registry the preserve
        // keeps the record's own desktop/activity, which is the documented
        // degradation rather than a crash.
        if (m_windowRegistry) {
            const QString instanceId = PhosphorIdentity::WindowId::extractInstanceId(windowId);
            if (const auto context = m_windowRegistry->windowContext(instanceId)) {
                preserved->virtualDesktop = context->virtualDesktop;
                preserved->activity = context->activity;
            }
        }
        // Synthesize the OWNING engine's floating slot whenever the record
        // lacks one — not merely when the map is empty (recordFloatingClose's
        // convention): the tiling engines' reopen accept path reads strictly their own
        // slot, so a record carrying only FOREIGN slots would otherwise
        // preserve nothing the reopening engine can see. Also what makes
        // record()'s merge adopt the close screen for a pure-float record.
        const QString owningEngine = m_service->owningModeEngineId(windowId, authoritativeScreen);
        // Same gate shape as recordFloatingClose: absent owning slot → add.
        // (An all-empty record is dropped by the hasRestorableContent gate
        // below either way — a bare floating slot is contentless residue.)
        if (!preserved->engines.contains(owningEngine)) {
            PhosphorEngine::EngineSlot slot;
            slot.state = PhosphorEngine::WindowPlacement::stateFloating();
            preserved->engines.insert(owningEngine, slot);
        }
        // Contentless residue must never enter the appId FIFO (mirrors the
        // primary capture path's gate): it would starve and evict real
        // placements.
        if (!preserved->hasRestorableContent()) {
            return;
        }
        const bool recorded = m_service->placementStore().record(*preserved);
        // Close-path-only prune (this branch requires a non-empty
        // authoritativeScreen above, which only the close path supplies) —
        // live captures must never prune siblings.
        const bool collapsed =
            m_service->placementStore().collapsePureFloatSiblings(preserved->appId, preserved->windowId);
        if (recorded || collapsed) {
            m_service->markDirty(PhosphorPlacement::WindowTrackingService::DirtyWindowPlacements);
        }
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
    // The scrolling engine joins the capture chain the same way. Its
    // capturePlacement returns nullopt for untracked windows, so trying it
    // FIRST when it holds the window is the same ordering insurance the
    // autotile swap above provides (snap would otherwise claim the window
    // as a stale floated record). Held IN VIEW: a background desktop's
    // column of a multi-desktop window snapped here must not capture first,
    // or its snap slot is never recorded (F361).
    PhosphorEngine::PlacementEngineBase* scrollFirst = nullptr;
    if (m_scrollEngine && !m_scrollEngine->heldScreenForWindow(windowId).isEmpty()) {
        scrollFirst = m_scrollEngine.data();
    }
    PhosphorEngine::PlacementEngineBase* engines[] = {scrollFirst, primary, secondary,
                                                      scrollFirst ? nullptr : m_scrollEngine.data()};
    for (PhosphorEngine::PlacementEngineBase* e : engines) {
        if (!e) {
            continue;
        }
        std::optional<PhosphorEngine::WindowPlacement> p = e->capturePlacement(windowId);
        if (p) {
            // The engine's capturePlacement fills ONLY its own slot (state + zone IDs
            // / tile order) — never a rectangle. This is one of the three float-back
            // writers (WindowTrackingService's float-back docs name the model), and
            // it writes ONLY when the window is floated in this engine. For a snapped/tiled window
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
                && !m_service->isWindowEngineTiled(windowId);
            if (unmanagedState) {
                const QRect frame = m_frameGeometry.value(shadowWindowId(windowId));
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
                        // The frame is a sample, so it takes the whole model: (P) it
                        // must lie on the screen it is filed under (a re-homed window's
                        // shadow can still be on the old screen, F365); (S) a maximized
                        // or fullscreen frame fills the output (F157); (M) a window
                        // floated off a zone or tile that has not moved yet is still on
                        // its managed frame (the float toggle clears the tiled bit
                        // before KWin applies the float-back). The next move while
                        // floating captures the real free spot.
                        const bool fillsOutput =
                            m_windowRegistry && m_windowRegistry->fillsOutputState(windowId).value_or(false);
                        if (!fillsOutput && m_service->geometryBelongsToScreen(frame, screenKey)
                            && !m_service->isManagedFrame(windowId, frame)) {
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
            // contentless — only a frame-less capture lands here). CONTINUE, not
            // return: a contentless capture has not "won" the first-non-null
            // ordering, and the other engine may still hold a real slot (a
            // tiled window whose mode-flip left the primary with residue).
            if (!p->hasRestorableContent()) {
                continue;
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
    // No m_service re-check: the entry guard at the top of this function
    // already established it.
    if (!authoritativeScreen.isEmpty() && !m_service->isWindowEngineTiled(windowId)) {
        const QRect frame = m_frameGeometry.value(shadowWindowId(windowId));
        // The managed-frame refusal, as in the primary capture path: a window tiled
        // by autotile, handed off, and closed
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
        if (frame.isValid() && !m_service->isManagedFrame(windowId, frame)) {
            m_service->recordFloatingClose(windowId, authoritativeScreen, frame);
        }
    }
}

QString WindowTrackingAdaptor::shadowWindowId(const QString& windowId) const
{
    return m_service ? m_service->canonicalizeForLookup(windowId) : windowId;
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
    // Snapshot the keys before iterating: captureWindowPlacement fans out into
    // engine code and signal handlers that can reach the m_frameGeometry
    // writers, and mutating a QHash mid-iteration is undefined.
    const QList<QString> windowIds = m_frameGeometry.keys();
    for (const QString& windowId : windowIds) {
        if (m_frameGeometry.value(windowId).isValid()) {
            captureWindowPlacement(windowId);
        }
    }
}

void WindowTrackingAdaptor::pruneExcludedPlacements(const QStringList& patterns)
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
    const QString instanceId = PhosphorIdentity::WindowId::extractInstanceId(windowId);
    if (PhosphorIdentity::WindowId::extractInstanceId(m_lastActiveWindowId) == instanceId) {
        m_lastActiveWindowId.clear();
    }

    const PhosphorEngine::WindowKind kind = PhosphorEngine::clampWindowKindFromWire(windowKind);

    // Release the open claim BEFORE the capture reads the store.
    m_service->placementStore().releaseOpenClaim(windowId);
    // A closed window has nothing left to return to an output.
    dropEvacueeParks(windowId);

    // Capture the window's final live placement before teardown drops the
    // frame-geometry shadow + per-engine state below: a floated WindowPlacement at
    // its live geometry (the single source of truth a reopen restores from), or the
    // snapped state. Runs while the window is still floating (m_service->windowClosed
    // below tears that down) and before m_frameGeometry is dropped.
    //
    // Pass the effect's authoritative close screen: a cross-screen move can orphan
    // the window from both engines by close time, so both capturePlacement calls
    // miss and the real screen would be lost; the capture falls back to this screen
    // for the float-back. Validate it first (a stale id from an output unplugged
    // mid-close would poison the reopen screen), falling back to the live frame's
    // centre; an empty result keeps the record's prior screen, which beats a dead one.
    QString closeScreen = screenId;
    if (!closeScreen.isEmpty() && m_service->screenManager()
        && !m_service->screenManager()->physicalScreenFor(closeScreen).isValid()) {
        const QRect closeFrame = m_frameGeometry.value(shadowWindowId(windowId));
        closeScreen = closeFrame.isValid() ? Utils::effectiveScreenIdAt(m_service->screenManager(), closeFrame.center())
                                           : QString();
        qCDebug(lcDbusWindow) << "windowClosed: unknown close screen" << screenId << "for" << windowId
                              << "— resolved to" << closeScreen;
    }
    captureWindowPlacement(windowId, closeScreen);
    // The engine kept answering the hold for this closed window so the capture
    // above classified its frame as a suspension; that answer is spent now.
    forgetClosedFullscreenHold(windowId);

    // Session-transient suspension-float classification dies with the window.
    m_service->clearSuspensionFloat(windowId);

    m_service->windowClosed(windowId, kind);

    // Drop the shadow maps AFTER the service teardown: windowClosed's cascade
    // can synchronously re-enter the float relay and re-insert a zombie.
    const QString shadowId = shadowWindowId(windowId);
    m_frameGeometry.remove(shadowId);
    m_lastManagedFrame.remove(shadowId);
    m_pendingOpenGeometry.remove(shadowId);
    m_pendingOpenSize.remove(shadowId);
    m_broadcastFloating.remove(shadowId);
    // shadowId, NOT the raw windowId: shadowWindowId() IS the canonical id,
    // and tabColorRuleParams is called with ids taken from the ScrollEngine
    // tab-strip payload, which stores them canonicalized. For a WM_CLASS-
    // mutating app (Electron/CEF) the effect's current composite differs from
    // the canonical one, so removing by the raw id would miss in exactly the
    // case canonicalization exists for and leak the entry for the session.
    // Identical to windowId whenever the id never mutated.
    m_tabColorMemo.remove(shadowId);
    // The evaluator's shared memo needs the same close-time drop, and it is
    // keyed differently: resolveCachedFiltered is called with the RAW windowId
    // its callers were handed, not the canonical shadow id. Evict both — the
    // two coincide for a well-behaved app, and for a class-mutating one the
    // window can have seeded an entry under either. evictCached is a no-op for
    // a key with no entry. Without this the entry lingers until the evaluator's
    // own cap eviction or the next rules save retires its whole generation, and
    // a reused window id would read a dead window's verdict.
    if (m_ruleEvaluator) {
        m_ruleEvaluator->evictCached(shadowId);
        if (windowId != shadowId) {
            m_ruleEvaluator->evictCached(windowId);
        }
    }

    // Drop registry state last: consumers subscribed to windowDisappeared may
    // rely on other WTS state still being present during their cleanup. The
    // canonical release MUST happen after remove() because WindowRegistry's
    // disappear signal fires synchronously from remove() and subscribers may
    // still call canonicalizeForLookup on their way out.
    if (m_windowRegistry) {
        m_windowRegistry->remove(instanceId);
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
        // Teardown or a registry-less unit test. The effect re-emits anyway.
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

    // Extended window-property snapshot (the trailing a{sv}). An EMPTY map — or
    // one carrying ONLY CaptionNormal — is a caption-only refresh (the effect
    // skips the snapshot on chatty title ticks but still sends captionNormal,
    // which derives from the caption and would otherwise stay permanently stale
    // on exactly that path): carry forward the registry's existing extended
    // fields so a per-frame title update does not wipe geometry/state, then take
    // the fresh captionNormal when present. Any other non-empty map fully
    // replaces them — each key present only when the effect could observe the
    // value, so an absent key disengages the optional (and its derived
    // WindowQuery field), mirroring the effect-side engage-only-when-known
    // contract in window_query.cpp. Lenient QVariant conversions are the
    // boundary policy here, matching the pid / windowType clamping above (a
    // malformed caller cannot corrupt placement).
    //
    // Known sentinel ambiguity, accepted: a FULL push whose optionals are all
    // disengaged would also arrive as an empty map and be read as a caption
    // refresh. Unreachable from the effect today (geometry keys are always
    // engaged for a live window), and the wire doc forbids senders using the
    // empty shape to mean "nothing known" — an explicit refresh marker would
    // be the upgrade path if a second bridge ever needs it.
    namespace Key = PhosphorProtocol::Service::WindowMetadataKey;
    const bool captionOnlyRefresh =
        extended.isEmpty() || (extended.size() == 1 && extended.contains(QString(Key::CaptionNormal)));
    if (captionOnlyRefresh) {
        if (const std::optional<PhosphorEngine::WindowMetadata> existing = m_windowRegistry->metadata(instanceId)) {
            // Carry the WHOLE existing record forward, then re-apply the
            // handful of fields THIS push is authoritative for (the plain
            // D-Bus arguments parsed above). A hand-copied per-field
            // carry-forward silently dropped every extended field later added
            // to WindowMetadata.
            const PhosphorEngine::WindowMetadata fresh = meta;
            meta = *existing;
            meta.appId = fresh.appId;
            meta.desktopFile = fresh.desktopFile;
            meta.title = fresh.title;
            meta.windowRole = fresh.windowRole;
            meta.pid = fresh.pid;
            meta.virtualDesktop = fresh.virtualDesktop;
            meta.activity = fresh.activity;
            meta.windowType = fresh.windowType;
            // The multi-desktop span is an EXTENDED field, so it is carried
            // forward rather than re-sent — but virtualDesktop above is
            // authoritative on this push, and the effect re-derives it from
            // x11DesktopNumber every time. A window that moved between the last
            // full push and this caption tick therefore arrives with a fresh
            // scalar beside a span from before the move, which breaks
            // WindowMetadata's stated invariant that a non-empty span starts
            // with the scalar. Drop the span in that case: it describes a
            // membership the window no longer has, and the next full push
            // re-establishes it. Consumers then read the scalar, which is the
            // one field this push actually knows.
            if (!meta.virtualDesktops.isEmpty() && meta.virtualDesktops.constFirst() != meta.virtualDesktop) {
                meta.virtualDesktops.clear();
            }
        }
        // Fresh captionNormal from the caption tick, when the effect sent one.
        if (const auto it = extended.constFind(QString(Key::CaptionNormal)); it != extended.constEnd()) {
            meta.captionNormal = it.value().toString();
        }
    } else {
        // Single pass over the map with allocation-free QString==QLatin1String
        // comparisons. The previous per-key constFind lambdas converted every
        // QLatin1String key to a temporary QString (~23 allocations per full
        // push, and full pushes now also ride every minimize edge). Absent
        // keys leave the optionals disengaged, same as before.
        for (auto it = extended.constBegin(); it != extended.constEnd(); ++it) {
            const QString& k = it.key();
            const QVariant& v = it.value();
            if (k == Key::IsMinimized) {
                meta.isMinimized = v.toBool();
            } else if (k == Key::IsDemandingAttention) {
                meta.isDemandingAttention = v.toBool();
            } else if (k == Key::IsFullscreen) {
                meta.isFullscreen = v.toBool();
            } else if (k == Key::IsSticky) {
                meta.isSticky = v.toBool();
            } else if (k == Key::IsMaximized) {
                meta.isMaximized = v.toBool();
            } else if (k == Key::IsFocused) {
                meta.isFocused = v.toBool();
            } else if (k == Key::IsTransient) {
                meta.isTransient = v.toBool();
            } else if (k == Key::IsNotification) {
                meta.isNotification = v.toBool();
            } else if (k == Key::KeepAbove) {
                meta.keepAbove = v.toBool();
            } else if (k == Key::KeepBelow) {
                meta.keepBelow = v.toBool();
            } else if (k == Key::SkipTaskbar) {
                meta.skipTaskbar = v.toBool();
            } else if (k == Key::SkipPager) {
                meta.skipPager = v.toBool();
            } else if (k == Key::SkipSwitcher) {
                meta.skipSwitcher = v.toBool();
            } else if (k == Key::IsModal) {
                meta.isModal = v.toBool();
            } else if (k == Key::HasDecoration) {
                meta.hasDecoration = v.toBool();
            } else if (k == Key::IsResizable) {
                meta.isResizable = v.toBool();
            } else if (k == Key::IsMovable) {
                meta.isMovable = v.toBool();
            } else if (k == Key::IsMaximizable) {
                meta.isMaximizable = v.toBool();
            } else if (k == Key::Width) {
                meta.width = v.toInt();
            } else if (k == Key::Height) {
                meta.height = v.toInt();
            } else if (k == Key::PositionX) {
                meta.positionX = v.toInt();
            } else if (k == Key::PositionY) {
                meta.positionY = v.toInt();
            } else if (k == Key::CaptionNormal) {
                meta.captionNormal = v.toString();
            } else if (k == Key::VirtualDesktops) {
                // Multi-desktop span list (absent for single-desktop / sticky
                // windows, so an absent key correctly clears a previous span).
                // Over the bus it arrives as a QDBusArgument, which toList()
                // reads as empty, so it is unwrapped first (F1003).
                const QVariantList list = DBusVariantUtils::convertDbusArgument(v).toList();
                meta.virtualDesktops.reserve(list.size());
                for (const QVariant& d : list) {
                    const int desktop = d.toInt();
                    if (desktop > 0) {
                        meta.virtualDesktops.append(desktop);
                    }
                }
            }
        }
    }

    // Universal canonical seed. setWindowMetadata is the per-window choke point —
    // the effect pushes it for every window it tracks, before any report naming
    // its context (only the sticky bit goes first), snap-mode included. Freezing the first-seen
    // composite (appId|instanceId) here gives EVERY window a canonical entry from
    // first contact, so the snap stores (which canonicalize their keys) resolve a
    // window even after the effect restarts and re-derives a mutated-class
    // composite for it. Idempotent: the instance id is stable, so a later push
    // carrying a mutated appId returns the original composite rather than
    // re-seeding (issue #628). That includes a first push whose appId is still
    // EMPTY: the appId-less composite is frozen like any other, because a
    // canonical that changed once the class arrived would strand the engine
    // state keyed under the earlier id. The registry's own remove() retires the
    // mapping when the window closes.
    m_windowRegistry->canonicalizeWindowId(PhosphorIdentity::WindowId::buildCompositeId(appId, instanceId));

    m_windowRegistry->upsert(instanceId, meta);
}

void WindowTrackingAdaptor::setFrameGeometry(const QString& windowId, int x, int y, int width, int height)
{
    if (windowId.isEmpty() || width <= 0 || height <= 0) {
        return;
    }
    // Key on the CANONICAL id: a raw key would drop a class-mutating app's
    // float-back. Reads match.
    const QString shadowId = shadowWindowId(windowId);
    const QRect frame(x, y, width, height);
    m_frameGeometry[shadowId] = frame;
    // The frame a managed window settled at, which can differ from the rect
    // the engine emitted (a size-constrained client centred in its tile, a
    // size-increment client short of its zone). Never from a drag: the drag
    // subject's frames are drop positions, a genuine float-back (F452).
    if (shadowId != m_interactiveDragWindow && m_service
        && (m_service->isWindowEngineTiled(windowId) || m_service->occupiesZoneInView(windowId))) {
        m_lastManagedFrame[shadowId] = frame;
    }
    // A fresh report supersedes what the open path asked for, landed or not.
    m_pendingOpenGeometry.remove(shadowId);
    m_pendingOpenSize.remove(shadowId);
}

void WindowTrackingAdaptor::setInteractiveDragWindow(const QString& windowId)
{
    m_interactiveDragWindow = windowId.isEmpty() ? QString() : shadowWindowId(windowId);
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
    // Keep the frame shadow in sync with the committed geometry, in the
    // map's canonical key space (see setFrameGeometry).
    m_frameGeometry[shadowWindowId(windowId)] = newFrame;

    const QRect oldFrame(oldX, oldY, oldWidth, oldHeight);
    // Scroll strips reconcile the interactive resize into the column's
    // stored intent; autotile reflows the tree. Route to the engine holding
    // the window IN VIEW: a hidden desktop's column of a multi-desktop window
    // would otherwise take the resize meant for the tiles in view (F361).
    if (m_scrollEngine) {
        const QString scrollScreen = m_scrollEngine->heldScreenForWindow(windowId);
        if (!scrollScreen.isEmpty()) {
            m_scrollEngine->onWindowResized(windowId, oldFrame, newFrame, scrollScreen);
            return;
        }
    }
    if (!m_autotileEngine) {
        return;
    }
    const QString screenId = m_autotileEngine->heldScreenForWindow(windowId);
    if (screenId.isEmpty()) {
        return;
    }
    m_autotileEngine->onWindowResized(windowId, oldFrame, newFrame, screenId);
}

void WindowTrackingAdaptor::relayWindowReleasedFromContext(const QString& windowId, const QString& screenId)
{
    if (windowId.isEmpty()) {
        return;
    }
    // Still snapped in the context in view (a multi-desktop window released
    // from another desktop): the effect's zone cache and the IsSnapped / Zone
    // rule fields follow the view, and they are right as they are.
    if (m_service && !m_service->zoneForWindow(windowId).isEmpty()) {
        return;
    }
    // "unsnapped", and an empty zoneId, which is what the effect's zone cache
    // reads as "this window occupies no zone" and removes the entry for. The
    // screen is carried so a subscriber that keys on it sees the context the
    // window was released FROM, which is the only screen this statement is
    // about. isFloating stays false: the release says the window stopped being
    // a resident there, not that it started floating, and the float domain is
    // per mode and answered elsewhere.
    Q_EMIT windowStateChanged(windowId,
                              PhosphorProtocol::WindowStateEntry{windowId, QString(), screenId, false,
                                                                 QStringLiteral("unsnapped"), QStringList{}, false});
}

} // namespace PlasmaZones
