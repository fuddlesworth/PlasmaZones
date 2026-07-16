// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_dbus_layout_service_replies.cpp
 * @brief DBusLayoutService reports the daemon's answer, not the reply's type.
 *
 * The LayoutRegistry methods the editor calls are declared with return values
 * that carry the outcome: updateLayout answers bool, getLayout answers the
 * layout document or an empty string. A refusal travels back as an ordinary
 * ReplyMessage, so a client that only checks QDBusMessage::type() reads every
 * refusal as a success.
 *
 * For updateLayout that is data loss. EditorController::saveLayout takes the
 * returned bool as proof the write landed: on true it clears
 * m_hasUnsavedChanges, marks the undo stack clean and emits layoutSaved, so the
 * editor shows a saved layout, offers no unsaved-changes prompt on close, and
 * the user's work is gone with the process. The daemon refuses a payload for
 * ordinary reasons (the schema gate, an id it does not hold, an autotile id
 * with an empty algorithm key), and each of those refusals is a ReplyMessage.
 *
 * This test stands a stub LayoutRegistry on the session bus under the daemon's
 * own name and drives the real client against it, because the reply parsing is
 * the thing under test and nothing below the bus can exercise it.
 */

#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusError>
#include <QSignalSpy>
#include <QTest>

#include <PhosphorProtocol/ServiceConstants.h>

#include "../../../src/editor/services/DBusLayoutService.h"

// =========================================================================
// Stub LayoutRegistry. Only the two methods under test are exposed, and each
// answers whatever the test told it to, so the client sees a well-formed
// ReplyMessage carrying a refusal — exactly what the daemon sends.
// =========================================================================

class StubLayoutRegistry : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.LayoutRegistry")

public:
    explicit StubLayoutRegistry(QObject* parent)
        : QDBusAbstractAdaptor(parent)
    {
    }

    bool updateLayoutResult = true;
    QString getLayoutResult = QStringLiteral("{}");

public Q_SLOTS:
    bool updateLayout(const QString& layoutJson)
    {
        Q_UNUSED(layoutJson)
        return updateLayoutResult;
    }

    QString getLayout(const QString& id)
    {
        Q_UNUSED(id)
        return getLayoutResult;
    }
};

class TestDBusLayoutServiceReplies : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        m_bus = QDBusConnection::sessionBus();
        if (!m_bus.isConnected()) {
            QSKIP("No session bus available.");
        }
        // The client hard-binds the daemon's name through
        // ClientHelpers::daemonClient(), so the stub has to answer to that name
        // and there is no seam to point it elsewhere. A running daemon already
        // owns it — run this suite under dbus-run-session.
        if (!m_bus.registerService(PhosphorProtocol::Service::Name)) {
            QSKIP(
                "org.plasmazones is already owned (a daemon is running). "
                "Run this test under dbus-run-session.");
        }

        m_owner = new QObject();
        m_stub = new StubLayoutRegistry(m_owner);
        QVERIFY2(m_bus.registerObject(PhosphorProtocol::Service::ObjectPath, m_owner, QDBusConnection::ExportAdaptors),
                 qPrintable(m_bus.lastError().message()));
    }

    void cleanupTestCase()
    {
        if (m_bus.isConnected()) {
            m_bus.unregisterObject(PhosphorProtocol::Service::ObjectPath);
            m_bus.unregisterService(PhosphorProtocol::Service::Name);
        }
        delete m_owner;
        m_owner = nullptr;
        m_stub = nullptr;
    }

    // The regression. A refused save must come back false and must tell the
    // user, or EditorController::saveLayout declares the layout saved.
    void updateLayoutReportsDaemonRefusal()
    {
        m_stub->updateLayoutResult = false;

        PlasmaZones::DBusLayoutService service;
        QSignalSpy errors(&service, &PlasmaZones::ILayoutService::errorOccurred);

        QVERIFY2(!service.updateLayout(QStringLiteral("{\"id\":\"x\"}")),
                 "The daemon answered false. Returning true here clears the editor's unsaved-changes flag and "
                 "undo stack over a write that never landed.");
        QCOMPARE(errors.count(), 1);
        QVERIFY(!errors.constFirst().constFirst().toString().isEmpty());
    }

    // The other half of the contract: a real success must still read as one,
    // so the guard above cannot be satisfied by refusing everything.
    void updateLayoutReportsDaemonAcceptance()
    {
        m_stub->updateLayoutResult = true;

        PlasmaZones::DBusLayoutService service;
        QSignalSpy errors(&service, &PlasmaZones::ILayoutService::errorOccurred);

        QVERIFY(service.updateLayout(QStringLiteral("{\"id\":\"x\"}")));
        QCOMPARE(errors.count(), 0);
    }

    // getLayout answers an empty document for a layout it does not hold.
    // EditorController::loadLayout reads the empty return as failure and stays
    // quiet because this method is supposed to have reported it.
    void loadLayoutReportsMissingLayout()
    {
        m_stub->getLayoutResult = QString();

        PlasmaZones::DBusLayoutService service;
        QSignalSpy errors(&service, &PlasmaZones::ILayoutService::errorOccurred);

        QVERIFY(service.loadLayout(QStringLiteral("no-such-layout")).isEmpty());
        QCOMPARE(errors.count(), 1);
    }

    void loadLayoutReturnsDocument()
    {
        m_stub->getLayoutResult = QStringLiteral("{\"id\":\"x\"}");

        PlasmaZones::DBusLayoutService service;
        QSignalSpy errors(&service, &PlasmaZones::ILayoutService::errorOccurred);

        QCOMPARE(service.loadLayout(QStringLiteral("x")), QStringLiteral("{\"id\":\"x\"}"));
        QCOMPARE(errors.count(), 0);
    }

private:
    QDBusConnection m_bus = QDBusConnection::sessionBus();
    QObject* m_owner = nullptr;
    StubLayoutRegistry* m_stub = nullptr;
};

QTEST_MAIN(TestDBusLayoutServiceReplies)
#include "test_dbus_layout_service_replies.moc"
