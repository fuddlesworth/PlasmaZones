// SPDX-FileCopyrightText: 2024 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_window_tracking.cpp
 * @brief Unit tests for WindowTrackingAdaptor behavior
 *
 * Tests cover:
 * 1. Window snap/unsnap tracking
 * 2. Pre-snap geometry storage (first snap preservation)
 * 3. Last used zone tracking
 * 4. Window close cleanup
 * 5. Layout change validation
 * 6. Floating window state management
 */

#include <QTest>
#include <QString>
#include <QHash>
#include <QRect>
#include <QSet>
#include <QUuid>
#include <QSignalSpy>

/**
 * @brief Mock/stub implementation of WindowTrackingAdaptor logic for isolated testing
 *
 * This class replicates the core tracking logic without D-Bus or daemon dependencies.
 * It allows testing the business logic in isolation.
 */
class MockWindowTracker : public QObject
{
    Q_OBJECT

public:
    explicit MockWindowTracker(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    // Tracking methods (replicate WindowTrackingAdaptor logic)
    void windowSnapped(const QString& windowId, const QString& zoneId)
    {
        if (windowId.isEmpty() || zoneId.isEmpty())
            return;

        QStringList previousZones = m_windowZoneAssignments.value(windowId);
        if (previousZones != QStringList{zoneId}) {
            m_windowZoneAssignments[windowId] = QStringList{zoneId};
            Q_EMIT windowZoneChanged(windowId, zoneId);
        }

        // Track last used zone (skip zoneselector- prefixed IDs)
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

            // Clear last used zone only if this window was snapped to it
            if (!m_lastUsedZoneId.isEmpty() && previousZoneIds.contains(m_lastUsedZoneId)) {
                m_lastUsedZoneId.clear();
            }
        }
    }

    void storePreSnapGeometry(const QString& windowId, int x, int y, int width, int height)
    {
        if (windowId.isEmpty() || width <= 0 || height <= 0)
            return;

        // Key design: Only store on FIRST snap
        if (m_preSnapGeometries.contains(windowId)) {
            return; // Keep original geometry
        }

        m_preSnapGeometries[windowId] = QRect(x, y, width, height);
    }

    bool getPreSnapGeometry(const QString& windowId, int& x, int& y, int& width, int& height)
    {
        x = y = width = height = 0;
        if (windowId.isEmpty())
            return false;

        auto it = m_preSnapGeometries.constFind(windowId);
        if (it == m_preSnapGeometries.constEnd())
            return false;

        const QRect& geometry = it.value();
        x = geometry.x();
        y = geometry.y();
        width = geometry.width();
        height = geometry.height();
        return true;
    }

    bool hasPreSnapGeometry(const QString& windowId)
    {
        return !windowId.isEmpty() && m_preSnapGeometries.contains(windowId);
    }

    void clearPreSnapGeometry(const QString& windowId)
    {
        if (!windowId.isEmpty()) {
            m_preSnapGeometries.remove(windowId);
        }
    }

    void windowClosed(const QString& windowId)
    {
        if (windowId.isEmpty())
            return;
        m_windowZoneAssignments.remove(windowId);
        m_preSnapGeometries.remove(windowId);
        QString stableId = extractStableId(windowId);
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

    QString getLastUsedZoneId()
    {
        return m_lastUsedZoneId;
    }

    void setWindowFloating(const QString& windowId, bool floating)
    {
        if (windowId.isEmpty())
            return;
        QString stableId = extractStableId(windowId);

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
        QString stableId = extractStableId(windowId);
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
    static QString extractStableId(const QString& windowId)
    {
        int lastColon = windowId.lastIndexOf(QLatin1Char(':'));
        if (lastColon <= 0)
            return windowId;

        QString potentialPointer = windowId.mid(lastColon + 1);
        bool isPointer = !potentialPointer.isEmpty();
        for (const QChar& c : potentialPointer) {
            if (!c.isDigit()) {
                isPointer = false;
                break;
            }
        }

        if (isPointer) {
            return windowId.left(lastColon);
        }
        return windowId;
    }

    QHash<QString, QStringList> m_windowZoneAssignments;
    QHash<QString, QRect> m_preSnapGeometries;
    QSet<QString> m_floatingWindows;
    QString m_lastUsedZoneId;
};

class TestWindowTracking : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_tracker = new MockWindowTracker(this);
    }

    void cleanup()
    {
        delete m_tracker;
        m_tracker = nullptr;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Window Snap/Unsnap Tests
    // ═══════════════════════════════════════════════════════════════════════

    void testWindowSnap_basic()
    {
        QString windowId = QStringLiteral("app:window:12345");
        QString zoneId = QUuid::createUuid().toString();

        QSignalSpy spy(m_tracker, &MockWindowTracker::windowZoneChanged);

        m_tracker->windowSnapped(windowId, zoneId);

        QCOMPARE(m_tracker->getZoneForWindow(windowId), zoneId);
        QCOMPARE(m_tracker->zoneAssignmentCount(), 1);
        QCOMPARE(spy.count(), 1);
    }

    void testWindowSnap_moveToNewZone()
    {
        QString windowId = QStringLiteral("app:window:12345");
        QString zoneA = QUuid::createUuid().toString();
        QString zoneB = QUuid::createUuid().toString();

        m_tracker->windowSnapped(windowId, zoneA);
        QCOMPARE(m_tracker->getZoneForWindow(windowId), zoneA);

        // Move to zone B
        m_tracker->windowSnapped(windowId, zoneB);
        QCOMPARE(m_tracker->getZoneForWindow(windowId), zoneB);

        // Should still be only 1 assignment (updated, not added)
        QCOMPARE(m_tracker->zoneAssignmentCount(), 1);
    }

    void testWindowSnap_duplicateSnapSameZone()
    {
        QString windowId = QStringLiteral("app:window:12345");
        QString zoneId = QUuid::createUuid().toString();

        QSignalSpy spy(m_tracker, &MockWindowTracker::windowZoneChanged);

        m_tracker->windowSnapped(windowId, zoneId);
        m_tracker->windowSnapped(windowId, zoneId); // Same zone again

        // Should only emit signal once (no change)
        QCOMPARE(spy.count(), 1);
    }

    void testWindowUnsnap_basic()
    {
        QString windowId = QStringLiteral("app:window:12345");
        QString zoneId = QUuid::createUuid().toString();

        m_tracker->windowSnapped(windowId, zoneId);
        QCOMPARE(m_tracker->zoneAssignmentCount(), 1);

        m_tracker->windowUnsnapped(windowId);
        QCOMPARE(m_tracker->zoneAssignmentCount(), 0);
        QVERIFY(m_tracker->getZoneForWindow(windowId).isEmpty());
    }

    void testWindowUnsnap_nonExistent()
    {
        // Unsnapping a window that was never snapped should not crash
        QString windowId = QStringLiteral("never:snapped:12345");
        m_tracker->windowUnsnapped(windowId);
        QCOMPARE(m_tracker->zoneAssignmentCount(), 0);
    }

    void testWindowSnap_emptyInputs()
    {
        QString validWindow = QStringLiteral("app:window:12345");
        QString validZone = QUuid::createUuid().toString();

        m_tracker->windowSnapped(QString(), validZone); // Empty window
        m_tracker->windowSnapped(validWindow, QString()); // Empty zone

        QCOMPARE(m_tracker->zoneAssignmentCount(), 0);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Pre-Snap Geometry Tests
    // ═══════════════════════════════════════════════════════════════════════

    void testPreSnapGeometry_stored()
    {
        QString windowId = QStringLiteral("app:window:12345");
        m_tracker->storePreSnapGeometry(windowId, 100, 200, 800, 600);

        int x, y, w, h;
        QVERIFY(m_tracker->getPreSnapGeometry(windowId, x, y, w, h));
        QCOMPARE(x, 100);
        QCOMPARE(y, 200);
        QCOMPARE(w, 800);
        QCOMPARE(h, 600);
    }

    void testPreSnapGeometry_firstSnapOnly()
    {
        // KEY DESIGN: Only store on FIRST snap, not A->B moves
        QString windowId = QStringLiteral("app:window:12345");

        // First snap: store original geometry
        m_tracker->storePreSnapGeometry(windowId, 100, 200, 800, 600);

        // Second snap attempt: should NOT overwrite
        m_tracker->storePreSnapGeometry(windowId, 500, 500, 1000, 800);

        int x, y, w, h;
        QVERIFY(m_tracker->getPreSnapGeometry(windowId, x, y, w, h));
        QCOMPARE(x, 100); // Still original values
        QCOMPARE(y, 200);
        QCOMPARE(w, 800);
        QCOMPARE(h, 600);
    }

    void testPreSnapGeometry_invalidDimensions()
    {
        QString windowId = QStringLiteral("app:window:12345");

        m_tracker->storePreSnapGeometry(windowId, 100, 200, 0, 600); // Zero width
        QVERIFY(!m_tracker->hasPreSnapGeometry(windowId));

        m_tracker->storePreSnapGeometry(windowId, 100, 200, 800, 0); // Zero height
        QVERIFY(!m_tracker->hasPreSnapGeometry(windowId));

        m_tracker->storePreSnapGeometry(windowId, 100, 200, -1, 600); // Negative width
        QVERIFY(!m_tracker->hasPreSnapGeometry(windowId));
    }

    void testPreSnapGeometry_clear()
    {
        QString windowId = QStringLiteral("app:window:12345");
        m_tracker->storePreSnapGeometry(windowId, 100, 200, 800, 600);

        QVERIFY(m_tracker->hasPreSnapGeometry(windowId));
        m_tracker->clearPreSnapGeometry(windowId);
        QVERIFY(!m_tracker->hasPreSnapGeometry(windowId));
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Last Used Zone Tests
    // ═══════════════════════════════════════════════════════════════════════

    void testLastUsedZone_tracked()
    {
        QString windowId = QStringLiteral("app:window:12345");
        QString zoneId = QUuid::createUuid().toString();

        QVERIFY(m_tracker->getLastUsedZoneId().isEmpty());

        m_tracker->windowSnapped(windowId, zoneId);
        QCOMPARE(m_tracker->getLastUsedZoneId(), zoneId);
    }

    void testLastUsedZone_updatedOnNewSnap()
    {
        QString window1 = QStringLiteral("app:window1:11111");
        QString window2 = QStringLiteral("app:window2:22222");
        QString zoneA = QUuid::createUuid().toString();
        QString zoneB = QUuid::createUuid().toString();

        m_tracker->windowSnapped(window1, zoneA);
        QCOMPARE(m_tracker->getLastUsedZoneId(), zoneA);

        m_tracker->windowSnapped(window2, zoneB);
        QCOMPARE(m_tracker->getLastUsedZoneId(), zoneB);
    }

    void testLastUsedZone_clearedOnUnsnap()
    {
        QString windowId = QStringLiteral("app:window:12345");
        QString zoneId = QUuid::createUuid().toString();

        m_tracker->windowSnapped(windowId, zoneId);
        QCOMPARE(m_tracker->getLastUsedZoneId(), zoneId);

        m_tracker->windowUnsnapped(windowId);
        QVERIFY(m_tracker->getLastUsedZoneId().isEmpty());
    }

    void testLastUsedZone_notClearedForDifferentWindow()
    {
        QString window1 = QStringLiteral("app:window1:11111");
        QString window2 = QStringLiteral("app:window2:22222");
        QString zoneA = QUuid::createUuid().toString();
        QString zoneB = QUuid::createUuid().toString();

        m_tracker->windowSnapped(window1, zoneA);
        m_tracker->windowSnapped(window2, zoneB);
        QCOMPARE(m_tracker->getLastUsedZoneId(), zoneB);

        // Unsnapping window1 should NOT clear lastUsedZone (it's zoneB, not zoneA)
        m_tracker->windowUnsnapped(window1);
        QCOMPARE(m_tracker->getLastUsedZoneId(), zoneB);
    }

    void testLastUsedZone_zoneSelectorIgnored()
    {
        QString windowId = QStringLiteral("app:window:12345");
        QString normalZone = QUuid::createUuid().toString();
        QString selectorZone = QStringLiteral("zoneselector-preview-123");

        m_tracker->windowSnapped(windowId, normalZone);
        QCOMPARE(m_tracker->getLastUsedZoneId(), normalZone);

        // Zone selector snaps should NOT update last used zone
        m_tracker->windowSnapped(windowId, selectorZone);
        QCOMPARE(m_tracker->getLastUsedZoneId(), normalZone); // Still the normal zone
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Window Close Cleanup Tests
    // ═══════════════════════════════════════════════════════════════════════

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

    // ═══════════════════════════════════════════════════════════════════════
    // Floating Window Tests
    // ═══════════════════════════════════════════════════════════════════════

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
        // (This is actually a BUG in the current implementation!)
        QVERIFY(m_tracker->isWindowFloating(window2));
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Multi-Window Query Tests
    // ═══════════════════════════════════════════════════════════════════════

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
    MockWindowTracker* m_tracker = nullptr;
};

QTEST_MAIN(TestWindowTracking)
#include "test_window_tracking.moc"
