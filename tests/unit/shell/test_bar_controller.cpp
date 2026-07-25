// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for the bar's registry owner and its generic QML-component
// factory. These live in the top-level GPL test tree rather than in
// libs/phosphor-shell-bar/tests because the classes under test ship in the
// GPL shell binary (src/shell), not in the LGPL bar module.
//
// The QML delegates themselves are covered by the QuickTest cases in
// libs/phosphor-shell-bar/tests; what matters here is the C++ contract:
// which ids are registered, the ordering handed to QML, and every guard on
// the createWidget path (a wrong guard silently drops a widget from the
// bar, or leaks one).

#include "shell/BarController.h"
#include "shell/QmlComponentBarWidgetFactory.h"

#include <QQmlEngine>
#include <QQuickItem>
#include <QRegularExpression>
#include <QStringList>
#include <QTest>

using namespace PhosphorShellApp;

class TestBarController : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void registersEveryBuiltin();
    void factoryIdsAreSorted();
    void unknownIdYieldsNull();
    void nullParentYieldsNull();
    void factoryRejectsNullEngine();
    void factoryRejectsNullParent();
};

void TestBarController::registersEveryBuiltin()
{
    BarController controller;
    const QStringList ids = controller.factoryIds();

    // The shipped catalogue, hand-written rather than derived from
    // builtinWidgets() so that adding or dropping a widget has to be a
    // deliberate edit in two places.
    const QStringList expected{
        QStringLiteral("audio"), QStringLiteral("battery"),       QStringLiteral("bluetooth"),
        QStringLiteral("clock"), QStringLiteral("controlcenter"), QStringLiteral("focusedapp"),
        QStringLiteral("media"), QStringLiteral("network"),       QStringLiteral("notification"),
        QStringLiteral("power"), QStringLiteral("spacer"),        QStringLiteral("systemmetrics"),
        QStringLiteral("tray"),  QStringLiteral("workspaces"),
    };

    QCOMPARE(ids.size(), expected.size());
    QCOMPARE(ids, expected);
    // Workspaces was held back through Phase 4.1 for want of a data source
    // and landed with the Workspaces QML singleton behind it. Asserted by
    // name because its absence was previously pinned here, and a silent
    // regression to that state would otherwise read as an ordinary count
    // change.
    QVERIFY(ids.contains(QStringLiteral("workspaces")));
}

void TestBarController::factoryIdsAreSorted()
{
    // Sorted so any future introspection consumer (a layout or config
    // editor) enumerates a stable order. The shipped bar does NOT read this
    // list: BarHost drives its slots from explicit ordered *Groups lists.
    BarController controller;
    QStringList ids = controller.factoryIds();
    QStringList sorted = ids;
    sorted.sort();
    QCOMPARE(ids, sorted);
}

void TestBarController::unknownIdYieldsNull()
{
    BarController controller;
    QQmlEngine engine;
    QQuickItem parent;
    QQmlEngine::setContextForObject(&parent, engine.rootContext());

    QCOMPARE(controller.createWidgetFor(QStringLiteral("nope"), &parent), nullptr);
}

void TestBarController::nullParentYieldsNull()
{
    // The engine is resolved from the parent, so a null parent has to be
    // refused rather than dereferenced.
    BarController controller;
    QCOMPARE(controller.createWidgetFor(QStringLiteral("clock"), nullptr), nullptr);
}

void TestBarController::factoryRejectsNullEngine()
{
    QmlComponentBarWidgetFactory factory(QStringLiteral("clock"), QStringLiteral("Clock"), BarController::moduleUri(),
                                         QStringLiteral("Clock"));
    QQuickItem parent;
    QCOMPARE(factory.createWidget(nullptr, &parent), nullptr);
}

void TestBarController::factoryRejectsNullParent()
{
    // Without a parent the item would have no owner at all: CppOwnership
    // plus no QObject parent means nothing would ever delete it.
    //
    // Assert the null-parent guard specifically, not just the null return.
    // This binary does not register the Phosphor.Bar module, so a missing
    // guard would still yield null via a component error — the test would
    // pass while the leak it exists to prevent went unnoticed. Matching the
    // message pins which branch ran.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("null parent for")));

    QmlComponentBarWidgetFactory factory(QStringLiteral("clock"), QStringLiteral("Clock"), BarController::moduleUri(),
                                         QStringLiteral("Clock"));
    QQmlEngine engine;
    QCOMPARE(factory.createWidget(&engine, nullptr), nullptr);
}

QTEST_MAIN(TestBarController)

#include "test_bar_controller.moc"
