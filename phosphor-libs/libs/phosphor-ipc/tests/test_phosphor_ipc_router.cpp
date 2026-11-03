// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// In-process IpcRouter tests. Exercise registerTarget / unregisterTarget /
// invoke / listTargets / schemaFor against a hand-built QObject "target"
// that exposes a few Q_INVOKABLE methods. Socket-level wire roundtrips
// live in test_phosphor_ipc_e2e.cpp.

#include <PhosphorIpc/IpcRouter.h>
#include <PhosphorIpc/IpcTarget.h>

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
    // Two overloads of the same name. The router must dispatch by
    // arity (parameterCount), not by first-by-name.
    Q_INVOKABLE QString combine(const QString& a)
    {
        return QStringLiteral("one:%1").arg(a);
    }
    Q_INVOKABLE QString combine(const QString& a, const QString& b)
    {
        return QStringLiteral("two:%1+%2").arg(a, b);
    }

protected:
    // Protected Q_INVOKABLE — subclass-only convention. The router
    // and schema generator must NOT expose this on the wire.
    Q_INVOKABLE QString protectedSecret()
    {
        return QStringLiteral("forbidden");
    }

private:
    QString m_lastNoReturn;
};

} // namespace

class TestPhosphorIpcRouter : public QObject
{
    Q_OBJECT
public:
    Q_DISABLE_COPY_MOVE(TestPhosphorIpcRouter)
    TestPhosphorIpcRouter() = default;
private Q_SLOTS:
    void register_addsAndEmits();
    void register_duplicateRejected();
    void register_idempotentSameObjectReturnsTrue();
    void register_nullObjectRejected();
    void register_emptyNameRejected();
    void unregister_removesAndEmits();
    void unregister_unknownNoOp();
    void unregister_ownershipCheckPreservesIncumbent();
    void listTargets_filtersDeletedObjects();
    void invoke_returnsStringResult();
    void invoke_returnsIntResult();
    void invoke_voidReturnSucceeds();
    void invoke_unknownTargetError();
    void invoke_unknownFnError();
    void invoke_argCountMismatchError();
    void invoke_argTypeCoercion();
    void invoke_argTypeCoercionFailure();
    void invoke_outcomeOkOnSuccess();
    void invoke_overloadDispatchesByArity();
    void invoke_overloadNoMatchingArityReturnsArgCountMismatch();
    void invoke_protectedQInvokableRejected();
    void invoke_ipcTargetWrapperMethodsRejected();
    void schemaFor_unknownReturnsEmpty();
    void schemaFor_listsFunctions();
    void schemaFor_skipsProtectedQInvokable();
};

void TestPhosphorIpcRouter::register_addsAndEmits()
{
    IpcRouter router;
    QSignalSpy spy(&router, &IpcRouter::targetRegistered);
    FakeTarget t;
    QVERIFY(router.registerTarget(QStringLiteral("greet"), &t));
    QCOMPARE(router.listTargets(), QStringList{QStringLiteral("greet")});
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("greet"));
}

void TestPhosphorIpcRouter::register_duplicateRejected()
{
    IpcRouter router;
    FakeTarget t1;
    FakeTarget t2;
    QVERIFY(router.registerTarget(QStringLiteral("greet"), &t1));
    QTest::ignoreMessage(QtWarningMsg, "PhosphorIpc::IpcRouter::registerTarget: duplicate target 'greet' ignored");
    // Duplicate-name registration returns false; the existing
    // binding is preserved (first registration wins).
    QVERIFY(!router.registerTarget(QStringLiteral("greet"), &t2));
    QCOMPARE(router.listTargets().size(), 1);
    QCOMPARE(router.target(QStringLiteral("greet")), &t1);
}

void TestPhosphorIpcRouter::register_idempotentSameObjectReturnsTrue()
{
    // Re-registering the SAME QObject under the SAME name is a
    // silent idempotent success (no warning, no signal re-emit).
    // The bool return is documented to be true on this path so a
    // wrapper like IpcTarget can blindly re-call register and treat
    // the result as authoritative ownership.
    IpcRouter router;
    QSignalSpy spy(&router, &IpcRouter::targetRegistered);
    FakeTarget t;
    QVERIFY(router.registerTarget(QStringLiteral("greet"), &t));
    QVERIFY(router.registerTarget(QStringLiteral("greet"), &t));
    QCOMPARE(spy.count(), 1); // exactly one register emit, not two
}

void TestPhosphorIpcRouter::register_nullObjectRejected()
{
    IpcRouter router;
    QTest::ignoreMessage(QtWarningMsg, "PhosphorIpc::IpcRouter::registerTarget: null object for 'greet' ignored");
    QVERIFY(!router.registerTarget(QStringLiteral("greet"), nullptr));
    QVERIFY(router.listTargets().isEmpty());
}

void TestPhosphorIpcRouter::register_emptyNameRejected()
{
    IpcRouter router;
    FakeTarget t;
    QTest::ignoreMessage(QtWarningMsg, "PhosphorIpc::IpcRouter::registerTarget: empty name ignored");
    QVERIFY(!router.registerTarget(QString(), &t));
    QVERIFY(router.listTargets().isEmpty());
}

void TestPhosphorIpcRouter::unregister_removesAndEmits()
{
    IpcRouter router;
    FakeTarget t;
    QVERIFY(router.registerTarget(QStringLiteral("greet"), &t));
    QSignalSpy spy(&router, &IpcRouter::targetUnregistered);
    router.unregisterTarget(QStringLiteral("greet"));
    QVERIFY(router.listTargets().isEmpty());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("greet"));
}

void TestPhosphorIpcRouter::unregister_ownershipCheckPreservesIncumbent()
{
    // unregisterTarget(name, obj) must only tear down the binding
    // when the registry currently binds `name` to `obj`. This is
    // the safety net used by IpcTarget's destructor when its own
    // registerTarget was rejected as a duplicate; without it,
    // destruction would unregister the legitimate owner.
    IpcRouter router;
    FakeTarget t1;
    FakeTarget t2;
    QVERIFY(router.registerTarget(QStringLiteral("greet"), &t1));
    QTest::ignoreMessage(QtWarningMsg, "PhosphorIpc::IpcRouter::registerTarget: duplicate target 'greet' ignored");
    QVERIFY(!router.registerTarget(QStringLiteral("greet"), &t2));

    // Pretend t2 "thinks" it owns the slot and unregisters by name+ptr.
    // The registry must reject the call (mismatched ownership) and
    // keep t1's binding intact.
    QSignalSpy spy(&router, &IpcRouter::targetUnregistered);
    router.unregisterTarget(QStringLiteral("greet"), &t2);
    QCOMPARE(spy.count(), 0);
    QCOMPARE(router.target(QStringLiteral("greet")), &t1);

    // The 1-arg administrative overload (or 2-arg with nullptr)
    // tears down unconditionally.
    router.unregisterTarget(QStringLiteral("greet"));
    QCOMPARE(spy.count(), 1);
    QVERIFY(router.listTargets().isEmpty());
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
        QVERIFY(router.registerTarget(QStringLiteral("scoped"), &t));
        QCOMPARE(router.listTargets().size(), 1);
    }
    // t went out of scope; the QPointer auto-clears so listTargets
    // should report 0. Also pin the target() accessor to nullptr
    // so a future refactor that caches names independently of the
    // QPointer state would be caught.
    QVERIFY(router.listTargets().isEmpty());
    QCOMPARE(router.target(QStringLiteral("scoped")), nullptr);
}

void TestPhosphorIpcRouter::invoke_returnsStringResult()
{
    IpcRouter router;
    FakeTarget t;
    QVERIFY(router.registerTarget(QStringLiteral("greet"), &t));
    QString err;
    const QVariant r = router.invoke(QStringLiteral("greet"), QStringLiteral("sayHello"),
                                     QVariantList{QStringLiteral("nate")}, nullptr, &err);
    QVERIFY(err.isEmpty());
    QCOMPARE(r.toString(), QStringLiteral("Hello, nate"));
}

void TestPhosphorIpcRouter::invoke_returnsIntResult()
{
    IpcRouter router;
    FakeTarget t;
    QVERIFY(router.registerTarget(QStringLiteral("math"), &t));
    QString err;
    const QVariant r = router.invoke(QStringLiteral("math"), QStringLiteral("add"), QVariantList{3, 4}, nullptr, &err);
    QVERIFY(err.isEmpty());
    QCOMPARE(r.toInt(), 7);
}

void TestPhosphorIpcRouter::invoke_voidReturnSucceeds()
{
    IpcRouter router;
    FakeTarget t;
    QVERIFY(router.registerTarget(QStringLiteral("store"), &t));
    QString err;
    const QVariant r = router.invoke(QStringLiteral("store"), QStringLiteral("noReturn"),
                                     QVariantList{QStringLiteral("payload")}, nullptr, &err);
    QVERIFY(err.isEmpty());
    QVERIFY(!r.isValid()); // void return → invalid QVariant
    // Verify the side effect happened.
    const QVariant r2 =
        router.invoke(QStringLiteral("store"), QStringLiteral("readLast"), QVariantList{}, nullptr, &err);
    QCOMPARE(r2.toString(), QStringLiteral("payload"));
}

void TestPhosphorIpcRouter::invoke_unknownTargetError()
{
    // Assert on the structured InvokeOutcome rather than the
    // human-readable message. Message text is i18n / refactor
    // surface; the enum is the wire contract.
    IpcRouter router;
    QString err;
    IpcRouter::InvokeOutcome outcome = IpcRouter::InvokeOutcome::Ok;
    const QVariant r = router.invoke(QStringLiteral("ghost"), QStringLiteral("fn"), {}, &outcome, &err);
    QVERIFY(!r.isValid());
    QCOMPARE(outcome, IpcRouter::InvokeOutcome::NoSuchTarget);
    QVERIFY(!err.isEmpty()); // message is populated, but content not asserted
}

void TestPhosphorIpcRouter::invoke_unknownFnError()
{
    IpcRouter router;
    FakeTarget t;
    QVERIFY(router.registerTarget(QStringLiteral("greet"), &t));
    QString err;
    IpcRouter::InvokeOutcome outcome = IpcRouter::InvokeOutcome::Ok;
    const QVariant r = router.invoke(QStringLiteral("greet"), QStringLiteral("ghostFn"), {}, &outcome, &err);
    QVERIFY(!r.isValid());
    QCOMPARE(outcome, IpcRouter::InvokeOutcome::NoSuchFn);
    QVERIFY(!err.isEmpty());
}

void TestPhosphorIpcRouter::invoke_argCountMismatchError()
{
    IpcRouter router;
    FakeTarget t;
    QVERIFY(router.registerTarget(QStringLiteral("math"), &t));
    QString err;
    IpcRouter::InvokeOutcome outcome = IpcRouter::InvokeOutcome::Ok;
    // add() expects 2 args; pass 1.
    const QVariant r = router.invoke(QStringLiteral("math"), QStringLiteral("add"), QVariantList{1}, &outcome, &err);
    QVERIFY(!r.isValid());
    QCOMPARE(outcome, IpcRouter::InvokeOutcome::ArgCountMismatch);
    QVERIFY(!err.isEmpty());
}

void TestPhosphorIpcRouter::invoke_argTypeCoercion()
{
    IpcRouter router;
    FakeTarget t;
    QVERIFY(router.registerTarget(QStringLiteral("math"), &t));
    QString err;
    // add expects int; pass a string-shaped int, QVariant::convert
    // promotes "3" → 3 via QString::toInt, so this should succeed.
    const QVariant r = router.invoke(QStringLiteral("math"), QStringLiteral("add"),
                                     QVariantList{QStringLiteral("3"), QStringLiteral("4")}, nullptr, &err);
    QVERIFY(err.isEmpty());
    QCOMPARE(r.toInt(), 7);
}

void TestPhosphorIpcRouter::invoke_argTypeCoercionFailure()
{
    // Negative path for arg coercion: passing a non-convertible
    // QVariant for an int-typed param surfaces ArgConvertFailed,
    // which the wire dispatcher maps to INVALID_ARG.
    IpcRouter router;
    FakeTarget t;
    QVERIFY(router.registerTarget(QStringLiteral("math"), &t));
    QString err;
    IpcRouter::InvokeOutcome outcome = IpcRouter::InvokeOutcome::Ok;
    const QVariant r =
        router.invoke(QStringLiteral("math"), QStringLiteral("add"), QVariantList{QVariantMap{}, 4}, &outcome, &err);
    QVERIFY(!r.isValid());
    QCOMPARE(outcome, IpcRouter::InvokeOutcome::ArgConvertFailed);
    QVERIFY(!err.isEmpty());
}

void TestPhosphorIpcRouter::invoke_outcomeOkOnSuccess()
{
    IpcRouter router;
    FakeTarget t;
    QVERIFY(router.registerTarget(QStringLiteral("math"), &t));
    IpcRouter::InvokeOutcome outcome = IpcRouter::InvokeOutcome::InvokeFailed;
    router.invoke(QStringLiteral("math"), QStringLiteral("add"), QVariantList{1, 2}, &outcome);
    QCOMPARE(outcome, IpcRouter::InvokeOutcome::Ok);
}

void TestPhosphorIpcRouter::schemaFor_unknownReturnsEmpty()
{
    // Unknown target produces the empty-shaped schema document:
    // {target: "<name>", functions: [], signals: []}. Same contract
    // IpcSchemaGenerator::schemaFor(name, nullptr) honors, so
    // router-direct callers and the schema generator agree on the
    // shape (the wire dispatcher still returns NO_SUCH_TARGET error
    // frames for `schema` requests on unknown targets; see the
    // dispatcher tests).
    IpcRouter router;
    const QJsonObject s = router.schemaFor(QStringLiteral("ghost"));
    QCOMPARE(s.value(QStringLiteral("target")).toString(), QStringLiteral("ghost"));
    QCOMPARE(s.value(QStringLiteral("functions")).toArray().size(), 0);
    QCOMPARE(s.value(QStringLiteral("signals")).toArray().size(), 0);
}

void TestPhosphorIpcRouter::schemaFor_listsFunctions()
{
    IpcRouter router;
    FakeTarget t;
    QVERIFY(router.registerTarget(QStringLiteral("greet"), &t));
    const QJsonObject schema = router.schemaFor(QStringLiteral("greet"));
    QCOMPARE(schema.value(QStringLiteral("target")).toString(), QStringLiteral("greet"));
    QVERIFY(schema.value(QStringLiteral("functions")).isArray());
    QCOMPARE(schema.value(QStringLiteral("signals")).toArray().size(), 0);
    // Public Q_INVOKABLE methods: sayHello, add, noReturn, readLast,
    // combine(QString), combine(QString,QString) = 6 functions. The
    // protectedSecret method MUST NOT appear (filter-to-Public
    // pinned by schemaFor_skipsProtectedQInvokable below).
    QCOMPARE(schema.value(QStringLiteral("functions")).toArray().size(), 6);
}

void TestPhosphorIpcRouter::invoke_overloadDispatchesByArity()
{
    // FakeTarget declares Q_INVOKABLE combine(QString) and
    // combine(QString, QString). The router picks the overload
    // whose parameterCount matches args.size(); a regression that
    // returned the first-by-name match would dispatch combine(a)
    // for the 2-arg call (and surface ArgCountMismatch) or vice
    // versa.
    IpcRouter router;
    FakeTarget t;
    QVERIFY(router.registerTarget(QStringLiteral("c"), &t));
    QString err;

    const QVariant r1 = router.invoke(QStringLiteral("c"), QStringLiteral("combine"),
                                      QVariantList{QStringLiteral("alpha")}, nullptr, &err);
    QVERIFY(err.isEmpty());
    QCOMPARE(r1.toString(), QStringLiteral("one:alpha"));

    const QVariant r2 = router.invoke(QStringLiteral("c"), QStringLiteral("combine"),
                                      QVariantList{QStringLiteral("alpha"), QStringLiteral("beta")}, nullptr, &err);
    QVERIFY(err.isEmpty());
    QCOMPARE(r2.toString(), QStringLiteral("two:alpha+beta"));
}

void TestPhosphorIpcRouter::invoke_overloadNoMatchingArityReturnsArgCountMismatch()
{
    // FakeTarget declares combine(QString) and combine(QString, QString)
    // — but no 3-arg combine. A call with 3 args must surface
    // ArgCountMismatch citing the parameter count of the name-only
    // fallback metamethod (whichever overload findInvokableMethod
    // returned). Pins the "no overload matches" branch so a
    // regression that silently succeeded with arg truncation or
    // returned NoSuchFn (the wrong code) is caught.
    IpcRouter router;
    FakeTarget t;
    QVERIFY(router.registerTarget(QStringLiteral("c"), &t));
    QString err;
    IpcRouter::InvokeOutcome outcome = IpcRouter::InvokeOutcome::Ok;
    const QVariant r =
        router.invoke(QStringLiteral("c"), QStringLiteral("combine"),
                      QVariantList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}, &outcome, &err);
    QVERIFY(!r.isValid());
    QCOMPARE(outcome, IpcRouter::InvokeOutcome::ArgCountMismatch);
    QVERIFY(!err.isEmpty());
}

void TestPhosphorIpcRouter::invoke_ipcTargetWrapperMethodsRejected()
{
    // IpcTarget declares its own Q_INVOKABLE methods (`emitEvent`)
    // and Q_PROPERTY signals (`targetChanged`) that are wrapper-
    // internal — they exist for QML to inject events / observe
    // property changes, NOT for remote wire callers. Without the
    // IpcTarget-aware introspection floor, a client could call
    // `phosphorctl call mytarget.emitEvent --arg signal=fake
    // --arg args=[...]` and inject arbitrary events into every
    // subscriber. The schema generator never advertises these
    // methods; the dispatcher MUST refuse to dispatch them so the
    // wire-callable surface matches what the schema advertised.
    IpcRouter router;
    IpcTarget t; // standalone, no QML — register manually below
    QVERIFY(router.registerTarget(QStringLiteral("wrapped"), &t));

    QString err;
    IpcRouter::InvokeOutcome outcome = IpcRouter::InvokeOutcome::Ok;
    const QVariant r = router.invoke(QStringLiteral("wrapped"), QStringLiteral("emitEvent"),
                                     QVariantList{QStringLiteral("targetChanged"), QVariantList{}}, &outcome, &err);
    QVERIFY(!r.isValid());
    QCOMPARE(outcome, IpcRouter::InvokeOutcome::NoSuchFn);
    QVERIFY(!err.isEmpty());

    // Same defense on the schema side: the IpcTarget wrapper's
    // methods MUST NOT appear in the advertised schema.
    const QJsonObject schema = router.schemaFor(QStringLiteral("wrapped"));
    const QJsonArray functions = schema.value(QStringLiteral("functions")).toArray();
    QStringList fnNames;
    for (const QJsonValue& v : functions) {
        fnNames.append(v.toObject().value(QStringLiteral("name")).toString());
    }
    QVERIFY2(!fnNames.contains(QStringLiteral("emitEvent")),
             "IpcTarget::emitEvent must not appear in the wire-visible schema");

    const QJsonArray signals_ = schema.value(QStringLiteral("signals")).toArray();
    QStringList sigNames;
    for (const QJsonValue& v : signals_) {
        sigNames.append(v.toObject().value(QStringLiteral("name")).toString());
    }
    QVERIFY2(!sigNames.contains(QStringLiteral("targetChanged")),
             "IpcTarget::targetChanged must not appear in the wire-visible schema");
}

void TestPhosphorIpcRouter::invoke_protectedQInvokableRejected()
{
    // FakeTarget::protectedSecret is `protected: Q_INVOKABLE`. The
    // router filters to QMetaMethod::Public, so it must NOT be
    // callable on the wire even though the metaobject knows about
    // it. The expected outcome is NoSuchFn (the wire couldn't find
    // it among the Public methods), not InvokeFailed.
    IpcRouter router;
    FakeTarget t;
    QVERIFY(router.registerTarget(QStringLiteral("t"), &t));
    QString err;
    IpcRouter::InvokeOutcome outcome = IpcRouter::InvokeOutcome::Ok;
    const QVariant r = router.invoke(QStringLiteral("t"), QStringLiteral("protectedSecret"), {}, &outcome, &err);
    QVERIFY(!r.isValid());
    QCOMPARE(outcome, IpcRouter::InvokeOutcome::NoSuchFn);
    QVERIFY(!err.isEmpty());
}

void TestPhosphorIpcRouter::schemaFor_skipsProtectedQInvokable()
{
    // Pin the schema-side filter: protected Q_INVOKABLE methods
    // must not appear in the generated functions array.
    IpcRouter router;
    FakeTarget t;
    QVERIFY(router.registerTarget(QStringLiteral("t"), &t));
    const QJsonObject schema = router.schemaFor(QStringLiteral("t"));
    const QJsonArray fns = schema.value(QStringLiteral("functions")).toArray();
    QStringList names;
    for (const QJsonValue& v : fns) {
        names.append(v.toObject().value(QStringLiteral("name")).toString());
    }
    QVERIFY2(!names.contains(QStringLiteral("protectedSecret")),
             "protected Q_INVOKABLE must not appear in the wire-visible schema");
}

QTEST_GUILESS_MAIN(TestPhosphorIpcRouter)
#include "test_phosphor_ipc_router.moc"
