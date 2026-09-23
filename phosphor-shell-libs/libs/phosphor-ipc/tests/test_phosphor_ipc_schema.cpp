// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Validates the QMetaType → JSON Schema table documented in
// IpcSchemaGenerator.h. Each test exercises one type by registering
// a target QObject whose Q_INVOKABLE method takes / returns that
// type, then asserts the generated schema entry.

#include <PhosphorIpc/IpcSchemaGenerator.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QVariantList>
#include <QVariantMap>
#include <QtCore/qtclasshelpermacros.h>

using namespace PhosphorIpc;

namespace {

class TypeProbe : public QObject
{
    Q_OBJECT
public:
    explicit TypeProbe(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    Q_DISABLE_COPY_MOVE(TypeProbe)

    Q_INVOKABLE void takeBool(bool /*v*/)
    {
    }
    Q_INVOKABLE void takeInt(int /*v*/)
    {
    }
    Q_INVOKABLE void takeLongLong(qlonglong /*v*/)
    {
    }
    Q_INVOKABLE void takeDouble(double /*v*/)
    {
    }
    Q_INVOKABLE void takeFloat(float /*v*/)
    {
    }
    Q_INVOKABLE void takeString(const QString& /*v*/)
    {
    }
    Q_INVOKABLE void takeStringList(const QStringList& /*v*/)
    {
    }
    Q_INVOKABLE void takeVariantList(const QVariantList& /*v*/)
    {
    }
    Q_INVOKABLE void takeVariantMap(const QVariantMap& /*v*/)
    {
    }
    Q_INVOKABLE QString returnString()
    {
        return QStringLiteral("x");
    }
    Q_INVOKABLE int returnInt()
    {
        return 1;
    }
    Q_INVOKABLE void returnVoid()
    {
    }

Q_SIGNALS:
    void aSignal(int n);
};

QJsonObject findFunction(const QJsonObject& schema, const QString& name)
{
    const QJsonArray fns = schema.value(QStringLiteral("functions")).toArray();
    for (const QJsonValue& v : fns) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("name")).toString() == name) {
            return o;
        }
    }
    return {};
}

} // namespace

class TestPhosphorIpcSchema : public QObject
{
    Q_OBJECT
public:
    Q_DISABLE_COPY_MOVE(TestPhosphorIpcSchema)
    TestPhosphorIpcSchema() = default;
private Q_SLOTS:
    void nullObject_emitsEmptyArrays();
    void boolean_param();
    void integer_int();
    void integer_longLong();
    void number_double();
    void number_float();
    void string_QString();
    void array_QStringList();
    void array_QVariantList();
    void object_QVariantMap();
    void returns_string();
    void returns_int();
    void returns_void_omitted();
    void signals_enumerated();
};

void TestPhosphorIpcSchema::nullObject_emitsEmptyArrays()
{
    const QJsonObject s = IpcSchemaGenerator::schemaFor(QStringLiteral("ghost"), nullptr);
    QCOMPARE(s.value(QStringLiteral("target")).toString(), QStringLiteral("ghost"));
    QCOMPARE(s.value(QStringLiteral("functions")).toArray().size(), 0);
    QCOMPARE(s.value(QStringLiteral("signals")).toArray().size(), 0);
}

void TestPhosphorIpcSchema::boolean_param()
{
    TypeProbe t;
    const QJsonObject fn =
        findFunction(IpcSchemaGenerator::schemaFor(QStringLiteral("probe"), &t), QStringLiteral("takeBool"));
    QCOMPARE(fn.value(QStringLiteral("params")).toArray().first().toObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("boolean"));
}

void TestPhosphorIpcSchema::integer_int()
{
    TypeProbe t;
    const QJsonObject fn =
        findFunction(IpcSchemaGenerator::schemaFor(QStringLiteral("probe"), &t), QStringLiteral("takeInt"));
    QCOMPARE(fn.value(QStringLiteral("params")).toArray().first().toObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("integer"));
}

void TestPhosphorIpcSchema::integer_longLong()
{
    TypeProbe t;
    const QJsonObject fn =
        findFunction(IpcSchemaGenerator::schemaFor(QStringLiteral("probe"), &t), QStringLiteral("takeLongLong"));
    QCOMPARE(fn.value(QStringLiteral("params")).toArray().first().toObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("integer"));
}

void TestPhosphorIpcSchema::number_double()
{
    TypeProbe t;
    const QJsonObject fn =
        findFunction(IpcSchemaGenerator::schemaFor(QStringLiteral("probe"), &t), QStringLiteral("takeDouble"));
    QCOMPARE(fn.value(QStringLiteral("params")).toArray().first().toObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("number"));
}

void TestPhosphorIpcSchema::number_float()
{
    TypeProbe t;
    const QJsonObject fn =
        findFunction(IpcSchemaGenerator::schemaFor(QStringLiteral("probe"), &t), QStringLiteral("takeFloat"));
    QCOMPARE(fn.value(QStringLiteral("params")).toArray().first().toObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("number"));
}

void TestPhosphorIpcSchema::string_QString()
{
    TypeProbe t;
    const QJsonObject fn =
        findFunction(IpcSchemaGenerator::schemaFor(QStringLiteral("probe"), &t), QStringLiteral("takeString"));
    QCOMPARE(fn.value(QStringLiteral("params")).toArray().first().toObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("string"));
}

void TestPhosphorIpcSchema::array_QStringList()
{
    TypeProbe t;
    const QJsonObject fn =
        findFunction(IpcSchemaGenerator::schemaFor(QStringLiteral("probe"), &t), QStringLiteral("takeStringList"));
    const QJsonObject p = fn.value(QStringLiteral("params")).toArray().first().toObject();
    QCOMPARE(p.value(QStringLiteral("type")).toString(), QStringLiteral("array"));
    QCOMPARE(p.value(QStringLiteral("items")).toObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("string"));
}

void TestPhosphorIpcSchema::array_QVariantList()
{
    TypeProbe t;
    const QJsonObject fn =
        findFunction(IpcSchemaGenerator::schemaFor(QStringLiteral("probe"), &t), QStringLiteral("takeVariantList"));
    const QJsonObject p = fn.value(QStringLiteral("params")).toArray().first().toObject();
    QCOMPARE(p.value(QStringLiteral("type")).toString(), QStringLiteral("array"));
    QVERIFY(!p.contains(QStringLiteral("items"))); // untyped element
}

void TestPhosphorIpcSchema::object_QVariantMap()
{
    TypeProbe t;
    const QJsonObject fn =
        findFunction(IpcSchemaGenerator::schemaFor(QStringLiteral("probe"), &t), QStringLiteral("takeVariantMap"));
    QCOMPARE(fn.value(QStringLiteral("params")).toArray().first().toObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("object"));
}

void TestPhosphorIpcSchema::returns_string()
{
    TypeProbe t;
    const QJsonObject fn =
        findFunction(IpcSchemaGenerator::schemaFor(QStringLiteral("probe"), &t), QStringLiteral("returnString"));
    QCOMPARE(fn.value(QStringLiteral("returns")).toObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("string"));
}

void TestPhosphorIpcSchema::returns_int()
{
    TypeProbe t;
    const QJsonObject fn =
        findFunction(IpcSchemaGenerator::schemaFor(QStringLiteral("probe"), &t), QStringLiteral("returnInt"));
    QCOMPARE(fn.value(QStringLiteral("returns")).toObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("integer"));
}

void TestPhosphorIpcSchema::returns_void_omitted()
{
    TypeProbe t;
    const QJsonObject fn =
        findFunction(IpcSchemaGenerator::schemaFor(QStringLiteral("probe"), &t), QStringLiteral("returnVoid"));
    QVERIFY(!fn.contains(QStringLiteral("returns")));
}

void TestPhosphorIpcSchema::signals_enumerated()
{
    TypeProbe t;
    const QJsonObject s = IpcSchemaGenerator::schemaFor(QStringLiteral("probe"), &t);
    const QJsonArray sigs = s.value(QStringLiteral("signals")).toArray();
    QCOMPARE(sigs.size(), 1);
    QCOMPARE(sigs.first().toObject().value(QStringLiteral("name")).toString(), QStringLiteral("aSignal"));
    QCOMPARE(sigs.first()
                 .toObject()
                 .value(QStringLiteral("params"))
                 .toArray()
                 .first()
                 .toObject()
                 .value(QStringLiteral("type"))
                 .toString(),
             QStringLiteral("integer"));
}

QTEST_GUILESS_MAIN(TestPhosphorIpcSchema)
#include "test_phosphor_ipc_schema.moc"
