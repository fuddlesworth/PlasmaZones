// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_window_tracking_float.cpp
 * @brief Unit tests for floating window state, close cleanup, and multi-window queries
 *
 * Tests cover:
 * 1. Floating window state management
 * 2. Window close cleanup
 * 3. Multi-window queries (windows-in-zone, snapped-windows)
 */

#include <QTest>
#include <QString>
#include <QHash>
#include <QRect>
#include <QSet>
#include <QUuid>
#include <QSignalSpy>

#include "../../src/core/utils.h"

/**
 * @brief Mock/stub implementation of WindowTrackingAdaptor logic for isolated testing
 *
 * Replicates core tracking logic including floating window management and
 * close cleanup without D-Bus or daemon dependencies.
 */
class MockWindowTrackerFloat : public QObject
{
    Q_OBJECT

public:
    explicit MockWindowTrackerFloat(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    void windowSnapped(const QString& windowId, const QString& zoneId)
    {
        if (windowId.isEmpty() || zoneId.isEmpty())
            return;

        QStringList previousZones = m_windowZoneAssignments.value(windowId);
        if (previousZones != QStringList{zoneId}) {
            m_windowZoneAssignments[windowId] = QStringList{zoneId};
            Q_EMIT windowZoneChanged(windowId, zoneId);
        }

        if (!zoneId.startsWith(QStringLiteral("zoneselector-"))) {
            m_lastUsedZoneId = zoneId;
        }
    }

    void windowUnsnapped(const QString& windowId)
    {
        if (windowId.isEmpty())
            return;

        QStringList previousZoneIds = m_windowZoneAssignments.value(windowId);
        if (m_windowZoneAssignments.remove(windowId) > 0) {
            Q_EMIT windowZoneChanged(windowId, QString());

            if (!m_lastUsedZoneId.isEmpty() && previousZoneIds.contains(m_lastUsedZoneId)) {
                m_lastUsedZoneId.clear();
            }
        }
    }

    void storePreSnapGeometry(const QString& windowId, int x, int y, int width, int height)
    {
        if (windowId.isEmpty() || width <= 0 || height <= 0)
            return;
        if (m_preSnapGeometries.contains(windowId))
            return;
        m_preSnapGeometries[windowId] = QRect(x, y, width, height);
    }

    void windowClosed(const QString& windowId)
    {
        if (windowId.isEmpty())
            return;
        m_windowZoneAssignments.remove(windowId);
        m_preSnapGeometries.remove(windowId);
        QString stableId = PlasmaZones::Utils::extractStableId(windowId);
        m_floatingWindows.remove(stableId);
    }

    QString getZoneForWindow(const QString& windowId)
    {
        const QStringList zones = m_windowZoneAssignments.value(windowId);
        return zones.isEmpty() ? QString() : zones.first();
    }

    QStringList getWindowsInZone(const QString& zoneId)
    {
        QStringList windows;
        for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
            if (it.value().contains(zoneId)) {
                windows.append(it.key());
            }
        }
        return windows;
    }

    QStringList getSnappedWindows()
    {
        return m_windowZoneAssignments.keys();
    }

    void setWindowFloating(const QString& windowId, bool floating)
    {
        if (windowId.isEmpty())
            return;
        QString stableId = PlasmaZones::Utils::extractStableId(windowId);

        if (floating) {
            if (!m_floatingWindows.contains(stableId)) {
                m_floatingWindows.insert(stableId);
                windowUnsnapped(windowId);
            }
        } else {
            m_floatingWindows.remove(stableId);
        }
    }

    bool isWindowFloating(const QString& windowId)
    {
        if (windowId.isEmpty())
            return false;
        QString stableId = PlasmaZones::Utils::extractStableId(windowId);
        return m_floatingWindows.contains(stableId);
    }

    // Accessors for testing
    int zoneAssignmentCount() const
    {
        return m_windowZoneAssignments.size();
    }
    int preSnapGeometryCount() const
    {
        return m_preSnapGeometries.size();
    }
    int floatingWindowCount() const
    {
        return m_floatingWindows.size();
    }

Q_SIGNALS:
    void windowZoneChanged(const QString& windowId, const QString& zoneId);

private:
    QHash<QString, QStringList> m_windowZoneAssignments;
    QHash<QString, QRect> m_preSnapGeometries;
    QSet<QString> m_floatingWindows;
    QString m_lastUsedZoneId;
};

class TestWindowTrackingFloat : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_tracker = new MockWindowTrackerFloat(this);
    }

    void cleanup()
    {
        delete m_tracker;
        m_tracker = nullptr;
    }

    // =====================================================================
    // Window Close Cleanup Tests
    // =====================================================================

    void testWindowClosed_cleanupAll()
    {
        QString windowId = QStringLiteral("app:window:12345");
        QString zoneId = QUuid::createUuid().toString();

        // Set up tracking data
        m_tracker->storePreSnapGeometry(windowId, 100, 200, 800, 600);
        m_tracker->windowSnapped(windowId, zoneId);
        m_tracker->setWindowFloating(windowId, true);

        // Close window
        m_tracker->windowClosed(windowId);

        // All tracking data should be cleaned up
        QCOMPARE(m_tracker->zoneAssignmentCount(), 0);
        QCOMPARE(m_tracker->preSnapGeometryCount(), 0);
        QCOMPARE(m_tracker->floatingWindowCount(), 0);
    }

    // =====================================================================
    // Floating Window Tests
    // =====================================================================

    void testFloatingWindow_basic()
    {
        QString windowId = QStringLiteral("app:window:12345");

        QVERIFY(!m_tracker->isWindowFloating(windowId));

        m_tracker->setWindowFloating(windowId, true);
        QVERIFY(m_tracker->isWindowFloating(windowId));

        m_tracker->setWindowFloating(windowId, false);
        QVERIFY(!m_tracker->isWindowFloating(windowId));
    }

    void testFloatingWindow_unsnapOnFloat()
    {
        QString windowId = QStringLiteral("app:window:12345");
        QString zoneId = QUuid::createUuid().toString();

        m_tracker->windowSnapped(windowId, zoneId);
        QVERIFY(!m_tracker->getZoneForWindow(windowId).isEmpty());

        // Floating a window should unsnap it
        m_tracker->setWindowFloating(windowId, true);
        QVERIFY(m_tracker->getZoneForWindow(windowId).isEmpty());
    }

    void testFloatingWindow_stableIdPersistence()
    {
        // Floating state uses stable ID, so should work across different pointer addresses
        QString window1 = QStringLiteral("app:window:12345");
        QString window2 = QStringLiteral("app:window:67890"); // Same class, different pointer

        m_tracker->setWindowFloating(window1, true);

        // window2 has same stable ID, so should also appear as floating
        // This is a known bug in the current implementation
        QEXPECT_FAIL("", "Known bug: identity collision - same-class windows share floating state", Continue);
        QVERIFY(!m_tracker->isWindowFloating(window2));
    }

    // =====================================================================
    // Multi-Window Query Tests
    // =====================================================================

    void testGetWindowsInZone()
    {
        QString zoneId = QUuid::createUuid().toString();
        QString window1 = QStringLiteral("app:window1:11111");
        QString window2 = QStringLiteral("app:window2:22222");
        QString window3 = QStringLiteral("app:window3:33333");
        QString otherZone = QUuid::createUuid().toString();

        m_tracker->windowSnapped(window1, zoneId);
        m_tracker->windowSnapped(window2, zoneId);
        m_tracker->windowSnapped(window3, otherZone);

        QStringList windowsInZone = m_tracker->getWindowsInZone(zoneId);
        QCOMPARE(windowsInZone.size(), 2);
        QVERIFY(windowsInZone.contains(window1));
        QVERIFY(windowsInZone.contains(window2));
        QVERIFY(!windowsInZone.contains(window3));
    }

    void testGetSnappedWindows()
    {
        QString window1 = QStringLiteral("app:window1:11111");
        QString window2 = QStringLiteral("app:window2:22222");
        QString zone1 = QUuid::createUuid().toString();
        QString zone2 = QUuid::createUuid().toString();

        m_tracker->windowSnapped(window1, zone1);
        m_tracker->windowSnapped(window2, zone2);

        QStringList snapped = m_tracker->getSnappedWindows();
        QCOMPARE(snapped.size(), 2);
        QVERIFY(snapped.contains(window1));
        QVERIFY(snapped.contains(window2));
    }

private:
    MockWindowTrackerFloat* m_tracker = nullptr;
};

QTEST_MAIN(TestWindowTrackingFloat)
#include "test_window_tracking_float.moc"
