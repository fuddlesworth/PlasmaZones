// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorConfig/IBackend.h>
#include <PhosphorConfig/IGroupPathResolver.h>
#include <PhosphorConfig/JsonBackend.h>
#include <PhosphorConfig/MigrationRunner.h>
#include <PhosphorConfig/Schema.h>
#include <PhosphorConfig/Store.h>

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMetaType>

#include <limits>

namespace PhosphorConfig {

// ─── Store::Private ──────────────────────────────────────────────────────────

class Store::Private
{
public:
    Private(IBackend* backendIn, Schema schemaIn)
        : backend(backendIn)
        , schema(std::move(schemaIn))
    {
    }

    IBackend* backend; // non-owning — lifetime managed by the caller
    Schema schema;
};

// ─── Variant dispatch helpers ────────────────────────────────────────────────
// write() takes a QVariant; we need to dispatch to the right typed write on
// IGroup to preserve JSON types (writing "true" as a string would round-trip
// as a string, not a bool). The helpers below centralize the switch so both
// write() and reset() share it.

namespace {

void writeVariantTo(IGroup& g, const QString& key, const QVariant& value, bool verbatimString)
{
    auto writeStringDispatch = [&](const QString& s) {
        if (verbatimString) {
            g.writeStringRaw(key, s);
        } else {
            g.writeString(key, s);
        }
    };

    // Narrowing guard for 64-bit integer types: if the value doesn't fit in
    // a 32-bit int, persist as a string so the value survives the round-trip
    // instead of being silently truncated. IGroup has no writeInt64 today;
    // extend the interface if precise typing becomes necessary.
    auto writeInt64 = [&](qlonglong v) {
        if (v >= std::numeric_limits<int>::min() && v <= std::numeric_limits<int>::max()) {
            g.writeInt(key, static_cast<int>(v));
        } else {
            qWarning("PhosphorConfig::Store: int64 value %lld for key '%s' does not fit in int — storing as string",
                     static_cast<long long>(v), qPrintable(key));
            writeStringDispatch(QString::number(v));
        }
    };
    auto writeUint64 = [&](qulonglong v) {
        if (v <= static_cast<qulonglong>(std::numeric_limits<int>::max())) {
            g.writeInt(key, static_cast<int>(v));
        } else {
            qWarning("PhosphorConfig::Store: uint64 value %llu for key '%s' does not fit in int — storing as string",
                     static_cast<unsigned long long>(v), qPrintable(key));
            writeStringDispatch(QString::number(v));
        }
    };

    switch (value.typeId()) {
    case QMetaType::Bool:
        g.writeBool(key, value.toBool());
        break;
    case QMetaType::Int:
    case QMetaType::UInt:
        // UInt up to INT_MAX fits; above that falls through via writeUint64
        // only for the LongLong/ULongLong cases — for QMetaType::UInt alone,
        // Qt's implicit promotion handles values >INT_MAX by wrapping. A
        // stricter check would require a separate writeUInt accessor; the
        // narrowing cost is accepted for the common 0..INT_MAX range.
        g.writeInt(key, value.toInt());
        break;
    case QMetaType::LongLong:
        writeInt64(value.toLongLong());
        break;
    case QMetaType::ULongLong:
        writeUint64(value.toULongLong());
        break;
    case QMetaType::Double:
    case QMetaType::Float:
        g.writeDouble(key, value.toDouble());
        break;
    case QMetaType::QColor:
        g.writeColor(key, value.value<QColor>());
        break;
    case QMetaType::QString:
        writeStringDispatch(value.toString());
        break;
    case QMetaType::QVariantList:
    case QMetaType::QVariantMap: {
        // Round-trip complex values as compact JSON strings. Backends treat
        // "looks like JSON" on write as native JSON, so this survives a
        // save/load cycle as structured data. Always uses the shape-aware
        // writeString — verbatim storage of structured values makes no
        // sense and the caller can't have opted in meaningfully anyway.
        const QJsonDocument doc = value.typeId() == QMetaType::QVariantList
            ? QJsonDocument(QJsonArray::fromVariantList(value.toList()))
            : QJsonDocument(QJsonObject::fromVariantMap(value.toMap()));
        g.writeString(key, QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
        break;
    }
    default:
        // Fall back to string representation. Consumers that need other
        // exact types should add an expectedType to KeyDef and we can
        // extend the dispatch.
        writeStringDispatch(value.toString());
        break;
    }
}

QVariant readVariantAs(const IGroup& g, const QString& key, const QVariant& defaultValue)
{
    // Use the declared default's type to pick the right reader. If no default
    // is declared, fall back to reading as a string (the universal backend
    // format) and let the caller coerce.
    const int typeId = defaultValue.typeId();
    switch (typeId) {
    case QMetaType::Bool:
        return QVariant(g.readBool(key, defaultValue.toBool()));
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::UInt:
    case QMetaType::ULongLong:
        return QVariant(g.readInt(key, defaultValue.toInt()));
    case QMetaType::Double:
    case QMetaType::Float:
        return QVariant(g.readDouble(key, defaultValue.toDouble()));
    case QMetaType::QColor:
        return QVariant::fromValue(g.readColor(key, defaultValue.value<QColor>()));
    case QMetaType::QString:
    default:
        return QVariant(g.readString(key, defaultValue.toString()));
    }
}

} // namespace

// ─── Store ───────────────────────────────────────────────────────────────────

Store::Store(IBackend* backend, Schema schema, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>(backend, std::move(schema)))
{
    Q_ASSERT_X(backend != nullptr, "PhosphorConfig::Store", "backend must not be null");
    // Wire the schema's path resolver onto the JsonBackend when present.
    // QSettingsBackend has no resolver concept; passing it one is a no-op.
    if (auto* json = dynamic_cast<JsonBackend*>(d->backend)) {
        if (d->schema.pathResolver) {
            // Shared-backend safety: refuse to clobber an existing resolver
            // since multiple Stores can share one backend (see class docs).
            // The first attached resolver wins; later Stores must either
            // declare a compatible resolver or rely on the one already
            // installed by their host.
            if (auto existing = json->pathResolver()) {
                if (existing != d->schema.pathResolver) {
                    qWarning(
                        "PhosphorConfig::Store: backend already has a path resolver attached — refusing to "
                        "overwrite. Stores sharing a backend must agree on the resolver.");
                }
            } else {
                json->setPathResolver(d->schema.pathResolver);
            }
        }
        if (!d->schema.versionKey.isEmpty()) {
            json->setVersionStamp(d->schema.versionKey, d->schema.version);
        }

        // Run the migration chain against the backend's in-memory state.
        // We don't use MigrationRunner::runOnFile because the backend has
        // already loaded the file; running on the file would race.
        if (!d->schema.migrations.isEmpty()) {
            QJsonObject snapshot = json->jsonRootSnapshot();
            const int before = snapshot.value(d->schema.versionKey).toInt(1);
            MigrationRunner runner(d->schema);
            runner.runInMemory(snapshot);
            const int after = snapshot.value(d->schema.versionKey).toInt(before);
            if (after != before) {
                // Rewrite the file in place, then push the migrated snapshot
                // into the backend's in-memory state. Going through
                // replaceRoot + clearDirty skips the disk re-read + parse
                // cycle that reparseConfiguration would do — we already
                // know the content is equivalent to disk.
                //
                // If the atomic write fails, leave the backend's in-memory
                // state at the unmigrated version: a fresh load from disk
                // would show the unmigrated file anyway, and skipping the
                // replace means the in-memory state matches disk until the
                // next save() retries the disk commit.
                if (JsonBackend::writeJsonAtomically(json->filePath(), snapshot)) {
                    json->replaceRoot(std::move(snapshot));
                    json->clearDirty();
                } else {
                    qWarning(
                        "PhosphorConfig::Store: failed to commit migrated config to %s — backend continues "
                        "with the unmigrated on-disk state until the next successful sync()",
                        qPrintable(json->filePath()));
                }
            }
        }
    } else {
        // Non-JsonBackend path: path resolvers, version stamping, and the
        // migration chain are all JsonBackend-only. Warn the caller so a
        // silently-dropped resolver or migration chain doesn't turn into a
        // surprise at runtime.
        if (d->schema.pathResolver) {
            qWarning(
                "PhosphorConfig::Store: schema supplies a pathResolver, but the backend is not a JsonBackend "
                "— the resolver will NOT be applied");
        }
        if (!d->schema.migrations.isEmpty()) {
            qWarning(
                "PhosphorConfig::Store: schema declares %lld migration step(s), but the backend is not a "
                "JsonBackend — migrations will NOT run",
                static_cast<long long>(d->schema.migrations.size()));
        }
    }
}

Store::~Store() = default;

IBackend* Store::backend() const
{
    return d->backend;
}

const Schema& Store::schema() const
{
    return d->schema;
}

bool Store::sync()
{
    return d->backend->sync();
}

QVariant Store::readVariant(const QString& group, const QString& key) const
{
    const KeyDef* def = d->schema.findKey(group, key);
    if (!def) {
        return {};
    }
    auto g = d->backend->group(group);
    QVariant value = readVariantAs(*g, key, def->defaultValue);
    if (def->validator) {
        value = def->validator(value);
    }
    return value;
}

void Store::write(const QString& group, const QString& key, const QVariant& value)
{
    const KeyDef* def = d->schema.findKey(group, key);
    if (!def) {
        // Reject writes to undeclared keys. Reads of undeclared keys return
        // the value-initialized default (per Store::read docstring), so
        // letting writes leak into storage would create an asymmetry where
        // values can be persisted but never observed through Store. Callers
        // that intentionally need raw access can go through backend()->group().
        qWarning("PhosphorConfig::Store: rejecting write to undeclared key %s/%s", qPrintable(group), qPrintable(key));
        return;
    }

    QVariant coerced = value;
    if (def->validator) {
        coerced = def->validator(coerced);
    }
    if (def->expectedType != QMetaType::UnknownType && coerced.typeId() != static_cast<int>(def->expectedType)) {
        qWarning("PhosphorConfig::Store: write to %s/%s with type %s, schema expected %s", qPrintable(group),
                 qPrintable(key), QMetaType(coerced.typeId()).name(), QMetaType(def->expectedType).name());
    }
    const bool verbatimString = def->verbatimStringStorage;
    {
        auto g = d->backend->group(group);
        // Skip the write (and the changed() emission) when the on-disk value
        // already exactly matches what we'd write. The comparison is against
        // the RAW disk value, not validator-coerced: this lets a canonicalising
        // flush loop overwrite a non-canonical disk value (e.g. " a , b "
        // re-written as "a,b") instead of being short-circuited by the
        // validator on both sides agreeing on the same canonical form.
        if (g->hasKey(key)) {
            const QVariant current = readVariantAs(*g, key, def->defaultValue);
            if (current == coerced) {
                return;
            }
        }
        writeVariantTo(*g, key, coerced, verbatimString);
    }
    Q_EMIT changed(group, key);
}

void Store::reset(const QString& group, const QString& key)
{
    const KeyDef* def = d->schema.findKey(group, key);
    if (!def) {
        return;
    }
    bool wasPresent = false;
    {
        auto g = d->backend->group(group);
        wasPresent = g->hasKey(key);
        writeVariantTo(*g, key, def->defaultValue, def->verbatimStringStorage);
    }
    if (wasPresent) {
        Q_EMIT changed(group, key);
    }
}

void Store::resetGroup(const QString& group)
{
    auto it = d->schema.groups.constFind(group);
    if (it == d->schema.groups.constEnd()) {
        return;
    }
    // Hold a single IGroup for the whole group so we don't pay the
    // resolver/path-walk cost once per declared key.
    auto g = d->backend->group(group);
    for (const KeyDef& def : *it) {
        const bool wasPresent = g->hasKey(def.key);
        writeVariantTo(*g, def.key, def.defaultValue, def.verbatimStringStorage);
        if (wasPresent) {
            Q_EMIT changed(group, def.key);
        }
    }
}

void Store::resetAll()
{
    for (auto it = d->schema.groups.constBegin(); it != d->schema.groups.constEnd(); ++it) {
        resetGroup(it.key());
    }
}

QJsonObject Store::exportToJson() const
{
    QJsonObject out;
    for (auto git = d->schema.groups.constBegin(); git != d->schema.groups.constEnd(); ++git) {
        QJsonObject groupObj;
        auto g = d->backend->group(git.key());
        for (const KeyDef& def : git.value()) {
            const QVariant value = readVariantAs(*g, def.key, def.defaultValue);
            groupObj[def.key] = QJsonValue::fromVariant(value);
        }
        out[git.key()] = groupObj;
    }
    if (!d->schema.versionKey.isEmpty()) {
        out[d->schema.versionKey] = d->schema.version;
    }
    return out;
}

void Store::importFromJson(const QJsonObject& snapshot)
{
    // Reject snapshots stamped with a different schema version. Callers
    // with an older snapshot must run MigrationRunner on it first —
    // otherwise we'd silently drop or mis-type keys that changed shape
    // between versions.
    if (!d->schema.versionKey.isEmpty() && snapshot.contains(d->schema.versionKey)) {
        const int snapshotVersion = snapshot.value(d->schema.versionKey).toInt(0);
        if (snapshotVersion != 0 && snapshotVersion != d->schema.version) {
            qWarning(
                "PhosphorConfig::Store::importFromJson: snapshot version %d does not match schema version %d — "
                "refusing import. Run MigrationRunner on the snapshot first.",
                snapshotVersion, d->schema.version);
            return;
        }
    }

    for (auto git = d->schema.groups.constBegin(); git != d->schema.groups.constEnd(); ++git) {
        if (!snapshot.contains(git.key())) {
            continue;
        }
        const QJsonObject groupObj = snapshot.value(git.key()).toObject();
        for (const KeyDef& def : git.value()) {
            if (!groupObj.contains(def.key)) {
                continue;
            }
            QVariant value = groupObj.value(def.key).toVariant();
            // QJsonValue::toVariant always returns Double for JSON numbers,
            // even when the schema declares an Int. Coerce to the schema's
            // expectedType when convertible so Store::write doesn't fire a
            // type-mismatch warning for every numeric import. Only safe for
            // bool/int/double/QString — int64-out-of-range is tested
            // independently by callers passing typed QVariants directly.
            if (def.expectedType != QMetaType::UnknownType && value.typeId() != static_cast<int>(def.expectedType)) {
                QVariant converted = value;
                if (converted.convert(QMetaType(def.expectedType))) {
                    value = converted;
                }
            }
            write(git.key(), def.key, value);
        }
    }
}

// ─── Templated read<T> ───────────────────────────────────────────────────────
// Each specialization fetches the typed value from the backend with the
// schema default as fallback, then runs the validator (if any) so clamping
// and normalization apply uniformly to every read path.

namespace {
template<typename T>
T applyValidator(const KeyDef* def, T value)
{
    if (!def || !def->validator) {
        return value;
    }
    const QVariant coerced = def->validator(QVariant::fromValue(value));
    if (!coerced.canConvert<T>()) {
        return value;
    }
    return coerced.value<T>();
}

/// Per-type reader: dispatch to the right IGroup accessor using the
/// KeyDef's default as the fallback. Specializations match the explicit
/// Store::read<T> set declared in the header.
template<typename T>
T readTyped(IGroup& g, const KeyDef& def);

template<>
QString readTyped<QString>(IGroup& g, const KeyDef& def)
{
    return g.readString(def.key, def.defaultValue.toString());
}
template<>
int readTyped<int>(IGroup& g, const KeyDef& def)
{
    return g.readInt(def.key, def.defaultValue.toInt());
}
template<>
bool readTyped<bool>(IGroup& g, const KeyDef& def)
{
    return g.readBool(def.key, def.defaultValue.toBool());
}
template<>
double readTyped<double>(IGroup& g, const KeyDef& def)
{
    return g.readDouble(def.key, def.defaultValue.toDouble());
}
template<>
QColor readTyped<QColor>(IGroup& g, const KeyDef& def)
{
    return g.readColor(def.key, def.defaultValue.value<QColor>());
}

template<typename T>
T readDeclared(const Schema& schema, IBackend* backend, const QString& group, const QString& key, T fallback)
{
    const KeyDef* def = schema.findKey(group, key);
    if (!def) {
        return fallback;
    }
    auto g = backend->group(group);
    return applyValidator(def, readTyped<T>(*g, *def));
}
} // namespace

// Undeclared keys return a value-initialized T (matching the docstring on
// Store::read). Falling through to the backend would surface keys the
// schema doesn't know about and inconsistently differ from
// readVariant()'s undeclared-key behavior.

template<>
QString Store::read<QString>(const QString& group, const QString& key) const
{
    return readDeclared<QString>(d->schema, d->backend, group, key, QString());
}

template<>
int Store::read<int>(const QString& group, const QString& key) const
{
    return readDeclared<int>(d->schema, d->backend, group, key, 0);
}

template<>
bool Store::read<bool>(const QString& group, const QString& key) const
{
    return readDeclared<bool>(d->schema, d->backend, group, key, false);
}

template<>
double Store::read<double>(const QString& group, const QString& key) const
{
    return readDeclared<double>(d->schema, d->backend, group, key, 0.0);
}

template<>
QColor Store::read<QColor>(const QString& group, const QString& key) const
{
    return readDeclared<QColor>(d->schema, d->backend, group, key, QColor());
}

// The explicit specializations above ARE the definitions — no separate
// "template Store::read<T>(...)" instantiation line is needed (and would be
// rejected because the primary template has no body in this TU).

} // namespace PhosphorConfig
