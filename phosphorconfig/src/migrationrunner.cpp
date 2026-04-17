// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorConfig/JsonBackend.h>
#include <PhosphorConfig/MigrationRunner.h>
#include <PhosphorConfig/Schema.h>

#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>

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
{
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
    int version = readVersion(root);
    for (const MigrationStep& step : m_schema.migrations) {
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
        const int bumped = root.value(m_schema.versionKey).toInt();
        if (bumped != step.fromVersion + 1) {
            qCritical("PhosphorConfig::MigrationRunner: step v%d did not bump '%s' to %d (got %d) — aborting chain",
                      step.fromVersion, qPrintable(m_schema.versionKey), step.fromVersion + 1, bumped);
            return;
        }
        version = bumped;
    }

    // Chain finished but we didn't reach the declared target version — the
    // schema is missing a step for some intermediate version. Silent skip
    // here would leave users permanently stuck at the stalled version with
    // no diagnostic.
    if (version < m_schema.version) {
        qWarning(
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
    const int newVersion = root.value(m_schema.versionKey).toInt();

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
