// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorConfig/JsonBackend.h>
#include <PhosphorConfig/MigrationRunner.h>
#include <PhosphorConfig/Schema.h>

#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>

#include <algorithm>

namespace PhosphorConfig {

// ─── Schema inline helpers ───────────────────────────────────────────────────

const KeyDef* Schema::findKey(const QString& group, const QString& key) const
{
    auto it = groups.constFind(group);
    if (it == groups.constEnd()) {
        return nullptr;
    }
    for (const KeyDef& def : *it) {
        if (def.key == key) {
            return &def;
        }
    }
    return nullptr;
}

QVariant Schema::defaultFor(const QString& group, const QString& key) const
{
    if (const KeyDef* def = findKey(group, key)) {
        return def->defaultValue;
    }
    return {};
}

// ─── MigrationRunner ─────────────────────────────────────────────────────────

MigrationRunner::MigrationRunner(const Schema& schema)
    : m_schema(schema)
    , m_orderedSteps(schema.migrations)
{
    // Refuse to run migrations when there's no version key to stamp. Without
    // it, readVersion() always reports 1, stampVersion() writes to root[""]
    // (an empty-string JSON key — silent data corruption), and every startup
    // re-runs the same v1→v2 step forever. Stores configured with an empty
    // versionKey deliberately skip version stamping, so migrations in the
    // same schema are a contradiction the caller must resolve. Clearing the
    // step list here lets the runner no-op safely instead of looping.
    if (m_schema.versionKey.isEmpty() && !m_orderedSteps.isEmpty()) {
        qCritical(
            "PhosphorConfig::MigrationRunner: schema declares %lld migration step(s) but has an empty versionKey "
            "— migrations cannot run without somewhere to stamp the target version. Schemas that need migrations "
            "must set versionKey; schemas that intentionally skip version stamping must not declare migrations.",
            static_cast<long long>(m_orderedSteps.size()));
        m_orderedSteps.clear();
        return;
    }

    // Sort once at construction so registration order doesn't matter and
    // runInMemory doesn't pay the sort cost on every call. Stable sort
    // preserves the relative order of any (illegal but possible) duplicate
    // fromVersion entries — the runner aborts on the second one anyway.
    // Short-circuit on 0/1 steps (the common case: single-version or
    // freshly-introduced schema) to avoid spinning up a comparator.
    if (m_orderedSteps.size() > 1) {
        std::stable_sort(m_orderedSteps.begin(), m_orderedSteps.end(),
                         [](const MigrationStep& a, const MigrationStep& b) {
                             return a.fromVersion < b.fromVersion;
                         });
    }
}

int MigrationRunner::readVersion(const QJsonObject& root) const
{
    const int v = root.value(m_schema.versionKey).toInt(1);
    return v < 1 ? 1 : v;
}

void MigrationRunner::stampVersion(QJsonObject& root) const
{
    root[m_schema.versionKey] = m_schema.version;
}

void MigrationRunner::runInMemory(QJsonObject& root) const
{
    // m_orderedSteps is sorted at construction, so a schema that declares
    // {2,...} before {1,...} still runs v1→v2 first.
    int version = readVersion(root);
    for (const MigrationStep& step : m_orderedSteps) {
        if (version >= m_schema.version) {
            break; // Already at or past target; remaining steps are no-ops.
        }
        if (version != step.fromVersion) {
            continue;
        }
        if (!step.migrate) {
            qCritical("PhosphorConfig::MigrationRunner: step v%d has null migrate function — aborting chain",
                      step.fromVersion);
            return;
        }
        qInfo("PhosphorConfig::MigrationRunner: running migration v%d → v%d", step.fromVersion, step.fromVersion + 1);
        step.migrate(root);
        // Use readVersion() so a migration that stamps a string-typed value
        // (or accidentally clears the key) is reported as a bump mismatch
        // consistently with how the initial version is read.
        const int bumped = readVersion(root);
        if (bumped != step.fromVersion + 1) {
            qCritical("PhosphorConfig::MigrationRunner: step v%d did not bump '%s' to %d (got %d) — aborting chain",
                      step.fromVersion, qPrintable(m_schema.versionKey), step.fromVersion + 1, bumped);
            return;
        }
        version = bumped;
    }

    // Chain finished but we didn't reach the declared target version — the
    // schema is missing a step for some intermediate version. This is a
    // permanent stall: every startup will leave the user at the stalled
    // version with no recovery path until the schema gains the missing
    // step, so log at @c qCritical to surface it as actionable rather than
    // background noise.
    if (version < m_schema.version) {
        qCritical(
            "PhosphorConfig::MigrationRunner: chain exhausted at v%d but Schema::version is %d — no step found "
            "with fromVersion=%d. Persisted config will NOT reach the target schema version.",
            version, m_schema.version, version);
    }
}

bool MigrationRunner::runOnFile(const QString& jsonPath) const
{
    if (!QFile::exists(jsonPath)) {
        return true; // Fresh install — nothing to migrate.
    }

    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning("PhosphorConfig::MigrationRunner: failed to open %s for migration", qPrintable(jsonPath));
        return false;
    }
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning("PhosphorConfig::MigrationRunner: invalid JSON in %s during migration", qPrintable(jsonPath));
        return false;
    }

    QJsonObject root = doc.object();
    const int oldVersion = readVersion(root);
    runInMemory(root);
    // Use readVersion (which clamps <1 → 1) for parity with the start-of-chain
    // read, so a migration that left the version key cleared is reported as
    // "no advance" instead of accidentally appearing to roll back to 0.
    const int newVersion = readVersion(root);

    if (newVersion == oldVersion) {
        return true;
    }

    if (!JsonBackend::writeJsonAtomically(jsonPath, root)) {
        qWarning("PhosphorConfig::MigrationRunner: failed to write migrated config to %s", qPrintable(jsonPath));
        return false;
    }
    qInfo("PhosphorConfig::MigrationRunner: schema migration v%d → v%d complete", oldVersion, newVersion);
    return true;
}

} // namespace PhosphorConfig
