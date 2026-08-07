// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilingadaptor.h"

#include "core/platform/logging.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"

#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorProtocol/WindowMarshalling.h>
#include <PhosphorScreens/Manager.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
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
        // Teardown contract (header: "pass an empty list on teardown"):
        // parked opens can never be retried without a pipeline, and the
        // announce path's empty-union bail deliberately skips the retry, so
        // they would otherwise sit for the rest of the process.
        m_unclaimedOpens.clear();
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
    // desktop switches) where every engine self-guards on ownership, so a
    // mid-flip miss is a harmless no-op — whereas an open adopted by the
    // wrong engine would tile a window on a screen it does not own.
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

void TilingAdaptor::relayTileRequestsJson(const QString& tileRequestsJson)
{
    if (tileRequestsJson.isEmpty()) {
        return;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(tileRequestsJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        qCWarning(lcDbusTiling) << "relayTileRequestsJson: invalid JSON:" << parseError.errorString();
        return;
    }

    PhosphorProtocol::TileRequestList requests;
    QSet<QString> seenWindowIds;
    for (const QJsonValue& val : doc.array()) {
        QJsonObject obj = val.toObject();
        PhosphorProtocol::TileRequestEntry entry;
        entry.windowId = obj.value(QLatin1String("windowId")).toString();
        // One entry per window per batch: the effect applies entries in
        // order, so a duplicate would apply twice (last wins) and, with
        // scrollEdge now driving the animation anchor, two entries naming
        // different edges would animate the window in from one side and
        // re-anchor it to the other. No producer emits duplicates today;
        // this is boundary hardening, first-entry-wins.
        if (seenWindowIds.contains(entry.windowId)) {
            qCDebug(lcDbusTiling) << "relayTileRequestsJson: dropping duplicate entry for" << entry.windowId;
            continue;
        }
        entry.floating = obj.value(QLatin1String("floating")).toBool(false);
        if (!entry.floating) {
            entry.x = obj.value(QLatin1String("x")).toInt();
            entry.y = obj.value(QLatin1String("y")).toInt();
            entry.width = obj.value(QLatin1String("width")).toInt();
            entry.height = obj.value(QLatin1String("height")).toInt();
            if (entry.width <= 0 || entry.height <= 0) {
                qCDebug(lcDbusTiling) << "relayTileRequestsJson: invalid geometry for" << entry.windowId;
                continue;
            }
        }
        entry.zoneId = obj.value(QLatin1String("zoneId")).toString();
        entry.screenId = obj.value(QLatin1String("screenId")).toString();
        entry.monocle = obj.value(QLatin1String("monocle")).toBool(false);
        entry.stacking = obj.value(QLatin1String("stacking")).toString();
        entry.scrollEdge = obj.value(QLatin1String("scrollEdge")).toString();
        // Absent for every non-scrolling producer, and absent within scrolling
        // for a window the view does not carry — both mean zero, which is what
        // the default gives.
        entry.viewDeltaX = obj.value(QLatin1String("viewDeltaX")).toInt(0);
        // Present only for a parked scrolling column; absent means the
        // committed rect IS the paint position.
        //
        // BOTH keys, both numeric, and never on a floating entry. This is an
        // unmarshal boundary, so it validates rather than coerces: presence of
        // visualX alone would let visualY default to 0 and paint the column at
        // the top of the screen, a non-numeric value would do the same while
        // still latching the flag, and a floating entry skips the geometry
        // parse above so its committed rect is (0,0,0,0) — the effect computes
        // the paint translation against that rect, so a visual position paired
        // with it is meaningless. The engine emits none of these; the point is
        // that a garbled payload fails closed instead of mispainting.
        const QJsonValue visualXVal = obj.value(QLatin1String("visualX"));
        const QJsonValue visualYVal = obj.value(QLatin1String("visualY"));
        if (!entry.floating && visualXVal.isDouble() && visualYVal.isDouble()) {
            entry.visualX = visualXVal.toInt(0);
            entry.visualY = visualYVal.toInt(0);
            entry.hasVisualPos = true;
        } else if (!visualXVal.isUndefined() || !visualYVal.isUndefined()) {
            qCDebug(lcDbusTiling) << "relayTileRequestsJson: ignoring malformed visual position for" << entry.windowId;
        }
        // The protocol type ships its own validator (empty windowId /
        // screenId, degenerate rect) — run it rather than re-deriving a
        // subset of its checks here.
        if (const QString validationError = entry.validationError(); !validationError.isEmpty()) {
            qCDebug(lcDbusTiling) << "relayTileRequestsJson: dropping entry:" << validationError;
            continue;
        }
        seenWindowIds.insert(entry.windowId);
        requests.append(entry);
    }

    if (!requests.isEmpty()) {
        qCDebug(lcDbusTiling) << "Emitting windowsTileRequested:" << requests.size() << "windows";
        Q_EMIT windowsTileRequested(requests);
    }
}

void TilingAdaptor::relayWindowsReleased(const QStringList& windowIds)
{
    Q_EMIT windowsReleasedFromTiling(windowIds);
}

void TilingAdaptor::notifyEngineScreensChanged(bool isDesktopSwitch)
{
    // Coalesce (see header doc): a mode flip fires this once per engine in
    // one synchronous pass; emitting eagerly would broadcast an intermediate
    // union that momentarily drops the flipping screen and triggers the
    // effect's full restore path. Defer to the event loop so one emission
    // carries the pass's final state.
    m_pendingIsDesktopSwitch = m_pendingIsDesktopSwitch || isDesktopSwitch;
    if (m_screensAnnouncePending) {
        return;
    }
    m_screensAnnouncePending = true;
    QMetaObject::invokeMethod(
        this,
        [this, generation = m_announceGeneration]() {
            if (generation != m_announceGeneration) {
                return; // clearEngine voided this session's announce
            }
            m_screensAnnouncePending = false;
            const bool desktopSwitch = m_pendingIsDesktopSwitch;
            m_pendingIsDesktopSwitch = false;
            // A queued announce that fires after clearEngine() (shutdown)
            // must NOT broadcast an empty union — the effect would treat it
            // as a genuine disable and run its destructive per-window
            // teardown against a daemon that is merely restarting.
            if (m_lifecycleEngines.isEmpty()) {
                return;
            }
            Q_EMIT managedScreensChanged(combinedManagedScreens(), desktopSwitch);
            relayEnabledChanged();
            // Retry opens parked during the flip (see m_unclaimedOpens):
            // engines have their post-flip screen sets by now. Insertion
            // order is preserved (column order / master assignment depend
            // on it), routing is NOT re-run (already baked into the parked
            // entry), and a still-unclaimed entry is dropped rather than
            // re-parked.
            if (!m_unclaimedOpens.isEmpty()) {
                const auto parked = std::exchange(m_unclaimedOpens, {});
                // Burst bracket, same as windowsOpenedBatch: a flip can park a
                // whole screen's opens, and replaying them per-arrival would
                // march the strip through the partial intermediates the
                // bracket exists to suppress.
                for (PhosphorEngine::IPlacementEngine* engine : m_lifecycleEngines) {
                    engine->beginArrivalBurst();
                }
                for (const auto& entry : parked) {
                    dispatchOpenToClaimingEngine(entry, /*allowPark=*/false);
                }
                for (PhosphorEngine::IPlacementEngine* engine : m_lifecycleEngines) {
                    engine->endArrivalBurst();
                }
            }
        },
        Qt::QueuedConnection);
}

void TilingAdaptor::relayEnabledChanged()
{
    // Dedup: two engines feed one signal, and every screen-set change on
    // either would otherwise re-broadcast an unchanged bool.
    const bool now = enabled();
    if (m_lastEnabledBroadcast.has_value() && *m_lastEnabledBroadcast == now) {
        return;
    }
    m_lastEnabledBroadcast = now;
    Q_EMIT enabledChanged(now);
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
    if (m_windowTrackingAdaptor) {
        const QString routed = m_windowTrackingAdaptor->applyOpenRoutingForTiling(entry.windowId, entry.screenId);
        if (!routed.isEmpty()) {
            routedEntry.screenId = routed;
        }
    }
    dispatchOpenToClaimingEngine(routedEntry, /*allowPark=*/true);
}

void TilingAdaptor::dispatchOpenToClaimingEngine(const PhosphorProtocol::WindowOpenedEntry& entry, bool allowPark)
{
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
        m_unclaimedOpens.append(entry);
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
        if (m_unclaimedOpens.at(i).windowId == windowId) {
            m_unclaimedOpens.removeAt(i);
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
    for (const auto& entry : toFlush) {
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
    for (const auto& entry : entries) {
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
    if (m_windowTrackingAdaptor) {
        m_lastFloatBroadcast.remove(m_windowTrackingAdaptor->shadowWindowId(windowId));
    }
    removeUnclaimedOpen(windowId);
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
    removeUnclaimedOpen(windowId);
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
    if (m_windowTrackingAdaptor) {
        m_lastFloatBroadcast.remove(m_windowTrackingAdaptor->shadowWindowId(windowId));
    }
    removeUnclaimedOpen(windowId);
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
    // every per-session queue/dedup cache — none of it may leak into a
    // restart (a stale dedup value could suppress the first genuine
    // broadcast of the new session).
    m_lifecycleEngines.clear();
    ++m_announceGeneration;
    m_screensAnnouncePending = false;
    m_pendingIsDesktopSwitch = false;
    m_unclaimedOpens.clear();
    m_pendingOpens.clear();
    m_lastFloatBroadcast.clear();
    m_lastEnabledBroadcast.reset();
    // The WTA borrow is NOT cleared here — Daemon::stop's teardown block is
    // its canonical clear (setWindowTrackingAdaptor(nullptr)), and every
    // deref in this file null-checks.
    // m_pendingOpensListenerInstalled deliberately survives: the underlying
    // connection object does too (sender and receiver both outlive a
    // session restart), so resetting the latch here would make the next
    // install a DUPLICATE connection, double-flushing the pending queue.
}

} // namespace PlasmaZones
