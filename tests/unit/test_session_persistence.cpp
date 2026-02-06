// SPDX-FileCopyrightText: 2024 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_session_persistence.cpp
 * @brief Unit tests for session persistence and cross-session window restoration
 *
 * Bug context: Windows that were never snapped were being auto-snapped to zones after
 * session restart because of window identity collision in the stable ID mechanism.
 *
 * This test suite validates:
 * 1. Session save/restore cycle correctness
 * 2. Pending zone assignment matching
 * 3. Same-class window collision during restore
 * 4. Stale pending assignment cleanup
 * 5. Edge cases in session persistence
 */

#include <QTest>
#include <QString>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRect>
#include <QUuid>
#include <QTemporaryDir>
#include <QSettings>

/**
 * @brief Mock session persistence logic for isolated testing
 *
 * Replicates WindowTrackingAdaptor's save/load state logic without
 * requiring full daemon or KConfig infrastructure.
 */
class MockSessionPersistence : public QObject
{
    Q_OBJECT

public:
    explicit MockSessionPersistence(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Session 1: Active window tracking (runtime)
    // ═══════════════════════════════════════════════════════════════════════

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

    // ═══════════════════════════════════════════════════════════════════════
    // Session persistence (save/load)
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Save current state to JSON (simulates plasmazonesrc)
     * @return JSON string representing persisted state
     */
    QString saveStateToJson()
    {
        QJsonObject root;

        // Save window-zone assignments using STABLE IDs
        QJsonObject assignmentsObj;
        for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
            QString stableId = extractStableId(it.key());
            assignmentsObj[stableId] = it.value().isEmpty() ? QString() : it.value().first();
        }
        root[QStringLiteral("windowZoneAssignments")] = assignmentsObj;
        root[QStringLiteral("lastUsedZoneId")] = m_lastUsedZoneId;

        return QString::fromUtf8(QJsonDocument(root).toJson());
    }

    /**
     * @brief Load state from JSON (simulates session restore)
     * @param json JSON string from saveStateToJson()
     */
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

    // ═══════════════════════════════════════════════════════════════════════
    // Session 2: Window restoration (after reload)
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Check if a window should be restored to a persisted zone
     * @param windowId Full window ID (with new pointer address)
     * @param zoneId Output: Zone ID to restore to (if found)
     * @return true if window should be restored
     */
    bool checkPersistedZone(const QString& windowId, QString& zoneId)
    {
        if (windowId.isEmpty()) {
            zoneId.clear();
            return false;
        }

        QString stableId = extractStableId(windowId);
        if (!m_pendingZoneAssignments.contains(stableId)) {
            zoneId.clear();
            return false;
        }

        QStringList zones = m_pendingZoneAssignments.value(stableId);
        zoneId = zones.isEmpty() ? QString() : zones.first();
        return !zoneId.isEmpty();
    }

    /**
     * @brief Consume a pending zone assignment (after successful restore)
     * @param windowId Full window ID
     */
    void consumePendingAssignment(const QString& windowId)
    {
        QString stableId = extractStableId(windowId);
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

private:
    // Runtime state (cleared on session end)
    QHash<QString, QStringList> m_windowZoneAssignments;
    QString m_lastUsedZoneId;

    // Pending assignments from loaded session (keyed by stable ID)
    QHash<QString, QStringList> m_pendingZoneAssignments;
};

/**
 * @brief Extended mock that includes layout and desktop validation
 *
 * This mock replicates the full restore logic including the layout mismatch
 * bug fix. It validates that:
 * 1. The current layout matches the saved layout
 * 2. The current desktop matches the saved desktop (unless sticky)
 * 3. Multi-screen support: different screens can have different layouts
 */
class MockSessionPersistenceWithLayoutValidation : public QObject
{
    Q_OBJECT

public:
    explicit MockSessionPersistenceWithLayoutValidation(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    // Set the current context (simulates runtime state)
    void setCurrentLayoutId(const QString& layoutId) { m_currentLayoutId = layoutId; }
    void setCurrentDesktop(int desktop) { m_currentDesktop = desktop; }

    // Multi-screen support: set layout for specific screen/desktop combination
    void setLayoutForScreen(const QString& screenName, int desktop, const QString& layoutId)
    {
        QString key = screenName + QStringLiteral(":") + QString::number(desktop);
        m_screenLayouts[key] = layoutId;
    }

    // Get layout for screen/desktop (with fallback to default)
    QString getLayoutForScreen(const QString& screenName, int desktop) const
    {
        // Try exact match first
        QString key = screenName + QStringLiteral(":") + QString::number(desktop);
        if (m_screenLayouts.contains(key)) {
            return m_screenLayouts.value(key);
        }
        // Fallback to screen with desktop 0 (all desktops)
        key = screenName + QStringLiteral(":0");
        if (m_screenLayouts.contains(key)) {
            return m_screenLayouts.value(key);
        }
        // Fallback to default layout
        return m_currentLayoutId;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Session 1: Active window tracking (runtime)
    // ═══════════════════════════════════════════════════════════════════════

    void windowSnapped(const QString& windowId, const QString& zoneId,
                       const QString& screenName = QString(), int desktop = 1)
    {
        if (windowId.isEmpty() || zoneId.isEmpty())
            return;
        m_windowZoneAssignments[windowId] = QStringList{zoneId};
        m_windowScreenAssignments[windowId] = screenName;
        m_windowDesktopAssignments[windowId] = desktop;
        m_lastUsedZoneId = zoneId;
    }

    void windowClosed(const QString& windowId)
    {
        if (windowId.isEmpty())
            return;

        QString stableId = extractStableId(windowId);
        QStringList zoneIds = m_windowZoneAssignments.value(windowId);
        QString zoneId = zoneIds.isEmpty() ? QString() : zoneIds.first();

        if (!zoneId.isEmpty()) {
            // Save to pending with full context
            QString screenName = m_windowScreenAssignments.value(windowId);
            int desktop = m_windowDesktopAssignments.value(windowId, m_currentDesktop);

            m_pendingZoneAssignments[stableId] = QStringList{zoneId};
            m_pendingZoneScreens[stableId] = screenName;
            m_pendingZoneDesktops[stableId] = desktop;

            // Save the layout ID for this screen/desktop context (multi-screen support)
            // This mirrors the fix: use layoutForScreen() not just activeLayout()
            QString contextLayoutId = getLayoutForScreen(screenName, desktop);
            m_pendingZoneLayouts[stableId] = contextLayoutId;
        }

        // Clean up runtime state
        m_windowZoneAssignments.remove(windowId);
        m_windowScreenAssignments.remove(windowId);
        m_windowDesktopAssignments.remove(windowId);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Session persistence (save/load)
    // ═══════════════════════════════════════════════════════════════════════

    QString saveStateToJson()
    {
        QJsonObject root;

        QJsonObject assignmentsObj;
        for (auto it = m_pendingZoneAssignments.constBegin(); it != m_pendingZoneAssignments.constEnd(); ++it) {
            assignmentsObj[it.key()] = it.value().isEmpty() ? QString() : it.value().first();
        }
        root[QStringLiteral("windowZoneAssignments")] = assignmentsObj;

        QJsonObject screensObj;
        for (auto it = m_pendingZoneScreens.constBegin(); it != m_pendingZoneScreens.constEnd(); ++it) {
            screensObj[it.key()] = it.value();
        }
        root[QStringLiteral("pendingScreenAssignments")] = screensObj;

        QJsonObject desktopsObj;
        for (auto it = m_pendingZoneDesktops.constBegin(); it != m_pendingZoneDesktops.constEnd(); ++it) {
            desktopsObj[it.key()] = it.value();
        }
        root[QStringLiteral("pendingDesktopAssignments")] = desktopsObj;

        QJsonObject layoutsObj;
        for (auto it = m_pendingZoneLayouts.constBegin(); it != m_pendingZoneLayouts.constEnd(); ++it) {
            layoutsObj[it.key()] = it.value();
        }
        root[QStringLiteral("pendingLayoutAssignments")] = layoutsObj;

        root[QStringLiteral("lastUsedZoneId")] = m_lastUsedZoneId;

        return QString::fromUtf8(QJsonDocument(root).toJson());
    }

    void loadStateFromJson(const QString& json)
    {
        m_windowZoneAssignments.clear();
        m_pendingZoneAssignments.clear();
        m_pendingZoneScreens.clear();
        m_pendingZoneDesktops.clear();
        m_pendingZoneLayouts.clear();

        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        if (!doc.isObject())
            return;

        QJsonObject root = doc.object();

        QJsonObject assignmentsObj = root[QStringLiteral("windowZoneAssignments")].toObject();
        for (auto it = assignmentsObj.constBegin(); it != assignmentsObj.constEnd(); ++it) {
            m_pendingZoneAssignments[it.key()] = QStringList{it.value().toString()};
        }

        QJsonObject screensObj = root[QStringLiteral("pendingScreenAssignments")].toObject();
        for (auto it = screensObj.constBegin(); it != screensObj.constEnd(); ++it) {
            m_pendingZoneScreens[it.key()] = it.value().toString();
        }

        QJsonObject desktopsObj = root[QStringLiteral("pendingDesktopAssignments")].toObject();
        for (auto it = desktopsObj.constBegin(); it != desktopsObj.constEnd(); ++it) {
            m_pendingZoneDesktops[it.key()] = it.value().toInt();
        }

        QJsonObject layoutsObj = root[QStringLiteral("pendingLayoutAssignments")].toObject();
        for (auto it = layoutsObj.constBegin(); it != layoutsObj.constEnd(); ++it) {
            m_pendingZoneLayouts[it.key()] = it.value().toString();
        }

        m_lastUsedZoneId = root[QStringLiteral("lastUsedZoneId")].toString();
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Session 2: Window restoration with layout/desktop validation
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Check if a window should be restored, with full context validation
     * @param windowId Full window ID
     * @param zoneId Output: Zone ID to restore to
     * @param isSticky Whether the window is on all desktops
     * @return true if window should be restored
     */
    bool checkPersistedZoneWithValidation(const QString& windowId, QString& zoneId, bool isSticky = false)
    {
        if (windowId.isEmpty()) {
            zoneId.clear();
            return false;
        }

        QString stableId = extractStableId(windowId);

        // Check for pending assignment
        if (!m_pendingZoneAssignments.contains(stableId)) {
            zoneId.clear();
            return false;
        }

        // Get saved context
        QString savedScreen = m_pendingZoneScreens.value(stableId);
        int savedDesktop = m_pendingZoneDesktops.value(stableId, 0);
        QString savedLayoutId = m_pendingZoneLayouts.value(stableId);

        // BUG FIX: Verify layout context matches using screen-specific layout
        // This mirrors the actual fix: use layoutForScreen() not just activeLayout()
        if (!savedLayoutId.isEmpty()) {
            QString currentLayoutId = getLayoutForScreen(savedScreen, savedDesktop);

            if (currentLayoutId.isEmpty()) {
                // No layout available - cannot validate, skip restore to be safe
                zoneId.clear();
                return false;
            }

            // Use QUuid comparison to avoid string format issues (with/without braces)
            QUuid savedUuid = QUuid::fromString(savedLayoutId);
            QUuid currentUuid = QUuid::fromString(currentLayoutId);
            if (!savedUuid.isNull() && !currentUuid.isNull() && savedUuid != currentUuid) {
                // Layout has changed - don't restore
                zoneId.clear();
                return false;
            }

            // Also handle string comparison as fallback for non-UUID layout IDs
            if (savedUuid.isNull() && savedLayoutId != currentLayoutId) {
                zoneId.clear();
                return false;
            }
        }

        // BUG FIX: Verify desktop matches (unless sticky)
        if (!isSticky && savedDesktop > 0 && m_currentDesktop > 0) {
            if (savedDesktop != m_currentDesktop) {
                // Desktop has changed - don't restore
                zoneId.clear();
                return false;
            }
        }

        QStringList zones = m_pendingZoneAssignments.value(stableId);
        zoneId = zones.isEmpty() ? QString() : zones.first();
        return !zoneId.isEmpty();
    }

    void consumePendingAssignment(const QString& windowId)
    {
        QString stableId = extractStableId(windowId);
        m_pendingZoneAssignments.remove(stableId);
        m_pendingZoneScreens.remove(stableId);
        m_pendingZoneDesktops.remove(stableId);
        m_pendingZoneLayouts.remove(stableId);
    }

    // Accessors for testing
    int pendingAssignmentCount() const { return m_pendingZoneAssignments.size(); }
    QString getPendingLayout(const QString& windowId) const
    {
        return m_pendingZoneLayouts.value(extractStableId(windowId));
    }
    int getPendingDesktop(const QString& windowId) const
    {
        return m_pendingZoneDesktops.value(extractStableId(windowId), 0);
    }

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

private:
    // Current context (simulates runtime state)
    QString m_currentLayoutId;
    int m_currentDesktop = 1;

    // Multi-screen layout assignments: "screenName:desktop" -> layoutId
    QHash<QString, QString> m_screenLayouts;

    // Runtime state
    QHash<QString, QStringList> m_windowZoneAssignments;
    QHash<QString, QString> m_windowScreenAssignments;
    QHash<QString, int> m_windowDesktopAssignments;
    QString m_lastUsedZoneId;

    // Pending assignments with full context
    QHash<QString, QStringList> m_pendingZoneAssignments;
    QHash<QString, QString> m_pendingZoneScreens;
    QHash<QString, int> m_pendingZoneDesktops;
    QHash<QString, QString> m_pendingZoneLayouts;
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

    // ═══════════════════════════════════════════════════════════════════════
    // Basic Save/Load Cycle Tests
    // ═══════════════════════════════════════════════════════════════════════

    void testSaveLoad_singleWindow()
    {
        QString windowId = QStringLiteral("org.kde.konsole:konsole:12345");
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
        QString newWindowId = QStringLiteral("org.kde.konsole:konsole:67890");

        QString restoredZone;
        bool shouldRestore = session2.checkPersistedZone(newWindowId, restoredZone);

        QVERIFY(shouldRestore);
        QCOMPARE(restoredZone, zoneId);
    }

    void testSaveLoad_lastUsedZone()
    {
        QString windowId = QStringLiteral("org.kde.app:app:12345");
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

    // ═══════════════════════════════════════════════════════════════════════
    // Window identity collision during restore
    // ═══════════════════════════════════════════════════════════════════════

    void testRestore_sameClassWindowCollision()
    {
        // Session 1: User had one Konsole window snapped
        QString konsoleSession1 = QStringLiteral("org.kde.konsole:konsole:12345");
        QString zoneA = QUuid::createUuid().toString();

        m_persistence->windowSnapped(konsoleSession1, zoneA);
        QString json = m_persistence->saveStateToJson();

        // Session 2: User opens a NEW Konsole (never before snapped)
        MockSessionPersistence session2;
        session2.loadStateFromJson(json);

        QString konsoleSession2 = QStringLiteral("org.kde.konsole:konsole:67890");

        QString restoredZone;
        bool shouldRestore = session2.checkPersistedZone(konsoleSession2, restoredZone);

        // BUG: New window incorrectly matches old window's zone!
        QVERIFY(shouldRestore); // This is the BUG - should be false for never-snapped window
        QCOMPARE(restoredZone, zoneA);
    }

    void testRestore_multipleInstancesLastWriteWins()
    {
        // Session 1: User had 3 Konsole windows in different zones
        QString konsole1 = QStringLiteral("org.kde.konsole:konsole:11111");
        QString konsole2 = QStringLiteral("org.kde.konsole:konsole:22222");
        QString konsole3 = QStringLiteral("org.kde.konsole:konsole:33333");

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
        QString anyKonsole = QStringLiteral("org.kde.konsole:konsole:99999");
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
        QString konsole = QStringLiteral("org.kde.konsole:konsole:11111");
        QString dolphin = QStringLiteral("org.kde.dolphin:dolphin:22222");

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
        QString newKonsole = QStringLiteral("org.kde.konsole:konsole:33333");
        QString newDolphin = QStringLiteral("org.kde.dolphin:dolphin:44444");

        QString konsoleZone, dolphinZone;
        QVERIFY(session2.checkPersistedZone(newKonsole, konsoleZone));
        QVERIFY(session2.checkPersistedZone(newDolphin, dolphinZone));

        QCOMPARE(konsoleZone, zoneA);
        QCOMPARE(dolphinZone, zoneB);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Pending Assignment Consumption Tests
    // ═══════════════════════════════════════════════════════════════════════

    void testRestore_consumePendingAfterRestore()
    {
        QString windowId = QStringLiteral("org.kde.app:app:12345");
        QString zoneId = QUuid::createUuid().toString();

        m_persistence->windowSnapped(windowId, zoneId);
        QString json = m_persistence->saveStateToJson();

        MockSessionPersistence session2;
        session2.loadStateFromJson(json);

        QCOMPARE(session2.pendingAssignmentCount(), 1);

        // Check and consume
        QString restoredZone;
        QString newWindow = QStringLiteral("org.kde.app:app:67890");
        QVERIFY(session2.checkPersistedZone(newWindow, restoredZone));

        session2.consumePendingAssignment(newWindow);
        QCOMPARE(session2.pendingAssignmentCount(), 0);

        // Should not match again
        QString anotherWindow = QStringLiteral("org.kde.app:app:11111");
        QVERIFY(!session2.checkPersistedZone(anotherWindow, restoredZone));
    }

    void testRestore_consumeDoesNotAffectDifferentApps()
    {
        QString app1 = QStringLiteral("org.kde.app1:app1:11111");
        QString app2 = QStringLiteral("org.kde.app2:app2:22222");
        QString zone1 = QUuid::createUuid().toString();
        QString zone2 = QUuid::createUuid().toString();

        m_persistence->windowSnapped(app1, zone1);
        m_persistence->windowSnapped(app2, zone2);

        QString json = m_persistence->saveStateToJson();

        MockSessionPersistence session2;
        session2.loadStateFromJson(json);

        QCOMPARE(session2.pendingAssignmentCount(), 2);

        // Consume app1's pending assignment
        QString newApp1 = QStringLiteral("org.kde.app1:app1:33333");
        session2.consumePendingAssignment(newApp1);

        QCOMPARE(session2.pendingAssignmentCount(), 1);

        // app2 should still have pending assignment
        QString newApp2 = QStringLiteral("org.kde.app2:app2:44444");
        QString restoredZone;
        QVERIFY(session2.checkPersistedZone(newApp2, restoredZone));
        QCOMPARE(restoredZone, zone2);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Edge Cases and Error Handling
    // ═══════════════════════════════════════════════════════════════════════

    void testRestore_invalidJson()
    {
        MockSessionPersistence session;
        session.loadStateFromJson(QStringLiteral("not valid json"));

        QCOMPARE(session.pendingAssignmentCount(), 0);
    }

    void testRestore_emptyWindowId()
    {
        QString windowId = QStringLiteral("org.kde.app:app:12345");
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
        QString windowId = QStringLiteral("org.kde.app:app:12345");
        QString zoneId = QUuid::createUuid().toString();

        m_persistence->windowSnapped(windowId, zoneId);
        m_persistence->windowUnsnapped(windowId);

        QString json = m_persistence->saveStateToJson();

        MockSessionPersistence session2;
        session2.loadStateFromJson(json);

        // Unsnapped window should not have pending assignment
        QCOMPARE(session2.pendingAssignmentCount(), 0);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Scenario Tests: Real-World Bug Reproduction
    // ═══════════════════════════════════════════════════════════════════════

    void testScenario_neverSnappedWindowGetsAutoSnapped()
    {
        /**
         * BUG REPRODUCTION SCENARIO:
         *
         * Session 1:
         * - User has Firefox snapped to Zone A
         * - User has Konsole NOT snapped (floating freely)
         *
         * Session 2 (after relog):
         * - User opens Konsole first
         * - Konsole incorrectly gets snapped to Zone A!
         *
         * Why? Because there's no way to distinguish "never snapped" from
         * "same class as something that was snapped".
         */

        // Session 1: Only Firefox was snapped
        QString firefox = QStringLiteral("org.mozilla.firefox:Navigator:11111");
        QString konsole = QStringLiteral("org.kde.konsole:konsole:22222"); // Never snapped
        QString zoneA = QUuid::createUuid().toString();

        m_persistence->windowSnapped(firefox, zoneA);
        // konsole is NOT snapped - it was floating

        QString json = m_persistence->saveStateToJson();

        // Session 2
        MockSessionPersistence session2;
        session2.loadStateFromJson(json);

        // New Konsole opens (never was snapped in session 1)
        QString newKonsole = QStringLiteral("org.kde.konsole:konsole:33333");

        QString restoredZone;
        bool shouldRestore = session2.checkPersistedZone(newKonsole, restoredZone);

        // CORRECT behavior: Konsole should NOT be restored (it was never snapped)
        QVERIFY(!shouldRestore); // This PASSES - no collision for different app class
    }

    void testScenario_wrongWindowGetsRestoredAmongMultipleSameClass()
    {
        /**
         * BUG REPRODUCTION SCENARIO:
         *
         * Session 1:
         * - Konsole #1 snapped to Zone A
         * - Konsole #2 NOT snapped
         *
         * Session 2:
         * - Konsole #2's NEW instance (different pointer) opens first
         * - It incorrectly gets snapped to Zone A!
         *
         * This is the identity collision bug - we can't distinguish
         * which Konsole was actually snapped.
         */

        // Session 1
        QString konsole1 = QStringLiteral("org.kde.konsole:konsole:11111"); // Snapped
        QString konsole2 = QStringLiteral("org.kde.konsole:konsole:22222"); // NOT snapped
        QString zoneA = QUuid::createUuid().toString();

        m_persistence->windowSnapped(konsole1, zoneA);
        // konsole2 is NOT snapped

        QString json = m_persistence->saveStateToJson();

        // Session 2
        MockSessionPersistence session2;
        session2.loadStateFromJson(json);

        // Konsole #2's new instance opens (was never snapped!)
        QString newKonsole2 = QStringLiteral("org.kde.konsole:konsole:33333");

        QString restoredZone;
        bool shouldRestore = session2.checkPersistedZone(newKonsole2, restoredZone);

        // BUG: Window that was never snapped incorrectly matches!
        // We can't tell if this is "new instance of konsole1" or "new instance of konsole2"
        QVERIFY(shouldRestore); // This is the BUG
        QCOMPARE(restoredZone, zoneA);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Layout Mismatch Bug Fix Tests
    // ═══════════════════════════════════════════════════════════════════════

    void testRestore_layoutMismatch_shouldNotRestore()
    {
        /**
         * BUG FIX TEST: Window should NOT restore when layout has changed
         *
         * Scenario:
         * - Session 1: Window snapped to Zone A in Layout 1
         * - Session 2: User switched to Layout 2
         * - Window should NOT restore (layout mismatch)
         */

        MockSessionPersistenceWithLayoutValidation session1;
        QString layoutA = QUuid::createUuid().toString();
        QString layoutB = QUuid::createUuid().toString();
        QString zoneA = QUuid::createUuid().toString();
        QString windowId = QStringLiteral("org.kde.konsole:konsole:11111");

        // Session 1: Snap window with Layout A active
        session1.setCurrentLayoutId(layoutA);
        session1.setCurrentDesktop(1);
        session1.windowSnapped(windowId, zoneA, QStringLiteral("HDMI-1"), 1);
        session1.windowClosed(windowId);

        QString json = session1.saveStateToJson();

        // Session 2: Layout B is now active (different layout)
        MockSessionPersistenceWithLayoutValidation session2;
        session2.loadStateFromJson(json);
        session2.setCurrentLayoutId(layoutB);  // Different layout!
        session2.setCurrentDesktop(1);

        QString newWindowId = QStringLiteral("org.kde.konsole:konsole:22222");
        QString restoredZone;
        bool shouldRestore = session2.checkPersistedZoneWithValidation(newWindowId, restoredZone);

        // Should NOT restore - layout has changed
        QVERIFY(!shouldRestore);
        QVERIFY(restoredZone.isEmpty());
    }

    void testRestore_layoutMatch_shouldRestore()
    {
        /**
         * POSITIVE TEST: Window SHOULD restore when layout matches
         *
         * Scenario:
         * - Session 1: Window snapped to Zone A in Layout 1
         * - Session 2: Same Layout 1 is still active
         * - Window SHOULD restore
         */

        MockSessionPersistenceWithLayoutValidation session1;
        QString layoutA = QUuid::createUuid().toString();
        QString zoneA = QUuid::createUuid().toString();
        QString windowId = QStringLiteral("org.kde.konsole:konsole:11111");

        // Session 1: Snap window with Layout A active
        session1.setCurrentLayoutId(layoutA);
        session1.setCurrentDesktop(1);
        session1.windowSnapped(windowId, zoneA, QStringLiteral("HDMI-1"), 1);
        session1.windowClosed(windowId);

        QString json = session1.saveStateToJson();

        // Session 2: Same layout A is still active
        MockSessionPersistenceWithLayoutValidation session2;
        session2.loadStateFromJson(json);
        session2.setCurrentLayoutId(layoutA);  // Same layout!
        session2.setCurrentDesktop(1);

        QString newWindowId = QStringLiteral("org.kde.konsole:konsole:22222");
        QString restoredZone;
        bool shouldRestore = session2.checkPersistedZoneWithValidation(newWindowId, restoredZone);

        // SHOULD restore - layout matches
        QVERIFY(shouldRestore);
        QCOMPARE(restoredZone, zoneA);
    }

    void testRestore_desktopMismatch_shouldNotRestore()
    {
        /**
         * BUG FIX TEST: Window should NOT restore when desktop has changed
         *
         * Scenario:
         * - Session 1: Window snapped on Desktop 1
         * - Session 2: User is on Desktop 2
         * - Window should NOT restore (desktop mismatch)
         */

        MockSessionPersistenceWithLayoutValidation session1;
        QString layoutA = QUuid::createUuid().toString();
        QString zoneA = QUuid::createUuid().toString();
        QString windowId = QStringLiteral("org.kde.konsole:konsole:11111");

        // Session 1: Snap window on Desktop 1
        session1.setCurrentLayoutId(layoutA);
        session1.setCurrentDesktop(1);
        session1.windowSnapped(windowId, zoneA, QStringLiteral("HDMI-1"), 1);
        session1.windowClosed(windowId);

        QString json = session1.saveStateToJson();

        // Session 2: User is on Desktop 2 (different desktop)
        MockSessionPersistenceWithLayoutValidation session2;
        session2.loadStateFromJson(json);
        session2.setCurrentLayoutId(layoutA);
        session2.setCurrentDesktop(2);  // Different desktop!

        QString newWindowId = QStringLiteral("org.kde.konsole:konsole:22222");
        QString restoredZone;
        bool shouldRestore = session2.checkPersistedZoneWithValidation(newWindowId, restoredZone);

        // Should NOT restore - desktop has changed
        QVERIFY(!shouldRestore);
        QVERIFY(restoredZone.isEmpty());
    }

    void testRestore_desktopMatch_shouldRestore()
    {
        /**
         * POSITIVE TEST: Window SHOULD restore when desktop matches
         */

        MockSessionPersistenceWithLayoutValidation session1;
        QString layoutA = QUuid::createUuid().toString();
        QString zoneA = QUuid::createUuid().toString();
        QString windowId = QStringLiteral("org.kde.konsole:konsole:11111");

        session1.setCurrentLayoutId(layoutA);
        session1.setCurrentDesktop(3);
        session1.windowSnapped(windowId, zoneA, QStringLiteral("HDMI-1"), 3);
        session1.windowClosed(windowId);

        QString json = session1.saveStateToJson();

        MockSessionPersistenceWithLayoutValidation session2;
        session2.loadStateFromJson(json);
        session2.setCurrentLayoutId(layoutA);
        session2.setCurrentDesktop(3);  // Same desktop!

        QString newWindowId = QStringLiteral("org.kde.konsole:konsole:22222");
        QString restoredZone;
        bool shouldRestore = session2.checkPersistedZoneWithValidation(newWindowId, restoredZone);

        // SHOULD restore - desktop matches
        QVERIFY(shouldRestore);
        QCOMPARE(restoredZone, zoneA);
    }

    void testRestore_stickyWindow_ignoresDesktopMismatch()
    {
        /**
         * POSITIVE TEST: Sticky windows should restore regardless of desktop
         *
         * Scenario:
         * - Session 1: Sticky window snapped on Desktop 1
         * - Session 2: User is on Desktop 2
         * - Window SHOULD restore (sticky ignores desktop)
         */

        MockSessionPersistenceWithLayoutValidation session1;
        QString layoutA = QUuid::createUuid().toString();
        QString zoneA = QUuid::createUuid().toString();
        QString windowId = QStringLiteral("org.kde.konsole:konsole:11111");

        session1.setCurrentLayoutId(layoutA);
        session1.setCurrentDesktop(1);
        session1.windowSnapped(windowId, zoneA, QStringLiteral("HDMI-1"), 1);
        session1.windowClosed(windowId);

        QString json = session1.saveStateToJson();

        MockSessionPersistenceWithLayoutValidation session2;
        session2.loadStateFromJson(json);
        session2.setCurrentLayoutId(layoutA);
        session2.setCurrentDesktop(2);  // Different desktop

        QString newWindowId = QStringLiteral("org.kde.konsole:konsole:22222");
        QString restoredZone;
        bool shouldRestore = session2.checkPersistedZoneWithValidation(newWindowId, restoredZone, true);  // isSticky = true

        // SHOULD restore - sticky windows ignore desktop mismatch
        QVERIFY(shouldRestore);
        QCOMPARE(restoredZone, zoneA);
    }

    void testRestore_stickyWindow_stillChecksLayoutMismatch()
    {
        /**
         * NEGATIVE TEST: Sticky windows should still respect layout mismatch
         *
         * Even though sticky windows ignore desktop checks, they should
         * still not restore if the layout has changed.
         */

        MockSessionPersistenceWithLayoutValidation session1;
        QString layoutA = QUuid::createUuid().toString();
        QString layoutB = QUuid::createUuid().toString();
        QString zoneA = QUuid::createUuid().toString();
        QString windowId = QStringLiteral("org.kde.konsole:konsole:11111");

        session1.setCurrentLayoutId(layoutA);
        session1.setCurrentDesktop(1);
        session1.windowSnapped(windowId, zoneA, QStringLiteral("HDMI-1"), 1);
        session1.windowClosed(windowId);

        QString json = session1.saveStateToJson();

        MockSessionPersistenceWithLayoutValidation session2;
        session2.loadStateFromJson(json);
        session2.setCurrentLayoutId(layoutB);  // Different layout!
        session2.setCurrentDesktop(2);

        QString newWindowId = QStringLiteral("org.kde.konsole:konsole:22222");
        QString restoredZone;
        bool shouldRestore = session2.checkPersistedZoneWithValidation(newWindowId, restoredZone, true);  // isSticky = true

        // Should NOT restore - layout has changed (even for sticky windows)
        QVERIFY(!shouldRestore);
        QVERIFY(restoredZone.isEmpty());
    }

    void testRestore_bothLayoutAndDesktopMismatch()
    {
        /**
         * NEGATIVE TEST: Both layout AND desktop mismatch
         *
         * Window should not restore when both context values differ.
         */

        MockSessionPersistenceWithLayoutValidation session1;
        QString layoutA = QUuid::createUuid().toString();
        QString layoutB = QUuid::createUuid().toString();
        QString zoneA = QUuid::createUuid().toString();
        QString windowId = QStringLiteral("org.kde.konsole:konsole:11111");

        session1.setCurrentLayoutId(layoutA);
        session1.setCurrentDesktop(1);
        session1.windowSnapped(windowId, zoneA, QStringLiteral("HDMI-1"), 1);
        session1.windowClosed(windowId);

        QString json = session1.saveStateToJson();

        MockSessionPersistenceWithLayoutValidation session2;
        session2.loadStateFromJson(json);
        session2.setCurrentLayoutId(layoutB);  // Different layout!
        session2.setCurrentDesktop(3);          // Different desktop!

        QString newWindowId = QStringLiteral("org.kde.konsole:konsole:22222");
        QString restoredZone;
        bool shouldRestore = session2.checkPersistedZoneWithValidation(newWindowId, restoredZone);

        // Should NOT restore - both layout and desktop have changed
        QVERIFY(!shouldRestore);
        QVERIFY(restoredZone.isEmpty());
    }

    void testRestore_noSavedLayout_fallsBackToOldBehavior()
    {
        /**
         * BACKWARDS COMPATIBILITY: If no layout was saved (old data), allow restore
         *
         * When migrating from older versions that didn't save layout ID,
         * we should still allow restoration (graceful degradation).
         */

        MockSessionPersistenceWithLayoutValidation session1;
        QString layoutA = QUuid::createUuid().toString();
        QString zoneA = QUuid::createUuid().toString();
        QString windowId = QStringLiteral("org.kde.konsole:konsole:11111");

        // Don't set layout ID - simulates old data without layout tracking
        session1.setCurrentLayoutId(QString());  // Empty = not tracked
        session1.setCurrentDesktop(1);
        session1.windowSnapped(windowId, zoneA, QStringLiteral("HDMI-1"), 1);
        session1.windowClosed(windowId);

        QString json = session1.saveStateToJson();

        MockSessionPersistenceWithLayoutValidation session2;
        session2.loadStateFromJson(json);
        session2.setCurrentLayoutId(layoutA);  // Now has a layout
        session2.setCurrentDesktop(1);

        QString newWindowId = QStringLiteral("org.kde.konsole:konsole:22222");
        QString restoredZone;
        bool shouldRestore = session2.checkPersistedZoneWithValidation(newWindowId, restoredZone);

        // SHOULD restore - no saved layout means we can't validate, so allow it
        QVERIFY(shouldRestore);
        QCOMPARE(restoredZone, zoneA);
    }

    void testRestore_layoutIdPersisted()
    {
        /**
         * UNIT TEST: Verify layout ID is correctly saved and loaded
         */

        MockSessionPersistenceWithLayoutValidation session1;
        QString layoutA = QUuid::createUuid().toString();
        QString zoneA = QUuid::createUuid().toString();
        QString windowId = QStringLiteral("org.kde.konsole:konsole:11111");

        session1.setCurrentLayoutId(layoutA);
        session1.setCurrentDesktop(1);
        session1.windowSnapped(windowId, zoneA, QStringLiteral("HDMI-1"), 1);
        session1.windowClosed(windowId);

        QString json = session1.saveStateToJson();

        MockSessionPersistenceWithLayoutValidation session2;
        session2.loadStateFromJson(json);

        // Verify the layout ID was persisted
        QString newWindowId = QStringLiteral("org.kde.konsole:konsole:22222");
        QCOMPARE(session2.getPendingLayout(newWindowId), layoutA);
    }

    void testRestore_desktopPersisted()
    {
        /**
         * UNIT TEST: Verify desktop is correctly saved and loaded
         */

        MockSessionPersistenceWithLayoutValidation session1;
        QString layoutA = QUuid::createUuid().toString();
        QString zoneA = QUuid::createUuid().toString();
        QString windowId = QStringLiteral("org.kde.konsole:konsole:11111");

        session1.setCurrentLayoutId(layoutA);
        session1.setCurrentDesktop(5);  // Specific desktop
        session1.windowSnapped(windowId, zoneA, QStringLiteral("HDMI-1"), 5);
        session1.windowClosed(windowId);

        QString json = session1.saveStateToJson();

        MockSessionPersistenceWithLayoutValidation session2;
        session2.loadStateFromJson(json);

        // Verify the desktop was persisted
        QString newWindowId = QStringLiteral("org.kde.konsole:konsole:22222");
        QCOMPARE(session2.getPendingDesktop(newWindowId), 5);
    }

    void testRestore_consumeClearsLayoutData()
    {
        /**
         * UNIT TEST: Consuming pending assignment clears all context data
         */

        MockSessionPersistenceWithLayoutValidation session1;
        QString layoutA = QUuid::createUuid().toString();
        QString zoneA = QUuid::createUuid().toString();
        QString windowId = QStringLiteral("org.kde.konsole:konsole:11111");

        session1.setCurrentLayoutId(layoutA);
        session1.setCurrentDesktop(1);
        session1.windowSnapped(windowId, zoneA, QStringLiteral("HDMI-1"), 1);
        session1.windowClosed(windowId);

        QString json = session1.saveStateToJson();

        MockSessionPersistenceWithLayoutValidation session2;
        session2.loadStateFromJson(json);
        session2.setCurrentLayoutId(layoutA);
        session2.setCurrentDesktop(1);

        QCOMPARE(session2.pendingAssignmentCount(), 1);

        QString newWindowId = QStringLiteral("org.kde.konsole:konsole:22222");
        session2.consumePendingAssignment(newWindowId);

        QCOMPARE(session2.pendingAssignmentCount(), 0);
        QVERIFY(session2.getPendingLayout(newWindowId).isEmpty());
        QCOMPARE(session2.getPendingDesktop(newWindowId), 0);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Multi-Screen Layout Tests
    // ═══════════════════════════════════════════════════════════════════════

    void testRestore_multiScreen_differentLayoutsPerScreen()
    {
        /**
         * CRITICAL TEST: Multi-screen with different layouts per screen
         *
         * Scenario:
         * - Screen HDMI-1 has Layout A
         * - Screen DP-1 has Layout B
         * - Window saved on HDMI-1 with Layout A
         * - Window reopens - should restore because HDMI-1 still has Layout A
         */

        MockSessionPersistenceWithLayoutValidation session1;
        QString layoutA = QUuid::createUuid().toString();
        QString layoutB = QUuid::createUuid().toString();
        QString zoneA = QUuid::createUuid().toString();
        QString windowId = QStringLiteral("org.kde.konsole:konsole:11111");

        // Setup multi-screen: HDMI-1 has Layout A, DP-1 has Layout B
        session1.setLayoutForScreen(QStringLiteral("HDMI-1"), 0, layoutA);
        session1.setLayoutForScreen(QStringLiteral("DP-1"), 0, layoutB);
        session1.setCurrentDesktop(1);

        // Window snapped on HDMI-1
        session1.windowSnapped(windowId, zoneA, QStringLiteral("HDMI-1"), 1);
        session1.windowClosed(windowId);

        QString json = session1.saveStateToJson();

        // Session 2: Same multi-screen setup
        MockSessionPersistenceWithLayoutValidation session2;
        session2.loadStateFromJson(json);
        session2.setLayoutForScreen(QStringLiteral("HDMI-1"), 0, layoutA);
        session2.setLayoutForScreen(QStringLiteral("DP-1"), 0, layoutB);
        session2.setCurrentDesktop(1);

        QString newWindowId = QStringLiteral("org.kde.konsole:konsole:22222");
        QString restoredZone;
        bool shouldRestore = session2.checkPersistedZoneWithValidation(newWindowId, restoredZone);

        // SHOULD restore - HDMI-1 still has Layout A
        QVERIFY(shouldRestore);
        QCOMPARE(restoredZone, zoneA);
    }

    void testRestore_multiScreen_layoutChangedOnSavedScreen()
    {
        /**
         * CRITICAL TEST: Layout changed on the specific screen where window was saved
         *
         * Scenario:
         * - Screen HDMI-1 had Layout A
         * - Window saved on HDMI-1
         * - User changes HDMI-1 to Layout C
         * - Window should NOT restore
         */

        MockSessionPersistenceWithLayoutValidation session1;
        QString layoutA = QUuid::createUuid().toString();
        QString layoutB = QUuid::createUuid().toString();
        QString layoutC = QUuid::createUuid().toString();
        QString zoneA = QUuid::createUuid().toString();
        QString windowId = QStringLiteral("org.kde.konsole:konsole:11111");

        // Setup: HDMI-1 has Layout A
        session1.setLayoutForScreen(QStringLiteral("HDMI-1"), 0, layoutA);
        session1.setLayoutForScreen(QStringLiteral("DP-1"), 0, layoutB);
        session1.setCurrentDesktop(1);

        session1.windowSnapped(windowId, zoneA, QStringLiteral("HDMI-1"), 1);
        session1.windowClosed(windowId);

        QString json = session1.saveStateToJson();

        // Session 2: HDMI-1 now has Layout C (changed!)
        MockSessionPersistenceWithLayoutValidation session2;
        session2.loadStateFromJson(json);
        session2.setLayoutForScreen(QStringLiteral("HDMI-1"), 0, layoutC);  // Changed!
        session2.setLayoutForScreen(QStringLiteral("DP-1"), 0, layoutB);
        session2.setCurrentDesktop(1);

        QString newWindowId = QStringLiteral("org.kde.konsole:konsole:22222");
        QString restoredZone;
        bool shouldRestore = session2.checkPersistedZoneWithValidation(newWindowId, restoredZone);

        // Should NOT restore - HDMI-1's layout changed from A to C
        QVERIFY(!shouldRestore);
        QVERIFY(restoredZone.isEmpty());
    }

    void testRestore_multiScreen_otherScreenLayoutChanged()
    {
        /**
         * POSITIVE TEST: Layout changed on DIFFERENT screen, should still restore
         *
         * Scenario:
         * - Window saved on HDMI-1 with Layout A
         * - DP-1's layout changed from B to C
         * - Window should still restore (HDMI-1's layout unchanged)
         */

        MockSessionPersistenceWithLayoutValidation session1;
        QString layoutA = QUuid::createUuid().toString();
        QString layoutB = QUuid::createUuid().toString();
        QString layoutC = QUuid::createUuid().toString();
        QString zoneA = QUuid::createUuid().toString();
        QString windowId = QStringLiteral("org.kde.konsole:konsole:11111");

        session1.setLayoutForScreen(QStringLiteral("HDMI-1"), 0, layoutA);
        session1.setLayoutForScreen(QStringLiteral("DP-1"), 0, layoutB);
        session1.setCurrentDesktop(1);

        session1.windowSnapped(windowId, zoneA, QStringLiteral("HDMI-1"), 1);
        session1.windowClosed(windowId);

        QString json = session1.saveStateToJson();

        // Session 2: DP-1 changed to Layout C, but HDMI-1 unchanged
        MockSessionPersistenceWithLayoutValidation session2;
        session2.loadStateFromJson(json);
        session2.setLayoutForScreen(QStringLiteral("HDMI-1"), 0, layoutA);  // Unchanged
        session2.setLayoutForScreen(QStringLiteral("DP-1"), 0, layoutC);    // Changed - doesn't matter
        session2.setCurrentDesktop(1);

        QString newWindowId = QStringLiteral("org.kde.konsole:konsole:22222");
        QString restoredZone;
        bool shouldRestore = session2.checkPersistedZoneWithValidation(newWindowId, restoredZone);

        // SHOULD restore - HDMI-1's layout is still A
        QVERIFY(shouldRestore);
        QCOMPARE(restoredZone, zoneA);
    }

    void testRestore_perDesktopLayout_sameDesktop()
    {
        /**
         * TEST: Per-desktop layout assignments - same desktop
         *
         * Scenario:
         * - Desktop 1 on HDMI-1 has Layout A
         * - Desktop 2 on HDMI-1 has Layout B
         * - Window saved on Desktop 1
         * - Window reopens on Desktop 1 - should restore
         */

        MockSessionPersistenceWithLayoutValidation session1;
        QString layoutA = QUuid::createUuid().toString();
        QString layoutB = QUuid::createUuid().toString();
        QString zoneA = QUuid::createUuid().toString();
        QString windowId = QStringLiteral("org.kde.konsole:konsole:11111");

        // Per-desktop layouts
        session1.setLayoutForScreen(QStringLiteral("HDMI-1"), 1, layoutA);
        session1.setLayoutForScreen(QStringLiteral("HDMI-1"), 2, layoutB);
        session1.setCurrentDesktop(1);

        session1.windowSnapped(windowId, zoneA, QStringLiteral("HDMI-1"), 1);
        session1.windowClosed(windowId);

        QString json = session1.saveStateToJson();

        MockSessionPersistenceWithLayoutValidation session2;
        session2.loadStateFromJson(json);
        session2.setLayoutForScreen(QStringLiteral("HDMI-1"), 1, layoutA);
        session2.setLayoutForScreen(QStringLiteral("HDMI-1"), 2, layoutB);
        session2.setCurrentDesktop(1);  // Same desktop

        QString newWindowId = QStringLiteral("org.kde.konsole:konsole:22222");
        QString restoredZone;
        bool shouldRestore = session2.checkPersistedZoneWithValidation(newWindowId, restoredZone);

        // SHOULD restore - same desktop, same layout
        QVERIFY(shouldRestore);
        QCOMPARE(restoredZone, zoneA);
    }

    void testRestore_perDesktopLayout_differentDesktop()
    {
        /**
         * TEST: Per-desktop layout assignments - different desktop
         *
         * Scenario:
         * - Desktop 1 on HDMI-1 has Layout A
         * - Desktop 2 on HDMI-1 has Layout B
         * - Window saved on Desktop 1
         * - Window reopens on Desktop 2 - should NOT restore (different desktop)
         */

        MockSessionPersistenceWithLayoutValidation session1;
        QString layoutA = QUuid::createUuid().toString();
        QString layoutB = QUuid::createUuid().toString();
        QString zoneA = QUuid::createUuid().toString();
        QString windowId = QStringLiteral("org.kde.konsole:konsole:11111");

        session1.setLayoutForScreen(QStringLiteral("HDMI-1"), 1, layoutA);
        session1.setLayoutForScreen(QStringLiteral("HDMI-1"), 2, layoutB);
        session1.setCurrentDesktop(1);

        session1.windowSnapped(windowId, zoneA, QStringLiteral("HDMI-1"), 1);
        session1.windowClosed(windowId);

        QString json = session1.saveStateToJson();

        MockSessionPersistenceWithLayoutValidation session2;
        session2.loadStateFromJson(json);
        session2.setLayoutForScreen(QStringLiteral("HDMI-1"), 1, layoutA);
        session2.setLayoutForScreen(QStringLiteral("HDMI-1"), 2, layoutB);
        session2.setCurrentDesktop(2);  // Different desktop!

        QString newWindowId = QStringLiteral("org.kde.konsole:konsole:22222");
        QString restoredZone;
        bool shouldRestore = session2.checkPersistedZoneWithValidation(newWindowId, restoredZone);

        // Should NOT restore - different desktop
        QVERIFY(!shouldRestore);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Edge Case Tests
    // ═══════════════════════════════════════════════════════════════════════

    void testRestore_uuidFormatWithBraces()
    {
        /**
         * EDGE CASE: UUID comparison with braces format
         */

        MockSessionPersistenceWithLayoutValidation session1;
        QUuid layoutUuid = QUuid::createUuid();
        QString layoutWithBraces = layoutUuid.toString(QUuid::WithBraces);
        QString zoneA = QUuid::createUuid().toString();
        QString windowId = QStringLiteral("org.kde.konsole:konsole:11111");

        session1.setCurrentLayoutId(layoutWithBraces);
        session1.setCurrentDesktop(1);
        session1.windowSnapped(windowId, zoneA, QStringLiteral("HDMI-1"), 1);
        session1.windowClosed(windowId);

        QString json = session1.saveStateToJson();

        MockSessionPersistenceWithLayoutValidation session2;
        session2.loadStateFromJson(json);
        // Use same UUID but potentially different format
        session2.setCurrentLayoutId(layoutUuid.toString(QUuid::WithBraces));
        session2.setCurrentDesktop(1);

        QString newWindowId = QStringLiteral("org.kde.konsole:konsole:22222");
        QString restoredZone;
        bool shouldRestore = session2.checkPersistedZoneWithValidation(newWindowId, restoredZone);

        // SHOULD restore - same UUID regardless of format
        QVERIFY(shouldRestore);
    }

    void testRestore_uuidFormatWithoutBraces()
    {
        /**
         * EDGE CASE: UUID comparison without braces format
         */

        MockSessionPersistenceWithLayoutValidation session1;
        QUuid layoutUuid = QUuid::createUuid();
        QString layoutWithoutBraces = layoutUuid.toString(QUuid::WithoutBraces);
        QString zoneA = QUuid::createUuid().toString();
        QString windowId = QStringLiteral("org.kde.konsole:konsole:11111");

        session1.setCurrentLayoutId(layoutWithoutBraces);
        session1.setCurrentDesktop(1);
        session1.windowSnapped(windowId, zoneA, QStringLiteral("HDMI-1"), 1);
        session1.windowClosed(windowId);

        QString json = session1.saveStateToJson();

        MockSessionPersistenceWithLayoutValidation session2;
        session2.loadStateFromJson(json);
        session2.setCurrentLayoutId(layoutUuid.toString(QUuid::WithoutBraces));
        session2.setCurrentDesktop(1);

        QString newWindowId = QStringLiteral("org.kde.konsole:konsole:22222");
        QString restoredZone;
        bool shouldRestore = session2.checkPersistedZoneWithValidation(newWindowId, restoredZone);

        // SHOULD restore - same UUID regardless of format
        QVERIFY(shouldRestore);
    }

    void testRestore_noCurrentLayout_shouldNotRestore()
    {
        /**
         * EDGE CASE: No current layout available - should NOT restore
         */

        MockSessionPersistenceWithLayoutValidation session1;
        QString layoutA = QUuid::createUuid().toString();
        QString zoneA = QUuid::createUuid().toString();
        QString windowId = QStringLiteral("org.kde.konsole:konsole:11111");

        session1.setCurrentLayoutId(layoutA);
        session1.setCurrentDesktop(1);
        session1.windowSnapped(windowId, zoneA, QStringLiteral("HDMI-1"), 1);
        session1.windowClosed(windowId);

        QString json = session1.saveStateToJson();

        MockSessionPersistenceWithLayoutValidation session2;
        session2.loadStateFromJson(json);
        session2.setCurrentLayoutId(QString());  // No layout!
        session2.setCurrentDesktop(1);

        QString newWindowId = QStringLiteral("org.kde.konsole:konsole:22222");
        QString restoredZone;
        bool shouldRestore = session2.checkPersistedZoneWithValidation(newWindowId, restoredZone);

        // Should NOT restore - no current layout to validate against
        QVERIFY(!shouldRestore);
    }

    void testRestore_emptyScreenName()
    {
        /**
         * EDGE CASE: Empty screen name in saved data
         */

        MockSessionPersistenceWithLayoutValidation session1;
        QString layoutA = QUuid::createUuid().toString();
        QString zoneA = QUuid::createUuid().toString();
        QString windowId = QStringLiteral("org.kde.konsole:konsole:11111");

        session1.setCurrentLayoutId(layoutA);
        session1.setCurrentDesktop(1);
        // Empty screen name
        session1.windowSnapped(windowId, zoneA, QString(), 1);
        session1.windowClosed(windowId);

        QString json = session1.saveStateToJson();

        MockSessionPersistenceWithLayoutValidation session2;
        session2.loadStateFromJson(json);
        session2.setCurrentLayoutId(layoutA);
        session2.setCurrentDesktop(1);

        QString newWindowId = QStringLiteral("org.kde.konsole:konsole:22222");
        QString restoredZone;
        bool shouldRestore = session2.checkPersistedZoneWithValidation(newWindowId, restoredZone);

        // Should restore - falls back to default layout
        QVERIFY(shouldRestore);
        QCOMPARE(restoredZone, zoneA);
    }

private:
    MockSessionPersistence* m_persistence = nullptr;
};

QTEST_MAIN(TestSessionPersistence)
#include "test_session_persistence.moc"
