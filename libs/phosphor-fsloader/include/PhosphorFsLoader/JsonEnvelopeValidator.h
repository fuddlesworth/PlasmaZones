// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorFsLoader/DirectoryLoader.h>

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QLatin1String>
#include <QtCore/QLoggingCategory>
#include <QtCore/QString>

#include <optional>
#include <utility>

namespace PhosphorFsLoader {

/**
 * @brief Result of a successful envelope validation.
 *
 * `name` is the validated `"name"` field (non-empty, matches the
 * filename's `completeBaseName`). `root` is the parsed JSON object with
 * the `"name"` field already removed — sinks can pass it straight to a
 * schema-specific `fromJson` that doesn't need to know about the
 * envelope's bookkeeping fields.
 */
struct JsonEnvelope
{
    QString name;
    QJsonObject root;
};

/**
 * @brief Validate the default envelope used by `DirectoryLoader` sinks.
 *
 * Every `IDirectoryLoaderSink` whose schema follows the default
 * `name == filename basename` envelope (CurveLoader and ProfileLoader
 * today) starts its `parseFile` with the same boilerplate:
 *
 *   1. Open the file (skip on read error).
 *   2. Parse JSON (skip on malformed).
 *   3. Reject non-object roots.
 *   4. Require a non-empty `"name"` field.
 *   5. Reject mismatch between `"name"` and the file's `completeBaseName`
 *      (silent shadowing guard — a user copies `widget.fade.json →
 *      custom.json` and forgets to rename the inner field, the result
 *      registers under the original key while the file on disk suggests
 *      a different identity).
 *
 * Loaders with a different envelope shape (e.g. `ShaderRegistry`, which
 * keys by directory name and parses `metadata.json` with its own
 * schema) do not use this helper — they roll their own parse pass.
 *
 * On success this returns the parsed root with `"name"` stripped —
 * sinks can pass `root` straight into a schema-specific `fromJson`
 * without re-handling the bookkeeping field. On failure it logs a clear
 * diagnostic at the supplied logging category and returns
 * `std::nullopt`.
 *
 * @param filePath  Absolute path to the JSON file. Used for the read,
 *                  for the `completeBaseName` diagnostic, and for log
 *                  output.
 * @param category  Caller's `Q_LOGGING_CATEGORY` so warnings are tagged
 *                  with the consumer-specific category (e.g.
 *                  `"phosphoranimation.curveloader"`) rather than a
 *                  generic shared category — operators filtering log
 *                  output by category keep their existing rules.
 *
 * Header-only inline so consumers don't pay a translation-unit boundary
 * crossing per parse — every loader sink is in a single source file
 * already, and this helper is on the per-file hot path.
 */
inline std::optional<JsonEnvelope> validateJsonEnvelope(const QString& filePath, const QLoggingCategory& category)
{
    // Single source of truth for the JSON-envelope size cap is
    // `DirectoryLoader::kMaxFileBytes`. The loader applies it first via
    // the default sink dispatch path; this helper enforces it again
    // because `validateJsonEnvelope` is a public free function and may
    // be called directly (without a loader stat in front of it). One
    // extra `QFileInfo::size()` per direct call — microscopic compared
    // with the alternative of letting a 2 GiB blob fall through to a
    // caller that didn't stat itself.
    QFileInfo info(filePath);
    if (info.exists() && info.size() > DirectoryLoader::kMaxFileBytes) {
        qCWarning(category).nospace() << "Skipping " << filePath << ": file size " << info.size() << " exceeds limit "
                                      << DirectoryLoader::kMaxFileBytes;
        return std::nullopt;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(category) << "Skipping unreadable file" << filePath << ":" << file.errorString();
        return std::nullopt;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(category) << "Skipping malformed JSON" << filePath << ":" << parseError.errorString();
        return std::nullopt;
    }
    if (!doc.isObject()) {
        qCWarning(category) << "Skipping non-object root JSON in" << filePath;
        return std::nullopt;
    }

    QJsonObject root = doc.object();

    const QString name = root.value(QLatin1String("name")).toString();
    if (name.isEmpty()) {
        qCWarning(category) << "Skipping" << filePath << ": missing required 'name' field";
        return std::nullopt;
    }

    // The filename (without extension) is the user's ergonomic handle on
    // the entity; the inner `name` field is what actually gets
    // registered. If the two diverge — typically because the user copied
    // `widget.fade.json → custom.json` and forgot to rename the inner
    // field — the result registers under the inner-name key while the
    // file on disk suggests a different identity. Reject up front with a
    // clear diagnostic naming both sides.
    const QString basename = QFileInfo(filePath).completeBaseName();
    if (name != basename) {
        qCWarning(category).nospace() << "Skipping " << filePath << ": name '" << name << "' does not match filename '"
                                      << basename << "' — rejecting to avoid silent shadowing";
        return std::nullopt;
    }

    // Strip the bookkeeping field so the sink's schema-specific
    // `fromJson` doesn't need to know about it (e.g. ProfileLoader's
    // `Profile::fromJson` would otherwise leak `name` into `presetName`).
    root.remove(QLatin1String("name"));

    return JsonEnvelope{name, std::move(root)};
}

} // namespace PhosphorFsLoader
