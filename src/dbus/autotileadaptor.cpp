// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "autotileadaptor.h"

#include "autotile/AlgorithmRegistry.h"
#include "autotile/AutotileConfig.h"
#include "autotile/AutotileEngine.h"
#include "autotile/TilingAlgorithm.h"
#include "core/constants.h"

#include "core/logging.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════
// Helper Methods
// ═══════════════════════════════════════════════════════════════════════════

bool AutotileAdaptor::ensureEngine(const char* methodName) const
{
    if (!m_engine) {
        qCWarning(lcDbusAutotile) << "Cannot" << methodName << "- engine not available";
        return false;
    }
    return true;
}

bool AutotileAdaptor::ensureEngineAndConfig(const char* methodName) const
{
    if (!m_engine) {
        qCWarning(lcDbusAutotile) << "Cannot" << methodName << "- engine not available";
        return false;
    }
    if (!m_engine->config()) {
        qCWarning(lcDbusAutotile) << "Cannot" << methodName << "- config not available";
        return false;
    }
    return true;
}

AutotileAdaptor::AutotileAdaptor(AutotileEngine* engine, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_engine(engine)
{
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
    connect(m_engine, &AutotileEngine::focusWindowRequested, this, &AutotileAdaptor::focusWindowRequested);
    connect(m_engine, &AutotileEngine::monocleVisibilityChanged, this, &AutotileAdaptor::monocleVisibilityChanged);
    connect(m_engine, &AutotileEngine::windowsReleasedFromTiling, this, &AutotileAdaptor::windowsReleasedFromTiling);

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
    if (!AlgorithmRegistry::instance()->algorithm(algorithmId)) {
        qCWarning(lcDbusAutotile) << "setAlgorithm: unknown algorithm ID:" << algorithmId;
        return;
    }
    const QString oldAlgorithm = m_engine->algorithm();
    m_engine->setAlgorithm(algorithmId);
    if (m_engine->algorithm() != oldAlgorithm) {
        Q_EMIT configChanged();
    }
}

double AutotileAdaptor::masterRatio() const
{
    if (!m_engine || !m_engine->config()) {
        return AutotileDefaults::DefaultSplitRatio;
    }
    return m_engine->config()->splitRatio;
}

// NOTE: D-Bus property changes (masterRatio, masterCount, etc.) update runtime state
// only. They are NOT written back to KConfig/Settings. Per-screen state (including
// splitRatio) is persisted separately via TilingState save/load on daemon shutdown.
void AutotileAdaptor::setMasterRatio(double ratio)
{
    if (!ensureEngineAndConfig("setMasterRatio")) {
        return;
    }
    ratio = qBound(AutotileDefaults::MinSplitRatio, ratio, AutotileDefaults::MaxSplitRatio);
    if (!qFuzzyCompare(m_engine->config()->splitRatio, ratio)) {
        // Update config AND all per-screen TilingState objects (which algorithms use)
        m_engine->setGlobalSplitRatio(ratio);
        Q_EMIT configChanged();
    }
}

int AutotileAdaptor::masterCount() const
{
    if (!m_engine || !m_engine->config()) {
        return AutotileDefaults::DefaultMasterCount;
    }
    return m_engine->config()->masterCount;
}

void AutotileAdaptor::setMasterCount(int count)
{
    if (!ensureEngineAndConfig("setMasterCount")) {
        return;
    }
    count = qBound(AutotileDefaults::MinMasterCount, count, AutotileDefaults::MaxMasterCount);
    if (m_engine->config()->masterCount != count) {
        // Update config AND all per-screen TilingState objects (which algorithms use)
        m_engine->setGlobalMasterCount(count);
        Q_EMIT configChanged();
    }
}

int AutotileAdaptor::innerGap() const
{
    if (!m_engine || !m_engine->config()) {
        return AutotileDefaults::DefaultGap;
    }
    return m_engine->config()->innerGap;
}

void AutotileAdaptor::setInnerGap(int gap)
{
    if (!ensureEngineAndConfig("setInnerGap")) {
        return;
    }
    const int oldGap = m_engine->config()->innerGap;
    m_engine->setInnerGap(gap);
    if (m_engine->config()->innerGap != oldGap) {
        Q_EMIT configChanged();
    }
}

int AutotileAdaptor::outerGap() const
{
    if (!m_engine || !m_engine->config()) {
        return AutotileDefaults::DefaultGap;
    }
    return m_engine->config()->outerGap;
}

void AutotileAdaptor::setOuterGap(int gap)
{
    if (!ensureEngineAndConfig("setOuterGap")) {
        return;
    }
    const int oldGap = m_engine->config()->outerGap;
    m_engine->setOuterGap(gap);
    if (m_engine->config()->outerGap != oldGap) {
        Q_EMIT configChanged();
    }
}

bool AutotileAdaptor::smartGaps() const
{
    if (!m_engine || !m_engine->config()) {
        return AutotileDefaults::DefaultSmartGaps;
    }
    return m_engine->config()->smartGaps;
}

void AutotileAdaptor::setSmartGaps(bool enabled)
{
    if (!ensureEngineAndConfig("setSmartGaps")) {
        return;
    }
    const bool oldSmartGaps = m_engine->config()->smartGaps;
    m_engine->setSmartGaps(enabled);
    if (m_engine->config()->smartGaps != oldSmartGaps) {
        Q_EMIT configChanged();
    }
}

bool AutotileAdaptor::focusNewWindows() const
{
    if (!m_engine || !m_engine->config()) {
        return AutotileDefaults::DefaultFocusNewWindows;
    }
    return m_engine->config()->focusNewWindows;
}

void AutotileAdaptor::setFocusNewWindows(bool enabled)
{
    if (!ensureEngineAndConfig("setFocusNewWindows")) {
        return;
    }
    const bool oldFocusNewWindows = m_engine->config()->focusNewWindows;
    m_engine->setFocusNewWindows(enabled);
    if (m_engine->config()->focusNewWindows != oldFocusNewWindows) {
        Q_EMIT configChanged();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Tiling Operations
// ═══════════════════════════════════════════════════════════════════════════

void AutotileAdaptor::retile(const QString& screenName)
{
    if (!ensureEngine("retile")) {
        return;
    }
    qCDebug(lcDbusAutotile) << "D-Bus retile request for screen:"
                            << (screenName.isEmpty() ? QStringLiteral("all") : screenName);
    m_engine->retile(screenName);
}

void AutotileAdaptor::swapWindows(const QString& windowId1, const QString& windowId2)
{
    if (!ensureEngine("swapWindows")) {
        return;
    }
    if (windowId1.isEmpty() || windowId2.isEmpty()) {
        qCWarning(lcDbusAutotile) << "Cannot swapWindows - empty window ID(s)";
        return;
    }
    // Early return for same window (no-op, but worth logging)
    if (windowId1 == windowId2) {
        qCDebug(lcDbusAutotile) << "swapWindows called with same window ID - no-op";
        return;
    }
    qCDebug(lcDbusAutotile) << "D-Bus swap request:" << windowId1 << "<->" << windowId2;
    m_engine->swapWindows(windowId1, windowId2);
}

void AutotileAdaptor::promoteToMaster(const QString& windowId)
{
    if (!ensureEngine("promoteToMaster")) {
        return;
    }
    if (windowId.isEmpty()) {
        qCWarning(lcDbusAutotile) << "Cannot promoteToMaster - empty window ID";
        return;
    }
    qCDebug(lcDbusAutotile) << "D-Bus promote request:" << windowId;
    m_engine->promoteToMaster(windowId);
}

void AutotileAdaptor::demoteFromMaster(const QString& windowId)
{
    if (!ensureEngine("demoteFromMaster")) {
        return;
    }
    if (windowId.isEmpty()) {
        qCWarning(lcDbusAutotile) << "Cannot demoteFromMaster - empty window ID";
        return;
    }
    qCDebug(lcDbusAutotile) << "D-Bus demote request:" << windowId;
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
    qCDebug(lcDbusAutotile) << "D-Bus focusMaster request";
    m_engine->focusMaster();
}

void AutotileAdaptor::focusNext()
{
    if (!ensureEngine("focusNext")) {
        return;
    }
    qCDebug(lcDbusAutotile) << "D-Bus focusNext request";
    m_engine->focusNext();
}

void AutotileAdaptor::focusPrevious()
{
    if (!ensureEngine("focusPrevious")) {
        return;
    }
    qCDebug(lcDbusAutotile) << "D-Bus focusPrevious request";
    m_engine->focusPrevious();
}

void AutotileAdaptor::windowOpened(const QString& windowId, const QString& screenName, int minWidth, int minHeight)
{
    if (!ensureEngine("windowOpened")) {
        return;
    }
    if (windowId.isEmpty()) {
        qCDebug(lcDbusAutotile) << "windowOpened: empty window ID";
        return;
    }
    if (screenName.isEmpty()) {
        qCDebug(lcDbusAutotile) << "windowOpened: empty screen name for window" << windowId;
        return;
    }
    qCDebug(lcDbusAutotile) << "D-Bus windowOpened:" << windowId << "on screen:" << screenName << "minSize:" << minWidth
                            << "x" << minHeight;
    m_engine->windowOpened(windowId, screenName, minWidth, minHeight);
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
    qCDebug(lcDbusAutotile) << "D-Bus windowClosed:" << windowId;
    m_engine->windowClosed(windowId);
}

void AutotileAdaptor::notifyWindowFocused(const QString& windowId, const QString& screenName)
{
    if (!ensureEngine("notifyWindowFocused")) {
        return;
    }
    if (windowId.isEmpty()) {
        qCDebug(lcDbusAutotile) << "notifyWindowFocused: empty window ID (focus cleared)";
        return;
    }
    qCDebug(lcDbusAutotile) << "D-Bus notifyWindowFocused:" << windowId << "screen:" << screenName;
    // R2 fix: Pass screen name to engine so m_windowToScreen is updated on focus
    // change. This also addresses R5 (cross-screen window movement detection) since
    // focus events carry the current screen, updating stale m_windowToScreen entries.
    m_engine->windowFocused(windowId, screenName);
}

void AutotileAdaptor::floatWindow(const QString& windowId)
{
    if (!ensureEngine("floatWindow")) {
        return;
    }
    if (windowId.isEmpty()) {
        qCWarning(lcDbusAutotile) << "Cannot floatWindow - empty window ID";
        return;
    }
    qCDebug(lcDbusAutotile) << "D-Bus floatWindow:" << windowId;
    m_engine->floatWindow(windowId);
}

// ═══════════════════════════════════════════════════════════════════════════
// Ratio/Count Adjustment
// ═══════════════════════════════════════════════════════════════════════════

void AutotileAdaptor::increaseMasterRatio(double delta)
{
    if (!ensureEngine("increaseMasterRatio")) {
        return;
    }
    // Validate delta is positive and reasonable
    if (delta <= 0.0 || delta > 1.0) {
        qCWarning(lcDbusAutotile) << "increaseMasterRatio: invalid delta" << delta << "(must be > 0 and <= 1.0)";
        return;
    }
    qCDebug(lcDbusAutotile) << "D-Bus increaseMasterRatio:" << delta;
    // Note: This modifies per-screen TilingState, not global config.
    // tilingChanged signal is emitted by engine after retile.
    m_engine->increaseMasterRatio(delta);
}

void AutotileAdaptor::decreaseMasterRatio(double delta)
{
    if (!ensureEngine("decreaseMasterRatio")) {
        return;
    }
    // Validate delta is positive and reasonable
    if (delta <= 0.0 || delta > 1.0) {
        qCWarning(lcDbusAutotile) << "decreaseMasterRatio: invalid delta" << delta << "(must be > 0 and <= 1.0)";
        return;
    }
    qCDebug(lcDbusAutotile) << "D-Bus decreaseMasterRatio:" << delta;
    // Note: This modifies per-screen TilingState, not global config.
    // tilingChanged signal is emitted by engine after retile.
    m_engine->decreaseMasterRatio(delta);
}

void AutotileAdaptor::increaseMasterCount()
{
    if (!ensureEngine("increaseMasterCount")) {
        return;
    }
    qCDebug(lcDbusAutotile) << "D-Bus increaseMasterCount";
    // Note: This modifies per-screen TilingState, not global config.
    // tilingChanged signal is emitted by engine after retile.
    m_engine->increaseMasterCount();
}

void AutotileAdaptor::decreaseMasterCount()
{
    if (!ensureEngine("decreaseMasterCount")) {
        return;
    }
    qCDebug(lcDbusAutotile) << "D-Bus decreaseMasterCount";
    // Note: This modifies per-screen TilingState, not global config.
    // tilingChanged signal is emitted by engine after retile.
    m_engine->decreaseMasterCount();
}

// ═══════════════════════════════════════════════════════════════════════════
// Algorithm Query
// ═══════════════════════════════════════════════════════════════════════════

QStringList AutotileAdaptor::availableAlgorithms()
{
    return AlgorithmRegistry::instance()->availableAlgorithms();
}

QString AutotileAdaptor::algorithmInfo(const QString& algorithmId)
{
    TilingAlgorithm* algo = AlgorithmRegistry::instance()->algorithm(algorithmId);
    if (!algo) {
        qCWarning(lcDbusAutotile) << "Unknown algorithm:" << algorithmId;
        return QStringLiteral("{}");
    }

    QJsonObject info;
    info[QStringLiteral("id")] = algorithmId;
    info[QStringLiteral("name")] = algo->name();
    info[QStringLiteral("description")] = algo->description();
    info[QStringLiteral("icon")] = algo->icon();
    info[QStringLiteral("supportsMasterCount")] = algo->supportsMasterCount();
    info[QStringLiteral("supportsSplitRatio")] = algo->supportsSplitRatio();

    return QString::fromUtf8(QJsonDocument(info).toJson(QJsonDocument::Compact));
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

    QJsonArray outArr;
    for (const QJsonValue& val : doc.array()) {
        QJsonObject obj = val.toObject();
        QString windowId = obj.value(QLatin1String("windowId")).toString();
        QRect geo(obj.value(QLatin1String("x")).toInt(), obj.value(QLatin1String("y")).toInt(),
                  obj.value(QLatin1String("width")).toInt(), obj.value(QLatin1String("height")).toInt());
        if (geo.width() <= 0 || geo.height() <= 0) {
            qCWarning(lcDbusAutotile) << "onWindowsTiled: invalid geometry for" << windowId << geo;
            continue;
        }
        outArr.append(obj);
    }

    if (!outArr.isEmpty()) {
        qCDebug(lcDbusAutotile) << "Emitting windowsTileRequested:" << outArr.size() << "windows";
        Q_EMIT windowsTileRequested(QString::fromUtf8(QJsonDocument(outArr).toJson(QJsonDocument::Compact)));
    }
}

} // namespace PlasmaZones