// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorFsLoader/phosphorfsloader_export.h>

#include <QtCore/QByteArray>
#include <QtCore/QJsonValue>
#include <QtCore/QList>
#include <QtCore/QString>

#include <memory>
#include <optional>

class QLoggingCategory;

namespace PhosphorFsLoader {

/**
 * @brief Validates parsed JSON documents against a JSON Schema (Draft 7).
 *
 * Thin, dependency-free wrapper over the vendored valijson engine. The
 * schema-validation library (valijson + its Qt adapter) is an implementation
 * detail confined to the .cpp; this header exposes only Qt/std types so the
 * engine never leaks into the public ABI or into consumers' include closure.
 *
 * Construct one validator per document type from the serialized schema bytes
 * (typically a schema embedded as a Qt resource), compile it once, and reuse
 * it across every file of that type — schema compilation is the expensive step
 * and @c validate() is cheap to call repeatedly.
 *
 * Failure model is fail-closed. If the schema bytes are themselves malformed,
 * @c isValid() returns false and every @c validate() call reports a single
 * error rather than silently passing documents through unvalidated.
 */
class PHOSPHORFSLOADER_EXPORT SchemaValidator
{
public:
    /// A single schema violation.
    struct Error
    {
        /// JSON Pointer to the node that failed validation (e.g.
        /// "/zones/0/relativeGeometry/width"). Empty for a document-root
        /// failure.
        QString path;

        /// Human-readable description of why the node failed.
        QString message;
    };

    /**
     * @brief Compile a JSON Schema from its serialized bytes.
     *
     * @param schemaJson  UTF-8 JSON text of a Draft 7 schema. A malformed or
     *                    non-object schema leaves the validator in an invalid
     *                    state (@c isValid() == false) instead of throwing.
     */
    explicit SchemaValidator(const QByteArray& schemaJson);
    ~SchemaValidator();

    SchemaValidator(SchemaValidator&&) noexcept;
    SchemaValidator& operator=(SchemaValidator&&) noexcept;
    SchemaValidator(const SchemaValidator&) = delete;
    SchemaValidator& operator=(const SchemaValidator&) = delete;

    /**
     * @brief Build a validator from a Qt resource (or file) path, failing closed.
     *
     * Opens @p resourcePath and compiles the schema from its bytes. If the
     * resource cannot be opened — for an RCC-embedded schema this means a build
     * regression — it logs a warning at @p category and returns a validator
     * whose @c isValid() is false, so every @c validate() call rejects rather
     * than silently passing documents through unvalidated. The single place the
     * embed-and-fail-closed contract lives, shared by every loader.
     */
    [[nodiscard]] static SchemaValidator fromResource(const QString& resourcePath, const QLoggingCategory& category);

    /// True when the schema compiled successfully and the validator is usable.
    [[nodiscard]] bool isValid() const;

    /**
     * @brief Validate a parsed JSON document against the compiled schema.
     *
     * @param document  Any parsed JSON value (typically a QJsonObject).
     * @return @c std::nullopt when the document satisfies the schema;
     *         otherwise the non-empty list of violations. A validator whose
     *         schema failed to compile always returns a one-element list.
     */
    [[nodiscard]] std::optional<QList<Error>> validate(const QJsonValue& document) const;

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorFsLoader
