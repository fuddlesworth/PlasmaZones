// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "animationspagecontroller.h"

#include "../config/configdefaults.h"
#include "../core/isettings.h"
#include "../core/logging.h"
#include "animationpresetlibrary.h"
#include "dbusutils.h"
#include "motionsetstore.h"

#include <PhosphorAnimation/Easing.h>
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
#include <QLoggingCategory>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

namespace PlasmaZones {

namespace animations_controller_detail {

// ProfileLoader's envelope helper reads the top-level `name` field to
// assign the registry path (and strips it from the returned root). We
// add it on write so the file is recognised. JSON keys are
// QLatin1String per the project's Qt6 string-literal rule. `static`
// (internal linkage) keeps unity-build merging safe even though sibling
// TUs declare the same symbol name in their own detail namespaces.
static constexpr QLatin1String JsonNameKey{"name"};

/// Title-case a single camelCase segment: "snapIn" → "Snap In", "show" →
/// "Show", "popIn" → "Pop In". Splits on lower→upper transitions; trivial
/// for single-word segments.
static QString humanizeSegment(const QString& segment)
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
static QVariantMap profileToVariantMap(const PhosphorAnimation::Profile& profile)
{
    return profile.toJson().toVariantMap();
}

/// Read the JSON object at @p path. Returns an empty object on missing
/// file / parse error / non-object root. The `name` field is stripped so
/// the returned map matches the QML-facing Profile shape. Parse errors
/// are logged so silent corruption surfaces in journalctl.
static QJsonObject readProfileJson(const QString& path)
{
    QFile file(path);
    if (!file.exists())
        return {};
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcConfig) << "AnimationsPageController: cannot open profile" << path;
        return {};
    }
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcConfig) << "AnimationsPageController: failed to parse" << path << ":" << err.errorString();
        return {};
    }
    QJsonObject obj = doc.object();
    obj.remove(JsonNameKey);
    return obj;
}

/// Merge fields from @p source into @p target without overwriting keys
/// already present in @p target. Implements ProfileTree-style "deeper
/// path wins" inheritance when called from leaf to root.
static void mergeMissingFields(QVariantMap& target, const QVariantMap& source)
{
    for (auto it = source.cbegin(); it != source.cend(); ++it) {
        if (!target.contains(it.key())) {
            target.insert(it.key(), it.value());
        }
    }
}

/// Fill any unset fields in @p profile with the `Profile::Default*`
/// library constants so the QML side always reads a populated map.
static void fillLibraryDefaults(QVariantMap& profile)
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
    // `curve` left unset → fill with the canonical library default
    // (default-constructed `Easing` is OutCubic, matching
    // `Profile::withDefaults()` and `AnimatedValue::defaultFallbackCurve()`).
    // Without this, QML cards crashed with "Cannot read property of
    // undefined" when no parent supplied a curve.
    if (!profile.contains(QLatin1String(P::JsonFieldCurve))) {
        profile.insert(QLatin1String(P::JsonFieldCurve), PhosphorAnimation::Easing().toString());
    }
}

} // namespace animations_controller_detail

using namespace animations_controller_detail;

// ─── Construction ──────────────────────────────────────────────────────

AnimationsPageController::AnimationsPageController(PhosphorAnimationShaders::AnimationShaderRegistry* shaderRegistry,
                                                   ISettings* settings, QObject* parent)
    : QObject(parent)
    , m_shaderRegistry(shaderRegistry)
    , m_settings(settings)
{
    // Forward the snapshot helper as a callable so the sub-services can
    // capture pre-edit content without coupling to the controller's
    // m_pendingFileSnapshots layout.
    auto snapshotFn = [this](const QString& filePath) {
        snapshotFileIfFirst(filePath);
    };
    auto profilesDirFn = [this]() {
        return userProfilesDir();
    };
    auto motionSetsDirFn = [this]() {
        return userMotionSetsDir();
    };
    auto writeOverrideFn = [this](const QString& path, const QVariantMap& profile) {
        return setOverride(path, profile);
    };

    m_presets = new AnimationPresetLibrary(profilesDirFn, snapshotFn, this);
    m_motionSets = new MotionSetStore(profilesDirFn, motionSetsDirFn, writeOverrideFn, snapshotFn, this);

    connect(m_presets, &AnimationPresetLibrary::userPresetsChanged, this,
            &AnimationsPageController::userPresetsChanged);
    connect(m_presets, &AnimationPresetLibrary::pendingChangesChanged, this,
            &AnimationsPageController::pendingChangesChanged);
    connect(m_motionSets, &MotionSetStore::motionSetsChanged, this, &AnimationsPageController::motionSetsChanged);
    connect(m_motionSets, &MotionSetStore::pendingChangesChanged, this,
            &AnimationsPageController::pendingChangesChanged);
    // applyMotionSet's overrideChanged emissions ride through the
    // controller's existing setOverride callback path (each setOverride
    // already emits overrideChanged), so we don't connect MotionSetStore's
    // overrideChanged to avoid double-firing.

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

AnimationsPageController::~AnimationsPageController() = default;

void AnimationsPageController::setUserProfilesDirOverride(const QString& dir)
{
    m_userProfilesDirOverride = dir;
}

bool AnimationsPageController::isValidEventPath(const QString& path) const
{
    if (path.isEmpty())
        return false;
    // Defensive prefilter — `allBuiltInPaths()` doesn't contain any of
    // these characters so membership alone would be enough, but the
    // explicit check keeps the security intent visible at the call site.
    if (path.contains(QLatin1Char('/')) || path.contains(QLatin1Char('\\')) || path.contains(QLatin1String("..")))
        return false;
    static const QSet<QString> kKnownPathSet = []() {
        const QStringList paths = PhosphorAnimation::ProfilePaths::allBuiltInPaths();
        return QSet<QString>(paths.cbegin(), paths.cend());
    }();
    return kKnownPathSet.contains(path);
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

    const QString profilesDir = userProfilesDir();
    const QString setsDir = userMotionSetsDir();
    const QStringList knownPaths = ProfilePaths::allBuiltInPaths();
    const QSet<QString> knownPathSet(knownPaths.cbegin(), knownPaths.cend());

    QStringList overrideEvents;
    bool anyPreset = false;
    bool anyMotionSet = false;

    // Failed restores are kept in the snapshot map so a follow-up revert
    // can retry. Successful restores are removed below.
    QHash<QString, std::optional<QByteArray>> retained;

    for (auto it = m_pendingFileSnapshots.cbegin(); it != m_pendingFileSnapshots.cend(); ++it) {
        const QString& filePath = it.key();
        const auto& content = it.value();

        bool restored = false;
        if (!content.has_value()) {
            // File didn't exist before this session — remove if present.
            if (!QFile::exists(filePath) || QFile::remove(filePath))
                restored = true;
        } else {
            QSaveFile f(filePath);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                f.write(*content);
                if (f.commit())
                    restored = true;
            }
        }

        if (!restored) {
            qCWarning(lcConfig) << "AnimationsPageController::revertPending: failed to restore" << filePath;
            retained.insert(filePath, content);
            continue;
        }

        // Classify so the right signal goes out for the restored file.
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
    m_pendingFileSnapshots = std::move(retained);

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
    return base + ConfigDefaults::userProfilesSubdir();
}

QString AnimationsPageController::profileFilePath(const QString& path) const
{
    // Filenames mirror the path (e.g. `zone.snapIn.json`) — same
    // convention as the daemon's shipped defaults. Validation on @p
    // path happens at every call site so this helper trusts its input.
    return userProfilesDir() + QLatin1Char('/') + path + QStringLiteral(".json");
}

QString AnimationsPageController::userMotionSetsDir() const
{
    if (!m_userProfilesDirOverride.isEmpty())
        return m_userProfilesDirOverride + QStringLiteral("/motionsets");
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return base + ConfigDefaults::userMotionSetsSubdir();
}

bool AnimationsPageController::hasOverride(const QString& path) const
{
    if (!isValidEventPath(path))
        return false;
    return QFileInfo::exists(profileFilePath(path));
}

QVariantMap AnimationsPageController::rawProfile(const QString& path) const
{
    if (!isValidEventPath(path))
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
        if (source.isEmpty() && isValidEventPath(cur)) {
            // Registry not published, or no entry at this path. Fall
            // back to a direct user-dir read so unit tests (which never
            // bootstrap a registry) still get walk-up resolution over
            // their own override files. Skip the read for non-event
            // paths so a crafted parent like "../" can't cause a stray
            // file open.
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
    if (!isValidEventPath(path))
        return false;

    const QString dir = userProfilesDir();
    if (!QDir().mkpath(dir))
        return false;

    const QString filePath = profileFilePath(path);
    snapshotFileIfFirst(filePath);

    QJsonObject obj = QJsonObject::fromVariantMap(profileJson);
    // The `name` field is what ProfileLoader's envelope helper reads to
    // assign the registry path. Always overwrite — the QML map shouldn't
    // carry a stale name.
    obj.insert(JsonNameKey, path);

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
    if (!isValidEventPath(path))
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

// ─── Preset library — delegated ────────────────────────────────────────

QVariantList AnimationsPageController::userPresets() const
{
    return m_presets ? m_presets->userPresets() : QVariantList{};
}

bool AnimationsPageController::addUserPreset(const QString& name, const QVariantMap& profileJson)
{
    return m_presets && m_presets->addUserPreset(name, profileJson);
}

bool AnimationsPageController::removeUserPreset(const QString& name)
{
    return m_presets && m_presets->removeUserPreset(name);
}

// ─── Motion sets — delegated ───────────────────────────────────────────

QVariantList AnimationsPageController::availableMotionSets() const
{
    return m_motionSets ? m_motionSets->availableMotionSets() : QVariantList{};
}

bool AnimationsPageController::applyMotionSet(const QString& name)
{
    return m_motionSets && m_motionSets->applyMotionSet(name);
}

bool AnimationsPageController::saveCurrentAsMotionSet(const QString& name, const QString& description)
{
    return m_motionSets && m_motionSets->saveCurrentAsMotionSet(name, description);
}

bool AnimationsPageController::removeMotionSet(const QString& name)
{
    return m_motionSets && m_motionSets->removeMotionSet(name);
}

// ─── Shader effects ────────────────────────────────────────────────────

namespace animations_controller_detail {

static QVariantMap parameterInfoToMap(const PhosphorAnimationShaders::AnimationShaderEffect::ParameterInfo& p)
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

static QVariantMap effectToMap(const PhosphorAnimationShaders::AnimationShaderEffect& effect)
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

static QVariantMap shaderProfileToMap(const PhosphorAnimationShaders::ShaderProfile& profile)
{
    QVariantMap m;
    if (profile.effectId)
        m.insert(QStringLiteral("effectId"), *profile.effectId);
    if (profile.parameters)
        m.insert(QStringLiteral("parameters"), *profile.parameters);
    return m;
}

} // namespace animations_controller_detail

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

QString AnimationsPageController::userShaderDirectoryPath() const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return base + ConfigDefaults::userAnimationsSubdir();
}

bool AnimationsPageController::ensureUserShaderDirectory()
{
    return QDir().mkpath(userShaderDirectoryPath());
}

void AnimationsPageController::openUserShaderDirectory()
{
    ensureUserShaderDirectory();
    QDesktopServices::openUrl(QUrl::fromLocalFile(userShaderDirectoryPath()));
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
    // ShaderProfile's "engaged-empty means no effect" semantics.
    // clearShaderOverride emits pendingChangesChanged itself when the
    // call actually changed state.
    if (effectId.isEmpty())
        return clearShaderOverride(path);

    // Standard pattern: write through Settings::setShaderProfileTree.
    // The shaderProfileTreeJson Q_PROPERTY emits NOTIFY, the
    // SettingsController meta-object loop catches it. No per-edit
    // notify here, no snapshot, no custom dirty plumbing.
    ShaderProfile profile;
    profile.effectId = effectId;
    if (!parameters.isEmpty())
        profile.parameters = parameters;

    ShaderProfileTree tree = m_settings->shaderProfileTree();
    tree.setOverride(path, profile);
    m_settings->setShaderProfileTree(tree);
    // shaderProfileTreeChanged → constructor lambda → shaderProfileChanged.
    // Emit pendingChangesChanged so SettingsController treats this as a
    // user-visible edit (without it the Save button never lit up for
    // shader edits).
    Q_EMIT pendingChangesChanged();
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
    // shaderProfileTreeChanged → constructor lambda → shaderProfileChanged.
    // Emit pendingChangesChanged so the SettingsController slot re-evaluates
    // hasPendingChanges and lights the Save button.
    Q_EMIT pendingChangesChanged();
    return true;
}

} // namespace PlasmaZones
