// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "animationspagecontroller.h"

#include "../core/isettings.h"
#include "dbusutils.h"

#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimationShaders/AnimationShaderEffect.h>
#include <PhosphorAnimationShaders/AnimationShaderRegistry.h>
#include <PhosphorAnimationShaders/ShaderProfile.h>
#include <PhosphorAnimationShaders/ShaderProfileTree.h>

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

namespace PlasmaZones {

namespace {

// ProfileLoader's envelope helper reads the top-level `name` field to
// assign the registry path (and strips it from the returned root). We
// add it on write so the file is recognised.
QString jsonNameKey()
{
    return QStringLiteral("name");
}

/// Title-case a single camelCase segment: "snapIn" → "Snap In", "show" →
/// "Show", "popIn" → "Pop In". Splits on lower→upper transitions; trivial
/// for single-word segments.
QString humanizeSegment(const QString& segment)
{
    if (segment.isEmpty())
        return segment;
    QString out;
    out.reserve(segment.size() + 4);
    out.append(segment.front().toUpper());
    for (int i = 1; i < segment.size(); ++i) {
        const QChar prev = segment.at(i - 1);
        const QChar cur = segment.at(i);
        if (cur.isUpper() && prev.isLower()) {
            out.append(QLatin1Char(' '));
        }
        out.append(cur);
    }
    return out;
}

/// Convert a `Profile` value to its `toJson()` shape as a QVariantMap.
/// Sparse — only engaged fields appear, matching the wire format.
QVariantMap profileToVariantMap(const PhosphorAnimation::Profile& profile)
{
    return profile.toJson().toVariantMap();
}

/// Read the JSON object at @p path. Returns an empty object on missing
/// file / parse error / non-object root. The `name` field is stripped so
/// the returned map matches the QML-facing Profile shape.
QJsonObject readProfileJson(const QString& path)
{
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return {};
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return {};
    QJsonObject obj = doc.object();
    obj.remove(jsonNameKey());
    return obj;
}

/// Merge fields from @p source into @p target without overwriting keys
/// already present in @p target. Implements ProfileTree-style "deeper
/// path wins" inheritance when called from leaf to root.
void mergeMissingFields(QVariantMap& target, const QVariantMap& source)
{
    for (auto it = source.cbegin(); it != source.cend(); ++it) {
        if (!target.contains(it.key())) {
            target.insert(it.key(), it.value());
        }
    }
}

/// Filesystem-safe slug for a user-supplied preset name. Lowercase
/// alphanumerics keep their identity; everything else collapses to a
/// hyphen. Preserves dots so display-style identifiers ("my.curve")
/// round-trip naturally; rejects empty results so an all-symbol name
/// fails the write rather than silently writing to ".json".
QString slugifyPresetName(const QString& name)
{
    QString out;
    out.reserve(name.size());
    bool lastWasDash = false;
    for (QChar c : name) {
        const QChar lower = c.toLower();
        if (lower.isLetterOrNumber() || lower == QLatin1Char('.')) {
            out.append(lower);
            lastWasDash = false;
        } else if (!lastWasDash) {
            out.append(QLatin1Char('-'));
            lastWasDash = true;
        }
    }
    while (out.startsWith(QLatin1Char('-')))
        out.remove(0, 1);
    while (out.endsWith(QLatin1Char('-')))
        out.chop(1);
    return out;
}

/// Fill any unset fields in @p profile with the `Profile::Default*`
/// library constants so the QML side always reads a populated map.
void fillLibraryDefaults(QVariantMap& profile)
{
    using P = PhosphorAnimation::Profile;
    if (!profile.contains(QLatin1String(P::JsonFieldDuration))) {
        profile.insert(QLatin1String(P::JsonFieldDuration), P::DefaultDuration);
    }
    if (!profile.contains(QLatin1String(P::JsonFieldMinDistance))) {
        profile.insert(QLatin1String(P::JsonFieldMinDistance), P::DefaultMinDistance);
    }
    if (!profile.contains(QLatin1String(P::JsonFieldSequenceMode))) {
        profile.insert(QLatin1String(P::JsonFieldSequenceMode), int(P::DefaultSequenceMode));
    }
    if (!profile.contains(QLatin1String(P::JsonFieldStaggerInterval))) {
        profile.insert(QLatin1String(P::JsonFieldStaggerInterval), P::DefaultStaggerInterval);
    }
    // `curve` intentionally left unset when missing — QML treats absence
    // as "use library-default cubic-bezier" rather than fabricating a
    // string here that would round-trip unequal.
}

} // namespace

// ─── Construction ──────────────────────────────────────────────────────

AnimationsPageController::AnimationsPageController(PhosphorAnimationShaders::AnimationShaderRegistry* shaderRegistry,
                                                   ISettings* settings, QObject* parent)
    : QObject(parent)
    , m_shaderRegistry(shaderRegistry)
    , m_settings(settings)
{
    if (m_shaderRegistry) {
        connect(m_shaderRegistry, &PhosphorAnimationShaders::AnimationShaderRegistry::effectsChanged, this,
                &AnimationsPageController::shaderEffectsChanged);
    }
    if (m_settings) {
        connect(m_settings, &ISettings::shaderProfileTreeChanged, this, [this]() {
            // Path-agnostic broadcast — the tree is a single Q_PROPERTY so we
            // can't tell which path moved without diffing. QML pages refresh
            // every visible event card on this signal which is cheap enough.
            Q_EMIT shaderProfileChanged(QString());
        });
    }
}

void AnimationsPageController::setUserProfilesDirOverride(const QString& dir)
{
    m_userProfilesDirOverride = dir;
}

// ─── Pending-changes snapshot machinery ────────────────────────────────

void AnimationsPageController::snapshotFileIfFirst(const QString& filePath)
{
    if (filePath.isEmpty() || m_pendingFileSnapshots.contains(filePath))
        return;
    QFile f(filePath);
    if (!f.exists()) {
        m_pendingFileSnapshots.insert(filePath, std::nullopt);
        return;
    }
    if (!f.open(QIODevice::ReadOnly))
        return;
    m_pendingFileSnapshots.insert(filePath, f.readAll());
}

bool AnimationsPageController::hasPendingChanges() const
{
    return !m_pendingFileSnapshots.isEmpty();
}

void AnimationsPageController::commitPending()
{
    const bool had = hasPendingChanges();
    m_pendingFileSnapshots.clear();
    if (had)
        Q_EMIT pendingChangesChanged();
}

void AnimationsPageController::revertPending()
{
    using namespace PhosphorAnimation;
    using namespace PhosphorAnimationShaders;

    if (!hasPendingChanges())
        return;

    // Categorize affected paths so the right "data changed" signals
    // fire at the end. Filenames in profilesDir map to either an event
    // override (path matches ProfilePaths::) or a user preset
    // (everything else); files in motionSetsDir map to motion sets.
    const QString profilesDir = userProfilesDir();
    const QString setsDir = userMotionSetsDir();
    const QStringList knownPaths = ProfilePaths::allBuiltInPaths();
    const QSet<QString> knownPathSet(knownPaths.cbegin(), knownPaths.cend());

    QStringList overrideEvents;
    bool anyPreset = false;
    bool anyMotionSet = false;

    for (auto it = m_pendingFileSnapshots.cbegin(); it != m_pendingFileSnapshots.cend(); ++it) {
        const QString& filePath = it.key();
        const auto& content = it.value();

        // Restore: write back content, or delete if didn't exist.
        if (!content.has_value()) {
            QFile::remove(filePath);
        } else {
            QSaveFile f(filePath);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                f.write(*content);
                f.commit();
            }
        }

        // Classify so the right signal goes out.
        const QFileInfo info(filePath);
        const QString absDir = info.absolutePath();
        const QString stem = info.completeBaseName();
        if (absDir == setsDir) {
            anyMotionSet = true;
        } else if (absDir == profilesDir) {
            if (knownPathSet.contains(stem))
                overrideEvents.append(stem);
            else
                anyPreset = true;
        }
    }
    m_pendingFileSnapshots.clear();

    // Shader tree no longer needs custom revert plumbing — it's now a
    // standard Q_PROPERTY (Settings::shaderProfileTreeJson) and
    // SettingsController::load() reads it back automatically via
    // Settings::load() → reparseConfiguration → meta-object NOTIFY loop,
    // identical to every other page.

    // Bulk emit so QML sub-pages refresh exactly the rows that moved.
    for (const QString& path : overrideEvents)
        Q_EMIT overrideChanged(path);
    if (anyPreset)
        Q_EMIT userPresetsChanged();
    if (anyMotionSet)
        Q_EMIT motionSetsChanged();
    Q_EMIT pendingChangesChanged();
}

// ─── Path discovery ────────────────────────────────────────────────────

QString AnimationsPageController::sectionForPath(const QString& path) const
{
    if (path.isEmpty())
        return {};
    const int dot = path.indexOf(QLatin1Char('.'));
    return dot < 0 ? path : path.left(dot);
}

QString AnimationsPageController::eventLabel(const QString& path) const
{
    if (path.isEmpty())
        return {};
    const int dot = path.lastIndexOf(QLatin1Char('.'));
    const QString segment = dot < 0 ? path : path.mid(dot + 1);
    return humanizeSegment(segment);
}

QString AnimationsPageController::parentPath(const QString& path) const
{
    return PhosphorAnimation::ProfilePaths::parentPath(path);
}

QStringList AnimationsPageController::parentChain(const QString& path) const
{
    QStringList chain;
    QString cur = path;
    while (!cur.isEmpty()) {
        chain.append(cur);
        cur = PhosphorAnimation::ProfilePaths::parentPath(cur);
    }
    return chain;
}

QVariantList AnimationsPageController::eventSections() const
{
    using namespace PhosphorAnimation;
    const QStringList paths = ProfilePaths::allBuiltInPaths();

    // Track section insertion order via a parallel list; QHash would lose
    // taxonomy ordering and the QML drilldown should mirror header order.
    QStringList sectionOrder;
    QHash<QString, QVariantList> sectionPaths;

    for (const QString& path : paths) {
        const QString section = sectionForPath(path);
        if (!sectionPaths.contains(section)) {
            sectionOrder.append(section);
        }
        QVariantMap entry;
        entry.insert(QStringLiteral("path"), path);
        entry.insert(QStringLiteral("label"), eventLabel(path));
        entry.insert(QStringLiteral("parent"), ProfilePaths::parentPath(path));
        // A "category" path is one whose label sits at a section/sub-
        // section root (e.g. "window", "panel.popup") rather than a leaf
        // event. Detect by checking whether any other built-in path uses
        // it as a parent prefix.
        const QString prefix = path + QLatin1Char('.');
        const bool isCategory = std::any_of(paths.cbegin(), paths.cend(), [&](const QString& other) {
            return other.startsWith(prefix);
        });
        entry.insert(QStringLiteral("isCategory"), isCategory);
        sectionPaths[section].append(entry);
    }

    QVariantList result;
    result.reserve(sectionOrder.size());
    for (const QString& section : sectionOrder) {
        QVariantMap sectionEntry;
        sectionEntry.insert(QStringLiteral("section"), section);
        sectionEntry.insert(QStringLiteral("label"), humanizeSegment(section));
        sectionEntry.insert(QStringLiteral("paths"), sectionPaths.value(section));
        result.append(sectionEntry);
    }
    return result;
}

// ─── Override CRUD ─────────────────────────────────────────────────────

QString AnimationsPageController::userProfilesDir() const
{
    if (!m_userProfilesDirOverride.isEmpty())
        return m_userProfilesDirOverride;
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return base + QStringLiteral("/plasmazones/profiles");
}

QString AnimationsPageController::profileFilePath(const QString& path) const
{
    // Filenames mirror the path (e.g. `zone.snapIn.json`) — same
    // convention as the daemon's shipped defaults under
    // `${KDE_INSTALL_DATADIR}/plasmazones/profiles/`. ProfileLoader's
    // envelope check requires `name` field to match filename stem.
    return userProfilesDir() + QLatin1Char('/') + path + QStringLiteral(".json");
}

QString AnimationsPageController::presetFilePath(const QString& presetName) const
{
    const QString slug = slugifyPresetName(presetName);
    if (slug.isEmpty())
        return {};
    return userProfilesDir() + QLatin1Char('/') + slug + QStringLiteral(".json");
}

QString AnimationsPageController::userMotionSetsDir() const
{
    if (!m_userProfilesDirOverride.isEmpty())
        return m_userProfilesDirOverride + QStringLiteral("/motionsets");
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return base + QStringLiteral("/plasmazones/motionsets");
}

QString AnimationsPageController::motionSetFilePath(const QString& setName) const
{
    const QString slug = slugifyPresetName(setName);
    if (slug.isEmpty())
        return {};
    return userMotionSetsDir() + QLatin1Char('/') + slug + QStringLiteral(".json");
}

bool AnimationsPageController::hasOverride(const QString& path) const
{
    if (path.isEmpty())
        return false;
    return QFileInfo::exists(profileFilePath(path));
}

QVariantMap AnimationsPageController::rawProfile(const QString& path) const
{
    if (path.isEmpty())
        return {};
    return readProfileJson(profileFilePath(path)).toVariantMap();
}

QVariantMap AnimationsPageController::resolvedProfile(const QString& path) const
{
    using namespace PhosphorAnimation;
    if (path.isEmpty())
        return {};

    QVariantMap merged;
    PhosphorProfileRegistry* registry = PhosphorProfileRegistry::defaultRegistry();

    QString cur = path;
    while (!cur.isEmpty()) {
        QVariantMap source;
        if (registry) {
            const auto entry = registry->resolve(cur);
            if (entry.has_value()) {
                source = profileToVariantMap(*entry);
            }
        }
        if (source.isEmpty()) {
            // Registry not published, or no entry at this path. Fall
            // back to a direct user-dir read so unit tests (which never
            // bootstrap a registry) still get walk-up resolution over
            // their own override files.
            source = readProfileJson(profileFilePath(cur)).toVariantMap();
        }
        mergeMissingFields(merged, source);
        cur = ProfilePaths::parentPath(cur);
    }

    fillLibraryDefaults(merged);
    return merged;
}

bool AnimationsPageController::setOverride(const QString& path, const QVariantMap& profileJson)
{
    if (path.isEmpty())
        return false;

    const QString dir = userProfilesDir();
    if (!QDir().mkpath(dir))
        return false;

    const QString filePath = profileFilePath(path);
    snapshotFileIfFirst(filePath);

    QJsonObject obj = QJsonObject::fromVariantMap(profileJson);
    // The `name` field is what ProfileLoader's envelope helper reads to
    // assign the registry path (per ProfileLoader.h schema docs). Always
    // overwrite — the QML map shouldn't carry a stale name.
    obj.insert(jsonNameKey(), path);

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    if (!file.commit())
        return false;

    Q_EMIT overrideChanged(path);
    Q_EMIT pendingChangesChanged();
    return true;
}

bool AnimationsPageController::clearOverride(const QString& path)
{
    if (path.isEmpty())
        return false;
    const QString filePath = profileFilePath(path);
    QFile file(filePath);
    if (!file.exists())
        return false;
    snapshotFileIfFirst(filePath);
    if (!file.remove())
        return false;
    Q_EMIT overrideChanged(path);
    Q_EMIT pendingChangesChanged();
    return true;
}

// ─── Preset library ────────────────────────────────────────────────────

QVariantList AnimationsPageController::userPresets() const
{
    using namespace PhosphorAnimation;

    QVariantList result;
    QDir dir(userProfilesDir());
    if (!dir.exists())
        return result;

    // Convert to QSet for O(1) membership checks; allBuiltInPaths() is
    // already filtered for reserved entries but we double-check
    // isReservedPath() so a future schema bump that loosens the
    // taxonomy doesn't quietly start surfacing reserved-path files as
    // "presets."
    const QStringList knownPaths = ProfilePaths::allBuiltInPaths();
    const QSet<QString> knownPathSet(knownPaths.cbegin(), knownPaths.cend());

    const auto entries = dir.entryInfoList(QStringList{QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QFileInfo& info : entries) {
        const QJsonObject obj = readProfileJson(info.absoluteFilePath());
        if (obj.isEmpty())
            continue;

        // readProfileJson strips the `name` field; re-read it here
        // because we need it both for the filter and for the QML row
        // identity.
        QFile raw(info.absoluteFilePath());
        if (!raw.open(QIODevice::ReadOnly))
            continue;
        const auto rawDoc = QJsonDocument::fromJson(raw.readAll());
        if (!rawDoc.isObject())
            continue;
        const QString name = rawDoc.object().value(jsonNameKey()).toString();
        if (name.isEmpty() || knownPathSet.contains(name) || ProfilePaths::isReservedPath(name))
            continue;

        QVariantMap entry = obj.toVariantMap();
        entry.insert(QStringLiteral("name"), name);
        result.append(entry);
    }
    return result;
}

bool AnimationsPageController::addUserPreset(const QString& name, const QVariantMap& profileJson)
{
    using namespace PhosphorAnimation;

    if (name.isEmpty())
        return false;

    // Reject names that match a built-in event path — the file would
    // collide with an override slot and the daemon would treat the
    // preset as a path-bound profile.
    if (ProfilePaths::allBuiltInPaths().contains(name) || ProfilePaths::isReservedPath(name))
        return false;

    const QString filePath = presetFilePath(name);
    if (filePath.isEmpty())
        return false;

    const QString dir = userProfilesDir();
    if (!QDir().mkpath(dir))
        return false;

    snapshotFileIfFirst(filePath);

    QJsonObject obj = QJsonObject::fromVariantMap(profileJson);
    obj.insert(jsonNameKey(), name);

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    if (!file.commit())
        return false;

    Q_EMIT userPresetsChanged();
    Q_EMIT pendingChangesChanged();
    return true;
}

bool AnimationsPageController::removeUserPreset(const QString& name)
{
    if (name.isEmpty())
        return false;

    // Try the slug-derived path first; fall back to a directory scan
    // for the file whose `name` field matches. The fallback covers the
    // edge case where the slug rule changes between writes (e.g. the
    // user upgraded across a slugify revision and an old file's name
    // no longer round-trips through the new slug).
    QString filePath = presetFilePath(name);
    if (!filePath.isEmpty() && !QFileInfo::exists(filePath))
        filePath.clear();

    if (filePath.isEmpty()) {
        QDir dir(userProfilesDir());
        const auto entries = dir.entryInfoList(QStringList{QStringLiteral("*.json")}, QDir::Files);
        for (const QFileInfo& info : entries) {
            QFile raw(info.absoluteFilePath());
            if (!raw.open(QIODevice::ReadOnly))
                continue;
            const auto doc = QJsonDocument::fromJson(raw.readAll());
            if (!doc.isObject())
                continue;
            if (doc.object().value(jsonNameKey()).toString() == name) {
                filePath = info.absoluteFilePath();
                break;
            }
        }
    }

    if (filePath.isEmpty())
        return false;

    QFile file(filePath);
    if (!file.exists())
        return false;
    snapshotFileIfFirst(filePath);
    if (!file.remove())
        return false;

    Q_EMIT userPresetsChanged();
    Q_EMIT pendingChangesChanged();
    return true;
}

// ─── Shader effects ────────────────────────────────────────────────────

namespace {

QVariantMap parameterInfoToMap(const PhosphorAnimationShaders::AnimationShaderEffect::ParameterInfo& p)
{
    QVariantMap m;
    m.insert(QStringLiteral("id"), p.id);
    m.insert(QStringLiteral("name"), p.name);
    m.insert(QStringLiteral("type"), p.type);
    m.insert(QStringLiteral("defaultValue"), p.defaultValue);
    m.insert(QStringLiteral("minValue"), p.minValue);
    m.insert(QStringLiteral("maxValue"), p.maxValue);
    return m;
}

QVariantMap effectToMap(const PhosphorAnimationShaders::AnimationShaderEffect& effect)
{
    QVariantMap m;
    m.insert(QStringLiteral("id"), effect.id);
    m.insert(QStringLiteral("name"), effect.name);
    m.insert(QStringLiteral("description"), effect.description);
    m.insert(QStringLiteral("author"), effect.author);
    m.insert(QStringLiteral("version"), effect.version);
    m.insert(QStringLiteral("category"), effect.category);
    m.insert(QStringLiteral("isUserEffect"), effect.isUserEffect);
    QVariantList params;
    params.reserve(effect.parameters.size());
    for (const auto& p : effect.parameters) {
        params.append(parameterInfoToMap(p));
    }
    m.insert(QStringLiteral("parameters"), params);
    return m;
}

QVariantMap shaderProfileToMap(const PhosphorAnimationShaders::ShaderProfile& profile)
{
    QVariantMap m;
    if (profile.effectId)
        m.insert(QStringLiteral("effectId"), *profile.effectId);
    if (profile.parameters)
        m.insert(QStringLiteral("parameters"), *profile.parameters);
    return m;
}

} // namespace

QVariantList AnimationsPageController::availableShaderEffects() const
{
    QVariantList result;
    if (!m_shaderRegistry)
        return result;
    const auto effects = m_shaderRegistry->availableEffects();
    result.reserve(effects.size());
    for (const auto& effect : effects)
        result.append(effectToMap(effect));
    return result;
}

QVariantMap AnimationsPageController::shaderEffectInfo(const QString& effectId) const
{
    if (!m_shaderRegistry || effectId.isEmpty() || !m_shaderRegistry->hasEffect(effectId))
        return {};
    return effectToMap(m_shaderRegistry->effect(effectId));
}

QVariantList AnimationsPageController::shaderParameters(const QString& effectId) const
{
    if (!m_shaderRegistry || effectId.isEmpty() || !m_shaderRegistry->hasEffect(effectId))
        return {};
    const auto effect = m_shaderRegistry->effect(effectId);
    QVariantList result;
    result.reserve(effect.parameters.size());
    for (const auto& p : effect.parameters)
        result.append(parameterInfoToMap(p));
    return result;
}

QString AnimationsPageController::userShaderDirectory() const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString dir = base + QStringLiteral("/plasmazones/animations");
    QDir().mkpath(dir);
    return dir;
}

void AnimationsPageController::openUserShaderDirectory() const
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(userShaderDirectory()));
}

QVariantMap AnimationsPageController::rawShaderProfile(const QString& path) const
{
    using namespace PhosphorAnimationShaders;
    if (!m_settings || path.isEmpty())
        return {};
    const ShaderProfileTree tree = m_settings->shaderProfileTree();
    if (!tree.hasOverride(path))
        return {};
    return shaderProfileToMap(tree.directOverride(path));
}

QVariantMap AnimationsPageController::resolvedShaderProfile(const QString& path) const
{
    using namespace PhosphorAnimationShaders;
    if (!m_settings || path.isEmpty())
        return {};
    const ShaderProfileTree tree = m_settings->shaderProfileTree();
    return shaderProfileToMap(tree.resolve(path));
}

bool AnimationsPageController::setShaderOverride(const QString& path, const QString& effectId,
                                                 const QVariantMap& parameters)
{
    using namespace PhosphorAnimationShaders;
    if (!m_settings || path.isEmpty())
        return false;

    // Empty effectId == clear assignment at this exact path. Mirrors
    // ShaderProfile's "engaged-empty means no effect" semantics in a
    // clean QML idiom — callers don't have to construct a partial map
    // to clear.
    if (effectId.isEmpty())
        return clearShaderOverride(path);

    // Standard pattern (matches every other settings page): write
    // through Settings::setShaderProfileTree. The shaderProfileTreeJson
    // Q_PROPERTY emits NOTIFY, the SettingsController meta-object loop
    // catches it and calls setNeedsSave automatically. Daemon sync
    // happens at Save time via SettingsController::save() →
    // DaemonDBus::notifyReload — no per-edit notify here, no snapshot,
    // no custom dirty plumbing.
    ShaderProfile profile;
    profile.effectId = effectId;
    if (!parameters.isEmpty())
        profile.parameters = parameters;

    ShaderProfileTree tree = m_settings->shaderProfileTree();
    tree.setOverride(path, profile);
    m_settings->setShaderProfileTree(tree);

    Q_EMIT shaderProfileChanged(path);
    return true;
}

bool AnimationsPageController::clearShaderOverride(const QString& path)
{
    using namespace PhosphorAnimationShaders;
    if (!m_settings || path.isEmpty())
        return false;
    ShaderProfileTree tree = m_settings->shaderProfileTree();
    if (!tree.hasOverride(path))
        return false;
    tree.clearOverride(path);
    m_settings->setShaderProfileTree(tree);
    Q_EMIT shaderProfileChanged(path);
    return true;
}

// ─── Motion sets ───────────────────────────────────────────────────────

QVariantList AnimationsPageController::availableMotionSets() const
{
    QVariantList result;
    QDir dir(userMotionSetsDir());
    if (!dir.exists())
        return result;

    const auto files = dir.entryInfoList(QStringList{QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QFileInfo& info : files) {
        QFile f(info.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const auto doc = QJsonDocument::fromJson(f.readAll());
        if (!doc.isObject())
            continue;
        const QJsonObject obj = doc.object();

        QVariantMap row;
        row.insert(QStringLiteral("name"), obj.value(QStringLiteral("name")).toString());
        row.insert(QStringLiteral("description"), obj.value(QStringLiteral("description")).toString());
        row.insert(QStringLiteral("slug"), info.completeBaseName());
        row.insert(QStringLiteral("overrideCount"), obj.value(QStringLiteral("overrides")).toArray().size());
        result.append(row);
    }
    return result;
}

bool AnimationsPageController::applyMotionSet(const QString& name)
{
    using namespace PhosphorAnimation;
    if (name.isEmpty())
        return false;

    const QString filePath = motionSetFilePath(name);
    if (filePath.isEmpty())
        return false;

    QFile f(filePath);
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return false;
    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return false;

    const QJsonArray overrides = doc.object().value(QStringLiteral("overrides")).toArray();
    const QStringList knownPaths = ProfilePaths::allBuiltInPaths();
    const QSet<QString> knownPathSet(knownPaths.cbegin(), knownPaths.cend());

    bool wroteAny = false;
    for (const QJsonValue& v : overrides) {
        const QJsonObject entry = v.toObject();
        const QString path = entry.value(QStringLiteral("path")).toString();
        // Only honor known event paths — a hand-edited set with a
        // path-typo shouldn't write a stray non-path override file.
        if (path.isEmpty() || !knownPathSet.contains(path))
            continue;
        const QJsonObject profile = entry.value(QStringLiteral("profile")).toObject();
        if (setOverride(path, profile.toVariantMap()))
            wroteAny = true;
    }
    if (wroteAny)
        Q_EMIT motionSetsChanged();
    return wroteAny;
}

bool AnimationsPageController::saveCurrentAsMotionSet(const QString& name, const QString& description)
{
    using namespace PhosphorAnimation;
    if (name.isEmpty())
        return false;

    const QString filePath = motionSetFilePath(name);
    if (filePath.isEmpty())
        return false;

    if (!QDir().mkpath(userMotionSetsDir()))
        return false;

    // Walk the profiles dir, pick out files whose `name` matches a
    // known event path. Build a sparse `overrides` array; non-path
    // names (presets) are skipped.
    QJsonArray overrides;
    QDir profilesDir(userProfilesDir());
    if (profilesDir.exists()) {
        const QStringList knownPaths = ProfilePaths::allBuiltInPaths();
        const QSet<QString> knownPathSet(knownPaths.cbegin(), knownPaths.cend());
        const auto files = profilesDir.entryInfoList(QStringList{QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        for (const QFileInfo& info : files) {
            QFile f(info.absoluteFilePath());
            if (!f.open(QIODevice::ReadOnly))
                continue;
            const auto doc = QJsonDocument::fromJson(f.readAll());
            if (!doc.isObject())
                continue;
            const QJsonObject obj = doc.object();
            const QString entryName = obj.value(jsonNameKey()).toString();
            if (!knownPathSet.contains(entryName))
                continue;

            QJsonObject profile = obj;
            profile.remove(jsonNameKey());
            QJsonObject entry;
            entry.insert(QStringLiteral("path"), entryName);
            entry.insert(QStringLiteral("profile"), profile);
            overrides.append(entry);
        }
    }

    snapshotFileIfFirst(filePath);

    QJsonObject root;
    root.insert(QStringLiteral("name"), name);
    if (!description.isEmpty())
        root.insert(QStringLiteral("description"), description);
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("overrides"), overrides);

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit())
        return false;

    Q_EMIT motionSetsChanged();
    Q_EMIT pendingChangesChanged();
    return true;
}

bool AnimationsPageController::removeMotionSet(const QString& name)
{
    if (name.isEmpty())
        return false;
    const QString filePath = motionSetFilePath(name);
    if (filePath.isEmpty())
        return false;
    QFile file(filePath);
    if (!file.exists())
        return false;
    snapshotFileIfFirst(filePath);
    if (!file.remove())
        return false;
    Q_EMIT motionSetsChanged();
    Q_EMIT pendingChangesChanged();
    return true;
}

} // namespace PlasmaZones
