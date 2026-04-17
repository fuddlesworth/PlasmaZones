// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorConfig/IGroupPathResolver.h>
#include <PhosphorConfig/JsonBackend.h>

#include <QColor>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QLatin1String>
#include <QList>
#include <QSaveFile>
#include <QSet>
#include <QStringList>

#include <functional>
#include <limits>

namespace PhosphorConfig {

namespace {

/// Maximum nesting depth for recursive JSON traversal. Guards against stack
/// overflow from pathologically deep dot paths or malicious configs.
constexpr int MaxDotPathDepth = 8;

/// Warn-once sink for per-key log messages. JsonBackend is single-threaded by
/// contract (main/GUI thread only), so the set doesn't need synchronization.
/// Keyed by "<tag>:<key>" so separate warning categories never clobber each
/// other's dedup state.
bool shouldWarnOnce(const char* tag, const QString& key)
{
    static QSet<QString> s_warned;
    const QString id = QLatin1String(tag) + QLatin1Char(':') + key;
    if (s_warned.contains(id)) {
        return false;
    }
    s_warned.insert(id);
    return true;
}

/// Split a dot-path group name into segments with depth enforcement.
/// Returns empty list if the group name resolves to no segments, or if it
/// exceeds @c MaxDotPathDepth.
QStringList splitDotPath(const QString& groupName, const char* caller)
{
    QStringList segments = groupName.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (segments.isEmpty()) {
        qWarning("PhosphorConfig::JsonBackend (%s): group '%s' resolved to no segments", caller, qPrintable(groupName));
        return segments;
    }
    if (segments.size() > MaxDotPathDepth) {
        qWarning(
            "PhosphorConfig::JsonBackend (%s): group '%s' exceeds MaxDotPathDepth (%lld segments, max %d) — rejecting",
            caller, qPrintable(groupName), static_cast<long long>(segments.size()), MaxDotPathDepth);
        return {};
    }
    return segments;
}

/// Build a chain of existing JSON objects along the given path.
/// chain[i] is the object found at segments[i] within the previous level;
/// absent nodes yield empty objects.
QList<QJsonObject> buildDotPathChain(const QJsonObject& root, const QStringList& segments)
{
    QList<QJsonObject> chain;
    chain.reserve(segments.size());
    for (int i = 0; i < segments.size(); ++i) {
        const QJsonObject& parent = (i == 0) ? root : chain[i - 1];
        chain.append(parent.value(segments[i]).toObject());
    }
    return chain;
}

QJsonObject navigatePath(const QJsonObject& root, const QStringList& segments)
{
    if (segments.isEmpty()) {
        return {};
    }
    const auto chain = buildDotPathChain(root, segments);
    return chain.last();
}

void writeAtPath(QJsonObject& root, const QStringList& segments, const QJsonObject& leaf)
{
    if (segments.isEmpty()) {
        return;
    }
    const auto chain = buildDotPathChain(root, segments);
    QJsonObject current = leaf;
    for (int i = segments.size() - 1; i >= 1; --i) {
        QJsonObject parent = chain[i - 1];
        parent[segments[i]] = current;
        current = parent;
    }
    root[segments[0]] = current;
}

void removeAtPath(QJsonObject& root, const QStringList& segments)
{
    if (segments.isEmpty()) {
        return;
    }
    if (segments.size() == 1) {
        root.remove(segments[0]);
        return;
    }
    const auto chain = buildDotPathChain(root, segments);
    const int parentIdx = segments.size() - 2;
    QJsonObject parentObj = chain[parentIdx];
    parentObj.remove(segments.last());

    for (int i = parentIdx; i >= 1; --i) {
        QJsonObject grandparent = chain[i - 1];
        if (parentObj.isEmpty()) {
            grandparent.remove(segments[i]);
        } else {
            grandparent[segments[i]] = parentObj;
        }
        parentObj = grandparent;
    }
    if (parentObj.isEmpty()) {
        root.remove(segments[0]);
    } else {
        root[segments[0]] = parentObj;
    }
}

} // namespace

// ─── JsonBackend::Data ──────────────────────────────────────────────────────

struct JsonBackend::Data
{
    QString filePath;
    QJsonObject root;
    bool dirty = false;
    int activeGroupCount = 0;
    std::shared_ptr<IGroupPathResolver> resolver;
    QString rootGroupName = QStringLiteral("General");
    QString versionStampKey;
    int versionStampValue = 0;
};

// ─── Path resolution (shared by groups + backend) ───────────────────────────

namespace {
/// Returns the JSON path to use for @p groupName. Empty list means "malformed,
/// refuse the operation". Single-segment lists map to a flat top-level key.
/// Takes only what it needs — the resolver — instead of the full private
/// @c JsonBackend::Data, so this stays a file-scope helper.
QStringList resolvePath(const std::shared_ptr<IGroupPathResolver>& resolver, const QString& groupName,
                        const char* caller)
{
    if (resolver) {
        if (auto custom = resolver->toJsonPath(groupName)) {
            return *custom;
        }
    }
    if (groupName.contains(QLatin1Char('.'))) {
        return splitDotPath(groupName, caller);
    }
    if (groupName.isEmpty()) {
        qWarning("PhosphorConfig::JsonBackend (%s): empty group name", caller);
        return {};
    }
    return {groupName};
}
} // namespace

// ─── JsonGroup ──────────────────────────────────────────────────────────────

JsonGroup::JsonGroup(QJsonObject& root, QString groupName, JsonBackend* backend)
    : m_root(root)
    , m_groupName(std::move(groupName))
    , m_backend(backend)
{
    const int count = m_backend->activeGroupCount();
    if (count != 0) {
        qWarning(
            "PhosphorConfig::JsonGroup: creating group '%s' while %d other group(s) still active — "
            "concurrent writes to the same backend may lose data",
            qPrintable(m_groupName), count);
    }
    Q_ASSERT_X(count == 0, "PhosphorConfig::JsonGroup", "Another JsonGroup is still alive — destroy it first");
    m_backend->incActiveGroupCount();
}

JsonGroup::~JsonGroup()
{
    m_backend->decActiveGroupCount();
}

QJsonObject JsonGroup::groupObject() const
{
    const auto segments = resolvePath(m_backend->d->resolver, m_groupName, "JsonGroup::groupObject");
    return navigatePath(m_root, segments);
}

void JsonGroup::setGroupObject(const QJsonObject& obj)
{
    const auto segments = resolvePath(m_backend->d->resolver, m_groupName, "JsonGroup::setGroupObject");
    if (segments.isEmpty()) {
        qWarning("PhosphorConfig::JsonGroup: refusing write to malformed group '%s'", qPrintable(m_groupName));
        return;
    }
    writeAtPath(m_root, segments, obj);
    m_backend->markDirty();
}

QString JsonGroup::readString(const QString& key, const QString& defaultValue) const
{
    const QJsonObject obj = groupObject();
    if (!obj.contains(key)) {
        return defaultValue;
    }
    const QJsonValue val = obj.value(key);
    // JSON arrays/objects returned as compact JSON so callers that round-trip
    // complex values (trigger lists etc.) via strings keep working.
    if (val.isArray()) {
        return QString::fromUtf8(QJsonDocument(val.toArray()).toJson(QJsonDocument::Compact));
    }
    if (val.isObject()) {
        return QString::fromUtf8(QJsonDocument(val.toObject()).toJson(QJsonDocument::Compact));
    }
    if (val.isBool()) {
        return val.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (val.isDouble()) {
        return QString::number(val.toDouble(), 'g', 17);
    }
    return val.toString(defaultValue);
}

int JsonGroup::readInt(const QString& key, int defaultValue) const
{
    const QJsonObject obj = groupObject();
    if (!obj.contains(key)) {
        return defaultValue;
    }
    const QJsonValue val = obj.value(key);
    if (val.isDouble()) {
        const double d = val.toDouble();
        if (d >= static_cast<double>(std::numeric_limits<int>::min())
            && d <= static_cast<double>(std::numeric_limits<int>::max())) {
            const int result = static_cast<int>(d);
            // Truncating a non-integer double silently would hide a bad
            // hand-edit; warning on every read would flood the log for a
            // key read many times per session. Warn once per key.
            if (d != static_cast<double>(result) && shouldWarnOnce("readInt.trunc", key)) {
                qWarning(
                    "PhosphorConfig::JsonGroup: key '%s' contains non-integer double %g, truncating to %d "
                    "(further truncations on this key will be suppressed)",
                    qPrintable(key), d, result);
            }
            return result;
        }
        if (shouldWarnOnce("readInt.oor", key)) {
            qWarning(
                "PhosphorConfig::JsonGroup: key '%s' contains double %g outside int range, using default %d "
                "(further out-of-range reads on this key will be suppressed)",
                qPrintable(key), d, defaultValue);
        }
        return defaultValue;
    }
    if (val.isString()) {
        bool ok = false;
        const int result = val.toString().toInt(&ok);
        return ok ? result : defaultValue;
    }
    return defaultValue;
}

bool JsonGroup::readBool(const QString& key, bool defaultValue) const
{
    const QJsonObject obj = groupObject();
    if (!obj.contains(key)) {
        return defaultValue;
    }
    const QJsonValue val = obj.value(key);
    if (val.isBool()) {
        return val.toBool();
    }
    if (val.isString()) {
        const QString s = val.toString().toLower();
        if (s == QLatin1String("true") || s == QLatin1String("1") || s == QLatin1String("yes")
            || s == QLatin1String("on")) {
            return true;
        }
        if (s == QLatin1String("false") || s == QLatin1String("0") || s == QLatin1String("no")
            || s == QLatin1String("off")) {
            return false;
        }
    }
    if (val.isDouble()) {
        const double d = val.toDouble();
        if (d == 0.0) {
            return false;
        }
        if (d == 1.0) {
            return true;
        }
    }
    return defaultValue;
}

double JsonGroup::readDouble(const QString& key, double defaultValue) const
{
    const QJsonObject obj = groupObject();
    if (!obj.contains(key)) {
        return defaultValue;
    }
    const QJsonValue val = obj.value(key);
    if (val.isDouble()) {
        return val.toDouble();
    }
    if (val.isString()) {
        bool ok = false;
        const double result = val.toString().toDouble(&ok);
        return ok ? result : defaultValue;
    }
    return defaultValue;
}

QColor JsonGroup::readColor(const QString& key, const QColor& defaultValue) const
{
    const QJsonObject obj = groupObject();
    if (!obj.contains(key)) {
        return defaultValue;
    }
    const QString s = obj.value(key).toString();
    if (s.isEmpty()) {
        return defaultValue;
    }

    if (s.startsWith(QLatin1Char('#'))) {
        QColor c(s);
        return c.isValid() ? c : defaultValue;
    }

    // KConfig r,g,b[,a] comma format
    const QStringList parts = s.split(QLatin1Char(','));
    if (parts.size() >= 3) {
        bool okR = false, okG = false, okB = false;
        int r = parts[0].trimmed().toInt(&okR);
        int g = parts[1].trimmed().toInt(&okG);
        int b = parts[2].trimmed().toInt(&okB);
        if (okR && okG && okB) {
            int a = 255;
            if (parts.size() >= 4) {
                bool okA = false;
                a = parts[3].trimmed().toInt(&okA);
                if (!okA) {
                    a = 255;
                }
            }
            QColor c(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255), qBound(0, a, 255));
            return c.isValid() ? c : defaultValue;
        }
    }
    return defaultValue;
}

void JsonGroup::writeString(const QString& key, const QString& value)
{
    QJsonObject obj = groupObject();
    // A string that parses as JSON array/object is stored as native JSON so
    // consumers that round-trip complex values via strings don't double-escape.
    // A user string that coincidentally parses as JSON will be reinterpreted —
    // consumers who need to store arbitrary free-form strings that might look
    // like JSON should use a JSON-string wrapping convention at the call site.
    if (!value.isEmpty() && (value.front() == QLatin1Char('[') || value.front() == QLatin1Char('{'))) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(value.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError) {
            if (doc.isArray()) {
                obj[key] = doc.array();
            } else if (doc.isObject()) {
                obj[key] = doc.object();
            } else {
                obj[key] = value;
            }
            setGroupObject(obj);
            return;
        }
    }
    obj[key] = value;
    setGroupObject(obj);
}

void JsonGroup::writeInt(const QString& key, int value)
{
    QJsonObject obj = groupObject();
    obj[key] = value;
    setGroupObject(obj);
}

void JsonGroup::writeBool(const QString& key, bool value)
{
    QJsonObject obj = groupObject();
    obj[key] = value;
    setGroupObject(obj);
}

void JsonGroup::writeDouble(const QString& key, double value)
{
    QJsonObject obj = groupObject();
    obj[key] = value;
    setGroupObject(obj);
}

void JsonGroup::writeColor(const QString& key, const QColor& value)
{
    QJsonObject obj = groupObject();
    obj[key] = value.name(QColor::HexArgb);
    setGroupObject(obj);
}

bool JsonGroup::hasKey(const QString& key) const
{
    return groupObject().contains(key);
}

void JsonGroup::deleteKey(const QString& key)
{
    QJsonObject obj = groupObject();
    if (obj.contains(key)) {
        obj.remove(key);
        setGroupObject(obj);
    }
}

QStringList JsonGroup::keyList() const
{
    QStringList keys;
    const QJsonObject obj = groupObject();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        // Scalar leaves only — nested object children are sub-groups, not keys
        // on this group. A Store-claimed descendant (e.g. "Snapping.Behavior"
        // is an ancestor of "Snapping.Behavior.ZoneSpan") is filtered out here
        // so a consumer purging stale keys can iterate keyList() without
        // clobbering declared sub-group contents.
        if (!it.value().isObject()) {
            keys.append(it.key());
        }
    }
    return keys;
}

// ─── JsonBackend ────────────────────────────────────────────────────────────

JsonBackend::JsonBackend(QString filePath)
    : d(std::make_unique<Data>())
{
    d->filePath = std::move(filePath);
    loadFromDisk();
}

JsonBackend::~JsonBackend()
{
    Q_ASSERT_X(d->activeGroupCount == 0, "PhosphorConfig::JsonBackend::~JsonBackend",
               "JsonGroup still alive when backend is being destroyed");
}

void JsonBackend::loadFromDisk()
{
    QFile f(d->filePath);
    if (!f.exists()) {
        d->root = QJsonObject();
        return;
    }
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning("PhosphorConfig::JsonBackend: failed to open %s for reading", qPrintable(d->filePath));
        d->root = QJsonObject();
        return;
    }
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning("PhosphorConfig::JsonBackend: JSON parse error in %s at offset %d: %s", qPrintable(d->filePath),
                 err.offset, qPrintable(err.errorString()));
        d->root = QJsonObject();
        return;
    }
    d->root = doc.object();
}

void JsonBackend::markDirty()
{
    d->dirty = true;
}

void JsonBackend::incActiveGroupCount()
{
    ++d->activeGroupCount;
}

void JsonBackend::decActiveGroupCount()
{
    --d->activeGroupCount;
}

int JsonBackend::activeGroupCount() const
{
    return d->activeGroupCount;
}

std::unique_ptr<IGroup> JsonBackend::group(const QString& name)
{
    return std::make_unique<JsonGroup>(d->root, name, this);
}

void JsonBackend::reparseConfiguration()
{
    Q_ASSERT_X(d->activeGroupCount == 0, "PhosphorConfig::JsonBackend::reparseConfiguration",
               "Cannot reparse while JsonGroup instances are alive");
    loadFromDisk();
    d->dirty = false;
}

void JsonBackend::sync()
{
    if (!d->dirty) {
        return;
    }

    if (!d->versionStampKey.isEmpty() && !d->root.contains(d->versionStampKey)) {
        d->root[d->versionStampKey] = d->versionStampValue;
    }

    if (!writeJsonAtomically(d->filePath, d->root)) {
        return;
    }

    d->dirty = false;
}

bool JsonBackend::writeJsonAtomically(const QString& filePath, const QJsonObject& root)
{
    QDir dir = QFileInfo(filePath).absoluteDir();
    if (!dir.exists() && !dir.mkpath(QLatin1String("."))) {
        qWarning("PhosphorConfig::JsonBackend: failed to create directory %s", qPrintable(dir.absolutePath()));
        return false;
    }

    QSaveFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning("PhosphorConfig::JsonBackend: failed to open %s for writing: %s", qPrintable(filePath),
                 qPrintable(f.errorString()));
        return false;
    }

    QJsonDocument doc(root);
    f.write(doc.toJson(QJsonDocument::Indented));

    if (!f.commit()) {
        qWarning("PhosphorConfig::JsonBackend: failed to commit write to %s: %s", qPrintable(filePath),
                 qPrintable(f.errorString()));
        return false;
    }

    return true;
}

void JsonBackend::deleteGroup(const QString& name)
{
    const auto segments = resolvePath(d->resolver, name, "JsonBackend::deleteGroup");
    if (segments.isEmpty()) {
        qWarning("PhosphorConfig::JsonBackend: ignoring deleteGroup for malformed name '%s'", qPrintable(name));
        return;
    }
    removeAtPath(d->root, segments);
    markDirty();
}

QString JsonBackend::readRootString(const QString& key, const QString& defaultValue) const
{
    const QJsonObject general = d->root.value(d->rootGroupName).toObject();
    if (general.contains(key)) {
        return general.value(key).toString(defaultValue);
    }
    return defaultValue;
}

void JsonBackend::writeRootString(const QString& key, const QString& value)
{
    QJsonObject general = d->root.value(d->rootGroupName).toObject();
    general[key] = value;
    d->root[d->rootGroupName] = general;
    markDirty();
}

void JsonBackend::removeRootKey(const QString& key)
{
    QJsonObject general = d->root.value(d->rootGroupName).toObject();
    if (general.contains(key)) {
        general.remove(key);
        if (general.isEmpty()) {
            d->root.remove(d->rootGroupName);
        } else {
            d->root[d->rootGroupName] = general;
        }
        markDirty();
    }
}

QStringList JsonBackend::groupList() const
{
    QStringList groups;

    // Reserved keys (version stamp + whatever the resolver owns) are hidden
    // from the default dot-path enumeration so they don't appear in the list.
    QStringList skip;
    if (!d->versionStampKey.isEmpty()) {
        skip << d->versionStampKey;
    }
    if (d->resolver) {
        skip << d->resolver->reservedRootKeys();
    }

    std::function<void(const QJsonObject&, const QString&, int)> enumerate = [&](const QJsonObject& obj,
                                                                                 const QString& prefix, int depth) {
        if (depth >= MaxDotPathDepth) {
            return;
        }
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            if (skip.contains(it.key())) {
                continue;
            }
            if (it.value().isObject()) {
                const QString path = prefix.isEmpty() ? it.key() : (prefix + QLatin1Char('.') + it.key());
                groups.append(path);
                enumerate(it.value().toObject(), path, depth + 1);
            }
        }
    };
    enumerate(d->root, QString(), 0);

    // Resolver-owned group names.
    if (d->resolver) {
        groups.append(d->resolver->enumerate(d->root));
    }

    return groups;
}

void JsonBackend::setPathResolver(std::shared_ptr<IGroupPathResolver> resolver)
{
    d->resolver = std::move(resolver);
}

std::shared_ptr<IGroupPathResolver> JsonBackend::pathResolver() const
{
    return d->resolver;
}

void JsonBackend::setRootGroupName(const QString& name)
{
    d->rootGroupName = name;
}

QString JsonBackend::rootGroupName() const
{
    return d->rootGroupName;
}

void JsonBackend::setVersionStamp(const QString& key, int version)
{
    d->versionStampKey = key;
    d->versionStampValue = version;
}

QJsonObject JsonBackend::jsonRootSnapshot() const
{
    return d->root;
}

QString JsonBackend::filePath() const
{
    return d->filePath;
}

void JsonBackend::clearDirty()
{
    d->dirty = false;
}

} // namespace PhosphorConfig
