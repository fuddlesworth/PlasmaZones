// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// QML facade test for phosphor-service-session. Proves the imperative
// registration produces a usable QML module: a real QQmlEngine imports
// Phosphor.Service.Session 1.0, instantiates a SessionHost, reads its
// capability + interactive surface, and drives the one safe invokable.
//
// SessionHost's QML (default) constructor talks to the live system-bus logind,
// so the capability values are environment-dependent and the destructive power
// actions (suspend / powerOff / reboot / ...) are deliberately NOT invoked here:
// they would act on the host running the test. Only logout(), a pure signal with
// no logind call, is driven. The action-routing and handshake logic is covered
// deterministically against a fake logind Manager in the M8 tests.
//
// Side effect to be aware of: when a live logind is present, simply constructing
// the SessionHost (which this test does, via the QML default ctor) takes the
// real delay (sleep) and block (handle-power/suspend/hibernate/lid keys)
// inhibitors for the lifetime of the object, i.e. the test briefly owns those
// keys. They are released on destruction. This is unavoidable with the default
// ctor; the fake-logind M8 tests inject a bogus service and so take no real
// inhibitors.

#include <PhosphorServiceSession/QmlRegistration.h>
#include <PhosphorServiceSession/SessionHost.h>

#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace PhosphorServiceSession;

namespace {
constexpr auto kQml = R"(
import QtQml
import Phosphor.Service.Session 1.0

QtObject {
    id: root
    property SessionHost host: SessionHost {}
    readonly property int canSuspend: root.host.canSuspend
    readonly property bool interactiveFlag: root.host.interactive
}
)";
} // namespace

class SessionQmlFacadeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        registerQmlTypes();
    }
    void moduleLoadsAndHostBinds();
    void logoutInvokableEmitsSafely();

private:
    static std::unique_ptr<QObject> create(QQmlComponent& component)
    {
        component.setData(kQml, QUrl(QStringLiteral("qrc:/test_session_facade.qml")));
        return std::unique_ptr<QObject>(component.create());
    }
};

void SessionQmlFacadeTest::moduleLoadsAndHostBinds()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    std::unique_ptr<QObject> root = create(component);
    QVERIFY2(root != nullptr, qPrintable(component.errorString())); // module + types resolved

    // interactive defaults true on the QML host (auth routes through our 2.6
    // polkit agent).
    QCOMPARE(root->property("interactiveFlag").toBool(), true);

    // canSuspend exposes the Availability enum as an int. The value depends on
    // whether a live logind answered (Unknown without one, Yes/No/... with), so
    // assert only that it is a valid enum member, never a specific availability.
    const int cap = root->property("canSuspend").toInt();
    QVERIFY(cap >= static_cast<int>(SessionHost::Availability::Unknown));
    QVERIFY(cap <= static_cast<int>(SessionHost::Availability::Challenge));
}

void SessionQmlFacadeTest::logoutInvokableEmitsSafely()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    std::unique_ptr<QObject> root = create(component);
    QVERIFY2(root != nullptr, qPrintable(component.errorString()));

    QObject* host = qvariant_cast<QObject*>(root->property("host"));
    QVERIFY(host != nullptr);

    // logout() is a pure signal (no logind call), so it is safe to drive against
    // a possibly-live system bus; it must reach the shell as logoutRequested().
    QSignalSpy spy(host, SIGNAL(logoutRequested()));
    QVERIFY(QMetaObject::invokeMethod(host, "logout"));
    QCOMPARE(spy.count(), 1);
}

QTEST_GUILESS_MAIN(SessionQmlFacadeTest)
#include "test_qmlfacade.moc"
