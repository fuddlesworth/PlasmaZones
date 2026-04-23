// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "autotileadaptor.h"

#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include "autotile/AutotileEngine.h"
#include <PhosphorTiles/TilingAlgorithm.h>

#include "core/logging.h"
#include <PhosphorScreens/Manager.h>

#include <PhosphorProtocol/WireTypes.h>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace PlasmaZones {

AutotileAdaptor::AutotileAdaptor(AutotileEngine* engine, Phosphor::Screens::ScreenManager* screenManager,
                                 PhosphorTiles::ITileAlgorithmRegistry* algorithmRegistry, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_engine(engine)
    , m_screenManager(screenManager)
    , m_algorithmRegistry(algorithmRegistry)
{
    Q_ASSERT_X(m_algorithmRegistry, "AutotileAdaptor",
               "null ITileAlgorithmRegistry — setAlgorithm / availableAlgorithms / algorithmInfo will crash");
    // Note: We use manual signal connections (below) instead of setAutoRelaySignals(true)
    // to avoid duplicate D-Bus signal emissions when engine signals are forwarded.

    if (!m_engine) {
        qCWarning(lcDbusAutotile) << "AutotileAdaptor created with null engine";
        return;
    }

    // Connect engine signals to D-Bus signals
    connect(m_engine, &AutotileEngine::enabledChanged, this, &AutotileAdaptor::enabledChanged);
    connect(m_engine, &AutotileEngine::autotileScreensChanged, this, &AutotileAdaptor::autotileScreensChanged);
    connect(m_engine, &AutotileEngine::algorithmChanged, this, &AutotileAdaptor::algorithmChanged);
    connect(m_engine, &AutotileEngine::tilingChanged, this, &AutotileAdaptor::tilingChanged);
    connect(m_engine, &AutotileEngine::windowsTiled, this, &AutotileAdaptor::onWindowsTiled);
    connect(m_engine, &PhosphorEngineApi::PlacementEngineBase::activateWindowRequested, this,
            &AutotileAdaptor::focusWindowRequested);
    // The in-process engine signal has a 2nd QSet<QString> argument for
    // daemon-side bookkeeping; strip it before forwarding over D-Bus since
    // QSet is not a D-Bus-marshallable type.
    connect(m_engine, &AutotileEngine::windowsReleasedFromTiling, this,
            [this](const QStringList& windowIds, const QSet<QString>& /*releasedScreenIds*/) {
                Q_EMIT windowsReleasedFromTiling(windowIds);
            });
    connect(m_engine, &PhosphorEngineApi::PlacementEngineBase::windowFloatingChanged, this,
            &AutotileAdaptor::windowFloatingChanged);
    qCDebug(lcDbusAutotile) << "AutotileAdaptor initialized";
}

// ═══════════════════════════════════════════════════════════════════════════
// Property Accessors
// ═══════════════════════════════════════════════════════════════════════════

bool AutotileAdaptor::enabled() const
{
    if (!m_engine) {
        return false;
    }
    return m_engine->isEnabled();
}

QStringList AutotileAdaptor::autotileScreens() const
{
    if (!m_engine) {
        return {};
    }
    const auto& screens = m_engine->autotileScreens();
    return QStringList(screens.begin(), screens.end());
}

QString AutotileAdaptor::algorithm() const
{
    if (!m_engine) {
        return QString();
    }
    return m_engine->algorithm();
}

void AutotileAdaptor::setAlgorithm(const QString& algorithmId)
{
    if (!ensureEngine("setAlgorithm")) {
        return;
    }
    if (!m_algorithmRegistry->algorithm(algorithmId)) {
        qCWarning(lcDbusAutotile) << "setAlgorithm: unknown algorithm ID:" << algorithmId;
        return;
    }
    const QString oldAlgorithm = m_engine->algorithm();
    m_engine->setAlgorithm(algorithmId);
    if (m_engine->algorithm() != oldAlgorithm) {
        Q_EMIT configChanged();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Tiling Operations
// ═══════════════════════════════════════════════════════════════════════════

void AutotileAdaptor::retile(const QString& screenId)
{
    if (!ensureEngine("retile")) {
        return;
    }
    qCDebug(lcDbusAutotile) << "retile: screen=" << (screenId.isEmpty() ? QStringLiteral("all") : screenId);
    m_engine->retile(screenId);
}

void AutotileAdaptor::retileAllScreens()
{
    retile(QString());
}

void AutotileAdaptor::swapWindows(const QString& windowId1, const QString& windowId2)
{
    if (!ensureEngine("swapWindows")) {
        return;
    }
    if (windowId1.isEmpty() || windowId2.isEmpty()) {
        qCWarning(lcDbusAutotile) << "swapWindows: empty window ID(s)";
        return;
    }
    // Early return for same window (no-op, but worth logging)
    if (windowId1 == windowId2) {
        qCDebug(lcDbusAutotile) << "swapWindows: same window ID, no-op";
        return;
    }
    qCDebug(lcDbusAutotile) << "swapWindows: windowId1=" << windowId1 << "windowId2=" << windowId2;
    m_engine->swapWindows(windowId1, windowId2);
}

void AutotileAdaptor::promoteToMaster(const QString& windowId)
{
    if (!ensureEngine("promoteToMaster")) {
        return;
    }
    if (windowId.isEmpty()) {
        qCWarning(lcDbusAutotile) << "promoteToMaster: empty window ID";
        return;
    }
    qCDebug(lcDbusAutotile) << "promoteToMaster: windowId=" << windowId;
    m_engine->promoteToMaster(windowId);
}

void AutotileAdaptor::demoteFromMaster(const QString& windowId)
{
    if (!ensureEngine("demoteFromMaster")) {
        return;
    }
    if (windowId.isEmpty()) {
        qCWarning(lcDbusAutotile) << "demoteFromMaster: empty window ID";
        return;
    }
    qCDebug(lcDbusAutotile) << "demoteFromMaster: windowId=" << windowId;
    m_engine->demoteFromMaster(windowId);
}

// ═══════════════════════════════════════════════════════════════════════════
// Focus Operations
// ═══════════════════════════════════════════════════════════════════════════

void AutotileAdaptor::focusMaster()
{
    if (!ensureEngine("focusMaster")) {
        return;
    }
    qCDebug(lcDbusAutotile) << "focusMaster";
    m_engine->focusMaster();
}

void AutotileAdaptor::focusNext()
{
    if (!ensureEngine("focusNext")) {
        return;
    }
    qCDebug(lcDbusAutotile) << "focusNext";
    m_engine->focusNext();
}

void AutotileAdaptor::focusPrevious()
{
    if (!ensureEngine("focusPrevious")) {
        return;
    }
    qCDebug(lcDbusAutotile) << "focusPrevious";
    m_engine->focusPrevious();
}

int AutotileAdaptor::pendingWindowOpensCount() const
{
    return m_pendingOpens.size();
}

void AutotileAdaptor::dispatchWindowOpened(const WindowOpenedEntry& entry)
{
    if (entry.windowId.isEmpty() || entry.screenId.isEmpty()) {
        return;
    }
    m_engine->windowOpened(entry.windowId, entry.screenId, qMax(0, entry.minWidth), qMax(0, entry.minHeight));
}

bool AutotileAdaptor::deferUntilPanelReady()
{
    // Fast path: panel geometry already known, or no Phosphor::Screens::ScreenManager at all (tests
    // without a singleton fall through and proceed with whatever geometry exists).
    if (!m_screenManager || (m_screenManager && m_screenManager->isPanelGeometryReady())) {
        return false;
    }

    // Lazily wire the flush slot on first deferral. AutoConnection resolves to a
    // direct call when the signal fires from our thread (production: the D-Bus
    // watcher's finished callback runs on the main thread, same as us), so there
    // is no posted-event reentrancy. Leaving the connection installed for the
    // session is fine — panelGeometryReady is a one-shot signal (see
    // Phosphor::Screens::ScreenManager::queryKdePlasmaPanels).
    if (!m_pendingOpensListenerInstalled) {
        connect(m_screenManager, &Phosphor::Screens::ScreenManager::panelGeometryReady, this,
                &AutotileAdaptor::flushPendingWindowOpens);
        m_pendingOpensListenerInstalled = true;
    }
    return true;
}

void AutotileAdaptor::flushPendingWindowOpens()
{
    if (m_pendingOpens.isEmpty()) {
        return;
    }
    if (!ensureEngine("flushPendingWindowOpens")) {
        m_pendingOpens.clear();
        return;
    }
    // Move-then-clear so any re-entrant dispatchWindowOpened → slot callback → new
    // deferral (unlikely post-ready, but defensive) queues into a fresh list rather
    // than mutating the one we're iterating.
    const WindowOpenedList toFlush = std::move(m_pendingOpens);
    m_pendingOpens.clear();
    qCInfo(lcDbusAutotile) << "flushPendingWindowOpens: processing" << toFlush.size()
                           << "deferred windows after panel geometry became ready";
    for (const auto& entry : toFlush) {
        dispatchWindowOpened(entry);
    }
}

void AutotileAdaptor::windowOpened(const QString& windowId, const QString& screenId, int minWidth, int minHeight)
{
    if (!ensureEngine("windowOpened")) {
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
    // against the unreserved full-screen rect (Phosphor::Screens::ScreenManager's availability cache
    // is empty until the sensor windows and Plasma D-Bus panel query finish), and
    // the daemon would emit a visible correction a frame later. Flushing happens in
    // flushPendingWindowOpens() when panelGeometryReady fires.
    WindowOpenedEntry entry{windowId, screenId, minWidth, minHeight};
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

void AutotileAdaptor::windowsOpenedBatch(const WindowOpenedList& entries)
{
    if (!ensureEngine("windowsOpenedBatch")) {
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

void AutotileAdaptor::windowMinSizeUpdated(const QString& windowId, int minWidth, int minHeight)
{
    if (!ensureEngine("windowMinSizeUpdated")) {
        return;
    }
    if (windowId.isEmpty()) {
        qCDebug(lcDbusAutotile) << "windowMinSizeUpdated: empty window ID";
        return;
    }
    qCDebug(lcDbusAutotile) << "windowMinSizeUpdated: windowId=" << windowId << "minSize=" << minWidth << "x"
                            << minHeight;
    m_engine->windowMinSizeUpdated(windowId, qMax(0, minWidth), qMax(0, minHeight));
}

void AutotileAdaptor::windowClosed(const QString& windowId)
{
    if (!ensureEngine("windowClosed")) {
        return;
    }
    if (windowId.isEmpty()) {
        qCDebug(lcDbusAutotile) << "windowClosed: empty window ID";
        return;
    }
    qCDebug(lcDbusAutotile) << "windowClosed: windowId=" << windowId;
    m_engine->windowClosed(windowId);
}

void AutotileAdaptor::notifyWindowFocused(const QString& windowId, const QString& screenId)
{
    if (!ensureEngine("notifyWindowFocused")) {
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
    m_engine->windowFocused(windowId, screenId);
}

// floatWindow, unfloatWindow, toggleFocusedWindowFloat, toggleWindowFloat removed:
// all float operations are now routed through the unified WTA methods
// (toggleFloatForWindow for toggle, setWindowFloatingForScreen for directional).

// ═══════════════════════════════════════════════════════════════════════════
// Algorithm Query
// ═══════════════════════════════════════════════════════════════════════════

QStringList AutotileAdaptor::availableAlgorithms()
{
    return m_algorithmRegistry->availableAlgorithms();
}

AlgorithmInfoEntry AutotileAdaptor::algorithmInfo(const QString& algorithmId)
{
    PhosphorTiles::TilingAlgorithm* algo = m_algorithmRegistry->algorithm(algorithmId);
    if (!algo) {
        qCWarning(lcDbusAutotile) << "Unknown algorithm:" << algorithmId;
        return {};
    }

    AlgorithmInfoEntry entry;
    entry.id = algorithmId; // Validated by successful lookup above
    entry.name = algo->name();
    entry.description = algo->description();
    entry.supportsMasterCount = algo->supportsMasterCount();
    entry.supportsSplitRatio = algo->supportsSplitRatio();
    entry.centerLayout = algo->centerLayout();
    entry.producesOverlappingZones = algo->producesOverlappingZones();
    entry.defaultSplitRatio = algo->defaultSplitRatio();
    entry.defaultMaxWindows = algo->defaultMaxWindows();
    entry.isScripted = algo->isScripted();
    entry.zoneNumberDisplay = algo->zoneNumberDisplay();
    entry.isUserScript = algo->isUserScript();
    entry.supportsMemory = algo->supportsMemory();

    return entry;
}

// ═══════════════════════════════════════════════════════════════════════════
// Private Slots
// ═══════════════════════════════════════════════════════════════════════════

void AutotileAdaptor::onWindowsTiled(const QString& tileRequestsJson)
{
    if (tileRequestsJson.isEmpty()) {
        return;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(tileRequestsJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        qCWarning(lcDbusAutotile) << "onWindowsTiled: invalid JSON:" << parseError.errorString();
        return;
    }

    TileRequestList requests;
    for (const QJsonValue& val : doc.array()) {
        QJsonObject obj = val.toObject();
        TileRequestEntry entry;
        entry.windowId = obj.value(QLatin1String("windowId")).toString();
        entry.floating = obj.value(QLatin1String("floating")).toBool(false);
        if (!entry.floating) {
            entry.x = obj.value(QLatin1String("x")).toInt();
            entry.y = obj.value(QLatin1String("y")).toInt();
            entry.width = obj.value(QLatin1String("width")).toInt();
            entry.height = obj.value(QLatin1String("height")).toInt();
            if (entry.width <= 0 || entry.height <= 0) {
                qCDebug(lcDbusAutotile) << "onWindowsTiled: invalid geometry for" << entry.windowId;
                continue;
            }
        }
        entry.zoneId = obj.value(QLatin1String("zoneId")).toString();
        entry.screenId = obj.value(QLatin1String("screenId")).toString();
        entry.monocle = obj.value(QLatin1String("monocle")).toBool(false);
        requests.append(entry);
    }

    if (!requests.isEmpty()) {
        qCDebug(lcDbusAutotile) << "Emitting windowsTileRequested:" << requests.size() << "windows";
        Q_EMIT windowsTileRequested(requests);
    }
}

void AutotileAdaptor::clearEngine()
{
    if (m_engine) {
        // Disconnect all 8 engine→this signal connections established in constructor.
        disconnect(m_engine, nullptr, this, nullptr);
        m_engine = nullptr;
    }
}

} // namespace PlasmaZones
