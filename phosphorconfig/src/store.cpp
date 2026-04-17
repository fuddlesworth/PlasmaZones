// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorConfig/IBackend.h>
#include <PhosphorConfig/IGroupPathResolver.h>
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

void writeVariantTo(IGroup& g, const QString& key, const QVariant& value)
{
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
            g.writeString(key, QString::number(v));
        }
    };
    auto writeUint64 = [&](qulonglong v) {
        if (v <= static_cast<qulonglong>(std::numeric_limits<int>::max())) {
            g.writeInt(key, static_cast<int>(v));
        } else {
            qWarning("PhosphorConfig::Store: uint64 value %llu for key '%s' does not fit in int — storing as string",
                     static_cast<unsigned long long>(v), qPrintable(key));
            g.writeString(key, QString::number(v));
        }
    };

    switch (value.typeId()) {
    case QMetaType::Bool:
        g.writeBool(key, value.toBool());
        break;
    case QMetaType::Int:
        g.writeInt(key, value.toInt());
        break;
    case QMetaType::UInt:
        // UInt values > INT_MAX would wrap if cast directly. Route through
        // the same range-check as ULongLong so oversized values are
        // persisted as strings rather than silently corrupted.
        writeUint64(value.toUInt());
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
        g.writeString(key, value.toString());
        break;
    case QMetaType::QVariantList:
        // Native-JSON write: the backend (JsonBackend) stores the array
        // directly in the JSON document, no string round-trip. QSettings-
        // backed storage falls back to the stringified default impl.
        g.writeJson(key, QJsonArray::fromVariantList(value.toList()));
        break;
    case QMetaType::QVariantMap:
        g.writeJson(key, QJsonObject::fromVariantMap(value.toMap()));
        break;
    default:
        // Fall back to string representation. Consumers that need other
        // exact types should add an expectedType to KeyDef and we can
        // extend the dispatch.
        g.writeString(key, value.toString());
        break;
    }
}

QVariant readVariantAs(const IGroup& g, const QString& key, const QVariant& defaultValue,
                       QMetaType::Type expectedType = QMetaType::UnknownType)
{
    // expectedType is the authoritative type source when set. Falling back to
    // defaultValue.typeId() keeps legacy callers working but means a KeyDef
    // declaring expectedType=Int with an Invalid/unrelated default would be
    // read as a string — a latent but real bug if a future caller leaves the
    // default empty while setting the expected type.
    const int typeId =
        (expectedType != QMetaType::UnknownType) ? static_cast<int>(expectedType) : defaultValue.typeId();
    switch (typeId) {
    case QMetaType::Bool:
        return QVariant(g.readBool(key, defaultValue.toBool()));
    case QMetaType::Int:
    case QMetaType::UInt:
        return QVariant(g.readInt(key, defaultValue.toInt()));
    case QMetaType::LongLong: {
        // writeVariantTo persists out-of-range int64 values as strings (see
        // writeInt64 below) so readInt(...) would parse-fail and silently
        // fall back to the default, dropping the stored value. Read as
        // string first and parse with toLongLong, falling through to
        // readInt for values written as native JSON numbers.
        if (!g.hasKey(key)) {
            return QVariant(defaultValue.toLongLong());
        }
        const QString raw = g.readString(key);
        if (!raw.isEmpty()) {
            bool ok = false;
            const qlonglong parsed = raw.toLongLong(&ok);
            if (ok) {
                return QVariant(parsed);
            }
        }
        // Either empty string or non-parseable — fall back to readInt so
        // values written via writeInt still round-trip, then widen.
        return QVariant(static_cast<qlonglong>(g.readInt(key, defaultValue.toInt())));
    }
    case QMetaType::ULongLong: {
        if (!g.hasKey(key)) {
            return QVariant(defaultValue.toULongLong());
        }
        const QString raw = g.readString(key);
        if (!raw.isEmpty()) {
            bool ok = false;
            const qulonglong parsed = raw.toULongLong(&ok);
            if (ok) {
                return QVariant(parsed);
            }
        }
        const int asInt = g.readInt(key, defaultValue.toInt());
        return QVariant(asInt < 0 ? 0ULL : static_cast<qulonglong>(asInt));
    }
    case QMetaType::Double:
    case QMetaType::Float:
        return QVariant(g.readDouble(key, defaultValue.toDouble()));
    case QMetaType::QColor:
        return QVariant::fromValue(g.readColor(key, defaultValue.value<QColor>()));
    case QMetaType::QVariantList: {
        const QJsonValue v = g.readJson(key);
        if (v.isArray()) {
            return QVariant(v.toArray().toVariantList());
        }
        // Legacy-string fallback: data written before the native-JSON path
        // landed (e.g. from a custom pre-refactor migration) may still sit
        // on disk as a JSON-encoded string. Try to parse before giving up.
        const QString raw = g.readString(key);
        if (!raw.isEmpty()) {
            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &err);
            if (err.error == QJsonParseError::NoError && doc.isArray()) {
                return QVariant(doc.array().toVariantList());
            }
        }
        return defaultValue;
    }
    case QMetaType::QVariantMap: {
        const QJsonValue v = g.readJson(key);
        if (v.isObject()) {
            return QVariant(v.toObject().toVariantMap());
        }
        const QString raw = g.readString(key);
        if (!raw.isEmpty()) {
            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                return QVariant(doc.object().toVariantMap());
            }
        }
        return defaultValue;
    }
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

    // Path resolver: install via the backend's polymorphic hook. Backends
    // without a resolver concept (QSettingsBackend) inherit the no-op
    // default, so the call is cheap and safe.
    //
    // Shared-backend safety: refuse to clobber an existing resolver since
    // multiple Stores can share one backend (see class docs). The first
    // attached resolver wins; later Stores must either declare a compatible
    // resolver or rely on the one already installed by their host.
    if (d->schema.pathResolver) {
        if (auto existing = d->backend->pathResolver()) {
            if (existing != d->schema.pathResolver) {
                qWarning(
                    "PhosphorConfig::Store: backend already has a path resolver attached — refusing to "
                    "overwrite. Stores sharing a backend must agree on the resolver.");
            }
        } else {
            d->backend->setPathResolver(d->schema.pathResolver);
        }
    }

    // Version stamp: same polymorphic-hook pattern as the resolver. The
    // no-op default on IBackend means a QSettings-backed Store silently
    // skips stamping, which matches pre-polymorphic behaviour. JsonBackend
    // overrides to persist the stamp through its next sync().
    if (!d->schema.versionKey.isEmpty()) {
        const auto [existingKey, existingVersion] = d->backend->versionStamp();
        if (existingKey.isEmpty()) {
            d->backend->setVersionStamp(d->schema.versionKey, d->schema.version);
        } else if (existingKey != d->schema.versionKey || existingVersion != d->schema.version) {
            qWarning(
                "PhosphorConfig::Store: backend already has a version stamp ('%s' = %d) — refusing to "
                "overwrite with ('%s' = %d). Stores sharing a backend must agree on the version stamp.",
                qPrintable(existingKey), existingVersion, qPrintable(d->schema.versionKey), d->schema.version);
        }
    }

    // Migration chain: route through IBackend::applyMigration. Backends that
    // support it (JsonBackend) take a snapshot, run the chain, and commit
    // atomically on a version bump. Backends that don't (QSettingsBackend,
    // in-memory mocks without a JSON root) inherit the no-op default that
    // returns false — surfaced here as a qWarning so a silently-dropped
    // migration chain doesn't become a runtime surprise.
    if (!d->schema.migrations.isEmpty()) {
        if (!d->backend->applyMigration(d->schema)) {
            qWarning(
                "PhosphorConfig::Store: schema declares %lld migration step(s) but the backend reported no "
                "migration support — migrations will NOT run. Ensure the backend implements applyMigration() "
                "or remove the migration chain from the schema.",
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
    QVariant value = readVariantAs(*g, key, def->defaultValue, def->expectedType);
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
    {
        auto g = d->backend->group(group);
        // Skip the write (and the changed() emission) when the on-disk value
        // already exactly matches what we'd write. The comparison is against
        // the RAW disk value, not validator-coerced: this lets a canonicalising
        // flush loop overwrite a non-canonical disk value (e.g. " a , b "
        // re-written as "a,b") instead of being short-circuited by the
        // validator on both sides agreeing on the same canonical form.
        if (g->hasKey(key)) {
            const QVariant current = readVariantAs(*g, key, def->defaultValue, def->expectedType);
            if (current == coerced) {
                return;
            }
        }
        writeVariantTo(*g, key, coerced);
    }
    Q_EMIT changed(group, key);
}

void Store::reset(const QString& group, const QString& key)
{
    const KeyDef* def = d->schema.findKey(group, key);
    if (!def) {
        return;
    }
    auto g = d->backend->group(group);
    if (!g->hasKey(key)) {
        // Key isn't on disk, so the read path already returns the default.
        // Skipping the write avoids stamping a default-valued key and dirtying
        // the backend on otherwise-idempotent reset() calls.
        return;
    }
    writeVariantTo(*g, key, def->defaultValue);
    Q_EMIT changed(group, key);
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
        // Skip absent keys for the same reason as reset() — otherwise
        // resetGroup() on a pristine install stamps every default to disk
        // with no observable behavior change but a full file rewrite.
        if (!g->hasKey(def.key)) {
            continue;
        }
        writeVariantTo(*g, def.key, def.defaultValue);
        Q_EMIT changed(group, def.key);
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
            const QVariant value = readVariantAs(*g, def.key, def.defaultValue, def.expectedType);
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
    //
    // The version key must be a JSON number. A missing key is acceptable
    // (callers doing partial imports / fresh exports), but a string or
    // other wrongly-typed value means the snapshot is malformed and we
    // refuse rather than letting the zero-default mask a type error.
    if (!d->schema.versionKey.isEmpty() && snapshot.contains(d->schema.versionKey)) {
        const QJsonValue versionValue = snapshot.value(d->schema.versionKey);
        if (!versionValue.isDouble()) {
            qWarning(
                "PhosphorConfig::Store::importFromJson: snapshot '%s' is not a JSON number — refusing import "
                "(malformed snapshot).",
                qPrintable(d->schema.versionKey));
            return;
        }
        const int snapshotVersion = versionValue.toInt();
        if (snapshotVersion != d->schema.version) {
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
