// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "autotileadaptor.h"

#include "core/platform/logging.h"

#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include <PhosphorTiles/TilingAlgorithm.h>

namespace PlasmaZones {

AutotileAdaptor::AutotileAdaptor(PhosphorTileEngine::AutotileEngine* engine,
                                 PhosphorTiles::ITileAlgorithmRegistry* algorithmRegistry, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_engine(engine)
    , m_algorithmRegistry(algorithmRegistry)
{
    Q_ASSERT_X(m_algorithmRegistry, "AutotileAdaptor",
               "null ITileAlgorithmRegistry — setAlgorithm / availableAlgorithms / algorithmInfo will crash");
    if (!m_engine) {
        qCWarning(lcDbusAutotile) << "AutotileAdaptor created with null engine";
        return;
    }
    connect(m_engine, &PhosphorEngine::PlacementEngineBase::algorithmChanged, this, &AutotileAdaptor::algorithmChanged);
    qCDebug(lcDbusAutotile) << "AutotileAdaptor initialized";
}

bool AutotileAdaptor::ensureRegistry(const char* methodName) const
{
    // Release-build pair of the ctor Q_ASSERT_X: these are D-Bus-callable
    // slots, so a wiring bug must degrade to a warning, not a crash
    // triggerable from the bus.
    if (!m_algorithmRegistry) {
        qCWarning(lcDbusAutotile) << "Cannot" << methodName << "- algorithm registry not available";
        return false;
    }
    return true;
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
    if (!ensureEngine("setAlgorithm") || !ensureRegistry("setAlgorithm")) {
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

QStringList AutotileAdaptor::availableAlgorithms()
{
    if (!ensureRegistry("availableAlgorithms")) {
        return {};
    }
    return m_algorithmRegistry->availableAlgorithms();
}

PhosphorProtocol::AlgorithmInfoEntry AutotileAdaptor::algorithmInfo(const QString& algorithmId)
{
    if (!ensureRegistry("algorithmInfo")) {
        return {};
    }
    PhosphorTiles::TilingAlgorithm* algo = m_algorithmRegistry->algorithm(algorithmId);
    if (!algo) {
        qCWarning(lcDbusAutotile) << "Unknown algorithm:" << algorithmId;
        return {};
    }

    PhosphorProtocol::AlgorithmInfoEntry entry;
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
    entry.supportsSingleWindow = algo->supportsSingleWindow();

    return entry;
}

void AutotileAdaptor::clearEngine()
{
    if (m_engine) {
        disconnect(m_engine, nullptr, this, nullptr);
        m_engine = nullptr;
    }
}

} // namespace PlasmaZones
