// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorFsLoader/SchemaValidator.h>

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonParseError>
#include <QtCore/QString>

// valijson is vendored and confined to this translation unit. Including its
// Qt adapter lets it walk an already-parsed QJsonValue tree directly, so we
// never re-serialize or re-parse the document under validation.
#include <valijson/adapters/qtjson_adapter.hpp>
#include <valijson/schema.hpp>
#include <valijson/schema_parser.hpp>
#include <valijson/validation_results.hpp>
#include <valijson/validator.hpp>

#include <exception>
#include <utility>

namespace PhosphorFsLoader {

struct SchemaValidator::Private
{
    // The compiled schema. Populated once at construction; reused by every
    // validate() call. Wrapped in optional so a failed compile leaves it
    // empty and validate() can fail closed.
    valijson::Schema schema;
    bool compiled = false;

    // Diagnostic captured when schema compilation fails, surfaced to every
    // validate() call so callers see why validation is unavailable.
    QString compileError;
};

SchemaValidator::SchemaValidator(const QByteArray& schemaJson)
    : d(std::make_unique<Private>())
{
    QJsonParseError parseError;
    const QJsonDocument schemaDoc = QJsonDocument::fromJson(schemaJson, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        d->compileError = QStringLiteral("schema is not valid JSON: %1").arg(parseError.errorString());
        return;
    }
    if (!schemaDoc.isObject()) {
        d->compileError = QStringLiteral("schema root is not a JSON object");
        return;
    }

    // valijson's SchemaParser throws on a structurally invalid schema. The
    // schemas we compile are our own embedded resources (validated in CI), so
    // this is a build-time guarantee in practice — but catch defensively so a
    // bad schema degrades to fail-closed validation rather than terminating
    // the process.
    try {
        const valijson::adapters::QtJsonAdapter schemaAdapter(schemaDoc.object());
        valijson::SchemaParser parser; // defaults to Draft 7
        parser.populateSchema(schemaAdapter, d->schema);
        d->compiled = true;
    } catch (const std::exception& e) {
        d->compileError = QStringLiteral("schema failed to compile: %1").arg(QString::fromUtf8(e.what()));
    } catch (...) {
        d->compileError = QStringLiteral("schema failed to compile: unknown error");
    }
}

SchemaValidator::~SchemaValidator() = default;
SchemaValidator::SchemaValidator(SchemaValidator&&) noexcept = default;
SchemaValidator& SchemaValidator::operator=(SchemaValidator&&) noexcept = default;

bool SchemaValidator::isValid() const
{
    return d->compiled;
}

std::optional<QList<SchemaValidator::Error>> SchemaValidator::validate(const QJsonValue& document) const
{
    if (!d->compiled) {
        return QList<Error>{Error{QString(), d->compileError}};
    }

    valijson::ValidationResults results;
    const valijson::adapters::QtJsonAdapter targetAdapter(document);
    valijson::Validator validator;

    if (validator.validate(d->schema, targetAdapter, &results)) {
        return std::nullopt;
    }

    QList<Error> errors;
    errors.reserve(static_cast<int>(results.numErrors()));
    valijson::ValidationResults::Error result;
    while (results.popError(result)) {
        errors.append(Error{QString::fromStdString(result.jsonPointer), QString::fromStdString(result.description)});
    }
    return errors;
}

} // namespace PhosphorFsLoader
