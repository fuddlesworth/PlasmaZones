// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_dbus_adaptor_routing.cpp
 * @brief End-to-end D-Bus routing test for typed-struct slot dispatch.
 *
 * Regression guard for the 2026-04-10 resnap crash. Root cause: slots written
 * as `void slot(const WindowOpenedList&)` inside `namespace PlasmaZones` were
 * recorded by moc with the unqualified type name "WindowOpenedList", but the
 * metatype was registered via `Q_DECLARE_METATYPE(PhosphorProtocol::WindowOpenedList)`
 * under the qualified name "PhosphorProtocol::WindowOpenedList". QDBusAbstractAdaptor
 * dispatch couldn't resolve the type → "Could not find slot ..." at runtime →
 * kwin_wayland downstream crash with "demarshalling function for type 'QString'
 * failed".
 *
 * The structural fix is that every typed-struct slot parameter in the adaptor
 * headers is now fully-qualified (e.g. `const PhosphorProtocol::WindowOpenedList&`).
 * This test registers an adaptor on a private QDBusConnection and verifies
 * that a method call with a typed-struct payload is actually dispatched to
 * the slot. If a future adaptor regresses by using an unqualified slot
 * parameter, this test will fail (or the alias registration in
 * registerDBusTypes() will save it — both layers are exercised here).
 */

#include <QTest>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusAbstractAdaptor>
#include <QSignalSpy>
#include <QEventLoop>
#include <QTimer>
#include <QUuid>
#include <QVariant>

#include <PhosphorProtocol/WireTypes.h>

using namespace PhosphorProtocol;

// =========================================================================
// Minimal owner object that hosts an adaptor on a private connection.
// We build a dedicated tiny adaptor here rather than reuse the real
// AutotileAdaptor so we don't drag in the daemon dependency tree.
// =========================================================================

class TestTypedAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.test.TypedRouting")

public:
    explicit TestTypedAdaptor(QObject* parent)
        : QDBusAbstractAdaptor(parent)
    {
    }

    // All slots use FULLY-QUALIFIED types exactly the way the real adaptors
    // are expected to declare them. If someone later writes these without
    // the `PlasmaZones::` prefix, the test will start failing.
public Q_SLOTS:
    void takeWindowOpenedList(const PhosphorProtocol::WindowOpenedList& entries)
    {
        m_lastReceivedCount = entries.size();
        if (!entries.isEmpty()) {
            m_lastFirstWindowId = entries.first().windowId;
            m_lastFirstMinWidth = entries.first().minWidth;
        }
        Q_EMIT received();
    }

    PhosphorProtocol::MoveTargetResult produceMoveTarget()
    {
        PhosphorProtocol::MoveTargetResult r;
        r.success = true;
        r.reason = QStringLiteral("ok");
        r.zoneId = QStringLiteral("{zone-uuid}");
        r.x = 100;
        r.y = 200;
        r.width = 300;
        r.height = 400;
        r.sourceZoneId = QStringLiteral("{src-uuid}");
        r.screenName = QStringLiteral("screen-0");
        return r;
    }

Q_SIGNALS:
    void received();

public:
    int m_lastReceivedCount = -1;
    QString m_lastFirstWindowId;
    int m_lastFirstMinWidth = -1;
};

// =========================================================================
// Test fixture
// =========================================================================

class TestDBusAdaptorRouting : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        // Must be called before any adaptors are constructed.
        PhosphorProtocol::registerWireTypes();

        // Each test run gets its own bus name on the session bus so tests
        // can run in parallel without stepping on each other. We use a UUID
        // suffix so the name is unique per invocation.
        m_serviceName =
            QStringLiteral("org.plasmazones.test.Routing_") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_objectPath = QStringLiteral("/Routing");

        m_bus = QDBusConnection::sessionBus();
        if (!m_bus.isConnected()) {
            QSKIP("No session bus available (expected in some CI environments).");
        }

        QVERIFY2(m_bus.registerService(m_serviceName), qPrintable(m_bus.lastError().message()));

        m_owner = new QObject();
        m_adaptor = new TestTypedAdaptor(m_owner);
        // ExportAdaptors is required so that child QDBusAbstractAdaptor
        // instances are actually exposed on the bus. ExportAllContents alone
        // does not pick them up.
        QVERIFY2(m_bus.registerObject(m_objectPath, m_owner, QDBusConnection::ExportAdaptors),
                 qPrintable(m_bus.lastError().message()));
    }

    void cleanupTestCase()
    {
        if (m_bus.isConnected()) {
            m_bus.unregisterObject(m_objectPath);
            m_bus.unregisterService(m_serviceName);
        }
        delete m_owner;
        m_owner = nullptr;
        m_adaptor = nullptr;
    }

    // ─────────────────────────────────────────────────────────────────────
    // Primary regression test: an incoming method call with a typed struct
    // array must actually dispatch to the slot. Before the fix, this call
    // would produce "Could not find slot ..." in the logs and return an
    // "Unknown method" D-Bus error.
    // ─────────────────────────────────────────────────────────────────────
    void slotWithTypedStructArrayIsDispatched()
    {
        QVERIFY(m_bus.isConnected());

        PhosphorProtocol::WindowOpenedList entries;
        PhosphorProtocol::WindowOpenedEntry e1{QStringLiteral("firefox|abc"), QStringLiteral("screen-0"), 640, 480};
        PhosphorProtocol::WindowOpenedEntry e2{QStringLiteral("konsole|def"), QStringLiteral("screen-0"), 320, 200};
        entries << e1 << e2;

        QDBusMessage msg = QDBusMessage::createMethodCall(m_serviceName, m_objectPath,
                                                          QStringLiteral("org.plasmazones.test.TypedRouting"),
                                                          QStringLiteral("takeWindowOpenedList"));
        msg << QVariant::fromValue(entries);

        QSignalSpy spy(m_adaptor, &TestTypedAdaptor::received);
        QDBusPendingCall pending = m_bus.asyncCall(msg);
        QDBusPendingCallWatcher watcher(pending);

        QEventLoop loop;
        connect(&watcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(2000, &loop, &QEventLoop::quit);
        loop.exec();

        QVERIFY2(watcher.isFinished(), "D-Bus call did not complete within 2s");
        QVERIFY2(!watcher.isError(), qPrintable(watcher.error().message()));
        QCOMPARE(spy.count(), 1);

        QCOMPARE(m_adaptor->m_lastReceivedCount, 2);
        QCOMPARE(m_adaptor->m_lastFirstWindowId, QStringLiteral("firefox|abc"));
        QCOMPARE(m_adaptor->m_lastFirstMinWidth, 640);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Typed-struct return value must also round-trip correctly.
    // ─────────────────────────────────────────────────────────────────────
    void methodReturningTypedStructRoundtrips()
    {
        QVERIFY(m_bus.isConnected());

        QDBusMessage msg = QDBusMessage::createMethodCall(m_serviceName, m_objectPath,
                                                          QStringLiteral("org.plasmazones.test.TypedRouting"),
                                                          QStringLiteral("produceMoveTarget"));

        QDBusPendingCall pending = m_bus.asyncCall(msg);
        QDBusPendingCallWatcher watcher(pending);

        QEventLoop loop;
        connect(&watcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(2000, &loop, &QEventLoop::quit);
        loop.exec();

        QVERIFY2(watcher.isFinished(), "D-Bus call did not complete within 2s");
        QVERIFY2(!watcher.isError(), qPrintable(watcher.error().message()));

        QDBusPendingReply<PhosphorProtocol::MoveTargetResult> reply(pending);
        QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));

        const PhosphorProtocol::MoveTargetResult r = reply.value();
        QCOMPARE(r.success, true);
        QCOMPARE(r.reason, QStringLiteral("ok"));
        QCOMPARE(r.zoneId, QStringLiteral("{zone-uuid}"));
        QCOMPARE(r.x, 100);
        QCOMPARE(r.y, 200);
        QCOMPARE(r.width, 300);
        QCOMPARE(r.height, 400);
        QCOMPARE(r.sourceZoneId, QStringLiteral("{src-uuid}"));
        QCOMPARE(r.screenName, QStringLiteral("screen-0"));
    }

    // ─────────────────────────────────────────────────────────────────────
    // Sanity: metatype is registered under BOTH the qualified and
    // unqualified names. The authoritative fix is to fully-qualify slot
    // parameters, but `registerDBusTypes()` also registers an unqualified
    // alias as belt-and-suspenders. This test asserts the alias exists so
    // nobody accidentally removes it.
    // ─────────────────────────────────────────────────────────────────────
    void metatypesAreRegisteredUnderBothNames()
    {
        QVERIFY(QMetaType::fromName("PhosphorProtocol::WindowOpenedList").isValid());
        QVERIFY(QMetaType::fromName("WindowOpenedList").isValid());
        QCOMPARE(QMetaType::fromName("PhosphorProtocol::WindowOpenedList").id(),
                 QMetaType::fromName("WindowOpenedList").id());

        QVERIFY(QMetaType::fromName("PhosphorProtocol::MoveTargetResult").isValid());
        QVERIFY(QMetaType::fromName("MoveTargetResult").isValid());

        QVERIFY(QMetaType::fromName("PhosphorProtocol::WindowStateEntry").isValid());
        QVERIFY(QMetaType::fromName("WindowStateEntry").isValid());
    }

private:
    QDBusConnection m_bus{QStringLiteral("")};
    QString m_serviceName;
    QString m_objectPath;
    QObject* m_owner = nullptr;
    TestTypedAdaptor* m_adaptor = nullptr;
};

QTEST_MAIN(TestDBusAdaptorRouting)
#include "test_dbus_adaptor_routing.moc"
