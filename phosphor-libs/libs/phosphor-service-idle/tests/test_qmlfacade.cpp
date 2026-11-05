// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// QML facade test for phosphor-service-idle. Proves the imperative registration
// produces a usable QML module: a real QQmlEngine imports Phosphor.Service.Idle
// 1.0, instantiates an IdleService (whose ctor is side-effect-free, so this does
// NOT register with any compositor), assigns its stages, reads its properties,
// and calls its inhibit / release invokables through the metaobject. The
// compositor-driven idle advance needs a live session and is exercised via the
// CLI demo.

#include <PhosphorServiceIdle/QmlRegistration.h>

#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTest>

#include <memory>

using namespace PhosphorServiceIdle;

namespace {
constexpr auto kQml = R"(
import QtQml
import Phosphor.Service.Idle 1.0

QtObject {
    id: root
    property IdleService service: IdleService {
        stages: [ { "name": "dim", "timeoutMs": 300000 } ]
    }
    readonly property bool serviceSupported: root.service.supported
    readonly property bool serviceIdle: root.service.idle
    readonly property bool serviceInhibited: root.service.inhibited
    readonly property int stageCount: root.service.stages.length
}
)";
} // namespace

class IdleQmlFacadeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        registerQmlTypes();
    }
    void moduleLoadsAndServiceBinds();
    void inhibitInvokableFromQml();

private:
    static std::unique_ptr<QObject> create(QQmlComponent& component)
    {
        component.setData(kQml, QUrl(QStringLiteral("qrc:/test_idle_facade.qml")));
        return std::unique_ptr<QObject>(component.create());
    }
};

void IdleQmlFacadeTest::moduleLoadsAndServiceBinds()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    std::unique_ptr<QObject> root = create(component);
    QVERIFY2(root != nullptr, qPrintable(component.errorString())); // module + types resolved

    // With no compositor (guiless QCoreApplication), supported / idle /
    // inhibited read false. The stages assigned in QML round-trip (one valid
    // stage), confirming the QVariantList property is writable from QML.
    QCOMPARE(root->property("serviceSupported").toBool(), false);
    QCOMPARE(root->property("serviceIdle").toBool(), false);
    QCOMPARE(root->property("serviceInhibited").toBool(), false);
    QCOMPARE(root->property("stageCount").toInt(), 1);
}

void IdleQmlFacadeTest::inhibitInvokableFromQml()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    std::unique_ptr<QObject> root = create(component);
    QVERIFY2(root != nullptr, qPrintable(component.errorString()));

    QObject* service = qvariant_cast<QObject*>(root->property("service"));
    QVERIFY(service != nullptr);

    // inhibit() / release() are Q_INVOKABLE; call them via the metaobject to
    // confirm the QML-facing surface is wired.
    int cookie = -1;
    QVERIFY(QMetaObject::invokeMethod(service, "inhibit", Q_RETURN_ARG(int, cookie)));
    QVERIFY(cookie > 0);
    QCOMPARE(service->property("inhibited").toBool(), true);

    bool released = false;
    QVERIFY(QMetaObject::invokeMethod(service, "release", Q_RETURN_ARG(bool, released), Q_ARG(int, cookie)));
    QVERIFY(released);
    QCOMPARE(service->property("inhibited").toBool(), false);
}

QTEST_GUILESS_MAIN(IdleQmlFacadeTest)
#include "test_qmlfacade.moc"
