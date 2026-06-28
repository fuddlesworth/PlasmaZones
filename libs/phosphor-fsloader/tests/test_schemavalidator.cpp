// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Coverage for SchemaValidator: the thin valijson wrapper that backs
// schema-validated JSON loading. Pins the contract (valid passes, each
// structural violation is reported with a JSON-Pointer path, and a bad
// schema fails closed) so a valijson bump or wrapper refactor surfaces here.

#include <PhosphorFsLoader/SchemaValidator.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QTemporaryFile>
#include <QTest>

#include <utility>

using namespace PhosphorFsLoader;

namespace {

// A small representative schema: required keys, a typed/pattern-constrained
// scalar, an array-of-objects, and a nested object with bounded numbers.
// No "$schema" key: valijson defaults to Draft 7, and a "http://" URL inside
// a multi-line raw string trips moc's line-comment scanner (it treats "//" as
// a comment start), which would hide this file's Q_OBJECT class from moc.
QByteArray sampleSchema()
{
    return QByteArrayLiteral(R"({
        "type": "object",
        "required": ["id", "zones"],
        "properties": {
            "id": { "type": "string", "pattern": "^\\{.*\\}$" },
            "zones": {
                "type": "array",
                "minItems": 1,
                "items": {
                    "type": "object",
                    "required": ["geometry"],
                    "properties": {
                        "geometry": {
                            "type": "object",
                            "required": ["x", "width"],
                            "additionalProperties": false,
                            "properties": {
                                "x": { "type": "number", "minimum": 0, "maximum": 1 },
                                "width": { "type": "number", "exclusiveMinimum": 0, "maximum": 1 }
                            }
                        }
                    }
                }
            }
        }
    })");
}

QJsonObject parse(const char* json)
{
    return QJsonDocument::fromJson(json).object();
}

} // namespace

class TestSchemaValidator : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void compilesValidSchema()
    {
        const SchemaValidator validator(sampleSchema());
        QVERIFY(validator.isValid());
    }

    void acceptsConformingDocument()
    {
        const SchemaValidator validator(sampleSchema());
        const auto doc = parse(R"({"id":"{abc}","zones":[{"geometry":{"x":0,"width":0.5}}]})");
        QVERIFY(!validator.validate(doc).has_value());
    }

    void rejectsMissingRequiredKey()
    {
        const SchemaValidator validator(sampleSchema());
        const auto doc = parse(R"({"zones":[{"geometry":{"x":0,"width":0.5}}]})");
        QVERIFY(validator.validate(doc).has_value());
    }

    void rejectsEmptyArray()
    {
        const SchemaValidator validator(sampleSchema());
        const auto doc = parse(R"({"id":"{abc}","zones":[]})");
        QVERIFY(validator.validate(doc).has_value());
    }

    void rejectsOutOfRangeNumberWithPath()
    {
        const SchemaValidator validator(sampleSchema());
        const auto doc = parse(R"({"id":"{abc}","zones":[{"geometry":{"x":0,"width":1.5}}]})");
        const auto errors = validator.validate(doc);
        QVERIFY(errors.has_value());
        QVERIFY(!errors->isEmpty());
        // The JSON Pointer should point at the offending node.
        QCOMPARE(errors->first().path, QStringLiteral("/zones/0/geometry/width"));
        QVERIFY(!errors->first().message.isEmpty());
    }

    void rejectsZeroWidthExclusiveMinimum()
    {
        const SchemaValidator validator(sampleSchema());
        const auto doc = parse(R"({"id":"{abc}","zones":[{"geometry":{"x":0,"width":0}}]})");
        QVERIFY(validator.validate(doc).has_value());
    }

    void rejectsPatternViolation()
    {
        const SchemaValidator validator(sampleSchema());
        const auto doc = parse(R"({"id":"no-braces","zones":[{"geometry":{"x":0,"width":0.5}}]})");
        QVERIFY(validator.validate(doc).has_value());
    }

    void rejectsUnknownGeometryKey()
    {
        const SchemaValidator validator(sampleSchema());
        const auto doc = parse(R"({"id":"{abc}","zones":[{"geometry":{"x":0,"width":0.5,"wdith":0.5}}]})");
        QVERIFY(validator.validate(doc).has_value());
    }

    void malformedSchemaIsInvalidAndFailsClosed()
    {
        const SchemaValidator validator(QByteArrayLiteral("{ this is not json"));
        QVERIFY(!validator.isValid());
        // A validator whose schema failed to compile rejects every document
        // rather than passing it through unvalidated.
        const auto doc = parse(R"({"id":"{abc}","zones":[{"geometry":{"x":0,"width":0.5}}]})");
        const auto errors = validator.validate(doc);
        QVERIFY(errors.has_value());
        QCOMPARE(errors->size(), 1);
    }

    void nonObjectSchemaIsInvalid()
    {
        const SchemaValidator validator(QByteArrayLiteral("[1, 2, 3]"));
        QVERIFY(!validator.isValid());
    }

    void movePreservesCompiledSchema()
    {
        SchemaValidator source(sampleSchema());
        QVERIFY(source.isValid());
        const SchemaValidator moved(std::move(source));
        QVERIFY(moved.isValid());
        const auto doc = parse(R"({"id":"{abc}","zones":[{"geometry":{"x":0,"width":0.5}}]})");
        QVERIFY(!moved.validate(doc).has_value());
    }

    void fromResourceMissingFailsClosed()
    {
        // A missing resource (e.g. a build regression dropping the embedded
        // schema) must fail closed: invalid validator, every document rejected.
        const QLoggingCategory cat("test.schemavalidator.fromresource");
        const auto validator = SchemaValidator::fromResource(QStringLiteral("/no/such/schema.json"), cat);
        QVERIFY(!validator.isValid());
        const auto errors = validator.validate(parse(R"({"id":"{abc}","zones":[{"geometry":{"x":0,"width":0.5}}]})"));
        QVERIFY(errors.has_value());
        QCOMPARE(errors->size(), 1);
    }

    void fromResourceValidFileCompiles()
    {
        QTemporaryFile file;
        QVERIFY(file.open());
        file.write(sampleSchema());
        QVERIFY(file.flush());
        const QLoggingCategory cat("test.schemavalidator.fromresource");
        const auto validator = SchemaValidator::fromResource(file.fileName(), cat);
        QVERIFY(validator.isValid());
        const auto doc = parse(R"({"id":"{abc}","zones":[{"geometry":{"x":0,"width":0.5}}]})");
        QVERIFY(!validator.validate(doc).has_value());
    }

    void moveAssignmentPreservesCompiledSchema()
    {
        SchemaValidator source(sampleSchema());
        SchemaValidator target(QByteArrayLiteral("{}")); // empty schema, matches anything
        target = std::move(source);
        QVERIFY(target.isValid());
        const auto doc = parse(R"({"id":"{abc}","zones":[{"geometry":{"x":0,"width":0.5}}]})");
        QVERIFY(!target.validate(doc).has_value());
        const auto bad = parse(R"({"zones":[]})");
        QVERIFY(target.validate(bad).has_value());
    }

    void invalidPatternFailsClosedWithoutThrowing()
    {
        // valijson compiles `pattern` regexes lazily at validate() time, so an
        // invalid ECMAScript pattern only fails when a string is checked. It
        // must fail closed (a returned error list), never throw — an escaping
        // throw would terminate, since callers are built under -fno-exceptions.
        // NOTE: a plain (non-raw) string literal — an unbalanced '[' inside a
        // raw string confuses moc's lexer and hides this Q_OBJECT class.
        const SchemaValidator validator(QByteArrayLiteral("{\"type\":\"string\",\"pattern\":\"[\"}"));
        const auto errors = validator.validate(parse("\"abc\""));
        QVERIFY(errors.has_value());
        QVERIFY(!errors->isEmpty());
    }
};

QTEST_MAIN(TestSchemaValidator)
#include "test_schemavalidator.moc"
