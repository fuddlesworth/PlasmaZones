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

/// Maximum unique warn-once entries to retain per backend instance. A
/// long-running daemon processing many distinct keys would otherwise grow
/// the set unboundedly. Once the cap is hit further unique warnings are
/// emitted but not deduped; the alternative (silencing them) would hide
/// real corruption.
constexpr int MaxWarnOnceEntries = 1024;

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

/// Write @p leaf into @p root along the segmented path. Takes a pre-built
/// chain of intermediate objects so the caller's equality-skip check (which
/// already walked the path) doesn't pay for a second traversal.
void writeAtPathWithChain(QJsonObject& root, const QStringList& segments, const QList<QJsonObject>& chain,
                          const QJsonObject& leaf)
{
    if (segments.isEmpty()) {
        return;
    }
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

    /// Per-instance warn-once sink keyed by "<tag>:<key>". Scoped to the
    /// backend so each test (or each process-level backend instance) gets a
    /// fresh dedup cache — otherwise a warning emitted during one test
    /// silences the same warning in every subsequent test in the same
    /// process, breaking QTest::ignoreMessage expectations. JsonBackend is
    /// single-threaded by contract so no synchronization is needed.
    QSet<QString> warnedKeys;
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
        // Release-mode safety net: when NDEBUG strips the assert below, we
        // still refuse to let this group mutate the shared root so the
        // already-live group retains sole ownership of writes. Reads go
        // through because they only observe the shared root, not mutate it.
        //
        // Disabled groups DON'T bump the active-group counter — if they did,
        // a third legitimate group constructed after the concurrent pair
        // would observe an inflated count and disable itself too, cascading
        // the failure indefinitely.
        m_disabled = true;
        qCritical(
            "PhosphorConfig::JsonGroup: refusing writes on group '%s' — %d other group(s) still active on this "
            "backend. Destroy them first; this instance is read-only for the remainder of its lifetime.",
            qPrintable(m_groupName), count);
    }
    Q_ASSERT_X(count == 0, "PhosphorConfig::JsonGroup", "Another JsonGroup is still alive — destroy it first");
    if (!m_disabled) {
        m_backend->incActiveGroupCount();
    }
}

JsonGroup::~JsonGroup()
{
    // Mirror the constructor: only disabled groups skipped the increment, so
    // they must also skip the decrement or the counter would drift negative.
    if (!m_disabled) {
        m_backend->decActiveGroupCount();
    }
}

bool JsonGroup::refuseWrite(const char* op) const
{
    if (!m_disabled) {
        return false;
    }
    qWarning("PhosphorConfig::JsonGroup: dropping %s on group '%s' (disabled due to single-active-group violation)", op,
             qPrintable(m_groupName));
    return true;
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
    // Walk the path once, share the chain between the equality-skip and the
    // write-back. The flush loop in Settings::save() can hit this hundreds of
    // times per save and a redundant traversal is pure waste.
    const auto chain = buildDotPathChain(m_root, segments);
    if (chain.last() == obj) {
        return;
    }
    writeAtPathWithChain(m_root, segments, chain, obj);
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
            // key read many times per session. Warn once per key per backend.
            if (d != static_cast<double>(result) && m_backend->shouldWarnOnce("readInt.trunc", key)) {
                qWarning(
                    "PhosphorConfig::JsonGroup: key '%s' contains non-integer double %g, truncating to %d "
                    "(further truncations on this key will be suppressed)",
                    qPrintable(key), d, result);
            }
            return result;
        }
        if (m_backend->shouldWarnOnce("readInt.oor", key)) {
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
    if (refuseWrite("writeString")) {
        return;
    }
    QJsonObject obj = groupObject();
    obj[key] = value;
    setGroupObject(obj);
}

void JsonGroup::writeStringRaw(const QString& key, const QString& value)
{
    // writeString is now always verbatim; this alias stays for source
    // compatibility with callers that historically opted out of the
    // (now-removed) JSON-shape reinterpretation heuristic.
    writeString(key, value);
}

void JsonGroup::writeJson(const QString& key, const QJsonValue& value)
{
    if (refuseWrite("writeJson")) {
        return;
    }
    // Store the QJsonValue natively — no string round-trip. Callers that need
    // structure-preserving storage (e.g. @c Store::write for QVariantList /
    // QVariantMap, trigger lists, per-algorithm settings) reach through here
    // instead of relying on data-dependent shape inference in writeString.
    QJsonObject obj = groupObject();
    obj[key] = value;
    setGroupObject(obj);
}

QJsonValue JsonGroup::readJson(const QString& key, const QJsonValue& defaultValue) const
{
    const QJsonObject obj = groupObject();
    const auto it = obj.constFind(key);
    if (it == obj.constEnd()) {
        return defaultValue;
    }
    return it.value();
}

void JsonGroup::writeInt(const QString& key, int value)
{
    if (refuseWrite("writeInt")) {
        return;
    }
    QJsonObject obj = groupObject();
    obj[key] = value;
    setGroupObject(obj);
}

void JsonGroup::writeBool(const QString& key, bool value)
{
    if (refuseWrite("writeBool")) {
        return;
    }
    QJsonObject obj = groupObject();
    obj[key] = value;
    setGroupObject(obj);
}

void JsonGroup::writeDouble(const QString& key, double value)
{
    if (refuseWrite("writeDouble")) {
        return;
    }
    QJsonObject obj = groupObject();
    obj[key] = value;
    setGroupObject(obj);
}

void JsonGroup::writeColor(const QString& key, const QColor& value)
{
    if (refuseWrite("writeColor")) {
        return;
    }
    QJsonObject obj = groupObject();
    obj[key] = value.name(QColor::HexArgb);
    setGroupObject(obj);
}

bool JsonGroup::hasKey(const QString& key) const
{
    // Match keyList()'s scalar-only contract: nested sub-groups are not
    // "keys" on this group. Returning true for sub-groups would let
    // callers that use hasKey() as a "should I purge this stale key?"
    // guard accidentally clobber declared descendants.
    const QJsonObject obj = groupObject();
    const auto it = obj.constFind(key);
    return it != obj.constEnd() && !it.value().isObject();
}

void JsonGroup::deleteKey(const QString& key)
{
    if (refuseWrite("deleteKey")) {
        return;
    }
    QJsonObject obj = groupObject();
    if (!obj.contains(key)) {
        return;
    }
    obj.remove(key);
    if (obj.isEmpty()) {
        // Prune the group itself rather than leaving a {} husk on disk,
        // matching removeAtPath's empty-parent pruning behaviour.
        const auto segments = resolvePath(m_backend->d->resolver, m_groupName, "JsonGroup::deleteKey");
        if (!segments.isEmpty()) {
            removeAtPath(m_root, segments);
            m_backend->markDirty();
            return;
        }
    }
    setGroupObject(obj);
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

bool JsonBackend::shouldWarnOnce(const char* tag, const QString& key)
{
    const QString id = QLatin1String(tag) + QLatin1Char(':') + key;
    if (d->warnedKeys.contains(id)) {
        return false;
    }
    if (d->warnedKeys.size() < MaxWarnOnceEntries) {
        d->warnedKeys.insert(id);
    }
    return true;
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

bool JsonBackend::sync()
{
    if (!d->dirty) {
        return true;
    }

    if (!d->versionStampKey.isEmpty() && !d->root.contains(d->versionStampKey)) {
        d->root[d->versionStampKey] = d->versionStampValue;
    }

    // writeJsonAtomically already logs on failure; leave dirty=true so the
    // next sync() retries. Propagate the failure up so callers can surface
    // a user-visible error if the flush was interactive (save dialog, etc.).
    if (!writeJsonAtomically(d->filePath, d->root)) {
        return false;
    }

    d->dirty = false;
    return true;
}

bool JsonBackend::writeJsonAtomically(const QString& filePath, const QJsonObject& root)
{
    QDir dir = QFileInfo(filePath).absoluteDir();
    if (!dir.exists() && !dir.mkpath(QLatin1String("."))) {
        qWarning("PhosphorConfig::JsonBackend: failed to create directory %s", qPrintable(dir.absolutePath()));
        return false;
    }

    // Capture the existing file's permissions before QSaveFile replaces it —
    // QSaveFile's commit() creates a new inode with default umask perms, so
    // a user's `chmod 600 config.json` would silently widen on every save.
    // Re-applied after commit (small race window with default perms during
    // the rename, acceptable for desktop config).
    QFile::Permissions originalPerms;
    bool restorePerms = false;
    if (QFile::exists(filePath)) {
        originalPerms = QFile::permissions(filePath);
        restorePerms = (originalPerms != QFile::Permissions{});
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

    if (restorePerms && !QFile::setPermissions(filePath, originalPerms)) {
        qWarning("PhosphorConfig::JsonBackend: failed to restore permissions on %s after atomic write",
                 qPrintable(filePath));
        // Don't fail the write — content is on disk and readable; perm
        // restore failure is recoverable by the user.
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

    // Reserved root keys (version stamp + whatever the resolver owns at the
    // top level) are hidden from the default dot-path enumeration. Resolver-
    // owned keys are its EXCLUSIVE domain: the entire subtree under them is
    // also skipped so descendants don't leak as dot-path groups the Store
    // doesn't know how to purge. Otherwise a blanket deleteGroup on a leaked
    // descendant would recursively wipe resolver-managed data.
    QSet<QString> skipRoots;
    if (!d->versionStampKey.isEmpty()) {
        skipRoots.insert(d->versionStampKey);
    }
    if (d->resolver) {
        const QStringList reserved = d->resolver->reservedRootKeys();
        for (const QString& r : reserved) {
            skipRoots.insert(r);
        }
    }

    std::function<void(const QJsonObject&, const QString&, int)> enumerate = [&](const QJsonObject& obj,
                                                                                 const QString& prefix, int depth) {
        if (depth >= MaxDotPathDepth) {
            return;
        }
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            // Reserved keys are filtered at every depth, not just depth 0.
            // A resolver like PerScreenPathResolver owns "PerScreen" as an
            // opaque container — its children ("PerScreen.Autotile.<id>")
            // must not be re-exposed as generic dot-path groups.
            if (skipRoots.contains(it.key())) {
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
    // Warn if the previous root group already carries any writes, since
    // renaming mid-life orphans those keys under the old name on disk.
    if (d->rootGroupName != name && !d->root.value(d->rootGroupName).toObject().isEmpty()) {
        qWarning(
            "PhosphorConfig::JsonBackend: setRootGroupName('%s') called after the previous root group '%s' "
            "already holds keys — existing root-level writes will NOT be migrated.",
            qPrintable(name), qPrintable(d->rootGroupName));
    }
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

void JsonBackend::replaceRoot(QJsonObject root)
{
    Q_ASSERT_X(d->activeGroupCount == 0, "PhosphorConfig::JsonBackend::replaceRoot",
               "Cannot replaceRoot while JsonGroup instances are alive");
    d->root = std::move(root);
    // Callers that pair this with a successful writeJsonAtomically()
    // know the on-disk and in-memory states match; they should call
    // clearDirty() right after. We mark dirty here to cover the general
    // case where disk state is unknown.
    d->dirty = true;
}

} // namespace PhosphorConfig
