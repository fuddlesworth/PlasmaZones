// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "animationspagecontroller.h"

#include "../config/configdefaults.h"
#include "../core/animationshadersupportedpaths.h"
#include "../core/isettings.h"
#include "../core/logging.h"
#include "animationpresetlibrary.h"
#include "animations_controller_detail.h"
#include "dbusutils.h"
#include "motionsetstore.h"

#include <PhosphorAnimation/AnimationShaderEffect.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/ShaderProfile.h>
#include <PhosphorAnimation/ShaderProfileTree.h>

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrent/QtConcurrent>

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
    : PhosphorSettingsUi::PageController(QStringLiteral("animations"), parent)
    , m_shaderRegistry(shaderRegistry)
    , m_settings(settings)
{
    // Forward the existing pendingChangesChanged() signal to the
    // framework's dirtyChanged() so ApplicationController picks up
    // animation-page edits as part of the global dirty flag.
    connect(this, &AnimationsPageController::pendingChangesChanged, this,
            &PhosphorSettingsUi::StagingDomain::dirtyChanged);
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
            // If this signal arrived from an external reload (Discard from
            // another page, import, settings.load()), the on-disk tree is
            // now authoritative — drop the staged-dirty flag so
            // hasPendingChanges() does not report phantom edits. The
            // m_mutatingShaderTree guard distinguishes our own writes
            // (which keep the dirty flag set) from external reloads.
            if (!m_mutatingShaderTree && m_shaderTreeDirty) {
                m_shaderTreeDirty = false;
                Q_EMIT pendingChangesChanged();
            }
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

bool AnimationsPageController::snapshotFileIfFirst(const QString& filePath)
{
    if (filePath.isEmpty())
        return false;
    if (m_pendingFileSnapshots.contains(filePath))
        return true;
    QFile f(filePath);
    if (!f.exists()) {
        m_pendingFileSnapshots.insert(filePath, std::nullopt);
        return true;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        // Mid-session permission drift on an existing file would
        // silently lose pre-edit content if a write proceeded without
        // a snapshot — log so the journal flags the data-loss path,
        // and return false so direct callers can refuse the write.
        qCWarning(lcConfig) << "snapshotFileIfFirst: cannot read existing file" << filePath << "for revert snapshot —"
                            << f.errorString();
        return false;
    }
    m_pendingFileSnapshots.insert(filePath, f.readAll());
    return true;
}

bool AnimationsPageController::hasPendingChanges() const
{
    return !m_pendingFileSnapshots.isEmpty() || m_shaderTreeDirty;
}

bool AnimationsPageController::isDirty() const
{
    return hasPendingChanges();
}

void AnimationsPageController::apply()
{
    commitPending();
    // commitPending is synchronous (just clears the snapshot map +
    // dirty bit; the per-edit writes already hit disk through
    // setOverride). Signal completion immediately so the chrome's
    // applyAllAsync wait-counter ticks down.
    Q_EMIT applyResult(true, QString());
}

void AnimationsPageController::discard()
{
    // The async revert moves the QSaveFile loop off the GUI thread
    // (motion-set discards can touch dozens of profile files) and
    // emits the inherited discardResult on completion — chrome
    // wait-counter then ticks down.
    asyncRevertPending();
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
    // discard() / revertPending() is the StagingDomain contract for "undo
    // everything since the last apply". This method:
    //   * Restores every snapshotted profile file from disk.
    //   * Clears our own dirty flag (m_shaderTreeDirty) only when all
    //     snapshots restore successfully — partial-failure keeps the flag
    //     so a retry path still sees hasPendingChanges()==true.
    //
    // IMPORTANT CALLER CONTRACT: the in-memory shader tree on m_settings
    // (Settings::shaderProfileTree) is NOT reverted here — that state is
    // owned by Settings, not this page, and is refreshed only by a
    // subsequent Settings::load(). SettingsController::discard() pairs
    // discard() with a follow-up load(); any future direct caller of
    // discard() MUST do the same, otherwise hasPendingChanges() returns
    // false while m_settings->shaderProfileTree() still holds unsaved
    // edits.
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

    // Clear shader-tree dirty when (and only when) every file snapshot was
    // successfully restored. If `retained` is non-empty, some restores
    // failed and the caller may retry — leave the dirty flag so the
    // retry path still sees hasPendingChanges()==true.
    if (m_pendingFileSnapshots.isEmpty()) {
        m_shaderTreeDirty = false;
    }

    // Bulk emit so QML sub-pages refresh exactly the rows that moved.
    for (const QString& path : overrideEvents)
        Q_EMIT overrideChanged(path);
    if (anyPreset)
        Q_EMIT userPresetsChanged();
    if (anyMotionSet)
        Q_EMIT motionSetsChanged();
    Q_EMIT pendingChangesChanged();
}

void AnimationsPageController::asyncRevertPending()
{
    // POD payload threaded between GUI thread and the worker.
    // Captured by value into the QtConcurrent lambda + returned by
    // value through QFuture so it lifecycles cleanly across threads.
    struct WorkerResult
    {
        QHash<QString, std::optional<QByteArray>> retained;
        QStringList overrideEvents;
        bool anyPreset = false;
        bool anyMotionSet = false;
    };

    using namespace PhosphorAnimation;
    using namespace PhosphorAnimationShaders;

    if (!hasPendingChanges()) {
        Q_EMIT discardResult(true, QString());
        return;
    }

    // Snapshot every input the worker needs by value. The worker
    // touches nothing else on `this` — that's what keeps the I/O
    // loop safe on a non-GUI thread.
    const QString profilesDir = userProfilesDir();
    const QString setsDir = userMotionSetsDir();
    const QStringList knownPaths = ProfilePaths::allBuiltInPaths();
    const QSet<QString> knownPathSet(knownPaths.cbegin(), knownPaths.cend());
    const QHash<QString, std::optional<QByteArray>> snapshots = m_pendingFileSnapshots;

    auto* watcher = new QFutureWatcher<WorkerResult>(this);
    connect(watcher, &QFutureWatcher<WorkerResult>::finished, this, [this, watcher]() {
        const WorkerResult result = watcher->result();
        watcher->deleteLater();

        // Back on the GUI thread — install retained + emit signals.
        m_pendingFileSnapshots = result.retained;
        if (m_pendingFileSnapshots.isEmpty())
            m_shaderTreeDirty = false;

        for (const QString& path : result.overrideEvents)
            Q_EMIT overrideChanged(path);
        if (result.anyPreset)
            Q_EMIT userPresetsChanged();
        if (result.anyMotionSet)
            Q_EMIT motionSetsChanged();
        Q_EMIT pendingChangesChanged();
        Q_EMIT discardResult(
            result.retained.isEmpty(),
            result.retained.isEmpty()
                ? QString()
                : tr("Failed to restore %1 profile file(s); they remain pending.").arg(result.retained.size()));
    });

    QFuture<WorkerResult> future = QtConcurrent::run([profilesDir, setsDir, knownPathSet, snapshots]() {
        WorkerResult result;
        for (auto it = snapshots.cbegin(); it != snapshots.cend(); ++it) {
            const QString& filePath = it.key();
            const auto& content = it.value();

            bool restored = false;
            if (!content.has_value()) {
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
                qCWarning(lcConfig) << "AnimationsPageController::asyncRevertPending: failed to restore" << filePath;
                result.retained.insert(filePath, content);
                continue;
            }

            const QFileInfo info(filePath);
            const QString absDir = info.absolutePath();
            const QString stem = info.completeBaseName();
            if (absDir == setsDir) {
                result.anyMotionSet = true;
            } else if (absDir == profilesDir) {
                if (knownPathSet.contains(stem))
                    result.overrideEvents.append(stem);
                else
                    result.anyPreset = true;
            }
        }
        return result;
    });
    watcher->setFuture(future);
}

// ─── Path discovery ────────────────────────────────────────────────────

QString AnimationsPageController::sectionForPath(const QString& path) const
{
    if (path.isEmpty())
        return {};
    const int dot = path.indexOf(QLatin1Char('.'));
    const QString topLevel = dot < 0 ? path : path.left(dot);

    // Merge osd.*, popup.*, and panel.* into the "overlays" UI section.
    if (topLevel == QLatin1String("osd") || topLevel == QLatin1String("popup") || topLevel == QLatin1String("panel"))
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
    // The event taxonomy is static for the process lifetime; cache the
    // materialised QVariantList in a mutable member so QML rebindings
    // skip the O(n) rebuild after the first call. Computed lazily on
    // first read rather than at construction because the helpers it
    // calls (sectionForPath, eventLabel) are const member functions
    // that need `this`.
    if (!m_eventSectionsCache.isEmpty()) {
        return m_eventSectionsCache;
    }

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
        // section root (e.g. "window", "popup") rather than a leaf
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
    m_eventSectionsCache = result;
    return m_eventSectionsCache;
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
    // Filenames mirror the path (e.g. `editor.snapIn.json`) — same
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
    if (!isValidEventPath(path))
        return false;

    const QString dir = userProfilesDir();
    if (!QDir().mkpath(dir))
        return false;
    // Capture dirty state AFTER the cheap validity / mkpath guards so an
    // invalid path doesn't pay for a hasPendingChanges() walk.
    const bool wasPending = hasPendingChanges();

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
    // Bail before touching disk if the snapshot couldn't be captured —
    // an unrecoverable revert is worse than the failed write.
    const bool firstSnapshot = !m_pendingFileSnapshots.contains(filePath);
    if (!snapshotFileIfFirst(filePath)) {
        qCWarning(lcConfig) << "setOverride: refusing to write" << filePath << "without a recoverable snapshot";
        return false;
    }

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
    if (!isValidEventPath(path))
        return false;
    const QString filePath = profileFilePath(path);
    QFile file(filePath);
    if (!file.exists())
        return false;
    // Capture dirty state AFTER the cheap validity / file.exists() guards
    // so an invalid path or no-op clear doesn't pay for a
    // hasPendingChanges() walk. Mirrors setOverride() ordering above so
    // a future refactor doesn't see two divergent capture-point shapes.
    const bool wasPending = hasPendingChanges();
    // Mirror setOverride's snapshot-rollback symmetry: capture whether
    // this call is the first to touch the file, snapshot, and on
    // remove() failure roll the snapshot back so hasPendingChanges()
    // doesn't report a phantom pending edit pointing at a file the
    // user never actually touched (the unsaved-changes badge would
    // light up and Discard would write the original content back over
    // an unchanged original — harmless but confusing UX).
    const bool firstSnapshot = !m_pendingFileSnapshots.contains(filePath);
    if (!snapshotFileIfFirst(filePath)) {
        qCWarning(lcConfig) << "clearOverride: refusing to delete" << filePath << "without a recoverable snapshot";
        return false;
    }
    if (!file.remove()) {
        if (firstSnapshot)
            m_pendingFileSnapshots.remove(filePath);
        return false;
    }
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
// The effectToMap / parameterInfoToMap / shaderProfileToMap helpers used
// by both animationspagecontroller.cpp and animationspagecontroller_shaders.cpp
// live in animations_controller_detail.h as inline functions so the two
// TUs don't depend on unity-build merging for cross-TU linkage.

} // namespace PlasmaZones
