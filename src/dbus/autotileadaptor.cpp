// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "autotileadaptor.h"

#include "autotile/AlgorithmRegistry.h"
#include "autotile/AutotileEngine.h"
#include "autotile/TilingAlgorithm.h"

#include "core/logging.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace PlasmaZones {

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
    connect(m_engine, &AutotileEngine::windowsReleasedFromTiling, this, &AutotileAdaptor::windowsReleasedFromTiling);
    connect(m_engine, &AutotileEngine::windowFloatingChanged, this, &AutotileAdaptor::windowFloatingChanged);
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

// ═══════════════════════════════════════════════════════════════════════════
// Tiling Operations
// ═══════════════════════════════════════════════════════════════════════════

void AutotileAdaptor::retile(const QString& screenName)
{
    if (!ensureEngine("retile")) {
        return;
    }
    qCDebug(lcDbusAutotile) << "retile: screen=" << (screenName.isEmpty() ? QStringLiteral("all") : screenName);
    m_engine->retile(screenName);
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
    qCDebug(lcDbusAutotile) << "windowOpened: windowId=" << windowId << "screen=" << screenName
                            << "minSize=" << minWidth << "x" << minHeight;
    m_engine->windowOpened(windowId, screenName, qMax(0, minWidth), qMax(0, minHeight));
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

void AutotileAdaptor::notifyWindowFocused(const QString& windowId, const QString& screenName)
{
    if (!ensureEngine("notifyWindowFocused")) {
        return;
    }
    if (windowId.isEmpty()) {
        qCDebug(lcDbusAutotile) << "notifyWindowFocused: empty window ID (focus cleared)";
        return;
    }
    if (screenName.isEmpty()) {
        qCDebug(lcDbusAutotile) << "notifyWindowFocused: empty screenName";
        return;
    }
    qCDebug(lcDbusAutotile) << "notifyWindowFocused: windowId=" << windowId << "screen=" << screenName;
    // R2 fix: Pass screen name to engine so m_windowToScreen is updated on focus
    // change. This also addresses R5 (cross-screen window movement detection) since
    // focus events carry the current screen, updating stale m_windowToScreen entries.
    m_engine->windowFocused(windowId, screenName);
}

// floatWindow, unfloatWindow, toggleFocusedWindowFloat, toggleWindowFloat removed:
// all float operations are now routed through the unified WTA methods
// (toggleFloatForWindow for toggle, setWindowFloatingForScreen for directional).

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
    info[QLatin1String("id")] = algorithmId; // Validated by successful lookup above
    info[QLatin1String("name")] = algo->name();
    info[QLatin1String("description")] = algo->description();
    info[QLatin1String("icon")] = algo->icon();
    info[QLatin1String("supportsMasterCount")] = algo->supportsMasterCount();
    info[QLatin1String("supportsSplitRatio")] = algo->supportsSplitRatio();

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

void AutotileAdaptor::clearEngine()
{
    if (m_engine) {
        // Disconnect all 8 engine→this signal connections established in constructor.
        disconnect(m_engine, nullptr, this, nullptr);
        m_engine = nullptr;
    }
}

} // namespace PlasmaZones
