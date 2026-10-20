// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_known_desktops.cpp
 * @brief The virtual desktop ids the session last saw, kept in session state
 *        through the WindowTrackingAdaptor.
 *
 * The daemon hands the list to VirtualDesktopManager::setBaselineDesktopIds
 * at the next start, so desktops removed, added or moved while it was down
 * renumber the stores keyed on the old positions (F809). Pinned here: the
 * list survives a save and a fresh adaptor's load in order, and setting the
 * list the adaptor already holds marks nothing to save.
 */

#include <QTest>
#include <memory>

#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorZones/LayoutRegistry.h>

#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"

#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;
using WTS = PhosphorPlacement::WindowTrackingService;

namespace {
QStringList sampleIds()
{
    return {QStringLiteral("3f2a6d1e-0b7c-4c1d-9a51-1d2e3f4a5b6c"),
            QStringLiteral("8c1b2a3d-4e5f-4a6b-8c7d-9e0f1a2b3c4d"),
            QStringLiteral("0d9e8f7a-6b5c-4d3e-8f2a-1b0c9d8e7f6a")};
}
} // namespace

class TestWtaKnownDesktops : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettings(nullptr);
        m_zoneDetector = new StubZoneDetector(nullptr);
    }

    void cleanup()
    {
        destroyAdaptor();
        delete m_zoneDetector;
        m_zoneDetector = nullptr;
        delete m_settings;
        m_settings = nullptr;
        delete m_layoutManager;
        m_layoutManager = nullptr;
        m_guard.reset();
    }

    void testKnownDesktopIdsRoundTripThroughTheSession()
    {
        makeAdaptor();
        QVERIFY(m_wta->knownDesktopIds().isEmpty());
        m_wta->setKnownDesktopIds(sampleIds());
        QVERIFY(m_wta->service()->peekDirty() & WTS::DirtyKnownDesktops);
        m_wta->saveStateOnShutdown();
        destroyAdaptor();

        makeAdaptor();
        QCOMPARE(m_wta->knownDesktopIds(), sampleIds());
    }

    void testSettingTheSameListMarksNothing()
    {
        makeAdaptor();
        m_wta->setKnownDesktopIds(sampleIds());
        m_wta->saveStateOnShutdown();
        destroyAdaptor();

        // The load leaves nothing dirty, and the list it read is the one the
        // daemon sets again on every start that changed nothing.
        makeAdaptor();
        QCOMPARE(m_wta->service()->peekDirty() & WTS::DirtyKnownDesktops, 0u);
        m_wta->setKnownDesktopIds(sampleIds());
        QCOMPARE(m_wta->service()->peekDirty() & WTS::DirtyKnownDesktops, 0u);

        // Discriminator: a different order is a different list.
        QStringList reordered = sampleIds();
        reordered.swapItemsAt(0, 2);
        m_wta->setKnownDesktopIds(reordered);
        QVERIFY(m_wta->service()->peekDirty() & WTS::DirtyKnownDesktops);
    }

private:
    void makeAdaptor()
    {
        m_parent = new QObject(nullptr);
        m_wta =
            new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, nullptr, m_settings, nullptr, nullptr, m_parent);
    }

    void destroyAdaptor()
    {
        if (!m_parent) {
            return;
        }
        delete m_parent;
        m_parent = nullptr;
        m_wta = nullptr;
    }

    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettings* m_settings = nullptr;
    StubZoneDetector* m_zoneDetector = nullptr;
    QObject* m_parent = nullptr;
    WindowTrackingAdaptor* m_wta = nullptr;
};

QTEST_MAIN(TestWtaKnownDesktops)
#include "test_wta_known_desktops.moc"
