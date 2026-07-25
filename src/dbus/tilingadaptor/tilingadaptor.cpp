// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilingadaptor.h"

#include "core/platform/logging.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"

#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorProtocol/WindowMarshalling.h>
#include <PhosphorScreens/Manager.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace PlasmaZones {

TilingAdaptor::TilingAdaptor(PhosphorScreens::ScreenManager* screenManager, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_screenManager(screenManager)
{
    // Engine-agnostic by construction: the adaptor holds no engine until the
    // composition root supplies the pipeline list via setLifecycleEngines and
    // wires each engine's outbound signals to the relay entry points.
    qCDebug(lcDbusAutotile) << "TilingAdaptor initialized";
}

void TilingAdaptor::setLifecycleEngines(const QVector<PhosphorEngine::IPlacementEngine*>& engines)
{
    // Interface-only borrows: the adaptor dispatches inbound lifecycle
    // calls through the list but makes no signal connections (the
    // composition root wires each engine's outbound signals to the relay
    // entry points).
    m_lifecycleEngines = engines;
    m_lifecycleEngines.removeAll(nullptr);
}

bool TilingAdaptor::ensurePipeline(const char* methodName) const
{
    if (m_lifecycleEngines.isEmpty()) {
        qCWarning(lcDbusAutotile) << "Cannot" << methodName << "- no pipeline engines available";
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
        qCWarning(lcDbusAutotile) << "relayTileRequestsJson: invalid JSON:" << parseError.errorString();
        return;
    }

    PhosphorProtocol::TileRequestList requests;
    for (const QJsonValue& val : doc.array()) {
        QJsonObject obj = val.toObject();
        PhosphorProtocol::TileRequestEntry entry;
        entry.windowId = obj.value(QLatin1String("windowId")).toString();
        entry.floating = obj.value(QLatin1String("floating")).toBool(false);
        if (!entry.floating) {
            entry.x = obj.value(QLatin1String("x")).toInt();
            entry.y = obj.value(QLatin1String("y")).toInt();
            entry.width = obj.value(QLatin1String("width")).toInt();
            entry.height = obj.value(QLatin1String("height")).toInt();
            if (entry.width <= 0 || entry.height <= 0) {
                qCDebug(lcDbusAutotile) << "relayTileRequestsJson: invalid geometry for" << entry.windowId;
                continue;
            }
        }
        entry.zoneId = obj.value(QLatin1String("zoneId")).toString();
        entry.screenId = obj.value(QLatin1String("screenId")).toString();
        entry.monocle = obj.value(QLatin1String("monocle")).toBool(false);
        entry.stacking = obj.value(QLatin1String("stacking")).toString();
        requests.append(entry);
    }

    if (!requests.isEmpty()) {
        qCDebug(lcDbusAutotile) << "Emitting windowsTileRequested:" << requests.size() << "windows";
        Q_EMIT windowsTileRequested(requests);
    }
}

void TilingAdaptor::relayWindowsReleased(const QStringList& windowIds)
{
    Q_EMIT windowsReleasedFromTiling(windowIds);
}

void TilingAdaptor::notifyEngineScreensChanged(bool isDesktopSwitch)
{
    // Screen set and enabled state flip together on the emitting engine;
    // re-announce both (the effect's property reads dedup a no-op).
    Q_EMIT managedScreensChanged(combinedManagedScreens(), isDesktopSwitch);
    Q_EMIT enabledChanged(enabled());
}

void TilingAdaptor::relayEnabledChanged()
{
    Q_EMIT enabledChanged(enabled());
}

QStringList TilingAdaptor::combinedManagedScreens() const
{
    QSet<QString> all;
    for (const PhosphorEngine::IPlacementEngine* engine : m_lifecycleEngines) {
        all += engine->activeScreens();
    }
    return QStringList(all.cbegin(), all.cend());
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
    qCDebug(lcDbusAutotile) << "retile: screen=" << (screenId.isEmpty() ? QStringLiteral("all") : screenId);
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
    QString screenId = entry.screenId;
    if (m_windowTrackingAdaptor) {
        const QString routed = m_windowTrackingAdaptor->applyOpenRoutingForTiling(entry.windowId, entry.screenId);
        if (!routed.isEmpty()) {
            screenId = routed;
        }
    }
    // Per-screen engine dispatch: the effect reports opens for every
    // engine-managed screen through this interface, so hand the window to
    // whichever pipeline engine owns the (possibly rule-routed) screen.
    if (PhosphorEngine::IPlacementEngine* engine = engineOwningScreen(screenId)) {
        engine->windowOpened(entry.windowId, screenId, qMax(0, entry.minWidth), qMax(0, entry.minHeight));
    }
}

bool TilingAdaptor::deferUntilPanelReady()
{
    // Fast path: panel geometry already known, or no PhosphorScreens::ScreenManager at all (tests
    // without a singleton fall through and proceed with whatever geometry exists).
    if (!m_screenManager || (m_screenManager && m_screenManager->isPanelGeometryReady())) {
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
    qCInfo(lcDbusAutotile) << "flushPendingWindowOpens: processing" << toFlush.size()
                           << "deferred windows after panel geometry became ready";
    for (const auto& entry : toFlush) {
        dispatchWindowOpened(entry);
    }
}

void TilingAdaptor::windowOpened(const QString& windowId, const QString& screenId, int minWidth, int minHeight)
{
    if (!ensurePipeline("windowOpened")) {
        return;
    }
    if (windowId.isEmpty()) {
        qCDebug(lcDbusAutotile) << "windowOpened: empty window ID";
        return;
    }
    if (screenId.isEmpty()) {
        qCDebug(lcDbusAutotile) << "windowOpened: empty screen ID for window" << windowId;
        return;
    }
    // Non-blocking startup gate: if the first panel D-Bus query has not completed
    // yet, queue this entry and return. Processing immediately would compute zones
    // against the unreserved full-screen rect (PhosphorScreens::ScreenManager's availability cache
    // is empty until the sensor windows and Plasma D-Bus panel query finish), and
    // the daemon would emit a visible correction a frame later. Flushing happens in
    // flushPendingWindowOpens() when panelGeometryReady fires.
    PhosphorProtocol::WindowOpenedEntry entry{windowId, screenId, minWidth, minHeight};
    if (deferUntilPanelReady()) {
        qCInfo(lcDbusAutotile) << "windowOpened: deferring" << windowId
                               << "until panel geometry ready (queue size=" << (m_pendingOpens.size() + 1) << ")";
        m_pendingOpens.append(entry);
        return;
    }
    qCDebug(lcDbusAutotile) << "windowOpened: windowId=" << windowId << "screen=" << screenId << "minSize=" << minWidth
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
    if (deferUntilPanelReady()) {
        qCInfo(lcDbusAutotile) << "windowsOpenedBatch: deferring" << entries.size()
                               << "windows until panel geometry ready";
        m_pendingOpens.append(entries);
        return;
    }

    qCInfo(lcDbusAutotile) << "windowsOpenedBatch: processing" << entries.size() << "windows";

    for (const auto& entry : entries) {
        dispatchWindowOpened(entry);
    }
}

void TilingAdaptor::windowMinSizeUpdated(const QString& windowId, int minWidth, int minHeight)
{
    if (!ensurePipeline("windowMinSizeUpdated")) {
        return;
    }
    if (windowId.isEmpty()) {
        qCDebug(lcDbusAutotile) << "windowMinSizeUpdated: empty window ID";
        return;
    }
    qCDebug(lcDbusAutotile) << "windowMinSizeUpdated: windowId=" << windowId << "minSize=" << minWidth << "x"
                            << minHeight;
    if (PhosphorEngine::IPlacementEngine* engine = engineOwningWindow(windowId)) {
        engine->windowMinSizeUpdated(windowId, qMax(0, minWidth), qMax(0, minHeight));
    }
}

void TilingAdaptor::windowClosed(const QString& windowId)
{
    if (!ensurePipeline("windowClosed")) {
        return;
    }
    if (windowId.isEmpty()) {
        qCDebug(lcDbusAutotile) << "windowClosed: empty window ID";
        return;
    }
    qCDebug(lcDbusAutotile) << "windowClosed: windowId=" << windowId;
    m_lastFloatBroadcast.remove(windowId);
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
        qCDebug(lcDbusAutotile) << "notifyWindowFocused: empty window ID (focus cleared)";
        return;
    }
    if (screenId.isEmpty()) {
        qCDebug(lcDbusAutotile) << "notifyWindowFocused: empty screenId";
        return;
    }
    qCDebug(lcDbusAutotile) << "notifyWindowFocused: windowId=" << windowId << "screen=" << screenId;
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
    // Interface-only borrows, no connections to drop.
    m_lifecycleEngines.clear();
}

} // namespace PlasmaZones
