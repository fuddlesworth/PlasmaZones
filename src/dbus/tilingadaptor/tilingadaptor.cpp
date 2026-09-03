// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilingadaptor.h"

#include "core/platform/logging.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"

#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorProtocol/WindowMarshalling.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace PlasmaZones {

TilingAdaptor::TilingAdaptor(PhosphorScreens::ScreenManager* screenManager, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_screenManager(screenManager)
{
    // Engine-agnostic by construction: the adaptor holds no engine until the
    // composition root supplies the pipeline list via setLifecycleEngines and
    // wires each engine's outbound signals to the relay entry points.
    qCDebug(lcDbusTiling) << "TilingAdaptor initialized";
}

void TilingAdaptor::setLifecycleEngines(const QVector<PhosphorEngine::IPlacementEngine*>& engines)
{
    // Interface-only borrows: the adaptor dispatches inbound lifecycle
    // calls through the list but makes no signal connections (the
    // composition root wires each engine's outbound signals to the relay
    // entry points).
    m_lifecycleEngines = engines;
    m_lifecycleEngines.removeAll(nullptr);
    if (m_lifecycleEngines.isEmpty()) {
        // Real teardown goes through clearEngine(); this empty-list form
        // exists for symmetry and has no production caller (see the header).
        // It still clears BOTH open queues, because neither can be retried
        // without a pipeline: parked opens are skipped by the announce path's
        // empty-union bail, and panel-deferred opens reach a flush that finds
        // no pipeline and discards them anyway. Clearing the deferral queue
        // here is also what makes the flush path's empty-pipeline branch
        // genuinely unreachable with entries in hand, which its test comment
        // claims.
        m_unclaimedOpens.clear();
        m_pendingOpens.clear();
    }
}

bool TilingAdaptor::ensurePipeline(const char* methodName) const
{
    if (m_lifecycleEngines.isEmpty()) {
        qCWarning(lcDbusTiling) << "Cannot" << methodName << "- no pipeline engines available";
        return false;
    }
    return true;
}

PhosphorEngine::IPlacementEngine* TilingAdaptor::engineOwningScreen(const QString& screenId) const
{
    for (PhosphorEngine::IPlacementEngine* engine : m_lifecycleEngines) {
        if (engine->isActiveOnScreen(screenId)) {
            return engine;
        }
    }
    // Primary fallback is deliberate here, UNLIKE dispatchWindowOpened's
    // strict claim loop: the callers route stateless notifications (focus,
    // desktop switches), and the fallback is the NORMAL route during a desktop
    // switch — isAutotileScreen is evaluated against the current desktop, so
    // switching to a non-autotile desktop makes the strict loop miss, and this
    // is what still delivers the report to the engine whose same-screen arm
    // schedules the revalidate repair. An open adopted by the wrong engine, by
    // contrast, would tile a window on a screen it does not own.
    //
    // NOT because "every engine self-guards on ownership", which this comment
    // used to claim and which is false: AutotileEngine::windowFocused guards on
    // WINDOW tracking, not screen ownership, and read an unknown screen as a
    // genuine cross-screen move — so an unvalidated screen id arriving here
    // untracked the window outright. That branch now checks isKnownScreen; the
    // fallback stays because the repair path depends on it.
    return m_lifecycleEngines.isEmpty() ? nullptr : m_lifecycleEngines.first();
}

PhosphorEngine::IPlacementEngine* TilingAdaptor::engineOwningWindow(const QString& windowId) const
{
    for (PhosphorEngine::IPlacementEngine* engine : m_lifecycleEngines) {
        if (engine->isWindowTracked(windowId)) {
            return engine;
        }
    }
    return m_lifecycleEngines.isEmpty() ? nullptr : m_lifecycleEngines.first();
}

void TilingAdaptor::setActiveLayouts(const QVariantMap& activeLayouts)
{
    // Change gate: the daemon pushes unconditionally from every
    // updateEngineScreens pass (see the header doc); only an actual map
    // change reaches the wire, so the effect's cache invalidation cost is
    // bounded by real layout changes.
    if (m_activeLayouts == activeLayouts) {
        return;
    }
    m_activeLayouts = activeLayouts;
    Q_EMIT activeLayoutsChanged(activeLayouts);
}

void TilingAdaptor::relayScrollTabStrips(const QString& screenId, const QString& stripsJson)
{
    if (screenId.isEmpty()) {
        qCWarning(lcDbusTiling) << "relayScrollTabStrips: empty screen id, dropping payload";
        return;
    }
    // "[]" is the retraction spelling, and the replay cache stores absence
    // rather than an empty array so a replaying effect reads "no indicators
    // here" from the map shape alone (see the header doc). An empty string is
    // treated the same way: it carries no columns either.
    const bool retracts = stripsJson.isEmpty() || stripsJson == QLatin1String("[]");
    // NOT change-gated, deliberately, unlike the paint-override relay below.
    // The gate lives upstream in the scroll engine (m_lastTabStripPayload in
    // engine_apply.cpp), which only emits on a real model change, so a second
    // gate here could only ever duplicate that one. The cache below is for
    // REPLAY (a freshly loaded effect asking what it missed), not for
    // suppression. testRelayEmitsAndCachesPerScreen pins the repeat emission.
    if (retracts) {
        m_lastScrollTabStrips.remove(screenId);
    } else {
        m_lastScrollTabStrips.insert(screenId, stripsJson);
    }
    // The signal always carries the canonical "[]" for a retraction, so a
    // receiver never has to decide what an empty string means.
    Q_EMIT scrollTabStripsChanged(screenId, retracts ? QStringLiteral("[]") : stripsJson);
}

void TilingAdaptor::setScrollTabPaintOverrides(const QString& screenId, const QVariantMap& overrides)
{
    if (screenId.isEmpty()) {
        qCWarning(lcDbusTiling) << "setScrollTabPaintOverrides: empty screen id, dropping overrides";
        return;
    }
    const auto it = m_scrollTabPaintOverrides.constFind(screenId);
    const bool had = it != m_scrollTabPaintOverrides.constEnd();
    if (overrides.isEmpty()) {
        if (!had) {
            return; // clearing a screen that carried nothing: no change
        }
        m_scrollTabPaintOverrides.remove(screenId);
    } else {
        if (had && *it == overrides) {
            return;
        }
        m_scrollTabPaintOverrides.insert(screenId, overrides);
    }
    Q_EMIT scrollTabPaintOverridesChanged(screenId, overrides);
}

QVariantMap TilingAdaptor::scrollTabPaintOverrides() const
{
    QVariantMap out;
    for (auto it = m_scrollTabPaintOverrides.constBegin(); it != m_scrollTabPaintOverrides.constEnd(); ++it) {
        out.insert(it.key(), it.value());
    }
    return out;
}

void TilingAdaptor::clearScrollTabPaintOverridesWhere(const std::function<bool(const QString&)>& pred)
{
    // Load-bearing copy of the keys: setScrollTabPaintOverrides mutates the
    // map being walked (it removes the entry for an empty map).
    const QStringList screens = m_scrollTabPaintOverrides.keys();
    for (const QString& screenId : screens) {
        if (pred(screenId)) {
            setScrollTabPaintOverrides(screenId, {});
        }
    }
}

void TilingAdaptor::relayScrollTabColorsChanged()
{
    // The broadcast makes the effect re-query every window it paints, so
    // its verdicts move without any targeted relay passing through here.
    // The targeted gate's memory is stale from this point and must not be
    // allowed to suppress the next per-window relay against it.
    m_lastScrollTabColorsRelay.clear();
    Q_EMIT scrollTabColorsChanged(QString(), QVariantMap());
}

void TilingAdaptor::relayScrollTabColorsForWindow(const QString& windowId)
{
    if (windowId.isEmpty() || !m_windowTrackingAdaptor) {
        return;
    }
    // Only windows the effect actually paints a pill for. The strip payload
    // is compact JSON whose "tabs" arrays hold the column members' window
    // ids, so a quoted-substring test over the cached payloads answers
    // "is this window a tab on some screen" without parsing. Bounding the
    // relay this way keeps the effect's per-window colour cache to the tabs
    // it draws rather than every window that ever changed its title; a
    // window that becomes a tab later is covered by the effect's own
    // scrollTabColors query when the strip for it arrives.
    //
    // The substring test is UNSCOPED — it does not check that the match sits
    // under a "tabs" array — and that is a robustness note rather than a bug.
    // A false positive needs some other string value in the payload to equal
    // a full window id including its quotes, and window ids are
    // "<class>|<uuid>" while every other string the payload carries is a
    // screen id, a colour or a title fragment. Nothing constructible collides.
    // The cost of a hypothetical collision is also bounded: one extra colour
    // relay for a window the effect does not paint a pill for, which the
    // effect ignores. Parsing the JSON per title change to close it would be
    // strictly worse on the hot path this bound exists to keep cheap.
    const QString quoted = QLatin1Char('"') + windowId + QLatin1Char('"');
    bool named = false;
    for (auto it = m_lastScrollTabStrips.constBegin(); it != m_lastScrollTabStrips.constEnd(); ++it) {
        if (it.value().contains(quoted)) {
            named = true;
            break;
        }
    }
    if (!named) {
        // The effect evicts its verdict for a window that stopped being a
        // tab anywhere and re-queries on re-tab; the gate's memory has to go
        // in step, or a re-tabbed window whose colours moved and moved back
        // during the interval would be answered from a memory the effect no
        // longer shares.
        m_lastScrollTabColorsRelay.remove(windowId);
        return;
    }
    // Change-gated HERE because nothing upstream gates this one: the params are
    // recomputed per relay from the rule set, so an unchanged map would cross
    // the bus every time (a window with no rule retitles as often as a terminal
    // does) and the effect would compare and drop it. Note relayScrollTabStrips
    // deliberately does NOT gate — the scroll engine already emits only on a
    // real model change there, so a second gate could only duplicate it. The
    // two differ on purpose.
    const QVariantMap colors = m_windowTrackingAdaptor->tabColorRuleParams(windowId);
    const auto it = m_lastScrollTabColorsRelay.constFind(windowId);
    if (it != m_lastScrollTabColorsRelay.constEnd() && *it == colors) {
        return;
    }
    m_lastScrollTabColorsRelay.insert(windowId, colors);
    Q_EMIT scrollTabColorsChanged(windowId, colors);
}

QVariantMap TilingAdaptor::scrollTabStrips() const
{
    QVariantMap out;
    for (auto it = m_lastScrollTabStrips.constBegin(); it != m_lastScrollTabStrips.constEnd(); ++it) {
        out.insert(it.key(), it.value());
    }
    return out;
}

QVariantMap TilingAdaptor::scrollTabColors(const QString& windowId)
{
    // The WTA owns the resolve and its memo; this is a pass-through so the
    // effect can reach it on the interface it already listens to.
    if (!m_windowTrackingAdaptor || windowId.isEmpty()) {
        return {};
    }
    return m_windowTrackingAdaptor->tabColorRuleParams(windowId);
}

QStringList TilingAdaptor::combinedManagedScreens() const
{
    QSet<QString> all;
    for (const PhosphorEngine::IPlacementEngine* engine : m_lifecycleEngines) {
        all += engine->activeScreens();
    }
    // Sorted: set iteration order is unspecified and wire consumers compare
    // successive payloads.
    QStringList out(all.cbegin(), all.cend());
    out.sort();
    return out;
}

void TilingAdaptor::relayWindowFloatingChanged(const QString& windowId, bool isFloating, const QString& screenId)
{
    const auto it = m_lastFloatBroadcast.constFind(windowId);
    if (it != m_lastFloatBroadcast.constEnd() && it.value() == isFloating) {
        return;
    }
    m_lastFloatBroadcast.insert(windowId, isFloating);
    Q_EMIT windowFloatingChanged(windowId, isFloating, screenId);
}

// ═══════════════════════════════════════════════════════════════════════════
// Property Accessors
// ═══════════════════════════════════════════════════════════════════════════

bool TilingAdaptor::enabled() const
{
    return std::any_of(m_lifecycleEngines.cbegin(), m_lifecycleEngines.cend(),
                       [](const PhosphorEngine::IPlacementEngine* engine) {
                           return engine->isEnabled();
                       });
}

QStringList TilingAdaptor::managedScreens() const
{
    return combinedManagedScreens();
}

// ═══════════════════════════════════════════════════════════════════════════
// Tiling Operations
// ═══════════════════════════════════════════════════════════════════════════

void TilingAdaptor::retile(const QString& screenId)
{
    if (!ensurePipeline("retile")) {
        return;
    }
    qCDebug(lcDbusTiling) << "retile: screen=" << (screenId.isEmpty() ? QStringLiteral("all") : screenId);
    for (PhosphorEngine::IPlacementEngine* engine : m_lifecycleEngines) {
        if (screenId.isEmpty() || engine->isActiveOnScreen(screenId)) {
            engine->retile(screenId);
        }
    }
}

void TilingAdaptor::retileAllScreens()
{
    retile(QString());
}

int TilingAdaptor::pendingWindowOpensCount() const
{
    return m_pendingOpens.size();
}

void TilingAdaptor::dispatchWindowOpened(const PhosphorProtocol::WindowOpenedEntry& entry)
{
    if (entry.windowId.isEmpty() || entry.screenId.isEmpty()) {
        return;
    }
    // Claim this instance's placement record before any selector reads one, the
    // same reason the snap channel does it at the head of resolveWindowRestore.
    // The two open channels and every later re-drive must agree on WHICH record
    // belongs to this window.
    if (m_windowTrackingAdaptor && m_windowTrackingAdaptor->service()) {
        auto* svc = m_windowTrackingAdaptor->service();
        svc->placementStore().claimForOpen(entry.windowId, svc->currentAppIdFor(entry.windowId));
    }

    // Window-rule open routing (RouteToScreen / RouteToDesktop). The WTA owns the
    // rule store + evaluator and the desktop/output-move relay signals. It emits a
    // RouteToDesktop move and, for a RouteToScreen pin onto a DIFFERENT engine-managed
    // monitor, returns that screen so we insert the window into its placement state
    // instead of the spawn screen's (snap-mode targets are handled by the snap
    // placement path, so the returned screen is always empty or engine-managed).
    // Routing runs ONCE per open: applyOpenRoutingForTiling has side
    // effects (RouteToDesktop move request, expected-output-move arming),
    // so a parked entry must not re-run it on retry — the routed screen is
    // baked into the parked entry instead.
    PhosphorProtocol::WindowOpenedEntry routedEntry = entry;
    bool ruleRouted = false;
    if (m_windowTrackingAdaptor) {
        const QString routed =
            m_windowTrackingAdaptor->applyOpenRoutingForTiling(entry.windowId, entry.screenId, &ruleRouted);
        if (!routed.isEmpty()) {
            routedEntry.screenId = routed;
        }
    }
    // An explicit routing/placement directive outranks the
    // remembered-placement reclaim — same precedence the snap facade
    // applies. Keyed on the directive MATCHING, not on a redirect actually
    // happening: a rule that pins the window to the screen it already opened
    // on (or to a disconnected one) still owns its monitor, and reading the
    // empty redirect as "no rule" is what let the two channels apply
    // opposite precedence.
    dispatchOpenToClaimingEngine(routedEntry, /*allowPark=*/true, /*allowCrossScreenClaim=*/!ruleRouted);
}

void TilingAdaptor::dispatchOpenToClaimingEngine(const PhosphorProtocol::WindowOpenedEntry& entry, bool allowPark,
                                                 bool allowCrossScreenClaim)
{
    // Cross-screen session reclaim FIRST, before any arrival-screen claim:
    // KWin's session restore opens windows on a nondeterministic output, so a
    // window recorded TILED on engine E's screen routinely arrives announced
    // on some other engine-managed screen. E pulls it home
    // (claimCrossScreenReopen consults the unified placement store's recorded
    // screen); handing it to the ARRIVAL screen's engine instead would tile
    // it into a strip/layout it never belonged to, and the engines' own defer
    // gates would still leave it stranded. Ordering, not just the gates,
    // because the claim and the defer must agree no matter which engine the
    // arrival loop below would have reached first. Arrivals on non-managed
    // (snap-mode) screens never reach this dispatch at all — their reclaim
    // runs off SnapAdaptor::resolveWindowRestore, which every open passes
    // through. Suppressed (allowCrossScreenClaim=false) when a routing or
    // placement DIRECTIVE MATCHED for this window — keyed on the match, not
    // on a redirect having happened, so a rule pinning the window to the
    // screen it already opened on still outranks the remembered placement
    // (dispatchWindowOpened documents the same distinction).
    //
    // Move-release suppression: this announce can be the second half of a
    // live release/re-announce pair (releaseWindowTracking then re-announce —
    // m_moveReleasedInstances documents the yank-back this caused). Consume
    // the one-shot and let the ARRIVAL screen's engine adopt the window where
    // the user put it.
    //
    // Consumed UNCONDITIONALLY, ahead of the allowCrossScreenClaim test rather
    // than behind it. This announce IS the release's re-announce whichever way
    // the claim gate already stands, so it is what the one-shot was armed for;
    // short-circuiting the remove() behind the gate left the entry armed on a
    // rule-routed re-announce (allowCrossScreenClaim already false), and the
    // window's NEXT announce — the effect re-announces a live window after a
    // desktop or activity demotion, with no daemon-side close, see
    // flushPendingWindowOpens — then spent the stale one-shot suppressing a
    // reclaim that had nothing to do with the move.
    if (m_moveReleasedInstances.remove(PhosphorIdentity::WindowId::extractInstanceId(entry.windowId)) > 0
        && allowCrossScreenClaim) {
        qCInfo(lcDbusTiling) << "dispatchOpenToClaimingEngine:" << entry.windowId
                             << "re-announced after a live move release — cross-screen reclaim suppressed";
        allowCrossScreenClaim = false;
    }
    if (allowCrossScreenClaim) {
        for (PhosphorEngine::IPlacementEngine* engine : m_lifecycleEngines) {
            if (engine->claimCrossScreenReopen(entry.windowId, entry.screenId, qMax(0, entry.minWidth),
                                               qMax(0, entry.minHeight))) {
                removeUnclaimedOpen(entry.windowId);
                return;
            }
        }
        // Post-reclaim ownership check. A reclaim can also come from the
        // OTHER channel (SnapAdaptor::resolveWindowRestore, which the effect
        // drives FIRST for a snap-restore candidate and whose reply callback
        // then sends this very announce), and that announce still carries
        // the ARRIVAL screen — the reclaim's retile is queued and cannot
        // have moved the window before the reply returns. Dispatching it
        // would hand the window to the arrival screen's engine, whose
        // windowOpened sees it as already tracked, skips its defer gate
        // entirely, and MIGRATES it back: the reclaim silently undone.
        //
        // INSIDE the directive guard, with the claim it protects. When a
        // routing directive matched, no reclaim ran, so there is nothing to
        // protect — and refusing the dispatch there would be actively wrong:
        // applyOpenRoutingForTiling has already emitted the output-move
        // marker, so the window physically moves to the routed screen while
        // no engine adopts it there and the old engine still believes it
        // holds it. The directive must win on both halves or on neither.
        //
        // Stated over LIVE engine state, so it is indifferent to which
        // channel claimed and covers every interleaving. heldScreenForWindow,
        // not isWindowTracked (raw reverse-map key, which a refused adoption
        // can leave dangling) and not isWindowManaged/isWindowTiled (both
        // exclude a legitimately-floated adoption). Compared by SCREEN, never
        // by engine identity: an identity test would refuse a window whose
        // engine legitimately holds it on THIS screen in another context.
        for (PhosphorEngine::IPlacementEngine* engine : m_lifecycleEngines) {
            const QString heldScreen = engine->heldScreenForWindow(entry.windowId);
            if (!heldScreen.isEmpty() && !PhosphorScreens::ScreenIdentity::screensMatch(heldScreen, entry.screenId)) {
                removeUnclaimedOpen(entry.windowId);
                qCInfo(lcDbusTiling) << "dispatchOpenToClaimingEngine:" << entry.windowId << "announced on"
                                     << entry.screenId << "but already held on" << heldScreen
                                     << "— ignoring the stale arrival (cross-screen reclaim already placed it)";
                return;
            }
        }
    }
    // Per-screen engine dispatch: the effect reports opens for every
    // engine-managed screen through this interface, so hand the window to
    // whichever pipeline engine CLAIMS the (possibly rule-routed) screen.
    // Deliberately NOT the primary-fallback helper: during a mode flip a
    // brief window exists where neither engine claims the screen, and the
    // fallback would let the primary engine silently adopt and tile a
    // window on a screen it does not own.
    for (PhosphorEngine::IPlacementEngine* engine : m_lifecycleEngines) {
        if (engine->isActiveOnScreen(entry.screenId)) {
            removeUnclaimedOpen(entry.windowId);
            engine->windowOpened(entry.windowId, entry.screenId, qMax(0, entry.minWidth), qMax(0, entry.minHeight));
            return;
        }
    }
    // Neither engine claims the screen. Park ONLY while a screens announce
    // is actually pending (mid-flip; a same-union autotile↔scrolling flip
    // never re-adds via the effect's managedScreensChanged diff, so the
    // coalesced announce retries these once the flip settles). Outside a
    // flip the screen is genuinely unmanaged — the effect's view merely
    // lags the daemon's — and parking would resurrect the window on some
    // unrelated later announce; drop instead. The retry itself passes
    // allowPark=false, so a still-unclaimed entry gets exactly one retry.
    if (allowPark && m_screensAnnouncePending) {
        removeUnclaimedOpen(entry.windowId);
        m_unclaimedOpens.append(ParkedOpen{entry, allowCrossScreenClaim});
        qCDebug(lcDbusTiling) << "dispatchOpenToClaimingEngine: no pipeline engine claims" << entry.screenId << "for"
                              << entry.windowId << "- parked until the screens announce retries it";
        return;
    }
    qCDebug(lcDbusTiling) << "dispatchOpenToClaimingEngine: no pipeline engine claims" << entry.screenId << "for"
                          << entry.windowId << "- dropped (screen not engine-managed)";
}

void TilingAdaptor::removeUnclaimedOpen(const QString& windowId)
{
    for (int i = m_unclaimedOpens.size() - 1; i >= 0; --i) {
        if (m_unclaimedOpens.at(i).entry.windowId == windowId) {
            m_unclaimedOpens.removeAt(i);
        }
    }
}

void TilingAdaptor::removePendingOpen(const QString& windowId)
{
    // The panel-geometry deferral queue's twin of removeUnclaimedOpen: a
    // window that opens and closes inside the startup panel-query window
    // (splash screens, quickly-dismissed session-restore dialogs) would
    // otherwise still be dispatched to an engine at panelGeometryReady — a
    // queued open for a window already reported closed can never be wanted.
    for (int i = m_pendingOpens.size() - 1; i >= 0; --i) {
        if (m_pendingOpens.at(i).windowId == windowId) {
            m_pendingOpens.removeAt(i);
        }
    }
}

bool TilingAdaptor::deferUntilPanelReady(qsizetype incomingCount)
{
    // Fast path: panel geometry already known, or no PhosphorScreens::ScreenManager at all (tests
    // without a singleton fall through and proceed with whatever geometry exists).
    if (!m_screenManager || m_screenManager->isPanelGeometryReady()) {
        return false;
    }

    // Overflow valve. panelGeometryReady is a one-shot that a wedged Plasma
    // D-Bus query may never deliver, so an unbounded queue would grow for the
    // session. Overflow processes immediately instead of dropping: computing
    // zones against the unreserved screen rect costs at most one visible
    // correction once the real geometry lands, whereas a dropped entry leaves
    // the window untiled with nothing to retry it.
    //
    // Flush the already-queued entries BEFORE returning. Replay order decides
    // strip column order and master assignment, so the newcomer the caller is
    // about to dispatch must land AFTER everything that arrived before it —
    // returning false without draining would put it in front of the whole
    // backlog, which then replays behind it on the next flush.
    if (m_pendingOpens.size() + incomingCount > kMaxPendingOpens) {
        qCWarning(lcDbusTiling) << "deferUntilPanelReady: pending-open queue at capacity" << kMaxPendingOpens
                                << "- processing" << incomingCount << "window(s) against unreserved screen geometry";
        flushPendingWindowOpens();
        return false;
    }

    // Lazily wire the flush slot on first deferral. AutoConnection resolves to a
    // direct call when the signal fires from our thread (production: the D-Bus
    // watcher's finished callback runs on the main thread, same as us), so there
    // is no posted-event reentrancy. Leaving the connection installed for the
    // session is fine — panelGeometryReady is a one-shot signal (see
    // PhosphorScreens::ScreenManager::queryKdePlasmaPanels).
    if (!m_pendingOpensListenerInstalled) {
        connect(m_screenManager, &PhosphorScreens::ScreenManager::panelGeometryReady, this,
                &TilingAdaptor::flushPendingWindowOpens);
        m_pendingOpensListenerInstalled = true;
    }
    return true;
}

void TilingAdaptor::flushPendingWindowOpens()
{
    if (m_pendingOpens.isEmpty()) {
        return;
    }
    if (!ensurePipeline("flushPendingWindowOpens")) {
        m_pendingOpens.clear();
        return;
    }
    // Move-then-clear so any re-entrant dispatchWindowOpened → slot callback → new
    // deferral (unlikely post-ready, but defensive) queues into a fresh list rather
    // than mutating the one we're iterating.
    const PhosphorProtocol::WindowOpenedList toFlush = std::move(m_pendingOpens);
    m_pendingOpens.clear();
    qCInfo(lcDbusTiling) << "flushPendingWindowOpens: processing" << toFlush.size() << "deferred windows";
    // Same burst bracket as windowsOpenedBatch — this flush IS the batch
    // path whenever the opens were queued behind the panel-geometry gate.
    for (PhosphorEngine::IPlacementEngine* engine : m_lifecycleEngines) {
        engine->beginArrivalBurst();
    }
    // DISPATCH is the choke point for validity and duplicates, not the two
    // append sites. windowsOpenedBatch applies its seenWindowIds guard only on
    // the immediate path, and windowOpened does not check the queue at all, so
    // a window can reach here twice: once from a batch carrying it twice, and
    // once from two separate deferred calls (the effect drops m_notifiedWindows
    // on a desktop or activity demotion with no daemon-side close, so a return
    // re-announces while the first entry is still queued). Dispatching twice
    // runs applyOpenRoutingForTiling's side effects twice, which the
    // parked-open path bakes in a routed screen specifically to avoid.
    //
    // Guarding here rather than at the appends means a future enqueue path
    // cannot bypass it. Keep-first preserves the replay order that decides
    // strip column order and master assignment, and matches the batch guard.
    QSet<QString> seenWindowIds;
    for (const auto& entry : toFlush) {
        if (entry.windowId.isEmpty() || entry.screenId.isEmpty()) {
            qCWarning(lcDbusTiling) << "flushPendingWindowOpens: dropping invalid queued entry" << entry.windowId
                                    << entry.screenId;
            continue;
        }
        if (seenWindowIds.contains(entry.windowId)) {
            qCDebug(lcDbusTiling) << "flushPendingWindowOpens: dropping duplicate queued open" << entry.windowId;
            continue;
        }
        seenWindowIds.insert(entry.windowId);
        dispatchWindowOpened(entry);
    }
    for (PhosphorEngine::IPlacementEngine* engine : m_lifecycleEngines) {
        engine->endArrivalBurst();
    }
}

void TilingAdaptor::windowOpened(const QString& windowId, const QString& screenId, int minWidth, int minHeight)
{
    if (!ensurePipeline("windowOpened")) {
        return;
    }
    if (windowId.isEmpty()) {
        qCDebug(lcDbusTiling) << "windowOpened: empty window ID";
        return;
    }
    if (screenId.isEmpty()) {
        qCDebug(lcDbusTiling) << "windowOpened: empty screen ID for window" << windowId;
        return;
    }
    // Non-blocking startup gate: if the first panel D-Bus query has not completed
    // yet, queue this entry and return. Processing immediately would compute zones
    // against the unreserved full-screen rect (PhosphorScreens::ScreenManager's availability cache
    // is empty until the sensor windows and Plasma D-Bus panel query finish), and
    // the daemon would emit a visible correction a frame later. Flushing happens in
    // flushPendingWindowOpens() when panelGeometryReady fires.
    PhosphorProtocol::WindowOpenedEntry entry{windowId, screenId, minWidth, minHeight};
    if (deferUntilPanelReady(1)) {
        qCInfo(lcDbusTiling) << "windowOpened: deferring" << windowId
                             << "until panel geometry ready (queue size=" << (m_pendingOpens.size() + 1) << ")";
        m_pendingOpens.append(entry);
        return;
    }
    qCDebug(lcDbusTiling) << "windowOpened: windowId=" << windowId << "screen=" << screenId << "minSize=" << minWidth
                          << "x" << minHeight;
    dispatchWindowOpened(entry);
}

void TilingAdaptor::windowsOpenedBatch(const PhosphorProtocol::WindowOpenedList& entries)
{
    if (!ensurePipeline("windowsOpenedBatch")) {
        return;
    }

    // See windowOpened() above for the startup-race rationale. The batch path queues
    // all entries atomically so windows in the same batch retain their original order
    // when flushed.
    if (deferUntilPanelReady(entries.size())) {
        qCInfo(lcDbusTiling) << "windowsOpenedBatch: deferring" << entries.size()
                             << "windows until panel geometry ready";
        m_pendingOpens.append(entries);
        return;
    }

    qCInfo(lcDbusTiling) << "windowsOpenedBatch: processing" << entries.size() << "windows";

    // Burst bracket (IPlacementEngine::beginArrivalBurst): engines that
    // apply geometry per arrival defer to one apply per screen, so a
    // daemon-restart re-announce of an unchanged session does not march
    // windows through partial intermediate layouts.
    for (PhosphorEngine::IPlacementEngine* engine : m_lifecycleEngines) {
        engine->beginArrivalBurst();
    }
    // Intra-batch duplicate guard, matching the tile-request JSON sibling's:
    // a peer bug sending one window twice would open it into two contexts,
    // and the second dispatch is never a legitimate re-announce (those come
    // as separate calls). Empty ids fall through to dispatchWindowOpened's
    // own validation.
    QSet<QString> seenWindowIds;
    for (const auto& entry : entries) {
        if (!entry.windowId.isEmpty() && seenWindowIds.contains(entry.windowId)) {
            qCDebug(lcDbusTiling) << "windowsOpenedBatch: dropping duplicate entry for" << entry.windowId;
            continue;
        }
        seenWindowIds.insert(entry.windowId);
        dispatchWindowOpened(entry);
    }
    for (PhosphorEngine::IPlacementEngine* engine : m_lifecycleEngines) {
        engine->endArrivalBurst();
    }
}

void TilingAdaptor::windowMinSizeUpdated(const QString& windowId, int minWidth, int minHeight)
{
    if (!ensurePipeline("windowMinSizeUpdated")) {
        return;
    }
    if (windowId.isEmpty()) {
        qCDebug(lcDbusTiling) << "windowMinSizeUpdated: empty window ID";
        return;
    }
    qCDebug(lcDbusTiling) << "windowMinSizeUpdated: windowId=" << windowId << "minSize=" << minWidth << "x"
                          << minHeight;
    if (PhosphorEngine::IPlacementEngine* engine = engineOwningWindow(windowId)) {
        engine->windowMinSizeUpdated(windowId, qMax(0, minWidth), qMax(0, minHeight));
    }
}

void TilingAdaptor::windowClosed(const QString& windowId)
{
    if (windowId.isEmpty()) {
        qCDebug(lcDbusTiling) << "windowClosed: empty window ID";
        return;
    }
    // The dedup-gate entry dies with the window UNCONDITIONALLY — a close
    // arriving after clearEngine() (shutdown) must not leak it, or a stale
    // value could suppress the first genuine broadcast of a reused id.
    // BOTH key forms go: engines relay float changes under the registry's
    // canonical id while the effect closes the window under its raw id (they
    // differ for a class-mutating app), so removing only the close id would
    // strand the other entry for the process lifetime.
    m_lastFloatBroadcast.remove(windowId);
    m_lastScrollTabColorsRelay.remove(windowId);
    if (m_windowTrackingAdaptor) {
        m_lastFloatBroadcast.remove(m_windowTrackingAdaptor->shadowWindowId(windowId));
        m_lastScrollTabColorsRelay.remove(m_windowTrackingAdaptor->shadowWindowId(windowId));
    }
    removeUnclaimedOpen(windowId);
    removePendingOpen(windowId);
    // A move-release one-shot for a window that closed instead of
    // re-announcing dies with it — instance ids are unique, so the entry
    // could never fire again, but the set must not accumulate corpses.
    m_moveReleasedInstances.remove(PhosphorIdentity::WindowId::extractInstanceId(windowId));
    if (!ensurePipeline("windowClosed")) {
        return;
    }
    qCDebug(lcDbusTiling) << "windowClosed: windowId=" << windowId;
    // Capture the window's final engine slot BEFORE the engine untracks it.
    // The effect relays Tiling.windowClosed ahead of
    // WindowTracking.windowClosed (in-order connection), so by the time the
    // WindowTracking close capture runs, the owning engine has already
    // dropped the window and its capturePlacement returns nullopt — the
    // persisted slot (float verdict, column order) was then only as fresh as
    // the last save-timer sweep. Capturing here runs the shared funnel while
    // the engine still answers authoritatively; the screen-less form
    // deliberately skips the close-only branches (minimize preserve, orphan
    // float-back fallback, sibling collapse), which stay with the
    // WindowTracking close where the authoritative screen is known. Hoisted
    // ABOVE the ownership lookup on purpose: the funnel self-guards for
    // untracked windows, and engineOwningWindow's first-engine fallback must
    // stay free to change without silently disabling this capture. This
    // method is a genuine close only — the drag-bypass tracking drop goes
    // through releaseWindowTracking, which captures nothing.
    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->captureWindowPlacement(windowId);
    }
    if (PhosphorEngine::IPlacementEngine* engine = engineOwningWindow(windowId)) {
        engine->windowClosed(windowId);
    }
}

void TilingAdaptor::onTrackedWindowDestroyed(const QString& windowId)
{
    // Post-teardown raw id only (see the header doc) — the canonical-form
    // residue of a class-mutating app is reclaimed by
    // pruneStaleFloatBroadcasts.
    m_lastFloatBroadcast.remove(windowId);
    m_lastScrollTabColorsRelay.remove(windowId);
    removeUnclaimedOpen(windowId);
    removePendingOpen(windowId);
    m_moveReleasedInstances.remove(PhosphorIdentity::WindowId::extractInstanceId(windowId));
}

void TilingAdaptor::pruneStaleFloatBroadcasts(const QStringList& aliveInstances)
{
    if (aliveInstances.isEmpty()) {
        // Same fail-closed stance as the WTA prune: an empty alive report
        // must not wipe live dedup state.
        return;
    }
    const QSet<QString> alive(aliveInstances.cbegin(), aliveInstances.cend());
    for (auto it = m_lastFloatBroadcast.begin(); it != m_lastFloatBroadcast.end();) {
        if (!alive.contains(PhosphorIdentity::WindowId::extractInstanceId(it.key()))) {
            it = m_lastFloatBroadcast.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_lastScrollTabColorsRelay.begin(); it != m_lastScrollTabColorsRelay.end();) {
        if (!alive.contains(PhosphorIdentity::WindowId::extractInstanceId(it.key()))) {
            it = m_lastScrollTabColorsRelay.erase(it);
        } else {
            ++it;
        }
    }
    // The two OPEN QUEUES and the move-release one-shot set go with them.
    // windowClosed drops all five together, and this prune is the backstop for
    // a window that never produced a destroy notification — so covering only
    // the two dedup maps left exactly the windows this function exists for
    // holding a queue slot.
    //
    // Milder than a stale dedup entry (both queues are capped, and a flush or
    // an announce retry drains them wholesale), but a dead entry that reaches
    // dispatch is a phantom tile, and behind a panel gate that never lifts it
    // sits for the session eating one of the 512 slots.
    m_unclaimedOpens.removeIf([&alive](const ParkedOpen& parked) {
        return !alive.contains(PhosphorIdentity::WindowId::extractInstanceId(parked.entry.windowId));
    });
    m_pendingOpens.removeIf([&alive](const PhosphorProtocol::WindowOpenedEntry& entry) {
        return !alive.contains(PhosphorIdentity::WindowId::extractInstanceId(entry.windowId));
    });
    // Already keyed by instance id, so it is compared against `alive` directly.
    // A leaked entry can never fire (instance ids are unique to a window), but
    // the set is otherwise unbounded across a session of drag-out floats and
    // desktop moves whose windows die without a destroy notification.
    m_moveReleasedInstances.removeIf([&alive](const QString& instanceId) {
        return !alive.contains(instanceId);
    });
}

void TilingAdaptor::releaseWindowTracking(const QString& windowId)
{
    if (windowId.isEmpty()) {
        qCDebug(lcDbusTiling) << "releaseWindowTracking: empty window ID";
        return;
    }
    // Same bookkeeping as windowClosed — both key forms of the float-relay
    // dedup entry and any parked open go with the tracking — but NO capture:
    // the window is live and mid-drag, and its frame is not a placement.
    m_lastFloatBroadcast.remove(windowId);
    // The tab-colour relay memo goes too, matching windowClosed and
    // onTrackedWindowDestroyed. It would self-heal on the next relay for
    // this id, but only via a retitle while the window is off-strip; the
    // symmetric eviction closes the verdict-moved-and-moved-back residue.
    m_lastScrollTabColorsRelay.remove(windowId);
    if (m_windowTrackingAdaptor) {
        m_lastFloatBroadcast.remove(m_windowTrackingAdaptor->shadowWindowId(windowId));
        m_lastScrollTabColorsRelay.remove(m_windowTrackingAdaptor->shadowWindowId(windowId));
    }
    removeUnclaimedOpen(windowId);
    removePendingOpen(windowId);
    // Arm the move-release one-shots BEFORE the pipeline gate, mirroring the
    // bookkeeping above: the window is live and being moved, and its next
    // announce must not be mistaken for a session restore (see
    // m_moveReleasedInstances).
    //
    // TWO of them, armed together because the same announce is misread in two
    // independent places and each excuse is consumed at a different moment:
    // this one by the dispatch below (suppressing the cross-screen reclaim),
    // the store's by the engine's takeForReopen (suppressing the
    // reclaim-credit burn). A single flag consumed at the first moment would
    // already be gone by the second — see markInstanceMovedLive.
    m_moveReleasedInstances.insert(PhosphorIdentity::WindowId::extractInstanceId(windowId));
    if (m_windowTrackingAdaptor && m_windowTrackingAdaptor->service()) {
        m_windowTrackingAdaptor->service()->placementStore().markInstanceMovedLive(windowId);
    }
    if (!ensurePipeline("releaseWindowTracking")) {
        return;
    }
    qCDebug(lcDbusTiling) << "releaseWindowTracking: windowId=" << windowId;
    if (PhosphorEngine::IPlacementEngine* engine = engineOwningWindow(windowId)) {
        engine->windowClosed(windowId);
    }
}

void TilingAdaptor::notifyWindowFocused(const QString& windowId, const QString& screenId)
{
    if (!ensurePipeline("notifyWindowFocused")) {
        return;
    }
    if (windowId.isEmpty()) {
        qCDebug(lcDbusTiling) << "notifyWindowFocused: empty window ID (focus cleared)";
        return;
    }
    if (screenId.isEmpty()) {
        qCDebug(lcDbusTiling) << "notifyWindowFocused: empty screenId";
        return;
    }
    qCDebug(lcDbusTiling) << "notifyWindowFocused: windowId=" << windowId << "screen=" << screenId;
    // R2 fix: Pass screen ID to engine so m_windowToScreen is updated on focus
    // change. This also addresses R5 (cross-screen window movement detection) since
    // focus events carry the current screen, updating stale m_windowToScreen entries.
    if (PhosphorEngine::IPlacementEngine* engine = engineOwningScreen(screenId)) {
        engine->windowFocused(windowId, screenId);
    }
}

// floatWindow, unfloatWindow, toggleFocusedWindowFloat, toggleWindowFloat removed:
// all float operations are now routed through the unified WTA methods
// (toggleFloatForWindow for toggle, setWindowFloatingForScreen for directional).

void TilingAdaptor::clearEngine()
{
    // Interface-only borrows, no connections to drop. Also neutralise any
    // pending coalesced announce (its lambda re-checks the empty list) and
    // every per-session queue/dedup cache except m_activeLayouts — none of it
    // may leak into a restart (a stale dedup value could suppress the first
    // genuine broadcast of the new session).
    //
    // m_activeLayouts is the exception, and the reason is the GETTER rather
    // than the emission: activeLayouts() is ungated, so it keeps answering
    // this map for as long as the object exists. Clearing it would replace a
    // true answer with an empty one, which a reader cannot tell from "no
    // screen has a layout" — the same call the scrolling adaptor's clearEngine
    // makes about its two daemon-built values, and for the same reason. It is
    // also not per-session state to begin with: the map is daemon-built, it
    // covers every effective screen rather than the engine-managed ones, and
    // the daemon re-pushes it from every updateEngineScreens pass, so a new
    // session restates it in full rather than incrementally.
    //
    // (setActiveLayouts documents only its change gate, which is a different
    // question from this one.)
    m_lifecycleEngines.clear();
    ++m_announceGeneration;
    m_screensAnnouncePending = false;
    m_pendingIsDesktopSwitch = false;
    // Held behind the announce just voided, and naming the dead session's
    // windows and rects — a restart's first announce must not flush them.
    m_tileBatchesHeldForAnnounce.clear();
    m_unclaimedOpens.clear();
    m_pendingOpens.clear();
    m_moveReleasedInstances.clear();
    m_lastFloatBroadcast.clear();
    m_lastScrollTabColorsRelay.clear();
    m_lastEnabledBroadcast.reset();
    // Unlike m_activeLayouts, the strip cache MUST go: it names live windows
    // and their column rects, and replaying it to an effect that loads during
    // a daemon restart would paint pills for a layout no engine owns any more.
    m_lastScrollTabStrips.clear();
    m_scrollTabPaintOverrides.clear();
    // The WTA borrow is NOT cleared here — Daemon::stop's teardown block is
    // its canonical clear (setWindowTrackingAdaptor(nullptr)), and every
    // deref in this file null-checks.
    // m_pendingOpensListenerInstalled is left alone, and it does not matter
    // either way: the latch is per-instance and the daemon deletes and re-news
    // this adaptor on every init(), so the connection it tracks dies with the
    // object and the fresh one starts with the latch already false. There is
    // no duplicate-connection hazard to guard against here.
}

} // namespace PlasmaZones
