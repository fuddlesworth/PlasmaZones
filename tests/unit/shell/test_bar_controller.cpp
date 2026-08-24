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
#include <QSignalSpy>
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
    void relaysActivationWithItsRegistryId();
    void activationWithoutAnIdIsRefused();
    void aDynamicPropertyWriteReportsFailureButStoresTheValue();
};

namespace {

// Stand-in for a trigger delegate. The shipped ones (PowerButton and the
// rest) cannot be instantiated in a unit-test binary because they import
// Phosphor.Shell and the Phosphor.Service.* modules, which only the shell
// process registers, so createWidgetFor cannot produce a real widget here.
//
// That bounds what these two cases pin: the RELAY (id recovery from the
// sender, the empty-id refusal, and the two-argument payload), wired the same
// string-based way createWidgetFor wires it. The duck-typing branch inside
// createWidgetFor — that a widget WITHOUT an `activated()` signal is left
// unconnected — is not covered here and needs a binary that can build a real
// delegate.
class FakeTriggerWidget : public QQuickItem
{
    Q_OBJECT

Q_SIGNALS:
    void activated();
};

} // namespace

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

void TestBarController::relaysActivationWithItsRegistryId()
{
    // QQuickItem* is not a metatype QSignalSpy can marshal on its own here,
    // and without this the source argument reads back as null and the
    // assertion below would fail for a reason that has nothing to do with
    // the relay.
    qRegisterMetaType<QQuickItem*>("QQuickItem*");

    BarController controller;
    FakeTriggerWidget widget;
    // Exactly what createWidgetFor does after the duck-type check.
    widget.setProperty("_barWidgetId", QStringLiteral("power"));
    QObject::connect(&widget, SIGNAL(activated()), &controller, SLOT(relayWidgetActivation()));

    QSignalSpy spy(&controller, &BarController::widgetActivated);
    Q_EMIT widget.activated();

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("power"));
    // The source travels with the id so a multi-monitor composer can tell
    // which bar's button fired.
    QCOMPARE(spy.first().at(1).value<QQuickItem*>(), &widget);
}

void TestBarController::activationWithoutAnIdIsRefused()
{
    // Reachable only if a delegate declares its own property of that name and
    // rejects the write. The relay must stay silent on the wire and loud in
    // the log, rather than emitting an empty id the composer would ignore.
    BarController controller;
    FakeTriggerWidget widget;
    QObject::connect(&widget, SIGNAL(activated()), &controller, SLOT(relayWidgetActivation()));

    QSignalSpy spy(&controller, &BarController::widgetActivated);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("carries no registry id")));
    Q_EMIT widget.activated();

    QCOMPARE(spy.count(), 0);
}

void TestBarController::aDynamicPropertyWriteReportsFailureButStoresTheValue()
{
    // Pins the Qt behaviour that createWidgetFor's id stamp depends on, and
    // that a "hardening" change once got backwards with the whole bar as the
    // blast radius.
    //
    // QObject::setProperty returns TRUE only for a declared Q_PROPERTY. For
    // any other name it stores a DYNAMIC property and returns FALSE. So a
    // false return here means "stored dynamically", not "rejected", and
    // treating it as failure skips the activated() connect for every widget —
    // which is inert bar buttons, with nothing in the log.
    //
    // `_barWidgetId` is dynamic by design (no bar delegate declares it), so
    // the only correct verification is a READ-BACK.
    FakeTriggerWidget widget;
    const QString id = QStringLiteral("power");

    QVERIFY2(!widget.setProperty("_barWidgetId", id),
             "Qt started returning true for a dynamic property write; "
             "revisit createWidgetFor, which is written around this returning false");
    QCOMPARE(widget.property("_barWidgetId").toString(), id);
}

QTEST_MAIN(TestBarController)

#include "test_bar_controller.moc"
