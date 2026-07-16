// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Layout JSON serialization / deserialization.
// Part of Layout class — split from layout.cpp for SRP.

#include <PhosphorZones/Layout.h>
#include <PhosphorZones/ZoneJsonKeys.h>
#include <PhosphorZones/LayoutUtils.h>
#include "../zoneslogging.h"
#include <PhosphorZones/Zone.h>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QStandardPaths>

#include <cmath>

namespace PhosphorZones {

QJsonObject Layout::toJson() const
{
    QJsonObject json;
    json[::PhosphorZones::ZoneJsonKeys::Id] = m_id.toString();
    json[::PhosphorZones::ZoneJsonKeys::Name] = m_name;
    if (!m_description.isEmpty()) {
        json[::PhosphorZones::ZoneJsonKeys::Description] = m_description;
    }
    // Only serialize gap overrides if they're set (>= 0)
    if (m_zonePadding >= 0) {
        json[::PhosphorZones::ZoneJsonKeys::ZonePadding] = m_zonePadding;
    }
    if (m_outerGap >= 0) {
        json[::PhosphorZones::ZoneJsonKeys::OuterGap] = m_outerGap;
    }
    // Per-side outer gap overrides — serialize toggle whenever enabled so user intent is preserved
    if (m_usePerSideOuterGap) {
        json[::PhosphorZones::ZoneJsonKeys::UsePerSideOuterGap] = true;
        if (m_outerGapTop >= 0)
            json[::PhosphorZones::ZoneJsonKeys::OuterGapTop] = m_outerGapTop;
        if (m_outerGapBottom >= 0)
            json[::PhosphorZones::ZoneJsonKeys::OuterGapBottom] = m_outerGapBottom;
        if (m_outerGapLeft >= 0)
            json[::PhosphorZones::ZoneJsonKeys::OuterGapLeft] = m_outerGapLeft;
        if (m_outerGapRight >= 0)
            json[::PhosphorZones::ZoneJsonKeys::OuterGapRight] = m_outerGapRight;
    }
    json[::PhosphorZones::ZoneJsonKeys::ShowZoneNumbers] = m_showZoneNumbers;
    if (m_overlayDisplayMode >= 0) {
        json[::PhosphorZones::ZoneJsonKeys::OverlayDisplayMode] = m_overlayDisplayMode;
    }
    if (m_defaultOrder != DefaultOrderUnset) {
        json[::PhosphorZones::ZoneJsonKeys::DefaultOrder] = m_defaultOrder;
    }
    // Note: isBuiltIn is no longer serialized - it's determined by source path at load time

    // Persist system origin path so user overrides can be restored on deletion
    if (!m_systemSourcePath.isEmpty()) {
        json[::PhosphorZones::ZoneJsonKeys::SystemSourcePath] = m_systemSourcePath;
    }

    // Shader support — empty shaderId means "no shader", we only persist the
    // key when populated. Layout is a pure data holder: it persists whatever
    // m_shaderParams was last set to, without reaching into a UI-side
    // validator. Stale-param cleanup (stripping values whose keys don't
    // belong to the current shader) is the editor's responsibility on the
    // edit boundary — see EditorController::stripStaleShaderParams and the
    // shader-refresh path that calls it when the active shader changes.
    // Keeping Layout decoupled from ShaderRegistry lets the core data type
    // eventually live in a standalone phosphor-zones library without
    // pulling phosphor-wayland into the dependency graph.
    if (!m_shaderId.isEmpty()) {
        json[::PhosphorZones::ZoneJsonKeys::ShaderId] = m_shaderId;
    }
    // Don't persist params when no shader is bound — stale params without a
    // shaderId are meaningless to consumers and just bloat the file.
    if (!m_shaderId.isEmpty() && !m_shaderParams.isEmpty()) {
        json[::PhosphorZones::ZoneJsonKeys::ShaderParams] = QJsonObject::fromVariantMap(m_shaderParams);
    }

    // Auto-assign - only serialize if true
    if (m_autoAssign) {
        json[::PhosphorZones::ZoneJsonKeys::AutoAssign] = true;
    }

    // Full screen geometry mode - only serialize if true
    if (m_useFullScreenGeometry) {
        json[::PhosphorZones::ZoneJsonKeys::UseFullScreenGeometry] = true;
    }

    // Aspect ratio classification - only serialize non-default values
    if (m_aspectRatioClass != ::PhosphorLayout::AspectRatioClass::Any) {
        json[::PhosphorZones::ZoneJsonKeys::AspectRatioClassKey] =
            ::PhosphorLayout::ScreenClassification::toString(m_aspectRatioClass);
    }
    if (m_minAspectRatio > 0.0) {
        json[::PhosphorZones::ZoneJsonKeys::MinAspectRatio] = m_minAspectRatio;
    }
    if (m_maxAspectRatio > 0.0) {
        json[::PhosphorZones::ZoneJsonKeys::MaxAspectRatio] = m_maxAspectRatio;
    }

    // Visibility filtering - only serialize non-default values
    if (m_hiddenFromSelector) {
        json[::PhosphorZones::ZoneJsonKeys::HiddenFromSelector] = true;
    }
    LayoutUtils::serializeAllowLists(json, m_allowedScreens, m_allowedDesktops, m_allowedActivities);

    QJsonArray zonesArray;
    for (const auto* zone : m_zones) {
        zonesArray.append(zone->toJson(m_lastRecalcGeometry));
    }
    json[::PhosphorZones::ZoneJsonKeys::Zones] = zonesArray;

    return json;
}

Layout* Layout::fromJson(const QJsonObject& json, QObject* parent)
{
    // Allocate a blank Layout and delegate population to a private member
    // method. This scopes the raw-member pokes to the class's own
    // implementation so future setter validation (e.g. name trimming)
    // lands naturally without needing to extend friendship to this TU.
    auto* layout = new Layout(parent);
    layout->initFromJson(json);
    return layout;
}

void Layout::initFromJson(const QJsonObject& json)
{
    // Boundary validation — flag missing/blank required fields with a single
    // breadcrumb per layout so a hand-edited or corrupted file is visible in
    // the log instead of silently producing a default-constructed shell.
    // Behaviour is preserved (synthesise an Id, accept an empty name) so we
    // don't break round-trips of older layouts that were tolerated previously,
    // but every defaulted boundary now leaves a trail.
    //
    // The warning is tied to the regeneration itself, not to the absent/blank
    // case alone: a present-but-unparseable Id ("not-a-uuid") and a nil Id
    // ("{00000000-...}") both land on a fresh UUID too, and those are exactly
    // the corrupt-file cases this breadcrumb exists for.
    const QString idString = json[::PhosphorZones::ZoneJsonKeys::Id].toString();
    m_id = QUuid::fromString(idString);
    if (m_id.isNull()) {
        qCWarning(lcLayoutLib) << "Layout::initFromJson: missing, empty or unusable Id" << idString
                               << "- generating fresh UUID";
        m_id = QUuid::createUuid();
    }

    if (!json.contains(::PhosphorZones::ZoneJsonKeys::Name)
        || json[::PhosphorZones::ZoneJsonKeys::Name].toString().trimmed().isEmpty()) {
        qCWarning(lcLayoutLib) << "Layout::initFromJson: missing or empty Name for layout" << m_id.toString();
    }
    m_name = json[::PhosphorZones::ZoneJsonKeys::Name].toString();
    // Note: "type" key is silently ignored for backward compatibility
    m_description = json[::PhosphorZones::ZoneJsonKeys::Description].toString();
    // Gap overrides: -1 means use global setting (key absent = no override)
    m_zonePadding = json.contains(::PhosphorZones::ZoneJsonKeys::ZonePadding)
        ? json[::PhosphorZones::ZoneJsonKeys::ZonePadding].toInt(-1)
        : -1;
    m_outerGap = json.contains(::PhosphorZones::ZoneJsonKeys::OuterGap)
        ? json[::PhosphorZones::ZoneJsonKeys::OuterGap].toInt(-1)
        : -1;
    // Per-side outer gap overrides. The `contains()` check mirrors the
    // adjacent gap-override deserialisation pattern — absent key means
    // "no override", not "explicit false" — so the read shape stays
    // uniform across every gap-related field.
    m_usePerSideOuterGap = json.contains(::PhosphorZones::ZoneJsonKeys::UsePerSideOuterGap)
        ? json[::PhosphorZones::ZoneJsonKeys::UsePerSideOuterGap].toBool(false)
        : false;
    m_outerGapTop = json.contains(::PhosphorZones::ZoneJsonKeys::OuterGapTop)
        ? json[::PhosphorZones::ZoneJsonKeys::OuterGapTop].toInt(-1)
        : -1;
    m_outerGapBottom = json.contains(::PhosphorZones::ZoneJsonKeys::OuterGapBottom)
        ? json[::PhosphorZones::ZoneJsonKeys::OuterGapBottom].toInt(-1)
        : -1;
    m_outerGapLeft = json.contains(::PhosphorZones::ZoneJsonKeys::OuterGapLeft)
        ? json[::PhosphorZones::ZoneJsonKeys::OuterGapLeft].toInt(-1)
        : -1;
    m_outerGapRight = json.contains(::PhosphorZones::ZoneJsonKeys::OuterGapRight)
        ? json[::PhosphorZones::ZoneJsonKeys::OuterGapRight].toInt(-1)
        : -1;
    m_showZoneNumbers = json[::PhosphorZones::ZoneJsonKeys::ShowZoneNumbers].toBool(true);
    m_overlayDisplayMode = json.contains(::PhosphorZones::ZoneJsonKeys::OverlayDisplayMode)
        ? json[::PhosphorZones::ZoneJsonKeys::OverlayDisplayMode].toInt(-1)
        : -1;
    m_defaultOrder = json[::PhosphorZones::ZoneJsonKeys::DefaultOrder].toInt(DefaultOrderUnset);
    // Note: sourcePath is set by LayoutManager after loading, not from JSON
    // But systemSourcePath IS persisted in user JSON for system override restoration
    m_systemSourcePath = json[::PhosphorZones::ZoneJsonKeys::SystemSourcePath].toString();

    // Sanity-check the persisted systemSourcePath: it must resolve to a file
    // CONTAINED IN a known system data directory (the OS-level
    // GenericDataLocation list, minus the writable user dir). A stale or
    // hand-edited config would otherwise leak an arbitrary path into the
    // "restore system original" code path, which opens and parses it — drop
    // it with a warning instead.
    //
    // Containment, not a string prefix. Both sides are resolved before the
    // compare and the match is anchored on a directory separator, so neither
    // "/usr/share/../../home/user/evil.json" (traversal) nor
    // "/usr/share-evil/x.json" (a sibling directory that merely shares a name
    // prefix) passes. The candidate is resolved with canonicalFilePath() when
    // the file exists, so a symlink planted inside a system directory cannot
    // point out of it either; when it does not exist, the lexically cleaned
    // absolute path is used instead, which still resolves ".." and keeps a
    // temporarily-missing system file (package upgrade in flight) from losing
    // its recorded origin.
    //
    // The check FAILS CLOSED: a path that no system prefix admits is dropped,
    // including when no prefix resolves at all. An environment with no
    // readable system data directory has nowhere a system layout could live,
    // so there is no candidate such an environment could legitimately accept —
    // "no prefixes, allow anything" would hand that case the widest answer
    // instead of the narrowest. The cost of dropping is bounded and visible:
    // the layout keeps working and only loses its "restore system original"
    // origin, which that environment could not have honoured anyway. Nothing
    // in the suite exercises this field, so no fixture depends on the check
    // being skipped. Skip only when the path is empty (no override tracked).
    if (!m_systemSourcePath.isEmpty()) {
        const QFileInfo sourceInfo(m_systemSourcePath);
        const QString canonicalSource =
            sourceInfo.exists() ? sourceInfo.canonicalFilePath() : QDir::cleanPath(sourceInfo.absoluteFilePath());
        const QStringList systemPrefixes = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
        // Canonicalised so the user-dir compare below is resolved-vs-resolved
        // like every other compare in this block. writableLocation() and the
        // matching standardLocations() entry can differ textually (a trailing
        // separator, a symlinked $HOME) while naming one directory, and a raw
        // compare would miss that and admit the user dir as a system prefix.
        const QString canonicalUserDataPath =
            QFileInfo(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).canonicalFilePath();
        bool valid = false;
        for (const QString& prefix : systemPrefixes) {
            // A prefix that doesn't resolve is not a directory anything can be
            // contained in, so it can neither admit nor reject a candidate.
            const QString canonicalPrefix = QFileInfo(prefix).canonicalFilePath();
            if (canonicalPrefix.isEmpty()) {
                continue;
            }
            if (canonicalPrefix == canonicalUserDataPath) {
                continue; // user dir isn't a "system" prefix
            }
            if (canonicalSource.startsWith(canonicalPrefix + QLatin1Char('/'))) {
                valid = true;
                break;
            }
        }
        if (!valid) {
            qCWarning(lcLayoutLib) << "dropping invalid systemSourcePath" << m_systemSourcePath;
            m_systemSourcePath.clear();
        }
    }

    // Shader support
    m_shaderId = json[::PhosphorZones::ZoneJsonKeys::ShaderId].toString();
    if (json.contains(::PhosphorZones::ZoneJsonKeys::ShaderParams)) {
        m_shaderParams = json[::PhosphorZones::ZoneJsonKeys::ShaderParams].toObject().toVariantMap();
    }

    // Auto-assign
    m_autoAssign = json[::PhosphorZones::ZoneJsonKeys::AutoAssign].toBool(false);

    // Full screen geometry mode
    m_useFullScreenGeometry = json[::PhosphorZones::ZoneJsonKeys::UseFullScreenGeometry].toBool(false);

    // Aspect ratio classification. Two serialized forms are live and
    // fromJsonValue accepts both, clamping an out-of-range int to Any the same
    // way Layout::setAspectRatioClassInt does.
    m_aspectRatioClass =
        ::PhosphorLayout::ScreenClassification::fromJsonValue(json[::PhosphorZones::ZoneJsonKeys::AspectRatioClassKey]);
    m_minAspectRatio = json[::PhosphorZones::ZoneJsonKeys::MinAspectRatio].toDouble(0.0);
    m_maxAspectRatio = json[::PhosphorZones::ZoneJsonKeys::MaxAspectRatio].toDouble(0.0);
    // NaN comparisons always return false, so a non-finite bound silently
    // disables aspect-ratio matching. Drop to the "unbounded" sentinel so the
    // layout still has a deterministic match policy.
    if (!std::isfinite(m_minAspectRatio)) {
        qCWarning(lcLayoutLib) << "Layout::initFromJson: non-finite minAspectRatio for layout" << m_id.toString()
                               << "— treating as unbounded";
        m_minAspectRatio = 0.0;
    }
    if (!std::isfinite(m_maxAspectRatio)) {
        qCWarning(lcLayoutLib) << "Layout::initFromJson: non-finite maxAspectRatio for layout" << m_id.toString()
                               << "— treating as unbounded";
        m_maxAspectRatio = 0.0;
    }

    // Visibility filtering
    m_hiddenFromSelector = json[::PhosphorZones::ZoneJsonKeys::HiddenFromSelector].toBool(false);
    LayoutUtils::deserializeAllowLists(json, m_allowedScreens, m_allowedDesktops, m_allowedActivities);

    // Translate any legacy connector names ("DP-2") in allowedScreens to the
    // application's stable screen identifier ("LG:Model:Serial") if a
    // resolver is installed. Daemon / editor / settings install a resolver
    // that walks QGuiApplication::screens(); headless contexts leave the
    // resolver unset and the strings pass through verbatim. Equality-path
    // matches (same connector on both sides) still work without a resolver.
    // Take a copy (not a reference) — the resolver accessor is guarded
    // internally and returns by value. Copying once here lets us invoke
    // without re-locking per allowedScreens entry.
    if (const auto resolver = Layout::screenIdResolver(); resolver) {
        for (int i = 0; i < m_allowedScreens.size(); ++i) {
            const QString resolved = resolver(m_allowedScreens[i]);
            if (!resolved.isEmpty()) {
                m_allowedScreens[i] = resolved;
            }
        }
    }

    // Route zones through addZone() so zoneAdded / zonesChanged fire
    // naturally during deserialization. addZone respects a pre-set
    // zoneNumber (>0) — the number was read by Zone::fromJson — and
    // only auto-assigns the next slot when the incoming zone lacks one.
    //
    // Wrap the loop in batchModify so the individual per-zone
    // emitModifiedIfNotBatched() calls coalesce into a single
    // layoutModified after construction — the layout was just loaded
    // from disk, not edited, so any listener that wires up after
    // fromJson returns sees a clean-but-populated container.
    const auto zonesArray = json[::PhosphorZones::ZoneJsonKeys::Zones].toArray();
    beginBatchModify();
    for (const auto& zoneValue : zonesArray) {
        auto* zone = Zone::fromJson(zoneValue.toObject(), this);
        addZone(zone);
    }
    endBatchModify();
    // Drop the dirty flag: deserialization isn't a user edit, and a stale
    // dirty flag would trick any future isDirty() probe into saving the
    // just-loaded contents back to disk.
    clearDirty();
}

} // namespace PhosphorZones
