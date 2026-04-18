// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_session_persistence.cpp
 * @brief Unit tests for session persistence: save/load cycle, collision, consumption
 *
 * WIRE FORMAT NOTE: this is a self-contained mock of the persistence pattern
 * using legacy composite ids. The disk format IS still class-keyed (so that
 * saves survive KWin restarts where instance ids regenerate) — that part
 * hasn't changed. Only the in-memory primary key changed from composite to
 * instance id. Production persistence now uses
 * WindowTrackingService::currentAppIdFor() to derive the disk key from the
 * live instance; see src/dbus/windowtrackingadaptor/saveload.cpp.
 */

#include <QTest>
#include <QString>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>

#include "../../../src/core/utils.h"

/// Mock session persistence logic for isolated testing (no daemon/KConfig needed)
class MockSessionPersistence : public QObject
{
    Q_OBJECT

public:
    explicit MockSessionPersistence(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    void windowSnapped(const QString& windowId, const QString& zoneId)
    {
        if (windowId.isEmpty() || zoneId.isEmpty())
            return;
        m_windowZoneAssignments[windowId] = QStringList{zoneId};
        m_lastUsedZoneId = zoneId;
    }

    void windowUnsnapped(const QString& windowId)
    {
        if (windowId.isEmpty())
            return;
        QStringList previousZones = m_windowZoneAssignments.value(windowId);
        QString previousZone = previousZones.isEmpty() ? QString() : previousZones.first();
        m_windowZoneAssignments.remove(windowId);
        if (previousZone == m_lastUsedZoneId) {
            m_lastUsedZoneId.clear();
        }
    }

    /// Save current state to JSON (simulates config.json)
    QString saveStateToJson()
    {
        QJsonObject root;

        // Save window-zone assignments using STABLE IDs
        QJsonObject assignmentsObj;
        for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
            QString stableId = PhosphorIdentity::WindowId::extractAppId(it.key());
            assignmentsObj[stableId] = it.value().isEmpty() ? QString() : it.value().first();
        }
        root[QStringLiteral("windowZoneAssignments")] = assignmentsObj;
        root[QStringLiteral("lastUsedZoneId")] = m_lastUsedZoneId;

        return QString::fromUtf8(QJsonDocument(root).toJson());
    }

    /// Load state from JSON (simulates session restore)
    void loadStateFromJson(const QString& json)
    {
        // Clear current runtime state
        m_windowZoneAssignments.clear();
        m_pendingZoneAssignments.clear();

        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        if (!doc.isObject())
            return;

        QJsonObject root = doc.object();

        // Load into PENDING assignments (keyed by stable ID)
        QJsonObject assignmentsObj = root[QStringLiteral("windowZoneAssignments")].toObject();
        for (auto it = assignmentsObj.constBegin(); it != assignmentsObj.constEnd(); ++it) {
            m_pendingZoneAssignments[it.key()] = QStringList{it.value().toString()};
        }

        m_lastUsedZoneId = root[QStringLiteral("lastUsedZoneId")].toString();
    }

    /// Check if a window should be restored to a persisted zone
    bool checkPersistedZone(const QString& windowId, QString& zoneId)
    {
        if (windowId.isEmpty()) {
            zoneId.clear();
            return false;
        }

        QString stableId = PhosphorIdentity::WindowId::extractAppId(windowId);
        if (!m_pendingZoneAssignments.contains(stableId)) {
            zoneId.clear();
            return false;
        }

        QStringList zones = m_pendingZoneAssignments.value(stableId);
        zoneId = zones.isEmpty() ? QString() : zones.first();
        return !zoneId.isEmpty();
    }

    /// Consume a pending zone assignment (after successful restore)
    void consumePendingAssignment(const QString& windowId)
    {
        QString stableId = PhosphorIdentity::WindowId::extractAppId(windowId);
        m_pendingZoneAssignments.remove(stableId);
    }

    // Accessors for testing
    int pendingAssignmentCount() const
    {
        return m_pendingZoneAssignments.size();
    }
    int activeAssignmentCount() const
    {
        return m_windowZoneAssignments.size();
    }
    QString getLastUsedZoneId() const
    {
        return m_lastUsedZoneId;
    }
    QStringList getPendingStableIds() const
    {
        return m_pendingZoneAssignments.keys();
    }

private:
    // Runtime state (cleared on session end)
    QHash<QString, QStringList> m_windowZoneAssignments;
    QString m_lastUsedZoneId;

    // Pending assignments from loaded session (keyed by stable ID)
    QHash<QString, QStringList> m_pendingZoneAssignments;
};

class TestSessionPersistence : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_persistence = new MockSessionPersistence(this);
    }

    void cleanup()
    {
        delete m_persistence;
        m_persistence = nullptr;
    }

    // --- Basic Save/Load Cycle ---

    void testSaveLoad_singleWindow()
    {
        QString windowId = QStringLiteral("org.kde.konsole|12345");
        QString zoneId = QUuid::createUuid().toString();

        // Session 1: Snap window
        m_persistence->windowSnapped(windowId, zoneId);

        // Save session
        QString json = m_persistence->saveStateToJson();
        QVERIFY(!json.isEmpty());

        // Create new persistence instance (simulates session restart)
        MockSessionPersistence session2;
        session2.loadStateFromJson(json);

        // Session 2: New window with different pointer
        QString newWindowId = QStringLiteral("org.kde.konsole|67890");

        QString restoredZone;
        bool shouldRestore = session2.checkPersistedZone(newWindowId, restoredZone);

        QVERIFY(shouldRestore);
        QCOMPARE(restoredZone, zoneId);
    }

    void testSaveLoad_lastUsedZone()
    {
        QString windowId = QStringLiteral("org.kde.app|12345");
        QString zoneId = QUuid::createUuid().toString();

        m_persistence->windowSnapped(windowId, zoneId);
        QCOMPARE(m_persistence->getLastUsedZoneId(), zoneId);

        QString json = m_persistence->saveStateToJson();

        MockSessionPersistence session2;
        session2.loadStateFromJson(json);

        QCOMPARE(session2.getLastUsedZoneId(), zoneId);
    }

    void testSaveLoad_emptyState()
    {
        QString json = m_persistence->saveStateToJson();

        MockSessionPersistence session2;
        session2.loadStateFromJson(json);

        QCOMPARE(session2.pendingAssignmentCount(), 0);
        QVERIFY(session2.getLastUsedZoneId().isEmpty());
    }

    // --- Window identity collision during restore ---

    void testRestore_sameClassWindowCollision()
    {
        // Session 1: User had one Konsole window snapped
        QString konsoleSession1 = QStringLiteral("org.kde.konsole|12345");
        QString zoneA = QUuid::createUuid().toString();

        m_persistence->windowSnapped(konsoleSession1, zoneA);
        QString json = m_persistence->saveStateToJson();

        // Session 2: User opens a NEW Konsole (never before snapped)
        MockSessionPersistence session2;
        session2.loadStateFromJson(json);

        QString konsoleSession2 = QStringLiteral("org.kde.konsole|67890");

        QString restoredZone;
        bool shouldRestore = session2.checkPersistedZone(konsoleSession2, restoredZone);

        // BUG: New window incorrectly matches old window's zone
        QEXPECT_FAIL("", "Known bug: identity collision causes wrong restore", Continue);
        QVERIFY(!shouldRestore);
        QCOMPARE(restoredZone, zoneA);
    }

    void testRestore_multipleInstancesLastWriteWins()
    {
        // Session 1: User had 3 Konsole windows in different zones
        QString konsole1 = QStringLiteral("org.kde.konsole|11111");
        QString konsole2 = QStringLiteral("org.kde.konsole|22222");
        QString konsole3 = QStringLiteral("org.kde.konsole|33333");

        QString zoneA = QUuid::createUuid().toString();
        QString zoneB = QUuid::createUuid().toString();
        QString zoneC = QUuid::createUuid().toString();

        m_persistence->windowSnapped(konsole1, zoneA);
        m_persistence->windowSnapped(konsole2, zoneB);
        m_persistence->windowSnapped(konsole3, zoneC);

        QString json = m_persistence->saveStateToJson();

        MockSessionPersistence session2;
        session2.loadStateFromJson(json);

        // BUG: Only ONE pending assignment exists (collision - one wins arbitrarily)
        QCOMPARE(session2.pendingAssignmentCount(), 1);

        // All new Konsole windows will match this single zone
        QString anyKonsole = QStringLiteral("org.kde.konsole|99999");
        QString restoredZone;
        bool shouldRestore = session2.checkPersistedZone(anyKonsole, restoredZone);

        QVERIFY(shouldRestore);
        // QHash iteration order is non-deterministic, so any of the three zones could win
        // The important behavior is that ONE zone is restored (collision happened)
        QVERIFY(restoredZone == zoneA || restoredZone == zoneB || restoredZone == zoneC);
    }

    void testRestore_differentAppsNoCollision()
    {
        // Different applications should NOT collide
        QString konsole = QStringLiteral("org.kde.konsole|11111");
        QString dolphin = QStringLiteral("org.kde.dolphin|22222");

        QString zoneA = QUuid::createUuid().toString();
        QString zoneB = QUuid::createUuid().toString();

        m_persistence->windowSnapped(konsole, zoneA);
        m_persistence->windowSnapped(dolphin, zoneB);

        QString json = m_persistence->saveStateToJson();

        MockSessionPersistence session2;
        session2.loadStateFromJson(json);

        // Should have 2 pending assignments (different stable IDs)
        QCOMPARE(session2.pendingAssignmentCount(), 2);

        // Each app restores to correct zone
        QString newKonsole = QStringLiteral("org.kde.konsole|33333");
        QString newDolphin = QStringLiteral("org.kde.dolphin|44444");

        QString konsoleZone, dolphinZone;
        QVERIFY(session2.checkPersistedZone(newKonsole, konsoleZone));
        QVERIFY(session2.checkPersistedZone(newDolphin, dolphinZone));

        QCOMPARE(konsoleZone, zoneA);
        QCOMPARE(dolphinZone, zoneB);
    }

    // --- Pending Assignment Consumption ---

    void testRestore_consumePendingAfterRestore()
    {
        QString windowId = QStringLiteral("org.kde.app|12345");
        QString zoneId = QUuid::createUuid().toString();

        m_persistence->windowSnapped(windowId, zoneId);
        QString json = m_persistence->saveStateToJson();

        MockSessionPersistence session2;
        session2.loadStateFromJson(json);

        QCOMPARE(session2.pendingAssignmentCount(), 1);

        // Check and consume
        QString restoredZone;
        QString newWindow = QStringLiteral("org.kde.app|67890");
        QVERIFY(session2.checkPersistedZone(newWindow, restoredZone));

        session2.consumePendingAssignment(newWindow);
        QCOMPARE(session2.pendingAssignmentCount(), 0);

        // Should not match again
        QString anotherWindow = QStringLiteral("org.kde.app|11111");
        QVERIFY(!session2.checkPersistedZone(anotherWindow, restoredZone));
    }

    void testRestore_consumeDoesNotAffectDifferentApps()
    {
        QString app1 = QStringLiteral("org.kde.app1|11111");
        QString app2 = QStringLiteral("org.kde.app2|22222");
        QString zone1 = QUuid::createUuid().toString();
        QString zone2 = QUuid::createUuid().toString();

        m_persistence->windowSnapped(app1, zone1);
        m_persistence->windowSnapped(app2, zone2);

        QString json = m_persistence->saveStateToJson();

        MockSessionPersistence session2;
        session2.loadStateFromJson(json);

        QCOMPARE(session2.pendingAssignmentCount(), 2);

        // Consume app1's pending assignment
        QString newApp1 = QStringLiteral("org.kde.app1|33333");
        session2.consumePendingAssignment(newApp1);

        QCOMPARE(session2.pendingAssignmentCount(), 1);

        // app2 should still have pending assignment
        QString newApp2 = QStringLiteral("org.kde.app2|44444");
        QString restoredZone;
        QVERIFY(session2.checkPersistedZone(newApp2, restoredZone));
        QCOMPARE(restoredZone, zone2);
    }

    // --- Edge Cases and Error Handling ---

    void testRestore_invalidJson()
    {
        MockSessionPersistence session;
        session.loadStateFromJson(QStringLiteral("not valid json"));

        QCOMPARE(session.pendingAssignmentCount(), 0);
    }

    void testRestore_emptyWindowId()
    {
        QString windowId = QStringLiteral("org.kde.app|12345");
        QString zoneId = QUuid::createUuid().toString();

        m_persistence->windowSnapped(windowId, zoneId);
        QString json = m_persistence->saveStateToJson();

        MockSessionPersistence session2;
        session2.loadStateFromJson(json);

        QString restoredZone;
        QVERIFY(!session2.checkPersistedZone(QString(), restoredZone));
    }

    void testRestore_unsnapBeforeSave()
    {
        QString windowId = QStringLiteral("org.kde.app|12345");
        QString zoneId = QUuid::createUuid().toString();

        m_persistence->windowSnapped(windowId, zoneId);
        m_persistence->windowUnsnapped(windowId);

        QString json = m_persistence->saveStateToJson();

        MockSessionPersistence session2;
        session2.loadStateFromJson(json);

        // Unsnapped window should not have pending assignment
        QCOMPARE(session2.pendingAssignmentCount(), 0);
    }

    // --- Scenario Tests: Real-World Bug Reproduction ---

    void testScenario_neverSnappedWindowGetsAutoSnapped()
    {
        // BUG: Firefox snapped to PhosphorZones::Zone A, Konsole never snapped.
        // After relog, new Konsole incorrectly matches Firefox's pending zone
        // because different app class -> no collision here (passes correctly).
        QString firefox = QStringLiteral("org.mozilla.firefox|11111");
        QString konsole = QStringLiteral("org.kde.konsole|22222"); // Never snapped
        QString zoneA = QUuid::createUuid().toString();

        m_persistence->windowSnapped(firefox, zoneA);
        // konsole is NOT snapped - it was floating

        QString json = m_persistence->saveStateToJson();

        // Session 2
        MockSessionPersistence session2;
        session2.loadStateFromJson(json);

        // New Konsole opens (never was snapped in session 1)
        QString newKonsole = QStringLiteral("org.kde.konsole|33333");

        QString restoredZone;
        bool shouldRestore = session2.checkPersistedZone(newKonsole, restoredZone);

        // CORRECT behavior: Konsole should NOT be restored (it was never snapped)
        QVERIFY(!shouldRestore); // This PASSES - no collision for different app class
    }

    void testScenario_wrongWindowGetsRestoredAmongMultipleSameClass()
    {
        // BUG: Konsole #1 snapped to PhosphorZones::Zone A, Konsole #2 not snapped.
        // After relog, new instance incorrectly matches because of
        // identity collision - can't distinguish which Konsole was snapped.
        QString konsole1 = QStringLiteral("org.kde.konsole|11111"); // Snapped
        QString konsole2 = QStringLiteral("org.kde.konsole|22222"); // NOT snapped
        QString zoneA = QUuid::createUuid().toString();

        m_persistence->windowSnapped(konsole1, zoneA);
        // konsole2 is NOT snapped

        QString json = m_persistence->saveStateToJson();

        // Session 2
        MockSessionPersistence session2;
        session2.loadStateFromJson(json);

        // Konsole #2's new instance opens (was never snapped!)
        QString newKonsole2 = QStringLiteral("org.kde.konsole|33333");

        QString restoredZone;
        bool shouldRestore = session2.checkPersistedZone(newKonsole2, restoredZone);

        // BUG: Window that was never snapped incorrectly matches
        QEXPECT_FAIL("", "Known bug: identity collision causes wrong restore", Continue);
        QVERIFY(!shouldRestore);
        QCOMPARE(restoredZone, zoneA);
    }

private:
    MockSessionPersistence* m_persistence = nullptr;
};

QTEST_MAIN(TestSessionPersistence)
#include "test_session_persistence.moc"
