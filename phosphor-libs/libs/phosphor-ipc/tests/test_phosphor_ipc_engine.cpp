// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// IpcEngine attaches an IpcRouter to a QQmlEngine via a dynamic
// property; IpcTarget instances look the router up at
// componentComplete. The unit here exercises the install / uninstall
// / routerFor surface against bogus inputs (null engine, null router,
// replacement, foreign property values) so misconfiguration surfaces
// as a qWarning rather than a silent dead binding.

#include <PhosphorIpc/IpcEngine.h>
#include <PhosphorIpc/IpcRouter.h>
#include <PhosphorIpc/IpcTarget.h>

#include <QObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QStringList>
#include <QVariantMap>
#include <QTest>
#include <QVariant>
#include <QtCore/qtclasshelpermacros.h>

#include <memory>

using namespace PhosphorIpc;

class TestPhosphorIpcEngine : public QObject
{
    Q_OBJECT
public:
    Q_DISABLE_COPY_MOVE(TestPhosphorIpcEngine)
    TestPhosphorIpcEngine() = default;
private Q_SLOTS:
    void install_storesRouterOnEngine();
    void install_idempotentSameRouterSameEngine();
    void install_replacesPriorRouterWithWarning();
    void install_rejectsNullEngine();
    void install_rejectsNullRouter();
    void uninstall_clearsBinding();
    void uninstall_nullEngineIsNoOp();
    void routerFor_returnsInstalled();
    void routerFor_nullEngineReturnsNullptr();
    void routerFor_returnsNullptrAfterUninstall();
    void routerFor_returnsNullptrForForeignProperty();
    void routerFor_returnsNullptrAfterRouterDestroyed();
    void install_uninstall_install_doesNotLeakDestroyedWatcher();
    void qmlFunctionFailsItsCall();
    void qmlUntypedAndTypedFunctionsAreCallable();
};

void TestPhosphorIpcEngine::install_storesRouterOnEngine()
{
    QQmlEngine engine;
    IpcRouter router;
    IpcEngine::install(&engine, &router);
    QCOMPARE(IpcEngine::routerFor(&engine), &router);
}

void TestPhosphorIpcEngine::install_idempotentSameRouterSameEngine()
{
    QQmlEngine engine;
    IpcRouter router;
    IpcEngine::install(&engine, &router);
    // Re-installing the SAME router on the SAME engine must NOT
    // emit any warning; if it did, repeated startup paths (e.g.
    // engine reload) would spam the console.
    IpcEngine::install(&engine, &router);
    QCOMPARE(IpcEngine::routerFor(&engine), &router);
}

void TestPhosphorIpcEngine::install_replacesPriorRouterWithWarning()
{
    QQmlEngine engine;
    IpcRouter routerA;
    IpcRouter routerB;
    IpcEngine::install(&engine, &routerA);
    QTest::ignoreMessage(QtWarningMsg, "PhosphorIpc::IpcEngine::install: replacing existing router on engine");
    IpcEngine::install(&engine, &routerB);
    QCOMPARE(IpcEngine::routerFor(&engine), &routerB);
}

void TestPhosphorIpcEngine::install_rejectsNullEngine()
{
    IpcRouter router;
    QTest::ignoreMessage(QtWarningMsg, "PhosphorIpc::IpcEngine::install: null engine ignored");
    IpcEngine::install(nullptr, &router);
    // No crash, no side effects we can observe directly; the
    // ignoreMessage above pins the diagnostic surface.
}

void TestPhosphorIpcEngine::install_rejectsNullRouter()
{
    QQmlEngine engine;
    QTest::ignoreMessage(
        QtWarningMsg, "PhosphorIpc::IpcEngine::install: null router; call uninstall() to drop the binding explicitly");
    IpcEngine::install(&engine, nullptr);
    // Null-router install must NOT silently uninstall a previously
    // installed binding; the routerFor lookup stays at whatever it
    // was before (nullptr in this fresh-engine case).
    QCOMPARE(IpcEngine::routerFor(&engine), nullptr);
}

void TestPhosphorIpcEngine::uninstall_clearsBinding()
{
    QQmlEngine engine;
    IpcRouter router;
    IpcEngine::install(&engine, &router);
    IpcEngine::uninstall(&engine);
    QCOMPARE(IpcEngine::routerFor(&engine), nullptr);
}

void TestPhosphorIpcEngine::uninstall_nullEngineIsNoOp()
{
    IpcEngine::uninstall(nullptr); // must not crash
}

void TestPhosphorIpcEngine::routerFor_returnsInstalled()
{
    QQmlEngine engine;
    IpcRouter router;
    IpcEngine::install(&engine, &router);
    QCOMPARE(IpcEngine::routerFor(&engine), &router);
}

void TestPhosphorIpcEngine::routerFor_nullEngineReturnsNullptr()
{
    QCOMPARE(IpcEngine::routerFor(nullptr), nullptr);
}

void TestPhosphorIpcEngine::routerFor_returnsNullptrAfterUninstall()
{
    QQmlEngine engine;
    IpcRouter router;
    IpcEngine::install(&engine, &router);
    IpcEngine::uninstall(&engine);
    QCOMPARE(IpcEngine::routerFor(&engine), nullptr);
}

void TestPhosphorIpcEngine::routerFor_returnsNullptrForForeignProperty()
{
    // Simulate a different consumer scribbling something else into
    // the dynamic-property slot. routerFor must qobject_cast back to
    // IpcRouter and return nullptr on a mismatch rather than crashing.
    // Declare `foreign` BEFORE `engine` so destruction order has the
    // QObject* held in the QVariant outlive the engine that holds
    // it (defensive against any Qt-internal property cleanup that
    // might deref the held pointer at engine teardown).
    QObject foreign;
    QQmlEngine engine;
    engine.setProperty("phosphorIpcRouter", QVariant::fromValue<QObject*>(&foreign));
    QCOMPARE(IpcEngine::routerFor(&engine), nullptr);
}

void TestPhosphorIpcEngine::routerFor_returnsNullptrAfterRouterDestroyed()
{
    // install() wires a destroyed-signal watcher on the router so
    // that when the router dies before uninstall() is called, the
    // engine property is auto-cleared. Without that wire, the
    // QVariant<QObject*> would hold a dangling pointer and
    // routerFor() would qobject_cast freed memory (UB).
    QQmlEngine engine;
    auto router = std::make_unique<IpcRouter>();
    IpcEngine::install(&engine, router.get());
    QCOMPARE(IpcEngine::routerFor(&engine), router.get());
    router.reset(); // ~IpcRouter() fires destroyed → cleanup
    QCOMPARE(IpcEngine::routerFor(&engine), nullptr);
}

void TestPhosphorIpcEngine::install_uninstall_install_doesNotLeakDestroyedWatcher()
{
    // Repeated install/uninstall/install cycles must not accumulate
    // engine-destroyed handlers on the engine. Each cycle clears the
    // per-engine bookkeeping; the engine-destroyed handler should be
    // attached exactly once per engine across the engine's lifetime,
    // not once per install() call. A regression that re-attached
    // would still pass this test functionally (the extra lambdas
    // are no-ops on map removal), but the leak would surface as a
    // growing connection count we can't easily assert here. What we
    // CAN assert: the destroyed-watcher contract continues to work
    // after the cycle, i.e. a router destroyed post-cycle still
    // clears the engine property.
    QQmlEngine engine;
    {
        auto routerA = std::make_unique<IpcRouter>();
        IpcEngine::install(&engine, routerA.get());
        IpcEngine::uninstall(&engine);
    }
    auto routerB = std::make_unique<IpcRouter>();
    IpcEngine::install(&engine, routerB.get());
    QCOMPARE(IpcEngine::routerFor(&engine), routerB.get());
    routerB.reset();
    QCOMPARE(IpcEngine::routerFor(&engine), nullptr);
}

void TestPhosphorIpcEngine::qmlFunctionFailsItsCall()
{
    // A function declared in QML runs through the QML engine's own
    // metaobject. sendErrorReply must still reach the router's call.
    qmlRegisterType<IpcTarget>("PhosphorIpcTest", 1, 0, "IpcTarget");
    IpcRouter router;
    QQmlEngine engine;
    IpcEngine::install(&engine, &router);
    QQmlComponent component(&engine);
    component.setData(R"(import PhosphorIpcTest
IpcTarget {
    target: "theme"
    function setMode(mode) {
        if (mode !== "dark") {
            sendErrorReply("mode is dark, not " + mode)
            return "dropped"
        }
        return mode
    }
})",
                      QUrl());
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(router.target(QStringLiteral("theme")), object.get());

    IpcRouter::InvokeOutcome outcome = IpcRouter::InvokeOutcome::Ok;
    QString message;
    const QVariant failed =
        router.invoke(QStringLiteral("theme"), QStringLiteral("setMode"), {QStringLiteral("blue")}, &outcome, &message);
    QCOMPARE(outcome, IpcRouter::InvokeOutcome::CallFailed);
    QCOMPARE(message, QStringLiteral("mode is dark, not blue"));
    QVERIFY(!failed.isValid());

    const QVariant ok =
        router.invoke(QStringLiteral("theme"), QStringLiteral("setMode"), {QStringLiteral("dark")}, &outcome);
    QCOMPARE(outcome, IpcRouter::InvokeOutcome::Ok);
    QCOMPARE(ok.toString(), QStringLiteral("dark"));
}

void TestPhosphorIpcEngine::qmlUntypedAndTypedFunctionsAreCallable()
{
    // An untyped QML function takes and returns QVariant. The router hands
    // the incoming variant over as it is and reads the returned one.
    qmlRegisterType<IpcTarget>("PhosphorIpcTest", 1, 0, "IpcTarget");
    IpcRouter router;
    QQmlEngine engine;
    IpcEngine::install(&engine, &router);
    QQmlComponent component(&engine);
    component.setData(R"(import PhosphorIpcTest
IpcTarget {
    target: "calc"
    function untyped(a, b) { return { sum: a + b, kinds: [typeof a, typeof b] } }
    function typed(a: int, b: int): int { return a * b }
    function nothing(text) { }
})",
                      QUrl());
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));

    IpcRouter::InvokeOutcome outcome = IpcRouter::InvokeOutcome::NoSuchFn;
    QString message;
    const QVariantMap untyped =
        router.invoke(QStringLiteral("calc"), QStringLiteral("untyped"), {2, 3.5}, &outcome, &message).toMap();
    QVERIFY2(outcome == IpcRouter::InvokeOutcome::Ok, qPrintable(message));
    QCOMPARE(untyped.value(QStringLiteral("sum")).toDouble(), 5.5);
    QCOMPARE(untyped.value(QStringLiteral("kinds")).toStringList(),
             (QStringList{QStringLiteral("number"), QStringLiteral("number")}));

    QCOMPARE(router.invoke(QStringLiteral("calc"), QStringLiteral("typed"), {4, 5}, &outcome).toInt(), 20);
    QCOMPARE(outcome, IpcRouter::InvokeOutcome::Ok);

    router.invoke(QStringLiteral("calc"), QStringLiteral("nothing"), {QStringLiteral("x")}, &outcome, &message);
    QVERIFY2(outcome == IpcRouter::InvokeOutcome::Ok, qPrintable(message));
}

QTEST_GUILESS_MAIN(TestPhosphorIpcEngine)
#include "test_phosphor_ipc_engine.moc"
