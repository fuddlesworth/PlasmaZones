// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorconfig_export.h>

#include <QJsonObject>
#include <QString>

namespace PhosphorConfig {

struct Schema;

/// Executes a @c Schema's migration chain against a JSON document.
///
/// Typical use: call @c runOnFile() once at startup, before opening a
/// @c JsonBackend against the same path. The runner is idempotent — if the
/// on-disk version already matches @c Schema::version it returns without
/// rewriting the file.
class PHOSPHORCONFIG_EXPORT MigrationRunner
{
public:
    explicit MigrationRunner(const Schema& schema);

    /// Apply the full migration chain in memory. Steps whose
    /// @c fromVersion matches the current @c Schema::versionKey are
    /// invoked in declared order; each step MUST bump the version by one.
    /// On a bump mismatch the chain aborts and an error is logged.
    void runInMemory(QJsonObject& root) const;

    /// Read the file at @p jsonPath, run the chain, and — if the version
    /// advanced — atomically write the result back. Returns @c true on
    /// success, on "nothing to do", or on "file doesn't exist" (fresh
    /// install). Returns @c false only on parse or write errors.
    bool runOnFile(const QString& jsonPath) const;

    /// Current version persisted in @p root, or 1 if unset / invalid.
    int readVersion(const QJsonObject& root) const;

    /// Stamp @p root with the schema's current version.
    void stampVersion(QJsonObject& root) const;

private:
    const Schema& m_schema;
};

} // namespace PhosphorConfig
