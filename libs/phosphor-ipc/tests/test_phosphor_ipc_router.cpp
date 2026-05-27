// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// In-process IpcRouter tests. Exercise registerTarget / unregisterTarget /
// invoke / listTargets / schemaFor against a hand-built QObject "target"
// that exposes a few Q_INVOKABLE methods. Socket-level wire roundtrips
// live in test_phosphor_ipc_e2e.cpp.

#include <PhosphorIpc/IpcRouter.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QtCore/qtclasshelpermacros.h>

using namespace PhosphorIpc;

namespace {

class FakeTarget : public QObject
{
    Q_OBJECT
public:
    explicit FakeTarget(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    Q_DISABLE_COPY_MOVE(FakeTarget)

    Q_INVOKABLE QString sayHello(const QString& name)
    {
        return QStringLiteral("Hello, %1").arg(name);
    }
    Q_INVOKABLE int add(int a, int b)
    {
        return a + b;
    }
    Q_INVOKABLE void noReturn(const QString& s)
    {
        m_lastNoReturn = s;
    }
    Q_INVOKABLE QString readLast() const
    {
        return m_lastNoReturn;
    }

private:
    QString m_lastNoReturn;
};

} // namespace

class TestPhosphorIpcRouter : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void register_addsAndEmits();
    void register_duplicateRejected();
    void register_nullObjectRejected();
    void register_emptyNameRejected();
    void unregister_removesAndEmits();
    void unregister_unknownNoOp();
    void listTargets_filtersDeletedObjects();
    void invoke_returnsStringResult();
    void invoke_returnsIntResult();
    void invoke_voidReturnSucceeds();
    void invoke_unknownTargetError();
    void invoke_unknownFnError();
    void invoke_argCountMismatchError();
    void invoke_argTypeCoercion();
    void schemaFor_unknownReturnsEmpty();
    void schemaFor_listsFunctions();
};

void TestPhosphorIpcRouter::register_addsAndEmits()
{
    IpcRouter router;
    QSignalSpy spy(&router, &IpcRouter::targetRegistered);
    FakeTarget t;
    router.registerTarget(QStringLiteral("greet"), &t);
    QCOMPARE(router.listTargets(), QStringList{QStringLiteral("greet")});
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("greet"));
}

void TestPhosphorIpcRouter::register_duplicateRejected()
{
    IpcRouter router;
    FakeTarget t1;
    FakeTarget t2;
    router.registerTarget(QStringLiteral("greet"), &t1);
    QTest::ignoreMessage(QtWarningMsg, "PhosphorIpc::IpcRouter::registerTarget: duplicate target 'greet' ignored");
    router.registerTarget(QStringLiteral("greet"), &t2);
    QCOMPARE(router.listTargets().size(), 1);
    QCOMPARE(router.target(QStringLiteral("greet")), &t1);
}

void TestPhosphorIpcRouter::register_nullObjectRejected()
{
    IpcRouter router;
    QTest::ignoreMessage(QtWarningMsg, "PhosphorIpc::IpcRouter::registerTarget: null object for 'greet' ignored");
    router.registerTarget(QStringLiteral("greet"), nullptr);
    QVERIFY(router.listTargets().isEmpty());
}

void TestPhosphorIpcRouter::register_emptyNameRejected()
{
    IpcRouter router;
    FakeTarget t;
    QTest::ignoreMessage(QtWarningMsg, "PhosphorIpc::IpcRouter::registerTarget: empty name ignored");
    router.registerTarget(QString(), &t);
    QVERIFY(router.listTargets().isEmpty());
}

void TestPhosphorIpcRouter::unregister_removesAndEmits()
{
    IpcRouter router;
    FakeTarget t;
    router.registerTarget(QStringLiteral("greet"), &t);
    QSignalSpy spy(&router, &IpcRouter::targetUnregistered);
    router.unregisterTarget(QStringLiteral("greet"));
    QVERIFY(router.listTargets().isEmpty());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("greet"));
}

void TestPhosphorIpcRouter::unregister_unknownNoOp()
{
    IpcRouter router;
    QSignalSpy spy(&router, &IpcRouter::targetUnregistered);
    router.unregisterTarget(QStringLiteral("ghost"));
    QCOMPARE(spy.count(), 0);
}

void TestPhosphorIpcRouter::listTargets_filtersDeletedObjects()
{
    IpcRouter router;
    {
        FakeTarget t;
        router.registerTarget(QStringLiteral("scoped"), &t);
        QCOMPARE(router.listTargets().size(), 1);
    }
    // t went out of scope; the QPointer auto-clears so listTargets
    // should report 0.
    QVERIFY(router.listTargets().isEmpty());
}

void TestPhosphorIpcRouter::invoke_returnsStringResult()
{
    IpcRouter router;
    FakeTarget t;
    router.registerTarget(QStringLiteral("greet"), &t);
    QString err;
    const QVariant r =
        router.invoke(QStringLiteral("greet"), QStringLiteral("sayHello"), QVariantList{QStringLiteral("nate")}, &err);
    QVERIFY(err.isEmpty());
    QCOMPARE(r.toString(), QStringLiteral("Hello, nate"));
}

void TestPhosphorIpcRouter::invoke_returnsIntResult()
{
    IpcRouter router;
    FakeTarget t;
    router.registerTarget(QStringLiteral("math"), &t);
    QString err;
    const QVariant r = router.invoke(QStringLiteral("math"), QStringLiteral("add"), QVariantList{3, 4}, &err);
    QVERIFY(err.isEmpty());
    QCOMPARE(r.toInt(), 7);
}

void TestPhosphorIpcRouter::invoke_voidReturnSucceeds()
{
    IpcRouter router;
    FakeTarget t;
    router.registerTarget(QStringLiteral("store"), &t);
    QString err;
    const QVariant r = router.invoke(QStringLiteral("store"), QStringLiteral("noReturn"),
                                     QVariantList{QStringLiteral("payload")}, &err);
    QVERIFY(err.isEmpty());
    QVERIFY(!r.isValid()); // void return → invalid QVariant
    // Verify the side effect happened.
    const QVariant r2 = router.invoke(QStringLiteral("store"), QStringLiteral("readLast"), QVariantList{}, &err);
    QCOMPARE(r2.toString(), QStringLiteral("payload"));
}

void TestPhosphorIpcRouter::invoke_unknownTargetError()
{
    IpcRouter router;
    QString err;
    const QVariant r = router.invoke(QStringLiteral("ghost"), QStringLiteral("fn"), {}, &err);
    QVERIFY(!r.isValid());
    QVERIFY(err.contains(QStringLiteral("unknown target")));
}

void TestPhosphorIpcRouter::invoke_unknownFnError()
{
    IpcRouter router;
    FakeTarget t;
    router.registerTarget(QStringLiteral("greet"), &t);
    QString err;
    const QVariant r = router.invoke(QStringLiteral("greet"), QStringLiteral("ghostFn"), {}, &err);
    QVERIFY(!r.isValid());
    QVERIFY(err.contains(QStringLiteral("no invokable method")));
}

void TestPhosphorIpcRouter::invoke_argCountMismatchError()
{
    IpcRouter router;
    FakeTarget t;
    router.registerTarget(QStringLiteral("math"), &t);
    QString err;
    // add() expects 2 args; pass 1.
    const QVariant r = router.invoke(QStringLiteral("math"), QStringLiteral("add"), QVariantList{1}, &err);
    QVERIFY(!r.isValid());
    QVERIFY(err.contains(QStringLiteral("argument count")));
}

void TestPhosphorIpcRouter::invoke_argTypeCoercion()
{
    IpcRouter router;
    FakeTarget t;
    router.registerTarget(QStringLiteral("math"), &t);
    QString err;
    // add expects int; pass a string-shaped int — QVariant::convert
    // promotes "3" → 3 via QString::toInt, so this should succeed.
    const QVariant r = router.invoke(QStringLiteral("math"), QStringLiteral("add"),
                                     QVariantList{QStringLiteral("3"), QStringLiteral("4")}, &err);
    QVERIFY(err.isEmpty());
    QCOMPARE(r.toInt(), 7);
}

void TestPhosphorIpcRouter::schemaFor_unknownReturnsEmpty()
{
    IpcRouter router;
    QCOMPARE(router.schemaFor(QStringLiteral("ghost")), QJsonObject());
}

void TestPhosphorIpcRouter::schemaFor_listsFunctions()
{
    IpcRouter router;
    FakeTarget t;
    router.registerTarget(QStringLiteral("greet"), &t);
    const QJsonObject schema = router.schemaFor(QStringLiteral("greet"));
    QCOMPARE(schema.value(QStringLiteral("target")).toString(), QStringLiteral("greet"));
    QVERIFY(schema.value(QStringLiteral("functions")).isArray());
    QCOMPARE(schema.value(QStringLiteral("signals")).toArray().size(), 0);
    // FakeTarget exposes 4 Q_INVOKABLE methods.
    QCOMPARE(schema.value(QStringLiteral("functions")).toArray().size(), 4);
}

QTEST_MAIN(TestPhosphorIpcRouter)
#include "test_phosphor_ipc_router.moc"
