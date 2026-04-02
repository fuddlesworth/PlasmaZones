// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wts_locking.cpp
 * @brief Unit tests for WindowTrackingService window locking
 *
 * Tests cover:
 * 1. Basic lock/unlock via setWindowLocked and toggleWindowLock
 * 2. isWindowLocked only matches exact windowId (no appId fallback)
 * 3. promoteAppIdLock: pending → active, single-instance only
 * 4. Multi-instance: second instance of same app is NOT locked
 * 5. windowClosed converts lock to pending appId
 * 6. isZoneLockedByWindow queries
 * 7. Orphaned pending locks don't leak via isWindowLocked
 */

#include <QTest>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QRect>
#include <QSet>
#include <QUuid>
#include <QSignalSpy>
#include <QRectF>
#include <memory>

#include "core/windowtrackingservice.h"
#include "core/layoutmanager.h"
#include "core/interfaces.h"
#include "core/layout.h"
#include "core/zone.h"
#include "core/virtualdesktopmanager.h"
#include "core/utils.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

// =========================================================================
// Stub Settings
// =========================================================================

#include "../helpers/StubSettings.h"

using StubSettingsLocking = StubSettings;

// =========================================================================
// Stub Zone Detector
// =========================================================================

class StubZoneDetectorLocking : public IZoneDetector
{
    Q_OBJECT
public:
    explicit StubZoneDetectorLocking(QObject* parent = nullptr)
        : IZoneDetector(parent)
    {
    }
    Layout* layout() const override
    {
        return m_layout;
    }
    void setLayout(Layout* layout) override
    {
        m_layout = layout;
    }
    ZoneDetectionResult detectZone(const QPointF&) const override
    {
        return {};
    }
    ZoneDetectionResult detectMultiZone(const QPointF&) const override
    {
        return {};
    }
    Zone* zoneAtPoint(const QPointF&) const override
    {
        return nullptr;
    }
    Zone* nearestZone(const QPointF&) const override
    {
        return nullptr;
    }
    QVector<Zone*> expandPaintedZonesToRect(const QVector<Zone*>&) const override
    {
        return {};
    }
    void highlightZone(Zone*) override
    {
    }
    void highlightZones(const QVector<Zone*>&) override
    {
    }
    void clearHighlights() override
    {
    }

private:
    Layout* m_layout = nullptr;
};

// =========================================================================
// Helper
// =========================================================================

static Layout* createTestLayout(int zoneCount, QObject* parent)
{
    auto* layout = new Layout(QStringLiteral("TestLayout"), parent);
    for (int i = 0; i < zoneCount; ++i) {
        auto* zone = new Zone(layout);
        qreal x = static_cast<qreal>(i) / zoneCount;
        qreal w = 1.0 / zoneCount;
        zone->setRelativeGeometry(QRectF(x, 0.0, w, 1.0));
        zone->setZoneNumber(i + 1);
        layout->addZone(zone);
    }
    return layout;
}

// =========================================================================
// Test Class
// =========================================================================

class TestWtsLocking : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = new LayoutManager(nullptr);
        m_settings = new StubSettingsLocking(nullptr);
        m_zoneDetector = new StubZoneDetectorLocking(nullptr);
        m_service = new WindowTrackingService(m_layoutManager, m_zoneDetector, m_settings, nullptr, nullptr);

        m_testLayout = createTestLayout(3, m_layoutManager);
        m_layoutManager->addLayout(m_testLayout);
        m_layoutManager->setActiveLayout(m_testLayout);

        m_zoneIds.clear();
        for (Zone* z : m_testLayout->zones()) {
            m_zoneIds.append(z->id().toString());
        }
    }

    void cleanup()
    {
        delete m_service;
        m_service = nullptr;
        delete m_zoneDetector;
        m_zoneDetector = nullptr;
        delete m_settings;
        m_settings = nullptr;
        delete m_layoutManager;
        m_layoutManager = nullptr;
        m_testLayout = nullptr;
        m_zoneIds.clear();
        m_guard.reset();
    }

    // =====================================================================
    // P0: Basic Lock / Unlock
    // =====================================================================

    void testSetWindowLocked_locksWindow()
    {
        QString windowId = QStringLiteral("org.kde.dolphin|a1b2c3d4-1234-5678-9abc-def012345678");
        m_service->setWindowLocked(windowId, true);
        QVERIFY(m_service->isWindowLocked(windowId));
    }

    void testSetWindowLocked_unlocksWindow()
    {
        QString windowId = QStringLiteral("org.kde.dolphin|a1b2c3d4-1234-5678-9abc-def012345678");
        m_service->setWindowLocked(windowId, true);
        m_service->setWindowLocked(windowId, false);
        QVERIFY(!m_service->isWindowLocked(windowId));
    }

    void testToggleWindowLock_returnsNewState()
    {
        QString windowId = QStringLiteral("org.kde.dolphin|a1b2c3d4-1234-5678-9abc-def012345678");
        QVERIFY(!m_service->isWindowLocked(windowId));

        bool nowLocked = m_service->toggleWindowLock(windowId);
        QVERIFY(nowLocked);
        QVERIFY(m_service->isWindowLocked(windowId));

        bool nowUnlocked = m_service->toggleWindowLock(windowId);
        QVERIFY(!nowUnlocked);
        QVERIFY(!m_service->isWindowLocked(windowId));
    }

    // =====================================================================
    // P1: No appId fallback in isWindowLocked
    // =====================================================================

    void testIsWindowLocked_doesNotMatchByAppId()
    {
        // Only exact windowId should match, not appId extraction
        QString windowId = QStringLiteral("org.kde.dolphin|a1b2c3d4-1234-5678-9abc-def012345678");
        QString differentInstance = QStringLiteral("org.kde.dolphin|ffff0000-0000-0000-0000-000000099999");

        m_service->setWindowLocked(windowId, true);
        QVERIFY(m_service->isWindowLocked(windowId));
        QVERIFY(!m_service->isWindowLocked(differentInstance));
    }

    // =====================================================================
    // P1: Pending AppId Lock Promotion
    // =====================================================================

    void testSetLockedWindows_storesAsPending()
    {
        QSet<QString> appIds = {QStringLiteral("org.kde.dolphin")};
        m_service->setLockedWindows(appIds);

        // Pending locks should NOT be visible via isWindowLocked
        QVERIFY(!m_service->isWindowLocked(QStringLiteral("org.kde.dolphin")));
        QVERIFY(!m_service->isWindowLocked(QStringLiteral("org.kde.dolphin|a1b2c3d4-1234-5678-9abc-def012345678")));

        // But should be in pendingAppIdLocks
        QCOMPARE(m_service->pendingAppIdLocks(), appIds);
    }

    void testPromoteAppIdLock_promotesToWindowId()
    {
        QSet<QString> appIds = {QStringLiteral("org.kde.dolphin")};
        m_service->setLockedWindows(appIds);

        QString windowId = QStringLiteral("org.kde.dolphin|a1b2c3d4-1234-5678-9abc-def012345678");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], QStringLiteral("DP-1"), 1);

        // After promotion, the specific windowId should be locked
        QVERIFY(m_service->isWindowLocked(windowId));
        // Pending entry should be consumed
        QVERIFY(m_service->pendingAppIdLocks().isEmpty());
    }

    void testPromoteAppIdLock_secondInstanceNotLocked()
    {
        QSet<QString> appIds = {QStringLiteral("org.kde.dolphin")};
        m_service->setLockedWindows(appIds);

        QString firstInstance = QStringLiteral("org.kde.dolphin|a1b2c3d4-0000-0000-0000-000000011111");
        QString secondInstance = QStringLiteral("org.kde.dolphin|a1b2c3d4-0000-0000-0000-000000022222");

        // First instance gets the lock
        m_service->assignWindowToZone(firstInstance, m_zoneIds[0], QStringLiteral("DP-1"), 1);
        QVERIFY(m_service->isWindowLocked(firstInstance));

        // Second instance does NOT get the lock (pending entry consumed)
        m_service->assignWindowToZone(secondInstance, m_zoneIds[1], QStringLiteral("DP-1"), 1);
        QVERIFY(!m_service->isWindowLocked(secondInstance));
    }

    // =====================================================================
    // P1: Window Close — Lock Persistence
    // =====================================================================

    void testWindowClosed_convertsLockToPendingAppId()
    {
        QString windowId = QStringLiteral("org.kde.dolphin|a1b2c3d4-1234-5678-9abc-def012345678");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], QStringLiteral("DP-1"), 1);
        m_service->setWindowLocked(windowId, true);

        m_service->windowClosed(windowId);

        // Active lock should be gone
        QVERIFY(!m_service->isWindowLocked(windowId));
        QVERIFY(!m_service->lockedWindows().contains(windowId));

        // AppId should be in pending for next instance
        QVERIFY(m_service->pendingAppIdLocks().contains(QStringLiteral("org.kde.dolphin")));
    }

    void testWindowClosed_nonLockedWindow_noPendingEntry()
    {
        QString windowId = QStringLiteral("org.kde.dolphin|a1b2c3d4-1234-5678-9abc-def012345678");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], QStringLiteral("DP-1"), 1);

        m_service->windowClosed(windowId);

        QVERIFY(m_service->pendingAppIdLocks().isEmpty());
    }

    // =====================================================================
    // P2: Zone Lock Queries
    // =====================================================================

    void testIsZoneLockedByWindow_trueWhenLocked()
    {
        QString windowId = QStringLiteral("org.kde.dolphin|a1b2c3d4-1234-5678-9abc-def012345678");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], QStringLiteral("DP-1"), 1);
        m_service->setWindowLocked(windowId, true);

        QVERIFY(m_service->isZoneLockedByWindow(m_zoneIds[0]));
    }

    void testIsZoneLockedByWindow_falseWhenUnlocked()
    {
        QString windowId = QStringLiteral("org.kde.dolphin|a1b2c3d4-1234-5678-9abc-def012345678");
        m_service->assignWindowToZone(windowId, m_zoneIds[0], QStringLiteral("DP-1"), 1);

        QVERIFY(!m_service->isZoneLockedByWindow(m_zoneIds[0]));
    }

    void testIsZoneLockedByWindow_emptyZone()
    {
        QVERIFY(!m_service->isZoneLockedByWindow(m_zoneIds[2]));
    }

    // =====================================================================
    // P2: setWindowLocked clears pending appId
    // =====================================================================

    void testSetWindowLocked_clearsPendingAppId()
    {
        // Simulate session restore with pending appId lock
        QSet<QString> appIds = {QStringLiteral("org.kde.dolphin")};
        m_service->setLockedWindows(appIds);

        // User explicitly locks — should clear pending entry
        QString windowId = QStringLiteral("org.kde.dolphin|a1b2c3d4-1234-5678-9abc-def012345678");
        m_service->setWindowLocked(windowId, true);

        QVERIFY(m_service->pendingAppIdLocks().isEmpty());
        QVERIFY(m_service->isWindowLocked(windowId));
    }

    void testSetWindowLocked_unlockClearsPendingAppId()
    {
        QSet<QString> appIds = {QStringLiteral("org.kde.dolphin")};
        m_service->setLockedWindows(appIds);

        QString windowId = QStringLiteral("org.kde.dolphin|a1b2c3d4-1234-5678-9abc-def012345678");
        m_service->setWindowLocked(windowId, false);

        // Both active and pending should be clear
        QVERIFY(!m_service->isWindowLocked(windowId));
        QVERIFY(m_service->pendingAppIdLocks().isEmpty());
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    LayoutManager* m_layoutManager = nullptr;
    StubSettingsLocking* m_settings = nullptr;
    StubZoneDetectorLocking* m_zoneDetector = nullptr;
    WindowTrackingService* m_service = nullptr;
    Layout* m_testLayout = nullptr;
    QStringList m_zoneIds;
};

QTEST_MAIN(TestWtsLocking)
#include "test_wts_locking.moc"
