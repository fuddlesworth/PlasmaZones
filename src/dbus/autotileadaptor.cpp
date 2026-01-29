// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "autotileadaptor.h"

#include "autotile/AlgorithmRegistry.h"
#include "autotile/AutotileConfig.h"
#include "autotile/AutotileEngine.h"
#include "autotile/TilingAlgorithm.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcDbusAutotile, "plasmazones.dbus.autotile", QtInfoMsg)

namespace PlasmaZones {

AutotileAdaptor::AutotileAdaptor(AutotileEngine *engine, QObject *parent)
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
    return m_engine ? m_engine->isEnabled() : false;
}

void AutotileAdaptor::setEnabled(bool enabled)
{
    if (!m_engine) {
        qCWarning(lcDbusAutotile) << "Cannot setEnabled - engine not available";
        return;
    }
    m_engine->setEnabled(enabled);
}

QString AutotileAdaptor::algorithm() const
{
    return m_engine ? m_engine->algorithm() : QString();
}

void AutotileAdaptor::setAlgorithm(const QString &algorithmId)
{
    if (!m_engine) {
        qCWarning(lcDbusAutotile) << "Cannot setAlgorithm - engine not available";
        return;
    }
    m_engine->setAlgorithm(algorithmId);
}

double AutotileAdaptor::masterRatio() const
{
    if (!m_engine || !m_engine->config()) {
        return 0.6; // Default
    }
    return m_engine->config()->splitRatio;
}

void AutotileAdaptor::setMasterRatio(double ratio)
{
    if (!m_engine || !m_engine->config()) {
        qCWarning(lcDbusAutotile) << "Cannot setMasterRatio - engine/config not available";
        return;
    }
    // Clamp ratio to valid range
    ratio = qBound(0.1, ratio, 0.9);
    if (!qFuzzyCompare(m_engine->config()->splitRatio, ratio)) {
        m_engine->config()->splitRatio = ratio;
        Q_EMIT configChanged();
        m_engine->retile(QString()); // Retile all screens
    }
}

int AutotileAdaptor::masterCount() const
{
    if (!m_engine || !m_engine->config()) {
        return 1; // Default
    }
    return m_engine->config()->masterCount;
}

void AutotileAdaptor::setMasterCount(int count)
{
    if (!m_engine || !m_engine->config()) {
        qCWarning(lcDbusAutotile) << "Cannot setMasterCount - engine/config not available";
        return;
    }
    // Clamp count to valid range
    count = qBound(1, count, 5);
    if (m_engine->config()->masterCount != count) {
        m_engine->config()->masterCount = count;
        Q_EMIT configChanged();
        m_engine->retile(QString()); // Retile all screens
    }
}

int AutotileAdaptor::innerGap() const
{
    if (!m_engine || !m_engine->config()) {
        return 8; // Default
    }
    return m_engine->config()->innerGap;
}

void AutotileAdaptor::setInnerGap(int gap)
{
    if (!m_engine || !m_engine->config()) {
        qCWarning(lcDbusAutotile) << "Cannot setInnerGap - engine/config not available";
        return;
    }
    gap = qBound(0, gap, 50);
    if (m_engine->config()->innerGap != gap) {
        m_engine->config()->innerGap = gap;
        Q_EMIT configChanged();
        m_engine->retile(QString());
    }
}

int AutotileAdaptor::outerGap() const
{
    if (!m_engine || !m_engine->config()) {
        return 8; // Default
    }
    return m_engine->config()->outerGap;
}

void AutotileAdaptor::setOuterGap(int gap)
{
    if (!m_engine || !m_engine->config()) {
        qCWarning(lcDbusAutotile) << "Cannot setOuterGap - engine/config not available";
        return;
    }
    gap = qBound(0, gap, 50);
    if (m_engine->config()->outerGap != gap) {
        m_engine->config()->outerGap = gap;
        Q_EMIT configChanged();
        m_engine->retile(QString());
    }
}

bool AutotileAdaptor::smartGaps() const
{
    if (!m_engine || !m_engine->config()) {
        return true; // Default
    }
    return m_engine->config()->smartGaps;
}

void AutotileAdaptor::setSmartGaps(bool enabled)
{
    if (!m_engine || !m_engine->config()) {
        qCWarning(lcDbusAutotile) << "Cannot setSmartGaps - engine/config not available";
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
        return true; // Default
    }
    return m_engine->config()->focusNewWindows;
}

void AutotileAdaptor::setFocusNewWindows(bool enabled)
{
    if (!m_engine || !m_engine->config()) {
        qCWarning(lcDbusAutotile) << "Cannot setFocusNewWindows - engine/config not available";
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

void AutotileAdaptor::retile(const QString &screenName)
{
    if (!m_engine) {
        qCWarning(lcDbusAutotile) << "Cannot retile - engine not available";
        return;
    }
    qCDebug(lcDbusAutotile) << "D-Bus retile request for screen:" << (screenName.isEmpty() ? QStringLiteral("all") : screenName);
    m_engine->retile(screenName);
}

void AutotileAdaptor::swapWindows(const QString &windowId1, const QString &windowId2)
{
    if (!m_engine) {
        qCWarning(lcDbusAutotile) << "Cannot swapWindows - engine not available";
        return;
    }
    if (windowId1.isEmpty() || windowId2.isEmpty()) {
        qCWarning(lcDbusAutotile) << "Cannot swapWindows - empty window ID(s)";
        return;
    }
    qCDebug(lcDbusAutotile) << "D-Bus swap request:" << windowId1 << "<->" << windowId2;
    m_engine->swapWindows(windowId1, windowId2);
}

void AutotileAdaptor::promoteToMaster(const QString &windowId)
{
    if (!m_engine) {
        qCWarning(lcDbusAutotile) << "Cannot promoteToMaster - engine not available";
        return;
    }
    if (windowId.isEmpty()) {
        qCWarning(lcDbusAutotile) << "Cannot promoteToMaster - empty window ID";
        return;
    }
    qCDebug(lcDbusAutotile) << "D-Bus promote request:" << windowId;
    m_engine->promoteToMaster(windowId);
}

void AutotileAdaptor::demoteFromMaster(const QString &windowId)
{
    if (!m_engine) {
        qCWarning(lcDbusAutotile) << "Cannot demoteFromMaster - engine not available";
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
    if (!m_engine) {
        qCWarning(lcDbusAutotile) << "Cannot focusMaster - engine not available";
        return;
    }
    qCDebug(lcDbusAutotile) << "D-Bus focusMaster request";
    m_engine->focusMaster();
}

void AutotileAdaptor::focusNext()
{
    if (!m_engine) {
        qCWarning(lcDbusAutotile) << "Cannot focusNext - engine not available";
        return;
    }
    qCDebug(lcDbusAutotile) << "D-Bus focusNext request";
    m_engine->focusNext();
}

void AutotileAdaptor::focusPrevious()
{
    if (!m_engine) {
        qCWarning(lcDbusAutotile) << "Cannot focusPrevious - engine not available";
        return;
    }
    qCDebug(lcDbusAutotile) << "D-Bus focusPrevious request";
    m_engine->focusPrevious();
}

// ═══════════════════════════════════════════════════════════════════════════
// Ratio/Count Adjustment
// ═══════════════════════════════════════════════════════════════════════════

void AutotileAdaptor::increaseMasterRatio(double delta)
{
    if (!m_engine) {
        qCWarning(lcDbusAutotile) << "Cannot increaseMasterRatio - engine not available";
        return;
    }
    qCDebug(lcDbusAutotile) << "D-Bus increaseMasterRatio:" << delta;
    m_engine->increaseMasterRatio(delta);
    Q_EMIT configChanged();
}

void AutotileAdaptor::decreaseMasterRatio(double delta)
{
    if (!m_engine) {
        qCWarning(lcDbusAutotile) << "Cannot decreaseMasterRatio - engine not available";
        return;
    }
    qCDebug(lcDbusAutotile) << "D-Bus decreaseMasterRatio:" << delta;
    m_engine->decreaseMasterRatio(delta);
    Q_EMIT configChanged();
}

void AutotileAdaptor::increaseMasterCount()
{
    if (!m_engine) {
        qCWarning(lcDbusAutotile) << "Cannot increaseMasterCount - engine not available";
        return;
    }
    qCDebug(lcDbusAutotile) << "D-Bus increaseMasterCount";
    m_engine->increaseMasterCount();
    Q_EMIT configChanged();
}

void AutotileAdaptor::decreaseMasterCount()
{
    if (!m_engine) {
        qCWarning(lcDbusAutotile) << "Cannot decreaseMasterCount - engine not available";
        return;
    }
    qCDebug(lcDbusAutotile) << "D-Bus decreaseMasterCount";
    m_engine->decreaseMasterCount();
    Q_EMIT configChanged();
}

// ═══════════════════════════════════════════════════════════════════════════
// Algorithm Query
// ═══════════════════════════════════════════════════════════════════════════

QStringList AutotileAdaptor::availableAlgorithms()
{
    return AlgorithmRegistry::instance()->availableAlgorithms();
}

QString AutotileAdaptor::algorithmInfo(const QString &algorithmId)
{
    TilingAlgorithm *algo = AlgorithmRegistry::instance()->algorithm(algorithmId);
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

void AutotileAdaptor::onWindowTiled(const QString &windowId, const QRect &geometry)
{
    // Convert engine's windowTiled signal to D-Bus windowTileRequested
    // This signal is listened to by the KWin effect to apply the geometry
    qCDebug(lcDbusAutotile) << "Emitting windowTileRequested:" << windowId << geometry;
    Q_EMIT windowTileRequested(windowId, geometry.x(), geometry.y(), geometry.width(), geometry.height());
}

} // namespace PlasmaZones
