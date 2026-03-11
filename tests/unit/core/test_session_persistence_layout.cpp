// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/// @file test_session_persistence_layout.cpp
/// @brief Tests for session persistence with layout/desktop validation, multi-screen, UUID edge cases

#include <QTest>
#include <QString>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#include "../../../src/core/utils.h"

/// Mock with layout and desktop validation for restore logic testing
class MockLayoutPersistence : public QObject
{
    Q_OBJECT
public:
    explicit MockLayoutPersistence(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    void setCurrentLayoutId(const QString& layoutId)
    {
        m_currentLayoutId = layoutId;
    }
    void setCurrentDesktop(int desktop)
    {
        m_currentDesktop = desktop;
    }

    void setLayoutForScreen(const QString& screenName, int desktop, const QString& layoutId)
    {
        m_screenLayouts[screenName + QStringLiteral(":") + QString::number(desktop)] = layoutId;
    }

    QString getLayoutForScreen(const QString& screenName, int desktop) const
    {
        QString key = screenName + QStringLiteral(":") + QString::number(desktop);
        if (m_screenLayouts.contains(key))
            return m_screenLayouts.value(key);
        key = screenName + QStringLiteral(":0");
        if (m_screenLayouts.contains(key))
            return m_screenLayouts.value(key);
        return m_currentLayoutId;
    }

    /// Helper: setup session-1 (set context, snap, close, return JSON)
    QString setupSession1(const QString& layoutId, int desktop, const QString& windowId, const QString& zoneId,
                          const QString& screenName = QStringLiteral("HDMI-1"))
    {
        setCurrentLayoutId(layoutId);
        setCurrentDesktop(desktop);
        windowSnapped(windowId, zoneId, screenName, desktop);
        windowClosed(windowId);
        return saveStateToJson();
    }

    void windowSnapped(const QString& windowId, const QString& zoneId, const QString& screenName = QString(),
                       int desktop = 1)
    {
        if (windowId.isEmpty() || zoneId.isEmpty())
            return;
        m_windowZones[windowId] = QStringList{zoneId};
        m_windowScreens[windowId] = screenName;
        m_windowDesktops[windowId] = desktop;
    }

    void windowClosed(const QString& windowId)
    {
        if (windowId.isEmpty())
            return;
        QString sid = PlasmaZones::Utils::extractAppId(windowId);
        QStringList zoneIds = m_windowZones.value(windowId);
        QString zoneId = zoneIds.isEmpty() ? QString() : zoneIds.first();
        if (!zoneId.isEmpty()) {
            QString screen = m_windowScreens.value(windowId);
            int desk = m_windowDesktops.value(windowId, m_currentDesktop);
            m_pendingZones[sid] = QStringList{zoneId};
            m_pendingScreens[sid] = screen;
            m_pendingDesktops[sid] = desk;
            m_pendingLayouts[sid] = getLayoutForScreen(screen, desk);
        }
        m_windowZones.remove(windowId);
        m_windowScreens.remove(windowId);
        m_windowDesktops.remove(windowId);
    }

    QString saveStateToJson()
    {
        QJsonObject root;
        QJsonObject a, s, d, l;
        for (auto it = m_pendingZones.constBegin(); it != m_pendingZones.constEnd(); ++it)
            a[it.key()] = it.value().isEmpty() ? QString() : it.value().first();
        for (auto it = m_pendingScreens.constBegin(); it != m_pendingScreens.constEnd(); ++it)
            s[it.key()] = it.value();
        for (auto it = m_pendingDesktops.constBegin(); it != m_pendingDesktops.constEnd(); ++it)
            d[it.key()] = it.value();
        for (auto it = m_pendingLayouts.constBegin(); it != m_pendingLayouts.constEnd(); ++it)
            l[it.key()] = it.value();
        root[QStringLiteral("zones")] = a;
        root[QStringLiteral("screens")] = s;
        root[QStringLiteral("desktops")] = d;
        root[QStringLiteral("layouts")] = l;
        return QString::fromUtf8(QJsonDocument(root).toJson());
    }

    void loadStateFromJson(const QString& json)
    {
        m_windowZones.clear();
        m_pendingZones.clear();
        m_pendingScreens.clear();
        m_pendingDesktops.clear();
        m_pendingLayouts.clear();
        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        if (!doc.isObject())
            return;
        QJsonObject root = doc.object();
        // Store each sub-object in a local variable before iterating.
        // Calling .toObject() twice creates two different temporaries,
        // yielding iterators from different containers (undefined behavior).
        const QJsonObject zonesObj = root[QStringLiteral("zones")].toObject();
        for (auto it = zonesObj.constBegin(); it != zonesObj.constEnd(); ++it)
            m_pendingZones[it.key()] = QStringList{it.value().toString()};
        const QJsonObject screensObj = root[QStringLiteral("screens")].toObject();
        for (auto it = screensObj.constBegin(); it != screensObj.constEnd(); ++it)
            m_pendingScreens[it.key()] = it.value().toString();
        const QJsonObject desktopsObj = root[QStringLiteral("desktops")].toObject();
        for (auto it = desktopsObj.constBegin(); it != desktopsObj.constEnd(); ++it)
            m_pendingDesktops[it.key()] = it.value().toInt();
        const QJsonObject layoutsObj = root[QStringLiteral("layouts")].toObject();
        for (auto it = layoutsObj.constBegin(); it != layoutsObj.constEnd(); ++it)
            m_pendingLayouts[it.key()] = it.value().toString();
    }

    bool checkRestore(const QString& windowId, QString& zoneId, bool isSticky = false)
    {
        if (windowId.isEmpty()) {
            zoneId.clear();
            return false;
        }
        QString sid = PlasmaZones::Utils::extractAppId(windowId);
        if (!m_pendingZones.contains(sid)) {
            zoneId.clear();
            return false;
        }
        QString savedScreen = m_pendingScreens.value(sid);
        int savedDesktop = m_pendingDesktops.value(sid, 0);
        QString savedLayout = m_pendingLayouts.value(sid);
        if (!savedLayout.isEmpty()) {
            QString curLayout = getLayoutForScreen(savedScreen, savedDesktop);
            if (curLayout.isEmpty()) {
                zoneId.clear();
                return false;
            }
            QUuid sU = QUuid::fromString(savedLayout), cU = QUuid::fromString(curLayout);
            if (!sU.isNull() && !cU.isNull() && sU != cU) {
                zoneId.clear();
                return false;
            }
            if (sU.isNull() && savedLayout != curLayout) {
                zoneId.clear();
                return false;
            }
        }
        if (!isSticky && savedDesktop > 0 && m_currentDesktop > 0 && savedDesktop != m_currentDesktop) {
            zoneId.clear();
            return false;
        }
        QStringList zones = m_pendingZones.value(sid);
        zoneId = zones.isEmpty() ? QString() : zones.first();
        return !zoneId.isEmpty();
    }

    void consumePending(const QString& windowId)
    {
        QString sid = PlasmaZones::Utils::extractAppId(windowId);
        m_pendingZones.remove(sid);
        m_pendingScreens.remove(sid);
        m_pendingDesktops.remove(sid);
        m_pendingLayouts.remove(sid);
    }

    int pendingCount() const
    {
        return m_pendingZones.size();
    }
    QString pendingLayout(const QString& wid) const
    {
        return m_pendingLayouts.value(PlasmaZones::Utils::extractAppId(wid));
    }
    int pendingDesktop(const QString& wid) const
    {
        return m_pendingDesktops.value(PlasmaZones::Utils::extractAppId(wid), 0);
    }

private:
    QString m_currentLayoutId;
    int m_currentDesktop = 1;
    QHash<QString, QString> m_screenLayouts;
    QHash<QString, QStringList> m_windowZones;
    QHash<QString, QString> m_windowScreens;
    QHash<QString, int> m_windowDesktops;
    QHash<QString, QStringList> m_pendingZones;
    QHash<QString, QString> m_pendingScreens;
    QHash<QString, int> m_pendingDesktops;
    QHash<QString, QString> m_pendingLayouts;
};

class TestSessionPersistenceLayout : public QObject
{
    Q_OBJECT

private:
    // Convenience constants
    static inline const QString kWin1 = QStringLiteral("org.kde.konsole|11111");
    static inline const QString kWin2 = QStringLiteral("org.kde.konsole|22222");

private Q_SLOTS:

    // --- Layout mismatch ---

    void testRestore_layoutMismatch_shouldNotRestore()
    {
        MockLayoutPersistence s1;
        QString lA = QUuid::createUuid().toString(), lB = QUuid::createUuid().toString();
        QString zA = QUuid::createUuid().toString();
        QString json = s1.setupSession1(lA, 1, kWin1, zA);

        MockLayoutPersistence s2;
        s2.loadStateFromJson(json);
        s2.setCurrentLayoutId(lB);
        s2.setCurrentDesktop(1);
        QString zone;
        QVERIFY(!s2.checkRestore(kWin2, zone));
        QVERIFY(zone.isEmpty());
    }

    void testRestore_layoutMatch_shouldRestore()
    {
        MockLayoutPersistence s1;
        QString lA = QUuid::createUuid().toString(), zA = QUuid::createUuid().toString();
        QString json = s1.setupSession1(lA, 1, kWin1, zA);

        MockLayoutPersistence s2;
        s2.loadStateFromJson(json);
        s2.setCurrentLayoutId(lA);
        s2.setCurrentDesktop(1);
        QString zone;
        QVERIFY(s2.checkRestore(kWin2, zone));
        QCOMPARE(zone, zA);
    }

    void testRestore_desktopMismatch_shouldNotRestore()
    {
        MockLayoutPersistence s1;
        QString lA = QUuid::createUuid().toString(), zA = QUuid::createUuid().toString();
        QString json = s1.setupSession1(lA, 1, kWin1, zA);

        MockLayoutPersistence s2;
        s2.loadStateFromJson(json);
        s2.setCurrentLayoutId(lA);
        s2.setCurrentDesktop(2);
        QString zone;
        QVERIFY(!s2.checkRestore(kWin2, zone));
    }

    void testRestore_desktopMatch_shouldRestore()
    {
        MockLayoutPersistence s1;
        QString lA = QUuid::createUuid().toString(), zA = QUuid::createUuid().toString();
        QString json = s1.setupSession1(lA, 3, kWin1, zA);

        MockLayoutPersistence s2;
        s2.loadStateFromJson(json);
        s2.setCurrentLayoutId(lA);
        s2.setCurrentDesktop(3);
        QString zone;
        QVERIFY(s2.checkRestore(kWin2, zone));
        QCOMPARE(zone, zA);
    }

    // --- Sticky windows ---

    void testRestore_stickyWindow_ignoresDesktopMismatch()
    {
        MockLayoutPersistence s1;
        QString lA = QUuid::createUuid().toString(), zA = QUuid::createUuid().toString();
        QString json = s1.setupSession1(lA, 1, kWin1, zA);

        MockLayoutPersistence s2;
        s2.loadStateFromJson(json);
        s2.setCurrentLayoutId(lA);
        s2.setCurrentDesktop(2);
        QString zone;
        QVERIFY(s2.checkRestore(kWin2, zone, true));
        QCOMPARE(zone, zA);
    }

    void testRestore_stickyWindow_stillChecksLayoutMismatch()
    {
        MockLayoutPersistence s1;
        QString lA = QUuid::createUuid().toString(), lB = QUuid::createUuid().toString();
        QString zA = QUuid::createUuid().toString();
        QString json = s1.setupSession1(lA, 1, kWin1, zA);

        MockLayoutPersistence s2;
        s2.loadStateFromJson(json);
        s2.setCurrentLayoutId(lB);
        s2.setCurrentDesktop(2);
        QString zone;
        QVERIFY(!s2.checkRestore(kWin2, zone, true));
    }

    void testRestore_bothLayoutAndDesktopMismatch()
    {
        MockLayoutPersistence s1;
        QString lA = QUuid::createUuid().toString(), lB = QUuid::createUuid().toString();
        QString zA = QUuid::createUuid().toString();
        QString json = s1.setupSession1(lA, 1, kWin1, zA);

        MockLayoutPersistence s2;
        s2.loadStateFromJson(json);
        s2.setCurrentLayoutId(lB);
        s2.setCurrentDesktop(3);
        QString zone;
        QVERIFY(!s2.checkRestore(kWin2, zone));
    }

    // --- Backwards compat and persistence checks ---

    void testRestore_noSavedLayout_fallsBackToOldBehavior()
    {
        MockLayoutPersistence s1;
        QString lA = QUuid::createUuid().toString(), zA = QUuid::createUuid().toString();
        QString json = s1.setupSession1(QString(), 1, kWin1, zA);

        MockLayoutPersistence s2;
        s2.loadStateFromJson(json);
        s2.setCurrentLayoutId(lA);
        s2.setCurrentDesktop(1);
        QString zone;
        QVERIFY(s2.checkRestore(kWin2, zone));
        QCOMPARE(zone, zA);
    }

    void testRestore_layoutIdPersisted()
    {
        MockLayoutPersistence s1;
        QString lA = QUuid::createUuid().toString(), zA = QUuid::createUuid().toString();
        QString json = s1.setupSession1(lA, 1, kWin1, zA);
        MockLayoutPersistence s2;
        s2.loadStateFromJson(json);
        QCOMPARE(s2.pendingLayout(kWin2), lA);
    }

    void testRestore_desktopPersisted()
    {
        MockLayoutPersistence s1;
        QString lA = QUuid::createUuid().toString(), zA = QUuid::createUuid().toString();
        QString json = s1.setupSession1(lA, 5, kWin1, zA);
        MockLayoutPersistence s2;
        s2.loadStateFromJson(json);
        QCOMPARE(s2.pendingDesktop(kWin2), 5);
    }

    void testRestore_consumeClearsLayoutData()
    {
        MockLayoutPersistence s1;
        QString lA = QUuid::createUuid().toString(), zA = QUuid::createUuid().toString();
        QString json = s1.setupSession1(lA, 1, kWin1, zA);

        MockLayoutPersistence s2;
        s2.loadStateFromJson(json);
        s2.setCurrentLayoutId(lA);
        s2.setCurrentDesktop(1);
        QCOMPARE(s2.pendingCount(), 1);
        s2.consumePending(kWin2);
        QCOMPARE(s2.pendingCount(), 0);
        QVERIFY(s2.pendingLayout(kWin2).isEmpty());
        QCOMPARE(s2.pendingDesktop(kWin2), 0);
    }

    // --- Multi-Screen ---

    void testRestore_multiScreen_differentLayoutsPerScreen()
    {
        MockLayoutPersistence s1;
        QString lA = QUuid::createUuid().toString(), lB = QUuid::createUuid().toString();
        QString zA = QUuid::createUuid().toString();
        s1.setLayoutForScreen(QStringLiteral("HDMI-1"), 0, lA);
        s1.setLayoutForScreen(QStringLiteral("DP-1"), 0, lB);
        QString json = s1.setupSession1(QString(), 1, kWin1, zA);

        MockLayoutPersistence s2;
        s2.loadStateFromJson(json);
        s2.setLayoutForScreen(QStringLiteral("HDMI-1"), 0, lA);
        s2.setLayoutForScreen(QStringLiteral("DP-1"), 0, lB);
        s2.setCurrentDesktop(1);
        QString zone;
        QVERIFY(s2.checkRestore(kWin2, zone));
        QCOMPARE(zone, zA);
    }

    void testRestore_multiScreen_layoutChangedOnSavedScreen()
    {
        MockLayoutPersistence s1;
        QString lA = QUuid::createUuid().toString(), lB = QUuid::createUuid().toString();
        QString lC = QUuid::createUuid().toString(), zA = QUuid::createUuid().toString();
        s1.setLayoutForScreen(QStringLiteral("HDMI-1"), 0, lA);
        s1.setLayoutForScreen(QStringLiteral("DP-1"), 0, lB);
        QString json = s1.setupSession1(QString(), 1, kWin1, zA);

        MockLayoutPersistence s2;
        s2.loadStateFromJson(json);
        s2.setLayoutForScreen(QStringLiteral("HDMI-1"), 0, lC);
        s2.setLayoutForScreen(QStringLiteral("DP-1"), 0, lB);
        s2.setCurrentDesktop(1);
        QString zone;
        QVERIFY(!s2.checkRestore(kWin2, zone));
    }

    void testRestore_multiScreen_otherScreenLayoutChanged()
    {
        MockLayoutPersistence s1;
        QString lA = QUuid::createUuid().toString(), lB = QUuid::createUuid().toString();
        QString lC = QUuid::createUuid().toString(), zA = QUuid::createUuid().toString();
        s1.setLayoutForScreen(QStringLiteral("HDMI-1"), 0, lA);
        s1.setLayoutForScreen(QStringLiteral("DP-1"), 0, lB);
        QString json = s1.setupSession1(QString(), 1, kWin1, zA);

        MockLayoutPersistence s2;
        s2.loadStateFromJson(json);
        s2.setLayoutForScreen(QStringLiteral("HDMI-1"), 0, lA);
        s2.setLayoutForScreen(QStringLiteral("DP-1"), 0, lC);
        s2.setCurrentDesktop(1);
        QString zone;
        QVERIFY(s2.checkRestore(kWin2, zone));
        QCOMPARE(zone, zA);
    }

    // --- Per-desktop layouts ---

    void testRestore_perDesktopLayout_sameDesktop()
    {
        MockLayoutPersistence s1;
        QString lA = QUuid::createUuid().toString(), lB = QUuid::createUuid().toString();
        QString zA = QUuid::createUuid().toString();
        s1.setLayoutForScreen(QStringLiteral("HDMI-1"), 1, lA);
        s1.setLayoutForScreen(QStringLiteral("HDMI-1"), 2, lB);
        QString json = s1.setupSession1(QString(), 1, kWin1, zA);

        MockLayoutPersistence s2;
        s2.loadStateFromJson(json);
        s2.setLayoutForScreen(QStringLiteral("HDMI-1"), 1, lA);
        s2.setLayoutForScreen(QStringLiteral("HDMI-1"), 2, lB);
        s2.setCurrentDesktop(1);
        QString zone;
        QVERIFY(s2.checkRestore(kWin2, zone));
        QCOMPARE(zone, zA);
    }

    void testRestore_perDesktopLayout_differentDesktop()
    {
        MockLayoutPersistence s1;
        QString lA = QUuid::createUuid().toString(), lB = QUuid::createUuid().toString();
        QString zA = QUuid::createUuid().toString();
        s1.setLayoutForScreen(QStringLiteral("HDMI-1"), 1, lA);
        s1.setLayoutForScreen(QStringLiteral("HDMI-1"), 2, lB);
        QString json = s1.setupSession1(QString(), 1, kWin1, zA);

        MockLayoutPersistence s2;
        s2.loadStateFromJson(json);
        s2.setLayoutForScreen(QStringLiteral("HDMI-1"), 1, lA);
        s2.setLayoutForScreen(QStringLiteral("HDMI-1"), 2, lB);
        s2.setCurrentDesktop(2);
        QString zone;
        QVERIFY(!s2.checkRestore(kWin2, zone));
    }

    // --- UUID format edge cases ---

    void testRestore_uuidFormatWithBraces()
    {
        MockLayoutPersistence s1;
        QUuid lu = QUuid::createUuid();
        QString zA = QUuid::createUuid().toString();
        QString json = s1.setupSession1(lu.toString(QUuid::WithBraces), 1, kWin1, zA);

        MockLayoutPersistence s2;
        s2.loadStateFromJson(json);
        s2.setCurrentLayoutId(lu.toString(QUuid::WithBraces));
        s2.setCurrentDesktop(1);
        QString zone;
        QVERIFY(s2.checkRestore(kWin2, zone));
    }

    void testRestore_uuidFormatWithoutBraces()
    {
        MockLayoutPersistence s1;
        QUuid lu = QUuid::createUuid();
        QString zA = QUuid::createUuid().toString();
        QString json = s1.setupSession1(lu.toString(QUuid::WithoutBraces), 1, kWin1, zA);

        MockLayoutPersistence s2;
        s2.loadStateFromJson(json);
        s2.setCurrentLayoutId(lu.toString(QUuid::WithoutBraces));
        s2.setCurrentDesktop(1);
        QString zone;
        QVERIFY(s2.checkRestore(kWin2, zone));
    }

    void testRestore_noCurrentLayout_shouldNotRestore()
    {
        MockLayoutPersistence s1;
        QString lA = QUuid::createUuid().toString(), zA = QUuid::createUuid().toString();
        QString json = s1.setupSession1(lA, 1, kWin1, zA);

        MockLayoutPersistence s2;
        s2.loadStateFromJson(json);
        s2.setCurrentLayoutId(QString());
        s2.setCurrentDesktop(1);
        QString zone;
        QVERIFY(!s2.checkRestore(kWin2, zone));
    }

    void testRestore_emptyScreenName()
    {
        MockLayoutPersistence s1;
        QString lA = QUuid::createUuid().toString(), zA = QUuid::createUuid().toString();
        s1.setCurrentLayoutId(lA);
        s1.setCurrentDesktop(1);
        s1.windowSnapped(kWin1, zA, QString(), 1);
        s1.windowClosed(kWin1);
        QString json = s1.saveStateToJson();

        MockLayoutPersistence s2;
        s2.loadStateFromJson(json);
        s2.setCurrentLayoutId(lA);
        s2.setCurrentDesktop(1);
        QString zone;
        QVERIFY(s2.checkRestore(kWin2, zone));
        QCOMPARE(zone, zA);
    }
};

QTEST_MAIN(TestSessionPersistenceLayout)
#include "test_session_persistence_layout.moc"
