// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_window_tracking_snap.cpp
 * @brief Unit tests for window snap/unsnap tracking, pre-snap geometry, and last-zone
 *
 * Tests cover:
 * 1. Window snap/unsnap tracking
 * 2. Pre-snap geometry storage (first snap preservation)
 * 3. Last used zone tracking
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
 * This class replicates the core tracking logic without D-Bus or daemon dependencies.
 * It allows testing the business logic in isolation.
 */
class MockWindowTrackerSnap : public QObject
{
    Q_OBJECT

public:
    explicit MockWindowTrackerSnap(QObject* parent = nullptr)
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
            return;
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

    QString getZoneForWindow(const QString& windowId)
    {
        const QStringList zones = m_windowZoneAssignments.value(windowId);
        return zones.isEmpty() ? QString() : zones.first();
    }

    QString getLastUsedZoneId()
    {
        return m_lastUsedZoneId;
    }

    int zoneAssignmentCount() const
    {
        return m_windowZoneAssignments.size();
    }

Q_SIGNALS:
    void windowZoneChanged(const QString& windowId, const QString& zoneId);

private:
    QHash<QString, QStringList> m_windowZoneAssignments;
    QHash<QString, QRect> m_preSnapGeometries;
    QString m_lastUsedZoneId;
};

class TestWindowTrackingSnap : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_tracker = new MockWindowTrackerSnap(this);
    }

    void cleanup()
    {
        delete m_tracker;
        m_tracker = nullptr;
    }

    // =====================================================================
    // Window Snap/Unsnap Tests
    // =====================================================================

    void testWindowSnap_basic()
    {
        QString windowId = QStringLiteral("app:window:12345");
        QString zoneId = QUuid::createUuid().toString();

        QSignalSpy spy(m_tracker, &MockWindowTrackerSnap::windowZoneChanged);

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

        m_tracker->windowSnapped(windowId, zoneB);
        QCOMPARE(m_tracker->getZoneForWindow(windowId), zoneB);

        QCOMPARE(m_tracker->zoneAssignmentCount(), 1);
    }

    void testWindowSnap_duplicateSnapSameZone()
    {
        QString windowId = QStringLiteral("app:window:12345");
        QString zoneId = QUuid::createUuid().toString();

        QSignalSpy spy(m_tracker, &MockWindowTrackerSnap::windowZoneChanged);

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

    // =====================================================================
    // Pre-Snap Geometry Tests
    // =====================================================================

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

    // =====================================================================
    // Last Used Zone Tests
    // =====================================================================

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
        QCOMPARE(m_tracker->getLastUsedZoneId(), normalZone);
    }

private:
    MockWindowTrackerSnap* m_tracker = nullptr;
};

QTEST_MAIN(TestWindowTrackingSnap)
#include "test_window_tracking_snap.moc"
