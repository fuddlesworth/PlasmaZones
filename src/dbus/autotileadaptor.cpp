// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "autotileadaptor.h"

#include "autotile/AlgorithmRegistry.h"
#include "autotile/AutotileConfig.h"
#include "autotile/AutotileEngine.h"
#include "autotile/TilingAlgorithm.h"
#include "core/constants.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcDbusAutotile, "plasmazones.dbus.autotile", QtInfoMsg)

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
    // Auto-relay with D-Bus
    setAutoRelaySignals(true);

    if (!m_engine) {
        qCWarning(lcDbusAutotile) << "AutotileAdaptor created with null engine";
        return;
    }

    // Connect engine signals to D-Bus signals
    connect(m_engine, &AutotileEngine::enabledChanged, this, &AutotileAdaptor::enabledChanged);
    connect(m_engine, &AutotileEngine::algorithmChanged, this, &AutotileAdaptor::algorithmChanged);
    connect(m_engine, &AutotileEngine::tilingChanged, this, &AutotileAdaptor::tilingChanged);
    connect(m_engine, &AutotileEngine::windowTiled, this, &AutotileAdaptor::onWindowTiled);
    connect(m_engine, &AutotileEngine::focusWindowRequested, this, &AutotileAdaptor::focusWindowRequested);

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

void AutotileAdaptor::setEnabled(bool enabled)
{
    if (!ensureEngine("setEnabled")) {
        return;
    }
    m_engine->setEnabled(enabled);
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
    m_engine->setAlgorithm(algorithmId);
}

double AutotileAdaptor::masterRatio() const
{
    if (!m_engine || !m_engine->config()) {
        return AutotileDefaults::DefaultSplitRatio;
    }
    return m_engine->config()->splitRatio;
}

void AutotileAdaptor::setMasterRatio(double ratio)
{
    if (!ensureEngineAndConfig("setMasterRatio")) {
        return;
    }
    ratio = qBound(AutotileDefaults::MinSplitRatio, ratio, AutotileDefaults::MaxSplitRatio);
    if (!qFuzzyCompare(m_engine->config()->splitRatio, ratio)) {
        m_engine->config()->splitRatio = ratio;
        Q_EMIT configChanged();
        m_engine->retile(QString());
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
        m_engine->config()->masterCount = count;
        Q_EMIT configChanged();
        m_engine->retile(QString());
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
    gap = qBound(AutotileDefaults::MinGap, gap, AutotileDefaults::MaxGap);
    if (m_engine->config()->innerGap != gap) {
        m_engine->config()->innerGap = gap;
        Q_EMIT configChanged();
        m_engine->retile(QString());
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
    gap = qBound(AutotileDefaults::MinGap, gap, AutotileDefaults::MaxGap);
    if (m_engine->config()->outerGap != gap) {
        m_engine->config()->outerGap = gap;
        Q_EMIT configChanged();
        m_engine->retile(QString());
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
    if (m_engine->config()->smartGaps != enabled) {
        m_engine->config()->smartGaps = enabled;
        Q_EMIT configChanged();
        m_engine->retile(QString());
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
    if (m_engine->config()->focusNewWindows != enabled) {
        m_engine->config()->focusNewWindows = enabled;
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

void AutotileAdaptor::notifyWindowFocused(const QString& windowId)
{
    if (!ensureEngine("notifyWindowFocused")) {
        return;
    }
    if (windowId.isEmpty()) {
        qCDebug(lcDbusAutotile) << "notifyWindowFocused: empty window ID (focus cleared)";
        return;
    }
    qCDebug(lcDbusAutotile) << "D-Bus notifyWindowFocused:" << windowId;
    m_engine->setFocusedWindow(windowId);
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

void AutotileAdaptor::onWindowTiled(const QString& windowId, const QRect& geometry)
{
    // Convert engine's windowTiled signal to D-Bus windowTileRequested
    // This signal is listened to by the KWin effect to apply the geometry
    qCDebug(lcDbusAutotile) << "Emitting windowTileRequested:" << windowId << geometry;
    Q_EMIT windowTileRequested(windowId, geometry.x(), geometry.y(), geometry.width(), geometry.height());
}

} // namespace PlasmaZones
