// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorIpc/IpcProtocol.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QTest>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtCore/qtclasshelpermacros.h>

using namespace PhosphorIpc;

class TestPhosphorIpcProtocol : public QObject
{
    Q_OBJECT
public:
    Q_DISABLE_COPY_MOVE(TestPhosphorIpcProtocol)
    TestPhosphorIpcProtocol() = default;
private Q_SLOTS:
    void parseRequest_acceptsValidCall();
    void parseRequest_acceptsList();
    void parseRequest_acceptsSchema();
    void parseRequest_acceptsSubscribe();
    void parseRequest_rejectsMissingType();
    void parseRequest_rejectsMalformedJson();
    void parseRequest_rejectsNonObjectRoot();
    void parseRequest_rejectsNonArrayArgs();
    void parseRequest_argsDefaultEmpty();
    void buildReply_shape();
    void buildEvent_shape();
    void buildError_shape();
    void buildError_omitsZeroId();
    void writeLine_appendsNewline();
    void parseRequest_rejectsNonNumericId();
    void parseRequest_rejectsFractionalId();
    void parseRequest_rejectsOutOfRangeId();
    void parseRequest_rejectsEmptyTargetOnCall();
    void parseRequest_rejectsEmptyFnOnCall();
    void parseRequest_rejectsEmptySignalOnSubscribe();
    void parseRequest_rejectsUnsubscribeMissingSubscriptionId();
    void parseRequest_acceptsUnsubscribe();
    void variantToJson_basics();
};

void TestPhosphorIpcProtocol::parseRequest_acceptsValidCall()
{
    const QByteArray line = R"({"type":"call","id":42,"target":"greet","fn":"sayHello","args":["nate"]})";
    QString err;
    const auto r = parseRequest(line, &err);
    QVERIFY(r.has_value());
    QVERIFY(err.isEmpty());
    QCOMPARE(r->type, QStringLiteral("call"));
    QCOMPARE(r->id, 42);
    QCOMPARE(r->target, QStringLiteral("greet"));
    QCOMPARE(r->fn, QStringLiteral("sayHello"));
    QCOMPARE(r->args.size(), 1);
    QCOMPARE(r->args.at(0).toString(), QStringLiteral("nate"));
}

void TestPhosphorIpcProtocol::parseRequest_acceptsList()
{
    const auto r = parseRequest(R"({"type":"list","id":1})", nullptr);
    QVERIFY(r.has_value());
    QCOMPARE(r->type, QStringLiteral("list"));
    QCOMPARE(r->id, 1);
}

void TestPhosphorIpcProtocol::parseRequest_acceptsSchema()
{
    const auto r = parseRequest(R"({"type":"schema","id":2,"target":"greet"})", nullptr);
    QVERIFY(r.has_value());
    QCOMPARE(r->type, QStringLiteral("schema"));
    QCOMPARE(r->target, QStringLiteral("greet"));
}

void TestPhosphorIpcProtocol::parseRequest_acceptsSubscribe()
{
    const auto r = parseRequest(R"({"type":"subscribe","id":3,"target":"count","signal":"countChanged"})", nullptr);
    QVERIFY(r.has_value());
    QCOMPARE(r->type, QStringLiteral("subscribe"));
    QCOMPARE(r->target, QStringLiteral("count"));
    QCOMPARE(r->signalName, QStringLiteral("countChanged"));
}

void TestPhosphorIpcProtocol::parseRequest_rejectsMissingType()
{
    QString err;
    QVERIFY(!parseRequest(R"({"id":1})", &err).has_value());
    QVERIFY(err.contains(QStringLiteral("'type'")));
}

void TestPhosphorIpcProtocol::parseRequest_rejectsMalformedJson()
{
    QString err;
    QVERIFY(!parseRequest("{ broken json", &err).has_value());
    QVERIFY(err.contains(QStringLiteral("malformed JSON")));
}

void TestPhosphorIpcProtocol::parseRequest_rejectsNonObjectRoot()
{
    QString err;
    QVERIFY(!parseRequest("[1,2,3]", &err).has_value());
    QVERIFY(err.contains(QStringLiteral("not a JSON object")));
}

void TestPhosphorIpcProtocol::parseRequest_rejectsNonArrayArgs()
{
    // Provide target/fn so the parser doesn't bail on the
    // per-type required-field check first; this test exists to
    // exercise the args-type validation branch specifically.
    QString err;
    QVERIFY(!parseRequest(R"({"type":"call","id":1,"target":"t","fn":"f","args":42})", &err).has_value());
    QVERIFY(err.contains(QStringLiteral("'args'")));
}

void TestPhosphorIpcProtocol::parseRequest_argsDefaultEmpty()
{
    // Missing args is fine (no-arg call).
    const auto r = parseRequest(R"({"type":"call","id":1,"target":"x","fn":"y"})", nullptr);
    QVERIFY(r.has_value());
    QCOMPARE(r->args.size(), 0);
}

void TestPhosphorIpcProtocol::buildReply_shape()
{
    const QJsonObject obj = buildReply(42, QStringLiteral("hello"));
    QCOMPARE(obj.value(QLatin1String("type")).toString(), QStringLiteral("reply"));
    QCOMPARE(obj.value(QLatin1String("id")).toInt(), 42);
    QCOMPARE(obj.value(QLatin1String("result")).toString(), QStringLiteral("hello"));
}

void TestPhosphorIpcProtocol::buildEvent_shape()
{
    QJsonArray args;
    args.append(7);
    const QJsonObject obj = buildEvent(99, args);
    QCOMPARE(obj.value(QLatin1String("type")).toString(), QStringLiteral("event"));
    QCOMPARE(obj.value(QLatin1String("subscriptionId")).toInt(), 99);
    QCOMPARE(obj.value(QLatin1String("args")).toArray().first().toInt(), 7);
}

void TestPhosphorIpcProtocol::buildError_shape()
{
    QJsonObject detail;
    detail.insert(QLatin1String("expected"), QStringLiteral("int"));
    const QJsonObject obj = buildError(5, QStringLiteral("INVALID_ARG"), QStringLiteral("bad"), detail);
    QCOMPARE(obj.value(QLatin1String("type")).toString(), QStringLiteral("error"));
    QCOMPARE(obj.value(QLatin1String("id")).toInt(), 5);
    QCOMPARE(obj.value(QLatin1String("code")).toString(), QStringLiteral("INVALID_ARG"));
    QCOMPARE(obj.value(QLatin1String("message")).toString(), QStringLiteral("bad"));
    QCOMPARE(obj.value(QLatin1String("detail")).toObject().value(QLatin1String("expected")).toString(),
             QStringLiteral("int"));
}

void TestPhosphorIpcProtocol::buildError_omitsZeroId()
{
    // id == 0 is reserved for "no client correlation" (e.g.,
    // malformed-line responses before we parsed an id). The error
    // shape should omit the id field rather than emit 0.
    const QJsonObject obj = buildError(0, QStringLiteral("X"), QStringLiteral("y"));
    QVERIFY(!obj.contains(QLatin1String("id")));
}

void TestPhosphorIpcProtocol::writeLine_appendsNewline()
{
    QJsonObject obj;
    obj.insert(QLatin1String("a"), 1);
    const QByteArray bytes = writeLine(obj);
    QVERIFY(bytes.endsWith('\n'));
    QVERIFY(!bytes.left(bytes.size() - 1).contains('\n'));
}

void TestPhosphorIpcProtocol::parseRequest_rejectsNonNumericId()
{
    QString err;
    QVERIFY(!parseRequest(R"({"type":"call","id":"forty-two","target":"x","fn":"y"})", &err).has_value());
    QVERIFY(err.contains(QStringLiteral("'id'")));
}

void TestPhosphorIpcProtocol::parseRequest_rejectsFractionalId()
{
    // JSON numbers are doubles. `"id": 1.5` would silently truncate
    // to id=1 if accepted; reject it so the wire protocol enforces
    // the documented "id is integer" contract.
    QString err;
    QVERIFY(!parseRequest(R"({"type":"call","id":1.5,"target":"x","fn":"y"})", &err).has_value());
    QVERIFY(err.contains(QStringLiteral("'id'")));
}

void TestPhosphorIpcProtocol::parseRequest_rejectsOutOfRangeId()
{
    // 1e30 exceeds INT64_MAX and would be UB to cast. The parser
    // must reject it as malformed, not truncate.
    QString err;
    QVERIFY(!parseRequest(R"({"type":"call","id":1e30,"target":"x","fn":"y"})", &err).has_value());
    QVERIFY(err.contains(QStringLiteral("'id'")));
}

void TestPhosphorIpcProtocol::parseRequest_rejectsEmptyTargetOnCall()
{
    // Empty target on a `call` is malformed at the wire level; the
    // router would otherwise surface NO_SUCH_TARGET, which is the
    // wrong shape (the fault is the request, not the registry).
    // Both shapes are pinned — ABSENT field and explicit "" literal —
    // so a future parser refactor that distinguishes missing from
    // empty can't silently start accepting the empty-literal form.
    QString err;
    QVERIFY(!parseRequest(R"({"type":"call","id":1,"fn":"y"})", &err).has_value());
    QVERIFY(err.contains(QStringLiteral("'target'")));
    QVERIFY(!parseRequest(R"({"type":"call","id":1,"target":"","fn":"y"})", &err).has_value());
    QVERIFY(err.contains(QStringLiteral("'target'")));
}

void TestPhosphorIpcProtocol::parseRequest_rejectsEmptyFnOnCall()
{
    // Absent and explicit-empty forms, same rationale as the target test.
    QString err;
    QVERIFY(!parseRequest(R"({"type":"call","id":1,"target":"x"})", &err).has_value());
    QVERIFY(err.contains(QStringLiteral("'fn'")));
    QVERIFY(!parseRequest(R"({"type":"call","id":1,"target":"x","fn":""})", &err).has_value());
    QVERIFY(err.contains(QStringLiteral("'fn'")));
}

void TestPhosphorIpcProtocol::parseRequest_rejectsEmptySignalOnSubscribe()
{
    // Absent and explicit-empty forms, same rationale as the target test.
    QString err;
    QVERIFY(!parseRequest(R"({"type":"subscribe","id":1,"target":"x"})", &err).has_value());
    QVERIFY(err.contains(QStringLiteral("'signal'")));
    QVERIFY(!parseRequest(R"({"type":"subscribe","id":1,"target":"x","signal":""})", &err).has_value());
    QVERIFY(err.contains(QStringLiteral("'signal'")));
}

void TestPhosphorIpcProtocol::parseRequest_rejectsUnsubscribeMissingSubscriptionId()
{
    // Unsubscribe with no subscriptionId (or an explicit zero) must
    // surface as MALFORMED_REQUEST, not as NO_SUCH_SUBSCRIPTION for
    // "unknown subscriptionId 0".
    QString err;
    QVERIFY(!parseRequest(R"({"type":"unsubscribe","id":1})", &err).has_value());
    QVERIFY(err.contains(QStringLiteral("subscriptionId")));
}

void TestPhosphorIpcProtocol::parseRequest_acceptsUnsubscribe()
{
    const auto r = parseRequest(R"({"type":"unsubscribe","id":3,"subscriptionId":42})", nullptr);
    QVERIFY(r.has_value());
    QCOMPARE(r->type, QStringLiteral("unsubscribe"));
    QCOMPARE(r->id, 3);
    QCOMPARE(r->subscriptionId, 42);
}

void TestPhosphorIpcProtocol::variantToJson_basics()
{
    QCOMPARE(variantToJson(QVariant()), QJsonValue(QJsonValue::Null));
    QCOMPARE(variantToJson(QVariant(true)), QJsonValue(true));
    QCOMPARE(variantToJson(QVariant(42)).toInt(), 42);
    QCOMPARE(variantToJson(QVariant(1.5)).toDouble(), 1.5);
    QCOMPARE(variantToJson(QVariant(QStringLiteral("hi"))).toString(), QStringLiteral("hi"));

    QVariantList list;
    list.append(1);
    list.append(QStringLiteral("two"));
    const QJsonArray arr = variantToJson(list).toArray();
    QCOMPARE(arr.size(), 2);
    QCOMPARE(arr.at(0).toInt(), 1);
    QCOMPARE(arr.at(1).toString(), QStringLiteral("two"));

    QVariantMap map;
    map.insert(QStringLiteral("k"), QStringLiteral("v"));
    const QJsonObject obj = variantToJson(map).toObject();
    QCOMPARE(obj.value(QStringLiteral("k")).toString(), QStringLiteral("v"));
}

QTEST_GUILESS_MAIN(TestPhosphorIpcProtocol)
#include "test_phosphor_ipc_protocol.moc"
