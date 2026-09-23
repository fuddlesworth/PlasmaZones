// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// QML facade test for phosphor-service-lock. Proves the imperative registration
// produces a usable QML module: a real QQmlEngine imports Phosphor.Service.Lock
// 1.0, instantiates a LockService, reads its supported / state / locked surface,
// and drives its invokables. With no live compositor (offscreen QPA) the service
// is unsupported; the compositor-driven lock path is exercised by the state
// machine test and the CLI demo.

#include <PhosphorServiceLock/LockService.h>
#include <PhosphorServiceLock/QmlRegistration.h>

#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTest>

#include <memory>

using namespace PhosphorServiceLock;
using State = LockService::State;

namespace {
constexpr auto kQml = R"(
import QtQml
import Phosphor.Service.Lock 1.0

QtObject {
    id: root
    property LockService service: LockService {}
    readonly property bool serviceSupported: root.service.supported
    readonly property int serviceState: root.service.state
    readonly property bool serviceLocked: root.service.locked
}
)";

int n(State s)
{
    return static_cast<int>(s);
}
} // namespace

class LockQmlFacadeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        registerQmlTypes();
    }
    void moduleLoadsAndServiceBinds();
    void invokablesAreCallable();

private:
    static std::unique_ptr<QObject> create(QQmlComponent& component)
    {
        component.setData(kQml, QUrl(QStringLiteral("qrc:/test_lock_facade.qml")));
        return std::unique_ptr<QObject>(component.create());
    }
};

void LockQmlFacadeTest::moduleLoadsAndServiceBinds()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    std::unique_ptr<QObject> root = create(component);
    QVERIFY2(root != nullptr, qPrintable(component.errorString())); // module + types resolved

    // Under the offscreen platform there is no compositor, so the service is
    // unsupported and starts in the Unlocked state.
    QCOMPARE(root->property("serviceSupported").toBool(), false);
    QCOMPARE(root->property("serviceState").toInt(), n(State::Unlocked));
    QCOMPARE(root->property("serviceLocked").toBool(), false);
}

void LockQmlFacadeTest::invokablesAreCallable()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    std::unique_ptr<QObject> root = create(component);
    QVERIFY2(root != nullptr, qPrintable(component.errorString()));

    QObject* service = qvariant_cast<QObject*>(root->property("service"));
    QVERIFY(service != nullptr);

    // unlock() before the session is locked is a safe no-op: no authentication
    // is attempted and the state is unchanged.
    QVERIFY(QMetaObject::invokeMethod(service, "unlock", Q_ARG(QString, QStringLiteral("irrelevant"))));
    QCOMPARE(service->property("state").toInt(), n(State::Unlocked));
    QCOMPARE(service->property("locked").toBool(), false);

    // lock() requests a lock; with no compositor to reply, the service settles in
    // Locking (and is not yet "locked", which only holds from Locked onward).
    QVERIFY(QMetaObject::invokeMethod(service, "lock"));
    QCOMPARE(service->property("state").toInt(), n(State::Locking));
    QCOMPARE(service->property("locked").toBool(), false);
}

QTEST_GUILESS_MAIN(LockQmlFacadeTest)
#include "test_qmlfacade.moc"
