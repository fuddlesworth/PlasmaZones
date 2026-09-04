// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ControlCenterController — the shell's control-center registry owner and
// the target-screen resolver the socket transport relies on.
//
// The screenOf ownership case is the one that matters. A QScreen has no
// QObject parent, and a Q_INVOKABLE returning a parentless QObject* hands
// QML JavaScriptOwnership by default, which lets the JS garbage collector
// DELETE the live screen. That took the shell down five times on
// 2026-09-03: on hot reload (ScreenModel dereferenced the freed screen) and,
// with different GC timing, mid-IPC-toggle. The controller must mark the
// screen CppOwnership before returning it, and this pins that it does.

#include "shell/ControlCenterController.h"

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopedPointer>
#include <QScreen>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>
#include <QVariant>

using PhosphorShellApp::ControlCenterController;

namespace {

// A bare QQmlEngine has not imported QtQuick, so it cannot resolve
// `QQuickItem*` as an invokable's parameter type and rejects even a `null`
// argument with "Unknown method parameter type". The shell always has
// QtQuick imported by the time screenOf runs; give the test engine the
// same footing by loading one trivial QtQuick component.
void importQtQuick(QQmlEngine& engine)
{
    QQmlComponent bootstrap(&engine);
    bootstrap.setData("import QtQuick\nQtObject {}\n", QUrl());
    QScopedPointer<QObject> loaded(bootstrap.create());
    QVERIFY2(loaded, qPrintable(bootstrap.errorString()));
}

} // namespace

class TestControlCenterController : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void screenOfNullFallsBackToThePrimary();
    void screenOfNeverHandsTheScreenToTheJsGarbageCollector();
    void screenOfAnUnparentedItemFallsBackToThePrimary();
    void screenOfAnItemInAWindowResolvesThatWindowsScreen();
    void openScreenNotifiesOncePerRealChange();
    void tileIdsListTheBuiltInCatalogInOrder();
};

void TestControlCenterController::screenOfNullFallsBackToThePrimary()
{
    ControlCenterController controller(nullptr);
    QVERIFY(QGuiApplication::primaryScreen());
    QCOMPARE(controller.screenOf(nullptr), QGuiApplication::primaryScreen());
}

void TestControlCenterController::screenOfNeverHandsTheScreenToTheJsGarbageCollector()
{
    ControlCenterController controller(nullptr);
    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);
    // The precondition for the bug: no parent, so nothing stops QML from
    // claiming it.
    QVERIFY(!screen->parent());

    // The call MUST go through a QML engine. The JavaScriptOwnership
    // transfer happens inside the engine's wrapper path for an invokable's
    // return value; a direct C++ call never touches ownership, so a test
    // that calls screenOf() from C++ passes with or without the fix (that
    // is how this case was first written, and a mutation run proved it
    // vacuous). Expose the controller the way the shell does — as a
    // context property — and invoke it from JS.
    QQmlEngine engine;
    importQtQuick(engine);
    engine.rootContext()->setContextProperty(QStringLiteral("ControlCenterRegistry"), &controller);
    QQmlExpression call(engine.rootContext(), nullptr, QStringLiteral("ControlCenterRegistry.screenOf(null)"));
    const QVariant result = call.evaluate();
    QVERIFY2(!call.hasError(), qPrintable(call.error().toString()));
    QCOMPARE(result.value<QObject*>(), screen);

    // The assertion. Without setObjectOwnership(CppOwnership) in screenOf,
    // the engine has now marked the LIVE QScreen JavaScriptOwnership and
    // will delete it at the next collection.
    QCOMPARE(QQmlEngine::objectOwnership(screen), QQmlEngine::CppOwnership);
}

void TestControlCenterController::screenOfAnUnparentedItemFallsBackToThePrimary()
{
    ControlCenterController controller(nullptr);
    // An item with no window resolves no screen of its own; the primary is
    // the sensible answer rather than null, which would leave the socket
    // transport with no bar to name. Ownership has to be right on this leg
    // too, and for the same reason it is checked through the engine.
    QQuickItem orphan;
    QQmlEngine engine;
    importQtQuick(engine);
    engine.rootContext()->setContextProperty(QStringLiteral("ControlCenterRegistry"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("orphan"), &orphan);
    QQmlExpression call(engine.rootContext(), nullptr, QStringLiteral("ControlCenterRegistry.screenOf(orphan)"));
    const QVariant result = call.evaluate();
    QVERIFY2(!call.hasError(), qPrintable(call.error().toString()));
    QCOMPARE(result.value<QObject*>(), QGuiApplication::primaryScreen());
    QCOMPARE(QQmlEngine::objectOwnership(QGuiApplication::primaryScreen()), QQmlEngine::CppOwnership);
}

void TestControlCenterController::screenOfAnItemInAWindowResolvesThatWindowsScreen()
{
    // The leg the other three cases cannot reach. Both of them pass either
    // nullptr or an item with no window, so the primary-screen fallback is
    // the right answer either way: deleting the whole window-resolution
    // block left the suite green. This runs on a SECOND screen, so the
    // resolved answer and the fallback are different pointers and the
    // block has to actually run.
    const auto screens = QGuiApplication::screens();
    if (screens.size() < 2) {
        QSKIP("needs two screens; the test is registered with the offscreen plugin's two-screen config");
    }
    QScreen* other = screens.at(0) == QGuiApplication::primaryScreen() ? screens.at(1) : screens.at(0);
    QVERIFY(other != QGuiApplication::primaryScreen());

    ControlCenterController controller(nullptr);
    QQuickWindow window;
    window.setScreen(other);
    QQuickItem item(window.contentItem());
    QCOMPARE(item.window(), &window);

    QCOMPARE(controller.screenOf(&item), other);
}

void TestControlCenterController::openScreenNotifiesOncePerRealChange()
{
    ControlCenterController controller(nullptr);
    QSignalSpy spy(&controller, &ControlCenterController::openScreenChanged);
    QCOMPARE(controller.openScreen(), QString());

    controller.setOpenScreen(QStringLiteral("DP-2"));
    QCOMPARE(controller.openScreen(), QStringLiteral("DP-2"));
    QCOMPARE(spy.count(), 1);

    // Same value again: no notify, or every bar rebinds for nothing.
    controller.setOpenScreen(QStringLiteral("DP-2"));
    QCOMPARE(spy.count(), 1);

    controller.setOpenScreen(QString());
    QCOMPARE(controller.openScreen(), QString());
    QCOMPARE(spy.count(), 2);
}

void TestControlCenterController::tileIdsListTheBuiltInCatalogInOrder()
{
    ControlCenterController controller(nullptr);
    // Grid order: three half-width toggles, then the two full-width sliders
    // so the toggles pack above them. shell.qml feeds this straight into
    // ControlCenter.tileIds, so the order here IS the on-screen order.
    const QStringList expected{QStringLiteral("network"), QStringLiteral("bluetooth"), QStringLiteral("idle"),
                               QStringLiteral("audio"), QStringLiteral("brightness")};
    QCOMPARE(controller.tileIds(), expected);
}

QTEST_MAIN(TestControlCenterController)

#include "test_control_center_controller.moc"
