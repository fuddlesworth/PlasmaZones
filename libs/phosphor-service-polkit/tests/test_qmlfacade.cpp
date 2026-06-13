// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// QML facade test for phosphor-service-polkit (milestone 5). Proves the
// imperative registration produces a usable QML module: a real QQmlEngine
// imports Phosphor.Service.Polkit 1.0, instantiates a PolkitAgent (whose ctor
// is side-effect-free, so this does NOT register as the session agent), and
// reads its `registered` and `activeRequest` properties through QML.
//
// Instantiating PolkitAgent in QML is safe precisely because registration is
// explicit (registerAgent()); a bare `PolkitAgent {}` never touches polkitd.

#include <PhosphorServicePolkit/QmlRegistration.h>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QTest>

#include <memory>

using namespace PhosphorServicePolkit;

namespace {
constexpr auto kQml = R"(
import QtQml
import Phosphor.Service.Polkit 1.0

QtObject {
    id: root
    property PolkitAgent agent: PolkitAgent {}
    readonly property bool agentRegistered: root.agent.registered
    readonly property bool hasActiveRequest: root.agent.activeRequest !== null
}
)";
} // namespace

class PolkitQmlFacadeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        registerQmlTypes();
    }
    void moduleLoadsAndAgentBinds();
};

void PolkitQmlFacadeTest::moduleLoadsAndAgentBinds()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(kQml, QUrl(QStringLiteral("qrc:/test_polkit_facade.qml")));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root != nullptr, qPrintable(component.errorString())); // module + types resolved

    // A bare agent has not registered and has no active request: confirms both
    // properties are readable through QML and that activeRequest is null on a
    // bare agent. Binding a live AuthRequest needs polkitd, exercised via the CLI
    // demo against pkexec.
    QCOMPARE(root->property("agentRegistered").toBool(), false);
    QCOMPARE(root->property("hasActiveRequest").toBool(), false);
}

QTEST_GUILESS_MAIN(PolkitQmlFacadeTest)
#include "test_qmlfacade.moc"
