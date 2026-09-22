// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// QML facade test for phosphor-service-clipboard. Proves the imperative
// registration produces a usable QML module: a real QQmlEngine imports
// Phosphor.Service.Clipboard 1.0, instantiates a ClipboardService (whose ctor is
// side-effect-free beyond loading the on-disk history), reads its supported /
// history / count surface, and drives its invokables. XDG_DATA_HOME is pointed at
// an isolated build-tree directory (see tests/CMakeLists.txt) so the service
// never reads or writes real user clipboard history. The compositor-driven
// capture / copy path needs a live session and is exercised via the CLI demo.

#include <PhosphorServiceClipboard/QmlRegistration.h>

#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTest>

#include <memory>

using namespace PhosphorServiceClipboard;

namespace {
constexpr auto kQml = R"(
import QtQml
import Phosphor.Service.Clipboard 1.0

QtObject {
    id: root
    property ClipboardService service: ClipboardService {}
    readonly property bool serviceSupported: root.service.supported
    readonly property var historyModel: root.service.history
    readonly property int historyCount: root.service.count
}
)";
} // namespace

class ClipboardQmlFacadeTest : public QObject
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
        component.setData(kQml, QUrl(QStringLiteral("qrc:/test_clipboard_facade.qml")));
        return std::unique_ptr<QObject>(component.create());
    }
};

void ClipboardQmlFacadeTest::moduleLoadsAndServiceBinds()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    std::unique_ptr<QObject> root = create(component);
    QVERIFY2(root != nullptr, qPrintable(component.errorString())); // module + types resolved

    // Under the offscreen platform there is no compositor, so the service is
    // unsupported. The history model resolves through QML and the count is a
    // valid non-negative int (the isolated XDG dir starts empty).
    QCOMPARE(root->property("serviceSupported").toBool(), false);
    QVERIFY(root->property("historyModel").value<QObject*>() != nullptr);
    QVERIFY(root->property("historyCount").toInt() >= 0);
}

void ClipboardQmlFacadeTest::invokablesAreCallable()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    std::unique_ptr<QObject> root = create(component);
    QVERIFY2(root != nullptr, qPrintable(component.errorString()));

    QObject* service = qvariant_cast<QObject*>(root->property("service"));
    QVERIFY(service != nullptr);

    // Out-of-range copy/remove are safe no-ops; clear() empties the (isolated)
    // history, which is deterministic regardless of the starting state.
    QVERIFY(QMetaObject::invokeMethod(service, "remove", Q_ARG(int, 999)));
    QVERIFY(QMetaObject::invokeMethod(service, "copy", Q_ARG(int, 999)));
    QVERIFY(QMetaObject::invokeMethod(service, "clear"));
    QCOMPARE(service->property("count").toInt(), 0);
}

QTEST_GUILESS_MAIN(ClipboardQmlFacadeTest)
#include "test_qmlfacade.moc"
