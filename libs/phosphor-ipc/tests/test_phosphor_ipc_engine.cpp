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

#include <QObject>
#include <QQmlEngine>
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

QTEST_GUILESS_MAIN(TestPhosphorIpcEngine)
#include "test_phosphor_ipc_engine.moc"
