// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "animationspagecontroller.h"

#include "../config/configdefaults.h"
#include "../core/animationshadersupportedpaths.h"
#include "../core/isettings.h"
#include "../core/logging.h"
#include "animationpresetlibrary.h"
#include "dbusutils.h"
#include "motionsetstore.h"

#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/AnimationShaderEffect.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorAnimation/ShaderProfile.h>
#include <PhosphorAnimation/ShaderProfileTree.h>

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
        // Cache the canonical default curve string. Constructing a fresh
        // PhosphorAnimation::Easing() per call just to read its toString()
        // is wasteful — the function-local static is initialised once,
        // thread-safely under C++11.
        static const QString kDefaultCurve = PhosphorAnimation::Easing().toString();
        profile.insert(QLatin1String(P::JsonFieldCurve), kDefaultCurve);
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
    // applyMotionSet's per-path overrideChanged emissions ride through
    // the m_writeOverride callback (which the controller wires to its
    // own setOverride). MotionSetStore therefore exposes no
    // overrideChanged signal — the controller is the single source of
    // truth for that signal.

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
    return !m_pendingFileSnapshots.isEmpty() || m_shaderTreeDirty;
}

void AnimationsPageController::commitPending()
{
    const bool had = hasPendingChanges();
    m_pendingFileSnapshots.clear();
    m_shaderTreeDirty = false;
    if (had)
        Q_EMIT pendingChangesChanged();
}

void AnimationsPageController::revertPending()
{
    // Shader tree changes are reverted by the subsequent m_settings.load() call
    // in SettingsController, not by this method. Do not call revertPending()
    // without a following load().
    using namespace PhosphorAnimation;
    using namespace PhosphorAnimationShaders;

    if (!hasPendingChanges())
        return;

    m_shaderTreeDirty = false;

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
    const QString topLevel = dot < 0 ? path : path.left(dot);

    // Merge osd.* and panel.* into the "overlays" UI section.
    if (topLevel == QLatin1String("osd") || topLevel == QLatin1String("panel"))
        return QStringLiteral("overlays");

    // Merge cursor.* into the "widget" UI section.
    if (topLevel == QLatin1String("cursor"))
        return QStringLiteral("widget");

    return topLevel;
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

    // Pre-compute the set of paths that are some other path's parent —
    // any path X with a child Y such that parentPath(Y) == X. Drives the
    // isCategory flag below in O(1) per row instead of an O(n) scan
    // (which made eventSections O(n²) overall and lit up profiler runs
    // on the first drilldown evaluation).
    QSet<QString> parentPaths;
    parentPaths.reserve(paths.size());
    for (const QString& path : paths) {
        const QString parent = ProfilePaths::parentPath(path);
        if (!parent.isEmpty())
            parentPaths.insert(parent);
    }

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
        // event — i.e. another built-in path uses it as parent.
        entry.insert(QStringLiteral("isCategory"), parentPaths.contains(path));
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
    // path happens at every call site so this helper trusts its input;
    // assert in debug builds so a future caller that forgets the gate
    // crashes fast rather than producing a path-traversal file open.
    Q_ASSERT(isValidEventPath(path));
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
    const bool wasPending = hasPendingChanges();
    if (!isValidEventPath(path))
        return false;

    const QString dir = userProfilesDir();
    if (!QDir().mkpath(dir))
        return false;

    const QString filePath = profileFilePath(path);

    // Strip the name field for the equality compare against on-disk content
    // (`readProfileJson` strips it too). Same `obj` is later given the name
    // back for the write — the canonical name is always the path.
    QJsonObject obj = QJsonObject::fromVariantMap(profileJson);
    obj.remove(JsonNameKey);
    const QJsonObject existing = readProfileJson(filePath);
    if (existing == obj) {
        // Round-trip with no real change — bail early so a QML two-way
        // binding cycle doesn't dirty the page or fire spurious
        // overrideChanged / pendingChangesChanged emissions.
        return true;
    }
    obj.insert(JsonNameKey, path);

    // Snapshot ONLY if this is the first edit to this path; remove the
    // snapshot if the write below fails so hasPendingChanges() doesn't
    // report a phantom pending edit pointing at content we never touched.
    const bool firstSnapshot = !m_pendingFileSnapshots.contains(filePath);
    snapshotFileIfFirst(filePath);

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (firstSnapshot)
            m_pendingFileSnapshots.remove(filePath);
        return false;
    }
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (firstSnapshot)
            m_pendingFileSnapshots.remove(filePath);
        return false;
    }

    const bool nowPending = hasPendingChanges();
    Q_EMIT overrideChanged(path);
    if (wasPending != nowPending)
        Q_EMIT pendingChangesChanged();
    return true;
}

bool AnimationsPageController::clearOverride(const QString& path)
{
    const bool wasPending = hasPendingChanges();
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
    if (wasPending != hasPendingChanges())
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
    // Keys mirror PhosphorRendering::ShaderRegistry::parameterInfoToVariantMap
    // so animation packs and overlay packs share QML editor components.
    // Optional fields are emitted only when valid/non-empty.
    QVariantMap m;
    m.insert(QLatin1String("id"), p.id);
    m.insert(QLatin1String("name"), p.name);
    m.insert(QLatin1String("type"), p.type);
    if (!p.description.isEmpty())
        m.insert(QLatin1String("description"), p.description);
    if (!p.group.isEmpty())
        m.insert(QLatin1String("group"), p.group);
    if (p.defaultValue.isValid())
        m.insert(QLatin1String("default"), p.defaultValue);
    if (p.minValue.isValid())
        m.insert(QLatin1String("min"), p.minValue);
    if (p.maxValue.isValid())
        m.insert(QLatin1String("max"), p.maxValue);
    if (p.stepValue.isValid())
        m.insert(QLatin1String("step"), p.stepValue);
    return m;
}

static QVariantMap effectToMap(const PhosphorAnimationShaders::AnimationShaderEffect& effect)
{
    QVariantMap m;
    m.insert(QLatin1String("id"), effect.id);
    m.insert(QLatin1String("name"), effect.name);
    m.insert(QLatin1String("description"), effect.description);
    m.insert(QLatin1String("author"), effect.author);
    m.insert(QLatin1String("version"), effect.version);
    m.insert(QLatin1String("category"), effect.category);
    m.insert(QLatin1String("isUserEffect"), effect.isUserEffect);
    // `previewPath` is resolved to an absolute path by the registry's
    // `parseEffect`, so QML can pass it directly to `Image.source` (with
    // a `file://` scheme prefix). Empty when the pack didn't ship a
    // preview — the page renders a placeholder for that case.
    m.insert(QLatin1String("previewPath"), effect.previewPath);
    QVariantList params;
    params.reserve(effect.parameters.size());
    for (const auto& p : effect.parameters) {
        params.append(parameterInfoToMap(p));
    }
    m.insert(QLatin1String("parameters"), params);
    return m;
}

static QVariantMap shaderProfileToMap(const PhosphorAnimationShaders::ShaderProfile& profile)
{
    QVariantMap m;
    if (profile.effectId)
        m.insert(QLatin1String("effectId"), *profile.effectId);
    if (profile.parameters)
        m.insert(QLatin1String("parameters"), *profile.parameters);
    return m;
}

} // namespace animations_controller_detail

bool AnimationsPageController::supportsShaderLeg(const QString& path) const
{
    return eventPathSupportsShaderLeg(path);
}

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

namespace animations_controller_detail {

/// Copy a directory recursively. Qt has no built-in equivalent; the
/// stdlib's `std::filesystem::copy` exists but we stick to QDir/QFile
/// for consistency with the rest of the codebase. Returns false on the
/// first failure (broken file copy, mkpath fail, etc.) so the caller
/// can roll back via QDir::removeRecursively.
///
/// Symlinks (file or dir) are explicitly skipped via `QDir::NoSymLinks`
/// AND a per-entry `isSymLink()` guard. Without that, a dropped pack
/// containing `metadata.json -> /etc/shadow` or `assets -> /etc` would
/// silently follow the link during traversal and the recursive copy
/// would smuggle arbitrary readable filesystem content into the user
/// shader dir under deceptive names. A shader pack contains regular
/// files only; anything exotic is suspect and refused.
static bool copyDirRecursive(const QString& sourcePath, const QString& destPath)
{
    QDir sourceDir(sourcePath);
    if (!sourceDir.exists())
        return false;
    if (!QDir().mkpath(destPath))
        return false;

    const QFileInfoList entries =
        sourceDir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoSymLinks | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        // QDir::NoSymLinks already filters at the entryInfoList layer, but
        // recheck per-entry: filesystem races (the entry being replaced
        // by a symlink between enumeration and this iteration) and the
        // historical leniency of QDir::NoSymLinks across Qt versions
        // both argue for an explicit guard at the copy boundary.
        if (entry.isSymLink())
            continue;

        const QString destEntryPath = destPath + QLatin1Char('/') + entry.fileName();
        if (entry.isDir()) {
            if (!copyDirRecursive(entry.absoluteFilePath(), destEntryPath))
                return false;
        } else if (entry.isFile()) {
            // QFile::copy refuses to overwrite — caller's collision check
            // already guarantees a clean destination, but defend against
            // a partial failed previous run leaving stale files.
            if (QFile::exists(destEntryPath))
                QFile::remove(destEntryPath);
            if (!QFile::copy(entry.absoluteFilePath(), destEntryPath))
                return false;
        }
        // Devices, FIFOs, sockets, etc. are not isFile()/isDir() and
        // therefore fall through silently — same intent as the symlink
        // skip above.
    }
    return true;
}

} // namespace animations_controller_detail

bool AnimationsPageController::installShaderPack(const QString& sourceUrl)
{
    if (sourceUrl.isEmpty())
        return false;

    // Accept both `file://...` URLs (drag-drop from a file manager) and
    // bare paths (programmatic callers).
    QString sourcePath = sourceUrl;
    if (sourcePath.startsWith(QLatin1String("file://")))
        sourcePath = QUrl(sourceUrl).toLocalFile();

    // Normalise trailing slashes and `.`/`..` components — without this,
    // a drop URL like `file:///path/to/pack/` produces an empty
    // `fileName()` below and the destDir collapses onto the user shader
    // dir itself, surfacing as a confusing "destination already exists"
    // rather than a clean parse failure.
    sourcePath = QDir::cleanPath(sourcePath);

    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isDir() || sourceInfo.isSymLink()) {
        qCWarning(lcConfig) << "installShaderPack: source is not an existing directory:" << sourcePath;
        return false;
    }
    const QString sourceBasename = sourceInfo.fileName();
    if (sourceBasename.isEmpty()) {
        qCWarning(lcConfig) << "installShaderPack: source path has no basename:" << sourcePath;
        return false;
    }

    // Validate metadata.json — without it the registry won't pick up the
    // pack, so accepting the drop would silently be a no-op. Reject
    // symlinked metadata so a malicious pack can't smuggle a non-shader
    // JSON file's content past the validation gate.
    const QString metadataPath = sourceInfo.absoluteFilePath() + QLatin1String("/metadata.json");
    const QFileInfo metadataInfo(metadataPath);
    if (!metadataInfo.exists() || !metadataInfo.isFile() || metadataInfo.isSymLink()) {
        qCWarning(lcConfig) << "installShaderPack: source has no metadata.json:" << sourcePath;
        return false;
    }

    if (!ensureUserShaderDirectory())
        return false;

    const QString destDir = userShaderDirectoryPath() + QLatin1Char('/') + sourceBasename;
    if (QFileInfo::exists(destDir)) {
        qCWarning(lcConfig) << "installShaderPack: destination already exists, refusing to overwrite:" << destDir;
        return false;
    }

    if (!animations_controller_detail::copyDirRecursive(sourceInfo.absoluteFilePath(), destDir)) {
        qCWarning(lcConfig) << "installShaderPack: copy failed; rolling back:" << destDir;
        QDir(destDir).removeRecursively();
        return false;
    }

    // The registry's filewatcher rescans on its own — no explicit poke
    // needed. If a poke is ever required, emit `shaderEffectsChanged`
    // here.
    return true;
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

    // Reject writes on paths the daemon's overlay service doesn't
    // consume as a shader-leg surface. Defence in depth: the QML UI
    // gates the picker via `supportsShaderLeg`, but a Q_INVOKABLE is
    // callable from anywhere (future scripts, tests, deserialisation
    // shims) and a stale tree entry on an unsupported path silently
    // shadows the user-intended parent override at runtime via the
    // resolver's deeper-leaf-wins overlay merge.
    if (!eventPathSupportsShaderLeg(path)) {
        qCWarning(lcConfig) << "setShaderOverride: path" << path
                            << "is not in shaderSupportedEventPaths() — ignoring (no daemon-side surface consumes it)";
        return false;
    }

    // Empty effectId writes an ENGAGED-EMPTY override at this path:
    // `ShaderProfile::effectId = std::optional<QString>("")`. This is
    // the "explicit no effect" sentinel — `ShaderProfile::overlay`
    // treats it as a real value that wins over a parent's effectId,
    // so inheritance from an ancestor (e.g. `panel` → "dissolve") is
    // BLOCKED at this path and every descendant resolves to no shader.
    //
    // This is intentionally distinct from `clearShaderOverride`, which
    // removes the override entry entirely so resolution falls through
    // to the parent. Without this distinction, an
    // AnimationEventCard's "Override OFF" toggle on `panel.popup`
    // (cleared override) cannot stop the parent's dissolve from
    // cascading down to every popup event — exactly the user-reported
    // "I disabled all popups but dissolve still plays" bug. The
    // engaged-empty profile gives the UI a way to express "disable
    // shader at this path AND every descendant that doesn't override".
    if (effectId.isEmpty()) {
        ShaderProfile disabledProfile;
        disabledProfile.effectId = QString();
        if (!parameters.isEmpty())
            disabledProfile.parameters = parameters;
        ShaderProfileTree tree = m_settings->shaderProfileTree();
        // Compare-and-skip relies on `ShaderProfile::operator==` being
        // engaged-state-sensitive (it forwards to `std::optional::operator==`,
        // which treats `nullopt` and `optional(empty)` as DISTINCT). The
        // engaged-empty `disabledProfile` constructed above (effectId =
        // engaged-empty QString, parameters = nullopt because the guard
        // above only engages on non-empty input) round-trips through
        // `ShaderProfile::toJson`/`fromJson` without changing engaged-state:
        // toJson omits `parameters` when nullopt, and fromJson leaves it
        // nullopt when absent. So a disk-loaded disable sentinel for an
        // unchanged path compares equal here and the write short-circuits.
        // If a future caller hands us engaged-but-empty parameters and
        // toJson starts emitting `"parameters": {}`, this comparison would
        // need explicit normalization (reset `parameters` to nullopt when
        // engaged-empty) to stay consistent with disk-loaded shape.
        if (tree.directOverride(path) == disabledProfile)
            return true;
        tree.setOverride(path, disabledProfile);
        m_settings->setShaderProfileTree(tree);
        m_shaderTreeDirty = true;
        Q_EMIT pendingChangesChanged();
        return true;
    }

    // Reject unknown effect ids at the boundary — without this, a typo
    // from QML silently writes garbage into the shader-profile tree, and
    // the daemon's lookup at runtime returns nothing with no settings-side
    // diagnostic (the failure mode is "no shader applied, no error").
    //
    // The `effectIds().isEmpty()` guard avoids tripping the gate when the
    // registry hasn't yet scanned XDG dirs (asynchronous on some setups,
    // and unit tests construct an empty registry on purpose) — we can't
    // distinguish "id is unknown" from "registry not yet populated"
    // without a separate readiness signal.
    if (m_shaderRegistry && !m_shaderRegistry->effectIds().isEmpty() && !m_shaderRegistry->hasEffect(effectId)) {
        qCWarning(lcConfig) << "setShaderOverride: unknown effectId" << effectId << "— ignoring assignment for" << path;
        return false;
    }

    // Standard pattern: write through Settings::setShaderProfileTree.
    // The shaderProfileTreeJson Q_PROPERTY emits NOTIFY, the
    // SettingsController meta-object loop catches it. No per-edit
    // notify here, no snapshot, no custom dirty plumbing.
    ShaderProfile profile;
    profile.effectId = effectId;
    if (!parameters.isEmpty())
        profile.parameters = parameters;

    ShaderProfileTree tree = m_settings->shaderProfileTree();
    // Short-circuit when the tree is already at the requested state — avoids
    // a same-tree write that would cycle through Settings + the boomerang
    // and fire a spurious pendingChangesChanged.
    if (tree.directOverride(path) == profile)
        return true;
    tree.setOverride(path, profile);
    m_settings->setShaderProfileTree(tree);
    m_shaderTreeDirty = true;
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
    m_shaderTreeDirty = true;
    Q_EMIT pendingChangesChanged();
    return true;
}

namespace {
/// Collect every override path strictly DEEPER than @p path
/// (i.e. starting with `<path>.`). Centralises the prefix-match math
/// so shaderOverrideDescendantCount and clearShaderOverrideDescendants
/// share one definition of "descendant" — the trailing `.` boundary
/// is what excludes both the path itself ("panel.popup") and sibling
/// names with shared prefix ("panel.popup-something").
QStringList collectShaderOverrideDescendants(const PhosphorAnimationShaders::ShaderProfileTree& tree,
                                             const QString& path)
{
    QStringList out;
    if (path.isEmpty()) {
        return out;
    }
    const QString prefix = path + QLatin1Char('.');
    const QStringList paths = tree.overriddenPaths();
    for (const QString& p : paths) {
        if (p.startsWith(prefix)) {
            out.append(p);
        }
    }
    return out;
}
} // namespace

int AnimationsPageController::shaderOverrideDescendantCount(const QString& path) const
{
    if (!m_settings)
        return 0;
    return collectShaderOverrideDescendants(m_settings->shaderProfileTree(), path).size();
}

int AnimationsPageController::clearShaderOverrideDescendants(const QString& path)
{
    using namespace PhosphorAnimationShaders;
    if (!m_settings)
        return 0;
    ShaderProfileTree tree = m_settings->shaderProfileTree();
    const QStringList toClear = collectShaderOverrideDescendants(tree, path);
    if (toClear.isEmpty())
        return 0;
    for (const QString& p : toClear)
        tree.clearOverride(p);
    m_settings->setShaderProfileTree(tree);
    m_shaderTreeDirty = true;
    Q_EMIT pendingChangesChanged();
    return toClear.size();
}

QVariantList AnimationsPageController::shaderEffectUsages(const QString& effectId) const
{
    using namespace PhosphorAnimationShaders;
    if (!m_settings || effectId.isEmpty())
        return {};
    const ShaderProfileTree tree = m_settings->shaderProfileTree();
    const QStringList overridden = tree.overriddenPaths();
    QVariantList out;
    for (const QString& p : overridden) {
        const ShaderProfile profile = tree.directOverride(p);
        if (!profile.effectId || *profile.effectId != effectId)
            continue;
        QVariantMap entry;
        entry.insert(QLatin1String("path"), p);
        entry.insert(QLatin1String("label"), eventLabel(p));
        out.append(entry);
    }
    // Sort by label for deterministic UI order across runs — the tree's
    // `overriddenPaths()` iterates a QHash internally so the order is
    // not stable.
    std::sort(out.begin(), out.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value(QLatin1String("label")).toString() < b.toMap().value(QLatin1String("label")).toString();
    });
    return out;
}

} // namespace PlasmaZones
