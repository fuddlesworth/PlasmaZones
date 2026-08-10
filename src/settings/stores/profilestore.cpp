// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "profilestore.h"

#include "config/configkeys.h"
#include "config/configmigration.h"
#include "config/configmigration_util.h"
#include "config/settingsvaluelabels.h"
#include "core/platform/logging.h"
#include "phosphor_i18n.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSaveFile>
#include <QUrl>

#include <algorithm>

namespace PlasmaZones {

namespace {

// Profiles first shipped in the same release as config schema v5, so no
// older stamp can name a profile-shaped file this store knows how to read.
constexpr int kProfOldestReadableVersion = 5;
// Frozen v5 spellings of the zone-colour groups, used only by the pre-v6
// child-delta refusal below (the same migration-freeze exemption the
// configmigration_v*.cpp constants carry).
constexpr QLatin1String kProfV5ColorsGroup{"Snapping.Zones.Colors"};
constexpr QLatin1String kProfV5LabelsGroup{"Snapping.Zones.Labels"};
constexpr QLatin1String kProfV5KeyFontColor{"FontColor"};
constexpr QLatin1String kProfIdKey{"id"};
constexpr QLatin1String kProfNameKey{"name"};
constexpr QLatin1String kProfDescriptionKey{"description"};
constexpr QLatin1String kProfParentKey{"parent"};
constexpr QLatin1String kProfConfigKey{"config"};
constexpr QLatin1String kProfRulesKey{"rules"};
constexpr QLatin1String kProfActiveKey{"active"};
constexpr QLatin1String kProfOrderKey{"order"};
constexpr QLatin1String kProfUpsertsKey{"upserts"};
constexpr QLatin1String kProfRemovedIdsKey{"removedIds"};

QList<QUuid> parseUuidArray(const QJsonArray& arr)
{
    QList<QUuid> out;
    for (const QJsonValue& v : arr) {
        const QUuid id(v.toString());
        if (!id.isNull()) {
            out.append(id);
        }
    }
    return out;
}

QJsonArray uuidArray(const QList<QUuid>& ids)
{
    QJsonArray arr;
    for (const QUuid& id : ids) {
        arr.append(id.toString());
    }
    return arr;
}

} // namespace

ProfileStore::ProfileStore(Config config, QObject* parent)
    : QObject(parent)
    , m_config(std::move(config))
{
}

// ── Paths ────────────────────────────────────────────────────────────────────

QString ProfileStore::profilesDirectory() const
{
    return m_config.profilesDir ? m_config.profilesDir() : QString();
}

QString ProfileStore::profileFilePath(const QUuid& id) const
{
    const QString dir = profilesDirectory();
    if (dir.isEmpty() || id.isNull()) {
        return QString();
    }
    // Filesystem paths use the brace-less UUID form (CLAUDE.md convention).
    return dir + QLatin1Char('/') + id.toString(QUuid::WithoutBraces) + QStringLiteral(".json");
}

QString ProfileStore::indexFilePath() const
{
    const QString dir = profilesDirectory();
    return dir.isEmpty() ? QString() : dir + QStringLiteral("/index.json");
}

// ── File I/O ──────────────────────────────────────────────────────────────────

bool ProfileStore::readProfileFile(const QString& path, Record* out) const
{
    // isFile() BEFORE open: the profiles dir is user-writable and
    // openProfilesDirectory() invites hand-placed files, so guard against a
    // fifo/device (which would block or report size 0) the same way the shader
    // set store does.
    const QFileInfo info(path);
    if (!info.isFile()) {
        qCWarning(lcConfig) << "ProfileStore: refusing to read a non-regular file" << path;
        return false;
    }
    if (info.size() > kMaxProfileFileBytes) {
        qCWarning(lcConfig) << "ProfileStore: profile file" << path << "is" << info.size() << "bytes, over the"
                            << kMaxProfileFileBytes << "byte cap — refusing";
        return false;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qCWarning(lcConfig) << "ProfileStore: cannot open profile file:" << path;
        return false;
    }
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcConfig) << "ProfileStore: failed to parse profile file" << path << ":" << err.errorString();
        return false;
    }
    QJsonObject root = doc.object();

    // Refuse a file stamped with an UNKNOWN schema version rather than
    // mis-applying config-delta keys whose shape moved between versions. A
    // file stamped with an older version we know (profiles first shipped at
    // schema v5, so v5 is the floor) is migrated forward in memory instead,
    // and the migrated record is simply served — the file itself is
    // rewritten on the profile's next save. Two contracts a future step must
    // honour on this path, on top of being a pure config→config transform:
    // the delta is stored FLAT (dot-path group names as top-level keys, the
    // Store export shape) while the migration helpers walk NESTED objects
    // (the config.json shape), so it is translated around the chain below;
    // and the delta is SPARSE, where a removed key means "inherit from the
    // parent" rather than "take the default", so a step that retires a value
    // must write the new default explicitly, never by removal. The
    // profileFormatTracksConfigSchemaVersion test pins the version so every
    // future ConfigSchemaVersion bump fails loudly there, forcing the author
    // to confirm both properties (or to extend this path when one fails).
    const QJsonValue versionVal = root.value(ConfigKeys::versionKey());
    const int fileVersion = versionVal.isDouble() ? versionVal.toInt() : -1;
    if (fileVersion < kProfOldestReadableVersion || fileVersion > m_config.formatVersion) {
        qCWarning(lcConfig) << "ProfileStore: profile file" << path << "has version" << versionVal
                            << "but this build reads versions" << kProfOldestReadableVersion << "through"
                            << m_config.formatVersion << "— refusing";
        return false;
    }
    if (fileVersion < m_config.formatVersion) {
        // The schema-group filter below is what turns the migrated nested
        // blob back into a delta; without the defaults blob it would emit an
        // EMPTY delta and silently erase the profile's config. Refuse the
        // migration path outright when the wiring is absent (test harnesses;
        // every production composition root supplies it).
        if (!m_config.defaultConfig) {
            qCWarning(lcConfig) << "ProfileStore: cannot migrate profile file" << path
                                << "without a defaultConfig source — refusing";
            return false;
        }

        QJsonObject flat = root.value(kProfConfigKey).toObject();

        // A pre-v6 CHILD delta touching the v5 zone-colour surface cannot be
        // migrated faithfully in isolation: whether its colour values were
        // palette snapshots or deliberate picks depends on the UseSystem
        // state resolved through its parents, which this per-file path does
        // not see. Drop ONLY the ambiguous colour entries and keep the
        // profile — the child then inherits its parent's colours, the
        // closest defensible reading — rather than refusing the whole file,
        // which would silently re-root its own children onto the wrong
        // resolution baseline. Root profiles diff against the defaults
        // blob, so their own delta carries the whole story and migrates
        // confidently.
        if (fileVersion < 6 && !QUuid(root.value(kProfParentKey).toString()).isNull()) {
            const bool touchesZoneColors = !flat.value(kProfV5ColorsGroup).toObject().isEmpty()
                || flat.value(kProfV5LabelsGroup).toObject().contains(kProfV5KeyFontColor);
            if (touchesZoneColors) {
                qCWarning(lcConfig) << "ProfileStore: profile file" << path
                                    << "is a version-5 child profile overriding zone colours; their"
                                    << "snapshot-vs-pick intent needs its parents, so the colour entries are"
                                    << "dropped and the profile inherits its parent's colours";
                flat.remove(kProfV5ColorsGroup);
                QJsonObject labels = flat.value(kProfV5LabelsGroup).toObject();
                labels.remove(kProfV5KeyFontColor);
                if (labels.isEmpty()) {
                    flat.remove(kProfV5LabelsGroup);
                } else {
                    flat[kProfV5LabelsGroup] = labels;
                }
            }
        }

        // Translate the flat delta into the nested config.json shape the
        // migration steps walk, run the chain, then flatten back through the
        // schema's group list. Keeping only schema-declared keys on the way
        // back doubles as a stale-key sweep (import would drop them anyway)
        // and keeps sub-group objects from masquerading as key values.
        QJsonObject nested;
        for (auto it = flat.constBegin(); it != flat.constEnd(); ++it) {
            if (!it.value().isObject()) {
                continue;
            }
            // A hand-edited empty or dot-only group key splits to NO
            // segments; the segment walkers assume at least one and would
            // read past the front of their chain in release builds.
            const QStringList segments = it.key().split(QLatin1Char('.'), Qt::SkipEmptyParts);
            if (segments.isEmpty()) {
                continue;
            }
            setGroupAtSegments(nested, segments, it.value().toObject());
        }
        nested[ConfigKeys::versionKey()] = fileVersion;
        ConfigMigration::runMigrationChainInMemory(nested);
        // The chain advances to ConfigSchemaVersion; a store configured for a
        // DIFFERENT target (tests inject formatVersion) cannot use the result.
        if (nested.value(ConfigKeys::versionKey()).toInt() != m_config.formatVersion) {
            qCWarning(lcConfig) << "ProfileStore: profile file" << path << "migrated to version"
                                << nested.value(ConfigKeys::versionKey()).toInt() << "but this store expects"
                                << m_config.formatVersion << "— refusing";
            return false;
        }
        QJsonObject migrated;
        const QJsonObject schemaGroups = m_config.defaultConfig();
        for (auto it = schemaGroups.constBegin(); it != schemaGroups.constEnd(); ++it) {
            if (!it.value().isObject()) {
                continue; // the defaults blob's own top-level _version stamp
            }
            const QJsonObject defGroup = it.value().toObject();
            const QJsonObject migratedGroup = groupObjectAtPath(nested, it.key());
            QJsonObject keptGroup;
            for (auto kit = migratedGroup.constBegin(); kit != migratedGroup.constEnd(); ++kit) {
                if (defGroup.contains(kit.key())) {
                    keptGroup.insert(kit.key(), kit.value());
                }
            }
            if (!keptGroup.isEmpty()) {
                migrated.insert(it.key(), keptGroup);
            }
        }
        root[kProfConfigKey] = migrated;
    }

    const QUuid id(root.value(kProfIdKey).toString());
    if (id.isNull()) {
        qCWarning(lcConfig) << "ProfileStore: profile file" << path << "has no valid id — refusing";
        return false;
    }

    out->id = id;
    out->name = root.value(kProfNameKey).toString();
    out->description = root.value(kProfDescriptionKey).toString();
    out->parent = QUuid(root.value(kProfParentKey).toString()); // null for a root profile
    out->configDelta = root.value(kProfConfigKey).toObject();

    const QJsonObject rules = root.value(kProfRulesKey).toObject();
    out->ruleOrder = parseUuidArray(rules.value(kProfOrderKey).toArray());
    out->ruleRemovedIds = parseUuidArray(rules.value(kProfRemovedIdsKey).toArray());
    out->ruleUpserts.clear();
    const QJsonArray upserts = rules.value(kProfUpsertsKey).toArray();
    for (const QJsonValue& v : upserts) {
        if (auto rule = PhosphorRules::Rule::fromJson(v.toObject())) {
            out->ruleUpserts.append(*rule);
        }
    }
    return true;
}

QJsonObject ProfileStore::recordToJson(const Record& rec, int formatVersion)
{
    QJsonObject root;
    // The envelope deliberately reuses the config schema's version-key
    // spelling so one accessor names both stamps.
    root.insert(ConfigKeys::versionKey(), formatVersion);
    root.insert(kProfIdKey, rec.id.toString());
    root.insert(kProfNameKey, rec.name);
    root.insert(kProfDescriptionKey, rec.description);
    // A root profile stores null so the field is always present and explicit.
    root.insert(kProfParentKey, rec.parent.isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(rec.parent.toString()));
    root.insert(kProfConfigKey, rec.configDelta);

    QJsonArray upserts;
    for (const PhosphorRules::Rule& rule : rec.ruleUpserts) {
        upserts.append(rule.toJson());
    }
    root.insert(kProfRulesKey,
                QJsonObject{
                    {kProfOrderKey, uuidArray(rec.ruleOrder)},
                    {kProfUpsertsKey, upserts},
                    {kProfRemovedIdsKey, uuidArray(rec.ruleRemovedIds)},
                });
    return root;
}

bool ProfileStore::writeProfileRecord(const Record& rec)
{
    const QString dir = profilesDirectory();
    const QString path = profileFilePath(rec.id);
    if (path.isEmpty() || dir.isEmpty() || !QDir().mkpath(dir)) {
        qCWarning(lcConfig) << "ProfileStore: cannot create the profiles folder" << dir;
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not create the profiles folder."));
        return false;
    }
    QSaveFile file(path);
    const QByteArray payload = QJsonDocument(recordToJson(rec, m_config.formatVersion)).toJson(QJsonDocument::Indented);
    const bool written =
        file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(payload) == payload.size() && file.commit();
    if (!written) {
        qCWarning(lcConfig) << "ProfileStore: could not write" << path << ":" << file.errorString();
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not write the profile to disk."));
        return false;
    }
    return true;
}

QHash<QUuid, ProfileStore::Record> ProfileStore::loadAll() const
{
    if (m_recordCache) {
        return *m_recordCache;
    }
    QHash<QUuid, Record> result;
    const QString dirPath = profilesDirectory();
    if (dirPath.isEmpty()) {
        return result;
    }
    const QDir dir(dirPath);
    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString& name : files) {
        if (name == QLatin1String("index.json")) {
            continue;
        }
        Record rec;
        if (readProfileFile(dir.absoluteFilePath(name), &rec)) {
            result.insert(rec.id, rec);
        }
    }
    m_recordCache = result;
    return result;
}

// ── Inheritance resolution / delta ────────────────────────────────────────────

namespace {

/// Keys that name THIS machine's hardware and must never ride into profiles.
/// A captured delta would ship a hardware identity into an export another
/// machine cannot satisfy, and applying one from an existing record would
/// silently repoint the render GPU on profile switch (with no daemon-running
/// gate or restart notice). Enforced on the capture side (diffConfig) and by
/// stripping the key from resolveConfig's returned blob — NOT inside
/// overlayConfig: the resolve seed comes from defaultConfig(), which carries
/// EVERY declared key including this one at its default, so a skip during
/// overlay would let that seeded default survive into the resolved blob and
/// RESET the user's live pin on every profile activation. Removing the key
/// from the resolved blob instead leaves the live value untouched, because
/// Store::importFromJson only writes keys present in the blob. Currently
/// just Rendering.Gpu, a PCI vendor:device pair.
bool isMachineScopedKey(const QString& group, const QString& key)
{
    return group == ConfigKeys::renderingGroup() && key == ConfigKeys::gpuKey();
}

void stripMachineScopedKeys(QJsonObject& config)
{
    for (auto git = config.begin(); git != config.end(); ++git) {
        if (!git.value().isObject()) {
            continue;
        }
        QJsonObject group = git.value().toObject();
        bool changed = false;
        for (auto kit = group.begin(); kit != group.end();) {
            if (isMachineScopedKey(git.key(), kit.key())) {
                kit = group.erase(kit);
                changed = true;
            } else {
                ++kit;
            }
        }
        if (changed) {
            git.value() = group;
        }
    }
}

} // namespace

void ProfileStore::overlayConfig(QJsonObject& base, const QJsonObject& delta)
{
    for (auto git = delta.constBegin(); git != delta.constEnd(); ++git) {
        // Skip non-object members the way diffConfig's capture side does: a
        // hand-edited scalar at a group slot would otherwise read as {} and
        // wipe the whole base group out of the resolved blob.
        if (!git.value().isObject()) {
            continue;
        }
        const QJsonObject deltaGroup = git.value().toObject();
        QJsonObject baseGroup = base.value(git.key()).toObject();
        for (auto kit = deltaGroup.constBegin(); kit != deltaGroup.constEnd(); ++kit) {
            baseGroup.insert(kit.key(), kit.value());
        }
        base.insert(git.key(), baseGroup);
    }
}

QJsonObject ProfileStore::diffConfig(const QJsonObject& full, const QJsonObject& base)
{
    QJsonObject delta;
    for (auto git = full.constBegin(); git != full.constEnd(); ++git) {
        // Skip the top-level `_version` marker — only group objects are walked.
        if (!git.value().isObject()) {
            continue;
        }
        const QJsonObject fullGroup = git.value().toObject();
        const QJsonObject baseGroup = base.value(git.key()).toObject();
        QJsonObject deltaGroup;
        for (auto kit = fullGroup.constBegin(); kit != fullGroup.constEnd(); ++kit) {
            if (isMachineScopedKey(git.key(), kit.key())) {
                continue;
            }
            if (baseGroup.value(kit.key()) != kit.value()) {
                deltaGroup.insert(kit.key(), kit.value());
            }
        }
        if (!deltaGroup.isEmpty()) {
            delta.insert(git.key(), deltaGroup);
        }
    }
    return delta;
}

QJsonObject ProfileStore::resolveConfig(const QUuid& id, const QHash<QUuid, Record>& all) const
{
    // Collect the chain root → … → id so deltas overlay in inheritance order
    // (a child overrides its ancestors). A broken parent link (missing id) or a
    // cycle stops the walk; the accumulated chain is still applied.
    QList<QUuid> chain;
    QUuid cursor = id;
    QSet<QUuid> seen;
    while (!cursor.isNull() && all.contains(cursor) && !seen.contains(cursor)) {
        seen.insert(cursor);
        chain.prepend(cursor);
        cursor = all.value(cursor).parent;
    }

    QJsonObject resolved = m_config.defaultConfig ? m_config.defaultConfig() : QJsonObject();
    for (const QUuid& node : chain) {
        overlayConfig(resolved, all.value(node).configDelta);
    }
    // Single choke point for the machine-scoped exclusion: every consumer of
    // a resolved blob (activation staging, the active-modified compare, the
    // profile signature, and the parent-resolved diff baselines) sees a blob
    // with no hardware-identity keys, and an absent key is a no-op on apply
    // (see isMachineScopedKey above). Profile EXPORT copies the profile file
    // verbatim and never resolves, so exports are protected by diffConfig's
    // capture-side skip instead. The null-parent diff baseline is the raw
    // defaultConfig() and stays deliberately unstripped: diffConfig's own
    // skip enforces the invariant on that path.
    stripMachineScopedKeys(resolved);
    return resolved;
}

bool ProfileStore::rulesSemanticallyEqual(const PhosphorRules::Rule& a, const PhosphorRules::Rule& b)
{
    // Rule::operator== compares every field including the list-order-derived
    // `priority`, which renormalizePriorities re-stamps. Neutralise priority so
    // two rules that differ only by re-stamped priority read as equal (id /
    // managed already match by construction of the caller).
    PhosphorRules::Rule normalized = a;
    normalized.priority = b.priority;
    return normalized == b;
}

QList<PhosphorRules::Rule> ProfileStore::resolveRules(const QUuid& id, const QHash<QUuid, Record>& all) const
{
    // Chain root → … → id, so each profile's delta applies over its ancestors'.
    QList<QUuid> chain;
    QUuid cursor = id;
    QSet<QUuid> seen;
    while (!cursor.isNull() && all.contains(cursor) && !seen.contains(cursor)) {
        seen.insert(cursor);
        chain.prepend(cursor);
        cursor = all.value(cursor).parent;
    }

    // Resolve the user rule set by id, applying each level's removals + upserts,
    // then ordering per the deepest level that specifies an order.
    QHash<QUuid, PhosphorRules::Rule> byId;
    QList<QUuid> order; // insertion order fallback
    for (const QUuid& node : chain) {
        const Record& rec = all.value(node);
        for (const QUuid& removed : rec.ruleRemovedIds) {
            byId.remove(removed);
            order.removeAll(removed);
        }
        for (const PhosphorRules::Rule& rule : rec.ruleUpserts) {
            if (!byId.contains(rule.id)) {
                order.append(rule.id);
            }
            byId.insert(rule.id, rule);
        }
        // A stored order for this level wins (captures the user's reordering).
        if (!rec.ruleOrder.isEmpty()) {
            QList<QUuid> reordered;
            for (const QUuid& oid : rec.ruleOrder) {
                if (byId.contains(oid) && !reordered.contains(oid)) {
                    reordered.append(oid);
                }
            }
            for (const QUuid& oid : order) {
                if (byId.contains(oid) && !reordered.contains(oid)) {
                    reordered.append(oid);
                }
            }
            order = reordered;
        }
    }

    QList<PhosphorRules::Rule> out;
    for (const QUuid& oid : order) {
        if (byId.contains(oid)) {
            out.append(byId.value(oid));
        }
    }
    return out;
}

void ProfileStore::computeRuleDelta(const QList<PhosphorRules::Rule>& full, const QList<PhosphorRules::Rule>& base,
                                    Record& rec)
{
    QHash<QUuid, PhosphorRules::Rule> baseById;
    for (const PhosphorRules::Rule& r : base) {
        baseById.insert(r.id, r);
    }
    QSet<QUuid> fullIds;

    rec.ruleUpserts.clear();
    rec.ruleOrder.clear();
    rec.ruleRemovedIds.clear();
    for (const PhosphorRules::Rule& r : full) {
        fullIds.insert(r.id);
        rec.ruleOrder.append(r.id);
        // New id, or present in the parent but semantically changed → store it.
        if (!baseById.contains(r.id) || !rulesSemanticallyEqual(r, baseById.value(r.id))) {
            rec.ruleUpserts.append(r);
        }
    }
    // Parent rules the profile drops.
    for (const PhosphorRules::Rule& r : base) {
        if (!fullIds.contains(r.id)) {
            rec.ruleRemovedIds.append(r.id);
        }
    }
}

bool ProfileStore::isSelfOrAncestor(const QUuid& maybeAncestor, const QUuid& id, const QHash<QUuid, Record>& all) const
{
    QUuid cursor = id;
    QSet<QUuid> seen;
    while (!cursor.isNull() && !seen.contains(cursor)) {
        if (cursor == maybeAncestor) {
            return true;
        }
        seen.insert(cursor);
        if (!all.contains(cursor)) {
            break;
        }
        cursor = all.value(cursor).parent;
    }
    return false;
}

QString ProfileStore::uniqueName(const QString& desired, const QHash<QUuid, Record>& all, const QUuid& excludeId) const
{
    const QString trimmed = desired.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }
    const auto taken = [&](const QString& candidate) {
        for (auto it = all.constBegin(); it != all.constEnd(); ++it) {
            if (it.key() != excludeId && it.value().name.compare(candidate, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
        return false;
    };
    QString candidate = trimmed;
    for (int suffix = 2; taken(candidate); ++suffix) {
        if (suffix > 999) {
            return QString();
        }
        candidate = QStringLiteral("%1 (%2)").arg(trimmed).arg(suffix);
    }
    return candidate;
}

// ── index.json (active pointer + display order) ───────────────────────────────

QJsonObject ProfileStore::readIndex() const
{
    if (m_indexCache) {
        return *m_indexCache;
    }
    const QString path = indexFilePath();
    if (path.isEmpty()) {
        return QJsonObject();
    }
    // Same prologue as readProfileFile, for the same reason: this directory is
    // user-writable and openProfilesDirectory() invites hand-placed files, so
    // a fifo/device (which would block) or an oversized file must be refused
    // before the open/readAll.
    const QFileInfo info(path);
    if (!info.isFile() || info.size() > kMaxProfileFileBytes) {
        return QJsonObject();
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return QJsonObject();
    }
    const QByteArray bytes = f.readAll();
    f.close();
    QJsonParseError parseErr{};
    const auto doc = QJsonDocument::fromJson(bytes, &parseErr);
    if (!bytes.trimmed().isEmpty() && (parseErr.error != QJsonParseError::NoError || !doc.isObject())) {
        // A corrupt index must not be treated as an empty one: the next
        // appendToOrder/writeActiveId would commit that emptiness over the
        // surviving order and active pointer. Quarantine the bytes (the
        // sibling readProfileFile refuses instead, but the index is
        // degradable state, so starting fresh is the better failure mode):
        // profiles stay visible through depthFirstOrder's name-sorted
        // leftovers, the sibling order degrades to alphabetical until the
        // next writeIndex commit re-records it, and the committed active
        // pointer is lost (the staged one is unaffected).
        const QString quarantine = path + QStringLiteral(".corrupt.bak");
        QFile::remove(quarantine);
        QFile::rename(path, quarantine);
        qCWarning(lcConfig) << "ProfileStore: index.json is not valid JSON (" << parseErr.errorString()
                            << ") — moved to" << quarantine << "and starting with a fresh index";
        m_indexCache = QJsonObject();
        return QJsonObject();
    }
    QJsonObject index = doc.isObject() ? doc.object() : QJsonObject();
    m_indexCache = index;
    return index;
}

bool ProfileStore::writeIndex(const QJsonObject& index)
{
    const QString dir = profilesDirectory();
    const QString path = indexFilePath();
    if (path.isEmpty() || dir.isEmpty() || !QDir().mkpath(dir)) {
        qCWarning(lcConfig) << "ProfileStore: cannot write index.json under" << dir;
        return false;
    }
    QSaveFile file(path);
    const QByteArray payload = QJsonDocument(index).toJson(QJsonDocument::Indented);
    if (!(file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(payload) == payload.size()
          && file.commit())) {
        qCWarning(lcConfig) << "ProfileStore: could not write index.json:" << file.errorString();
        return false;
    }
    m_indexCache = index;
    return true;
}

QList<QUuid> ProfileStore::readOrder() const
{
    QList<QUuid> order;
    const QJsonArray arr = readIndex().value(kProfOrderKey).toArray();
    for (const QJsonValue& v : arr) {
        const QUuid id(v.toString());
        if (!id.isNull()) {
            order.append(id);
        }
    }
    return order;
}

void ProfileStore::appendToOrder(const QUuid& id)
{
    QJsonObject index = readIndex();
    QJsonArray order = index.value(kProfOrderKey).toArray();
    for (const QJsonValue& v : order) {
        if (QUuid(v.toString()) == id) {
            return; // already present
        }
    }
    order.append(id.toString());
    index.insert(kProfOrderKey, order);
    writeIndex(index);
}

void ProfileStore::removeFromOrder(const QUuid& id)
{
    QJsonObject index = readIndex();
    const QJsonArray order = index.value(kProfOrderKey).toArray();
    QJsonArray kept;
    for (const QJsonValue& v : order) {
        if (QUuid(v.toString()) != id) {
            kept.append(v);
        }
    }
    index.insert(kProfOrderKey, kept);
    writeIndex(index);
}

QString ProfileStore::committedActiveId() const
{
    return readIndex().value(kProfActiveKey).toString();
}

bool ProfileStore::writeActiveId(const QString& id)
{
    const bool changed = committedActiveId() != id;
    QJsonObject index = readIndex();
    index.insert(kProfActiveKey, id.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(id));
    if (!writeIndex(index)) {
        return false;
    }
    if (changed) {
        Q_EMIT committedActiveIdChanged(id);
    }
    return true;
}

// ── Query ─────────────────────────────────────────────────────────────────────

QString ProfileStore::activeProfileId() const
{
    return m_config.stagedActiveId ? m_config.stagedActiveId() : QString();
}

void ProfileStore::refresh()
{
    notifyProfilesChanged();
}

void ProfileStore::notifyProfilesChanged()
{
    m_signatureCache.clear();
    m_recordCache.reset();
    Q_EMIT profilesChanged();
}

void ProfileStore::depthFirstOrder(const QHash<QUuid, Record>& all, QList<QUuid>& orderOut,
                                   QHash<QUuid, int>& depthOut) const
{
    orderOut.clear();
    depthOut.clear();

    // Sibling order: index.json order first (for ids still present), then any
    // profiles not in the order (hand-placed / freshly imported), by name.
    QList<QUuid> flat;
    for (const QUuid& id : readOrder()) {
        if (all.contains(id) && !flat.contains(id)) {
            flat.append(id);
        }
    }
    QList<QUuid> leftovers;
    for (auto it = all.constBegin(); it != all.constEnd(); ++it) {
        if (!flat.contains(it.key())) {
            leftovers.append(it.key());
        }
    }
    std::sort(leftovers.begin(), leftovers.end(), [&](const QUuid& a, const QUuid& b) {
        return all.value(a).name.compare(all.value(b).name, Qt::CaseInsensitive) < 0;
    });
    flat.append(leftovers);

    // Children of each node, in sibling (flat) order. A profile whose parent is
    // null or missing is a root.
    const auto isRoot = [&](const QUuid& id) {
        const QUuid p = all.value(id).parent;
        return p.isNull() || !all.contains(p);
    };
    QHash<QUuid, QList<QUuid>> childrenOf;
    for (const QUuid& id : flat) {
        if (!isRoot(id)) {
            childrenOf[all.value(id).parent].append(id);
        }
    }

    // Depth-first walk from each root, cycle-guarded.
    QSet<QUuid> visited;
    std::function<void(const QUuid&, int)> visit = [&](const QUuid& id, int depth) {
        if (visited.contains(id)) {
            return;
        }
        visited.insert(id);
        orderOut.append(id);
        depthOut.insert(id, depth);
        for (const QUuid& child : childrenOf.value(id)) {
            visit(child, depth + 1);
        }
    };
    for (const QUuid& id : flat) {
        if (isRoot(id)) {
            visit(id, 0);
        }
    }
    // Any node not reached (part of a cycle) is appended at depth 0 so it stays
    // visible and editable.
    for (const QUuid& id : flat) {
        if (!visited.contains(id)) {
            visit(id, 0);
        }
    }
}

QString ProfileStore::profileSignature(const QUuid& id, const QHash<QUuid, Record>& all) const
{
    // Hash the RESOLVED cascade, not this profile's delta: two profiles that end
    // up at the same settings should carry the same signature. QJsonObject keys
    // serialize in sorted order, so the Compact form is a canonical encoding.
    //
    // Cached until the next mutation (notifyProfilesChanged clears): the
    // signature depends only on the profile files, and this runs per row on
    // every availableProfiles() call.
    if (const auto it = m_signatureCache.constFind(id); it != m_signatureCache.constEnd()) {
        return *it;
    }
    QByteArray payload = QJsonDocument(resolveConfig(id, all)).toJson(QJsonDocument::Compact);
    for (const PhosphorRules::Rule& rule : resolveRules(id, all)) {
        payload += QJsonDocument(rule.toJson()).toJson(QJsonDocument::Compact);
    }
    const QString signature = QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha1).toHex());
    m_signatureCache.insert(id, signature);
    return signature;
}

QVariantList ProfileStore::availableProfiles() const
{
    const QHash<QUuid, Record> all = loadAll();
    const QString stagedActive = activeProfileId();
    const bool activeModified = isActiveModified(all);

    QList<QUuid> ordered;
    QHash<QUuid, int> depth;
    depthFirstOrder(all, ordered, depth);

    QVariantList rows;
    for (const QUuid& id : ordered) {
        const Record& rec = all.value(id);
        const QString idStr = rec.id.toString();
        QVariantMap row;
        row.insert(QStringLiteral("id"), idStr);
        row.insert(QStringLiteral("name"), rec.name);
        row.insert(QStringLiteral("description"), rec.description);
        row.insert(QStringLiteral("parentId"), rec.parent.isNull() ? QString() : rec.parent.toString());
        row.insert(QStringLiteral("parentName"),
                   rec.parent.isNull() || !all.contains(rec.parent) ? QString() : all.value(rec.parent).name);
        row.insert(QStringLiteral("isRoot"), rec.parent.isNull() || !all.contains(rec.parent));
        row.insert(QStringLiteral("depth"), depth.value(id, 0));
        const bool active = idStr == stagedActive;
        row.insert(QStringLiteral("active"), active);
        row.insert(QStringLiteral("modified"), active && activeModified);
        row.insert(QStringLiteral("signature"), profileSignature(id, all));
        rows.append(row);
    }
    return rows;
}

bool ProfileStore::isActiveModified(const QHash<QUuid, Record>& all) const
{
    const QUuid uid(activeProfileId());
    if (uid.isNull() || !all.contains(uid)) {
        return false;
    }

    // Config: compare the profile's resolved blob to the live config, key by
    // key over the RESOLVED blob's keys. The resolved blob carries every
    // declared key EXCEPT the machine-scoped ones stripped by resolveConfig,
    // so a wholesale group compare would read a live GPU pin as a permanent
    // modification; comparing only the keys the blob carries tolerates the
    // machine-scoped extras while still catching every real divergence.
    if (m_config.currentConfig) {
        const QJsonObject resolved = resolveConfig(uid, all);
        const QJsonObject current = m_config.currentConfig();
        for (auto git = resolved.constBegin(); git != resolved.constEnd(); ++git) {
            if (!git.value().isObject()) {
                continue;
            }
            const QJsonObject resolvedGroup = git.value().toObject();
            const QJsonObject currentGroup = current.value(git.key()).toObject();
            for (auto kit = resolvedGroup.constBegin(); kit != resolvedGroup.constEnd(); ++kit) {
                if (currentGroup.value(kit.key()) != kit.value()) {
                    return true;
                }
            }
        }
    }

    // Rules: compare resolved user rules to the live user rules, order-sensitive,
    // ignoring the renormalized priority (rulesSemanticallyEqual).
    if (m_config.currentUserRules) {
        const QList<PhosphorRules::Rule> resolved = resolveRules(uid, all);
        const QList<PhosphorRules::Rule> current = m_config.currentUserRules();
        if (resolved.size() != current.size()) {
            return true;
        }
        for (int i = 0; i < resolved.size(); ++i) {
            if (resolved[i].id != current[i].id || !rulesSemanticallyEqual(resolved[i], current[i])) {
                return true;
            }
        }
    }
    return false;
}

bool ProfileStore::updateProfileFromCurrent(const QString& id)
{
    QHash<QUuid, Record> all = loadAll();
    const QUuid uid(id);
    if (!all.contains(uid)) {
        return false;
    }
    Record rec = all.value(uid);
    const QUuid parent = rec.parent;

    // Recompute the config + rule delta from the current live settings, against
    // the same parent-resolved baseline createProfile uses.
    const QJsonObject parentResolved = parent.isNull() || !all.contains(parent)
        ? (m_config.defaultConfig ? m_config.defaultConfig() : QJsonObject())
        : resolveConfig(parent, all);
    const QJsonObject current = m_config.currentConfig ? m_config.currentConfig() : QJsonObject();
    rec.configDelta = diffConfig(current, parentResolved);

    if (m_config.currentUserRules) {
        const QList<PhosphorRules::Rule> parentRules =
            parent.isNull() || !all.contains(parent) ? QList<PhosphorRules::Rule>() : resolveRules(parent, all);
        computeRuleDelta(m_config.currentUserRules(), parentRules, rec);
    }

    if (!writeProfileRecord(rec)) {
        return false;
    }
    notifyProfilesChanged();
    return true;
}

// ── CRUD ──────────────────────────────────────────────────────────────────────

QString ProfileStore::createProfile(const QString& name, const QString& description, const QString& parentId)
{
    const QHash<QUuid, Record> all = loadAll();

    const QString finalName = uniqueName(name, all);
    if (finalName.isEmpty()) {
        Q_EMIT toastRequested(PhosphorI18n::tr("Please enter a name for the profile."));
        return QString();
    }

    const QUuid parent(parentId); // null when empty
    if (!parent.isNull() && !all.contains(parent)) {
        qCWarning(lcConfig) << "ProfileStore::createProfile: unknown parent" << parentId;
        return QString();
    }

    Record rec;
    rec.id = QUuid::createUuid();
    rec.name = finalName;
    rec.description = description.trimmed();
    rec.parent = parent;
    // Capture the current live config as a delta against the parent's resolved
    // config (schema defaults for a root profile).
    const QJsonObject parentResolved = parent.isNull()
        ? (m_config.defaultConfig ? m_config.defaultConfig() : QJsonObject())
        : resolveConfig(parent, all);
    const QJsonObject current = m_config.currentConfig ? m_config.currentConfig() : QJsonObject();
    rec.configDelta = diffConfig(current, parentResolved);

    // Capture the current user rules as a delta against the parent-resolved set.
    if (m_config.currentUserRules) {
        const QList<PhosphorRules::Rule> parentRules =
            parent.isNull() ? QList<PhosphorRules::Rule>() : resolveRules(parent, all);
        computeRuleDelta(m_config.currentUserRules(), parentRules, rec);
    }

    if (!writeProfileRecord(rec)) {
        return QString();
    }
    appendToOrder(rec.id);

    // The new profile matches the current settings, so it becomes the staged
    // active one without staging any config change.
    if (m_config.setStagedActiveId) {
        m_config.setStagedActiveId(rec.id.toString());
    }
    notifyProfilesChanged();
    return rec.id.toString();
}

bool ProfileStore::renameProfile(const QString& id, const QString& newName, const QString& description)
{
    QHash<QUuid, Record> all = loadAll();
    const QUuid uid(id);
    if (!all.contains(uid)) {
        return false;
    }
    const QString finalName = uniqueName(newName, all, uid);
    if (finalName.isEmpty()) {
        Q_EMIT toastRequested(PhosphorI18n::tr("Please enter a name for the profile."));
        return false;
    }
    // Create/duplicate/import auto-suffix silently, but a rename is the user
    // typing an exact target name; say so when it had to be adjusted.
    if (finalName != newName.trimmed()) {
        Q_EMIT toastRequested(PhosphorI18n::tr("Another profile is named “%1”, so this one is now “%2”.")
                                  .arg(newName.trimmed(), finalName));
    }
    Record rec = all.value(uid);
    if (rec.name == finalName && rec.description == description.trimmed()) {
        return true; // no-op, like setParent's same-parent guard
    }
    rec.name = finalName;
    rec.description = description.trimmed();
    if (!writeProfileRecord(rec)) {
        return false;
    }
    notifyProfilesChanged();
    return true;
}

QString ProfileStore::duplicateProfile(const QString& id)
{
    const QHash<QUuid, Record> all = loadAll();
    const QUuid uid(id);
    if (!all.contains(uid)) {
        return QString();
    }
    Record rec = all.value(uid);
    rec.id = QUuid::createUuid();
    // ruleUpserts is copied verbatim, so the clone shares Rule::ids with the
    // original. Intentional: each profile resolves its own independent chain,
    // and the shared id is what lets the clone keep overriding the same parent
    // rule its source did.
    rec.name = uniqueName(PhosphorI18n::tr("%1 (copy)").arg(rec.name), all);
    if (rec.name.isEmpty()) {
        return QString();
    }
    if (!writeProfileRecord(rec)) {
        return QString();
    }
    appendToOrder(rec.id);
    notifyProfilesChanged();
    return rec.id.toString();
}

bool ProfileStore::setParent(const QString& id, const QString& parentId)
{
    QHash<QUuid, Record> all = loadAll();
    const QUuid uid(id);
    const QUuid parent(parentId); // null → root
    if (!all.contains(uid)) {
        return false;
    }
    if (!parent.isNull() && !all.contains(parent)) {
        return false;
    }
    // Reject a cycle: the new parent must not be the profile itself or one of
    // its descendants.
    if (!parent.isNull() && isSelfOrAncestor(uid, parent, all)) {
        Q_EMIT toastRequested(PhosphorI18n::tr("A profile cannot inherit from itself."));
        return false;
    }

    Record rec = all.value(uid);
    if (rec.parent == parent) {
        return true; // no-op
    }
    // Re-flatten the delta so the RESOLVED config is unchanged after reparenting.
    const QJsonObject resolvedSelf = resolveConfig(uid, all);
    const QJsonObject newParentResolved = parent.isNull()
        ? (m_config.defaultConfig ? m_config.defaultConfig() : QJsonObject())
        : resolveConfig(parent, all);
    // Re-flatten rules the same way, keeping the resolved user rule set unchanged.
    const QList<PhosphorRules::Rule> resolvedRulesSelf = resolveRules(uid, all);
    const QList<PhosphorRules::Rule> newParentRules =
        parent.isNull() ? QList<PhosphorRules::Rule>() : resolveRules(parent, all);
    rec.parent = parent;
    rec.configDelta = diffConfig(resolvedSelf, newParentResolved);
    computeRuleDelta(resolvedRulesSelf, newParentRules, rec);
    if (!writeProfileRecord(rec)) {
        return false;
    }
    notifyProfilesChanged();
    return true;
}

bool ProfileStore::removeProfile(const QString& id)
{
    QHash<QUuid, Record> all = loadAll();
    const QUuid uid(id);
    if (!all.contains(uid)) {
        return false;
    }
    const QUuid grandparent = all.value(uid).parent;

    // Rebind each direct child to the deleted node's parent, re-flattening its
    // delta so its resolved config is unchanged. Compute every child's resolved
    // config against the CURRENT tree before mutating anything. The new
    // parent's resolution depends only on the grandparent, so it is computed
    // once, not per child.
    const QJsonObject newParentResolved = grandparent.isNull()
        ? (m_config.defaultConfig ? m_config.defaultConfig() : QJsonObject())
        : resolveConfig(grandparent, all);
    const QList<PhosphorRules::Rule> newParentRules =
        grandparent.isNull() ? QList<PhosphorRules::Rule>() : resolveRules(grandparent, all);
    for (auto it = all.constBegin(); it != all.constEnd(); ++it) {
        if (it.value().parent != uid) {
            continue;
        }
        const QUuid childId = it.key();
        const QJsonObject childResolved = resolveConfig(childId, all);
        const QList<PhosphorRules::Rule> childRules = resolveRules(childId, all);
        Record child = it.value();
        child.parent = grandparent;
        child.configDelta = diffConfig(childResolved, newParentResolved);
        computeRuleDelta(childRules, newParentRules, child);
        if (!writeProfileRecord(child)) {
            // Children rewritten so far ARE reparented on disk; announce the
            // partial state rather than leaving the view showing the old tree.
            notifyProfilesChanged();
            return false;
        }
    }

    const QString path = profileFilePath(uid);
    if (!path.isEmpty() && QFile::exists(path) && !QFile::remove(path)) {
        qCWarning(lcConfig) << "ProfileStore::removeProfile: could not delete" << path;
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not delete the profile."));
        // Children rewritten above ARE reparented on disk; announce the
        // partial state (same contract as the loop-failure branch) so the
        // record cache does not keep serving the pre-reparent tree.
        notifyProfilesChanged();
        return false;
    }
    removeFromOrder(uid);

    // Clear BOTH active pointers if they referenced the deleted profile. The
    // staged one drives the UI; the committed one lives in index.json, and left
    // dangling it would be re-adopted as the staged id on next launch (or on
    // Discard), pointing the page at a profile that no longer exists.
    // The two clears are separate notifications, so the controller passes
    // through a one-call transient where only one pointer is cleared and the
    // page reads dirty; the second clear lands synchronously right after, so
    // no UI frame ever observes it.
    if (m_config.stagedActiveId && m_config.setStagedActiveId && QUuid(m_config.stagedActiveId()) == uid) {
        m_config.setStagedActiveId(QString());
    }
    if (QUuid(committedActiveId()) == uid) {
        writeActiveId(QString());
    }
    notifyProfilesChanged();
    return true;
}

bool ProfileStore::activateProfile(const QString& id)
{
    const QHash<QUuid, Record> all = loadAll();
    const QUuid uid(id);
    if (!all.contains(uid)) {
        return false;
    }
    // Re-activating the clean active profile would stage nothing new but WOULD
    // renormalize rule priorities and dirty the Rules page. When the user has
    // edited away from it, activation is the "re-apply, discarding my edits"
    // action and must run.
    if (uid == QUuid(activeProfileId()) && !isActiveModified(all)) {
        return true;
    }
    // Resolve the full config and stage it into the settings store (uncommitted,
    // lights the Save footer). The controller's Save commits it. The store
    // refuses a blob from a mismatched schema version; abort the whole
    // activation then, or the rules and active pointer would flip while the
    // config silently stayed put.
    if (m_config.applyConfig && !m_config.applyConfig(resolveConfig(uid, all))) {
        Q_EMIT toastRequested(
            PhosphorI18n::tr("Could not apply this profile. Its settings do not match this version."));
        return false;
    }
    // Stage the resolved user rules into the Rules page the same way.
    if (m_config.applyUserRules) {
        m_config.applyUserRules(resolveRules(uid, all));
    }
    if (m_config.setStagedActiveId) {
        m_config.setStagedActiveId(id);
    }
    notifyProfilesChanged();
    return true;
}

// ── Import / export ────────────────────────────────────────────────────────────

bool ProfileStore::exportProfile(const QString& id, const QString& destLocalPath)
{
    if (destLocalPath.isEmpty()) {
        // urlToLocalFile yields an empty string for a non-local save target.
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not write to that location."));
        return false;
    }
    const QUuid uid(id);
    const QString sourcePath = profileFilePath(uid);
    if (sourcePath.isEmpty()) {
        return false;
    }
    // Export the RECORD, not the raw file bytes: a pre-current profile is
    // migrated forward in memory by loadAll, and exporting the stale bytes
    // would hand out a file that stops being importable the moment a future
    // build raises the version floor. The record's delta was captured with
    // the machine-scoped-key skip applied, so re-serialising keeps exports
    // free of machine-scoped keys exactly like the verbatim copy did.
    const QHash<QUuid, Record> all = loadAll();
    if (!all.contains(uid)) {
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not read that profile."));
        return false;
    }
    const QByteArray payload =
        QJsonDocument(recordToJson(all.value(uid), m_config.formatVersion)).toJson(QJsonDocument::Indented);
    QSaveFile dest(destLocalPath);
    if (!(dest.open(QIODevice::WriteOnly | QIODevice::Truncate) && dest.write(payload) == payload.size()
          && dest.commit())) {
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not write to %1.").arg(destLocalPath));
        return false;
    }
    return true;
}

QString ProfileStore::importProfile(const QString& sourcePathOrUrl)
{
    if (sourcePathOrUrl.isEmpty()) {
        return QString();
    }
    // The drop zone hands over raw file:// URLs; the file dialog hands over
    // local paths. Accept both.
    QString sourcePath = sourcePathOrUrl;
    const QUrl url(sourcePathOrUrl);
    if (url.isLocalFile()) {
        sourcePath = url.toLocalFile();
    }

    Record rec;
    if (!readProfileFile(sourcePath, &rec)) {
        Q_EMIT toastRequested(PhosphorI18n::tr("That file is not a readable profile."));
        return QString();
    }

    QHash<QUuid, Record> all = loadAll();
    // Always mint a FRESH id so an import never overwrites an existing profile.
    rec.id = QUuid::createUuid();
    // Drop a dangling parent reference (the parent is not in this install).
    if (!rec.parent.isNull() && !all.contains(rec.parent)) {
        rec.parent = QUuid();
    }
    QString desiredName = rec.name;
    if (desiredName.trimmed().isEmpty()) {
        desiredName = QFileInfo(sourcePath).completeBaseName();
    }
    rec.name = uniqueName(desiredName, all);
    if (rec.name.isEmpty()) {
        rec.name = uniqueName(PhosphorI18n::tr("Imported profile"), all);
    }
    if (rec.name.isEmpty() || !writeProfileRecord(rec)) {
        return QString();
    }
    appendToOrder(rec.id);
    notifyProfilesChanged();
    return rec.id.toString();
}

void ProfileStore::openProfilesDirectory()
{
    const QString dir = profilesDirectory();
    if (dir.isEmpty() || !QDir().mkpath(dir)) {
        qCWarning(lcConfig) << "ProfileStore::openProfilesDirectory: cannot create" << dir;
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not create the profiles folder."));
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(dir))) {
        qCWarning(lcConfig) << "ProfileStore::openProfilesDirectory: no handler for" << dir;
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not open the profiles folder."));
    }
}

} // namespace PlasmaZones
