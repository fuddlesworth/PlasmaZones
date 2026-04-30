// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "animationspagecontroller.h"

#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfilePaths.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

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

AnimationsPageController::AnimationsPageController(QObject* parent)
    : QObject(parent)
{
}

void AnimationsPageController::setUserProfilesDirOverride(const QString& dir)
{
    m_userProfilesDirOverride = dir;
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

    QJsonObject obj = QJsonObject::fromVariantMap(profileJson);
    // The `name` field is what ProfileLoader's envelope helper reads to
    // assign the registry path (per ProfileLoader.h schema docs). Always
    // overwrite — the QML map shouldn't carry a stale name.
    obj.insert(jsonNameKey(), path);

    QSaveFile file(profileFilePath(path));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    if (!file.commit())
        return false;

    Q_EMIT overrideChanged(path);
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
    if (!file.remove())
        return false;
    Q_EMIT overrideChanged(path);
    return true;
}

} // namespace PlasmaZones
