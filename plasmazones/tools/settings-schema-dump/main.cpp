// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// plasmazones-settings-schema-dump — serialize the PlasmaZones settings
// schema to JSON, offline.
//
// The documentation site (phosphor-works.github.io/plasmazones/settings/)
// generates its settings reference from this output, the same way it
// generates the shader gallery from plasmazones-shader-render.  Both exist
// so the site tracks the app instead of drifting from it.
//
// Offline is the point.  `phosphorctl schema <target>` answers the same
// question but needs a running daemon on a live socket, which a docs build
// has no way to provide.  buildSettingsSchema() is pure — it reads no
// config file, opens no D-Bus connection, and touches no display — so this
// links plasmazones_core, calls it, and walks the result.
//
//     plasmazones-settings-schema-dump > settings-schema.json
//
// RANGES: a key's clamp bounds live inside its KeyDef::validator lambda,
// where nothing can read them back.  Rather than duplicate every bound in a
// second table that would rot, this recovers them by PROBING: validators
// are contractually idempotent coercions, so feeding one an absurdly small
// value returns the low bound and an absurdly large one returns the high
// bound.  See probeRange() for why that is sound and, just as important,
// where it refuses to answer — not every validator is a clamp, and a wrong
// range published as fact is worse than no range.

#include "config/settingsschema.h"

#include <PhosphorConfig/Schema.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMetaType>
#include <QTextStream>
#include <QVariant>

#include <limits>

namespace {

// QVariant → QJsonValue for the types the schema actually stores.  Colours
// and font families are persisted as strings, so the string branch covers
// them; QJsonValue::fromVariant handles the rest but turns an unset
// QVariant into Null rather than dropping it, which is what we want for a
// key whose default is deliberately empty (the "follow the colour scheme"
// sentinel, for one).
QJsonValue toJson(const QVariant& v)
{
    if (!v.isValid()) {
        return QJsonValue(QJsonValue::Null);
    }
    return QJsonValue::fromVariant(v);
}

QString typeName(QMetaType::Type t)
{
    if (t == QMetaType::UnknownType) {
        return QStringLiteral("any");
    }
    return QString::fromLatin1(QMetaType(t).name());
}

// Recover a numeric key's clamp bounds by running its validator against the
// extremes of the type.
//
// This is sound because of the idempotence contract KeyDef::validator
// documents: the validator is the single coercion path and must satisfy
// validator(validator(x)) == validator(x).  A clamp built from qBound
// therefore maps everything below its floor to exactly that floor, and
// everything above its ceiling to exactly that ceiling.  Two probes read
// both ends.
//
// It DECLINES to guess in three cases, reporting no range rather than a
// wrong one.
//
// A key with no validator is unclamped by definition.
//
// A validator that hands back the extreme it was given is not clamping —
// it is a passthrough — so an unchanged probe means "no bound here" rather
// than a published range of +/-INT_MAX.
//
// And a validator whose two probes AGREE is not clamping either.  Every
// enum-valued key in the schema validates by falling back to its default
// when handed something illegal, so both extremes come back as that one
// value.  Read as a range that says "min 0, max 0" — for a key that in
// fact accepts three legal values.  Such a key carries `choices`, which
// states what it accepts far better than any range could, so a degenerate
// probe is dropped on the floor.  A genuine single-value range would be
// vacuous documentation anyway.
bool probeRange(const PhosphorConfig::KeyDef& def, QJsonObject& out)
{
    if (!def.validator) {
        return false;
    }

    if (def.expectedType == QMetaType::Int) {
        const int lo = def.validator(QVariant(std::numeric_limits<int>::min())).toInt();
        const int hi = def.validator(QVariant(std::numeric_limits<int>::max())).toInt();
        if (lo == std::numeric_limits<int>::min() || hi == std::numeric_limits<int>::max()) {
            return false;
        }
        if (lo >= hi) { // >= : a degenerate lo == hi is a fallback, not a clamp
            return false;
        }
        out[QStringLiteral("min")] = lo;
        out[QStringLiteral("max")] = hi;
        return true;
    }

    if (def.expectedType == QMetaType::Double) {
        // -1e18/1e18 rather than the double extremes: a validator that
        // multiplies or adds before clamping would overflow to inf on
        // ±DBL_MAX and report a bound of inf, which JSON cannot represent.
        // Any real setting's bounds sit far inside this.
        const double lo = def.validator(QVariant(-1e18)).toDouble();
        const double hi = def.validator(QVariant(1e18)).toDouble();
        if (lo <= -1e18 || hi >= 1e18) {
            return false;
        }
        if (!(lo < hi)) { // also rejects NaN, and the degenerate lo == hi
            return false;
        }
        out[QStringLiteral("min")] = lo;
        out[QStringLiteral("max")] = hi;
        return true;
    }

    return false;
}

QJsonObject dumpKey(const PhosphorConfig::KeyDef& def)
{
    QJsonObject o;
    o[QStringLiteral("key")] = def.key;
    o[QStringLiteral("type")] = typeName(def.expectedType);
    o[QStringLiteral("default")] = toJson(def.defaultValue);

    // Emitted unconditionally, including when it is empty, so the site's
    // generator sees one shape for every key and a coverage count of
    // documented-vs-total is a trivial query against this file rather than
    // a source grep.
    o[QStringLiteral("description")] = def.description;

    if (!def.choices.isEmpty()) {
        QJsonArray choices;
        for (const PhosphorConfig::ChoiceDef& c : def.choices) {
            QJsonObject co;
            co[QStringLiteral("value")] = toJson(c.value);
            co[QStringLiteral("token")] = c.token;
            choices.append(co);
        }
        o[QStringLiteral("choices")] = choices;
    }

    QJsonObject range;
    if (probeRange(def, range)) {
        o[QStringLiteral("range")] = range;
    }

    return o;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    const PhosphorConfig::Schema schema = PlasmaZones::buildSettingsSchema();

    QJsonObject root;
    root[QStringLiteral("schemaVersion")] = schema.version;

    // Schema::groups is a QMap, so iteration is already sorted by group
    // name and this output is byte-stable across runs — it is checked into
    // the site repo, and a nondeterministic dump would produce noise diffs
    // on every sync.
    QJsonArray groups;
    int keyCount = 0;
    for (auto it = schema.groups.constBegin(); it != schema.groups.constEnd(); ++it) {
        QJsonObject g;
        g[QStringLiteral("group")] = it.key();

        QJsonArray keys;
        for (const PhosphorConfig::KeyDef& def : it.value()) {
            keys.append(dumpKey(def));
            ++keyCount;
        }
        g[QStringLiteral("keys")] = keys;
        groups.append(g);
    }
    root[QStringLiteral("groups")] = groups;
    root[QStringLiteral("keyCount")] = keyCount;

    QTextStream out(stdout);
    out << QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));

    return 0;
}
