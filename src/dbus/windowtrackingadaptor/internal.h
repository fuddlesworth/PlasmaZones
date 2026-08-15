// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QRect>
#include <QHash>
#include <QString>
#include <QPointer>
#include <optional>
#include "core/types/constants.h"
#include "core/utils/utils.h"
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorRules/WindowQuery.h>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {
namespace WindowTrackingInternal {

inline QJsonArray toJsonArray(const QStringList& list)
{
    QJsonArray arr;
    for (const QString& s : list) {
        arr.append(s);
    }
    return arr;
}

inline QJsonObject rectToJsonObject(const QRect& rect)
{
    QJsonObject obj;
    obj[::PhosphorZones::ZoneJsonKeys::X] = rect.x();
    obj[::PhosphorZones::ZoneJsonKeys::Y] = rect.y();
    obj[::PhosphorZones::ZoneJsonKeys::Width] = rect.width();
    obj[::PhosphorZones::ZoneJsonKeys::Height] = rect.height();
    return obj;
}

} // namespace WindowTrackingInternal

// Build a per-window rule query from the registry metadata, or nullopt when no
// metadata is tracked (the caller falls back to its own default). Never call this
// directly — every daemon-side per-window resolver goes through
// WindowTrackingAdaptor::buildContextualRuleQuery, which wraps this and adds the
// screen-derived context fields (see below).
//
// Rules match on appId / title / role / type / desktop / pid plus the recorded
// desktop / activity context. The extended window properties (state flags,
// geometry, accessory flags, captionNormal) are carried straight through from the
// effect's snapshot (setWindowMetadata's a{sv}), so a Float / RestorePosition rule
// keyed on e.g. IsModal or Width matches the same values the effect path resolves
// live.
//
// Two known-dead field pairings, both pre-existing and neither closable here:
//
//   * windowClass is not tracked daemon-side (the compositor reports appId, which
//     is class-derived) and setWindowMetadata's wire carries no windowClass field.
//     The effect stamps it, but the effect resolves only APPEARANCE actions, so a
//     `WindowClass ⇒ Float` rule matches on neither side. Closing it needs a new
//     wire field, or the rule builder must stop offering WindowClass alongside
//     window-domain open-path actions.
//
//   * Placement state (IsFloating / IsSnapped / IsTiled / Zone) is absent because
//     these resolvers were written for window-open, before any placement exists.
//     That premise no longer holds for all of them: shouldRestoreSizeOnUnsnap
//     fires mid-session on every unsnap, and the snap engine's exclusion-query
//     provider is consulted per snap attempt, both at moments when the placement
//     IS known. So an Exclude or SetRestoreSizeOnUnsnap rule carrying a placement
//     leaf never matches. These fields are optional, so it fails closed (inert)
//     rather than matching spuriously — but it fails silently. Threading the
//     placement state into the mid-session callers is a design change, not a
//     stamping fix.
inline std::optional<PhosphorRules::WindowQuery>
buildRuleQueryForWindow(const QPointer<PhosphorEngine::WindowRegistry>& registry, const QString& windowId)
{
    if (registry.isNull()) {
        return std::nullopt;
    }
    // WindowRegistry is keyed by the BARE instance id; the engine hands us the
    // composite `appId|instanceId`. Extract first — every other registry reader
    // (currentAppIdFor, windowClosed, AutotileEngine) does the same. Looking up by
    // the composite id always misses, which would silently make the rule inert.
    const QString instanceId = PhosphorIdentity::WindowId::extractInstanceId(windowId);
    const std::optional<PhosphorEngine::WindowMetadata> meta = registry->metadata(instanceId);
    if (!meta) {
        return std::nullopt;
    }
    PhosphorRules::WindowQuery query;
    // Engage appId only when known, matching the effect-side builder (window_query.cpp)
    // and the sibling string fields below. Engaging an empty appId here would make a
    // degenerate `AppId Equals ""` / negated-appId predicate resolve differently on the
    // daemon open-path than on the effect live-path (WindowQuery::appId is optional, so
    // an unknown appId must stay disengaged per the engage-only-when-known contract).
    if (!meta->appId.isEmpty()) {
        query.appId = meta->appId;
    }
    if (!meta->title.isEmpty()) {
        query.title = meta->title;
    }
    if (!meta->windowRole.isEmpty()) {
        query.windowRole = meta->windowRole;
    }
    if (!meta->desktopFile.isEmpty()) {
        query.desktopFile = meta->desktopFile;
    }
    if (meta->pid > 0) {
        query.pid = meta->pid;
    }
    query.windowType = meta->windowType;
    query.virtualDesktop = meta->virtualDesktop;
    query.activity = meta->activity;
    // The four screen-derived context fields (ScreenId, ActiveLayout,
    // ScreenOrientation, Mode) are not stamped here, because the window metadata
    // carries no screen. All four are filled by the caller-side wrapper
    // WindowTrackingAdaptor::buildContextualRuleQuery, which resolves the window's
    // screen (or takes the open path's hint) and reads that screen's context.
    //
    // They are NOT optional fields: WindowQuery::valueForField returns each of
    // them always-engaged, so leaving one unstamped compares an empty string
    // rather than reporting "no value". `Equals <x>` correctly fails against
    // that, and an empty-Equals leaf cannot be authored (MatchExpression::isValid
    // rejects it), but a NEGATED leaf matches. That is why the wrapper is
    // mandatory rather than merely preferable.
    // Extended properties — optional→optional copy preserves engagement exactly,
    // so a field the effect could not observe stays disengaged and inert here too.
    query.isMinimized = meta->isMinimized;
    query.isFullscreen = meta->isFullscreen;
    query.isSticky = meta->isSticky;
    query.isMaximized = meta->isMaximized;
    query.isFocused = meta->isFocused;
    query.isTransient = meta->isTransient;
    query.isNotification = meta->isNotification;
    query.keepAbove = meta->keepAbove;
    query.keepBelow = meta->keepBelow;
    query.skipTaskbar = meta->skipTaskbar;
    query.skipPager = meta->skipPager;
    query.skipSwitcher = meta->skipSwitcher;
    query.isModal = meta->isModal;
    query.hasDecoration = meta->hasDecoration;
    query.isResizable = meta->isResizable;
    query.isMovable = meta->isMovable;
    query.isMaximizable = meta->isMaximizable;
    query.width = meta->width;
    query.height = meta->height;
    query.positionX = meta->positionX;
    query.positionY = meta->positionY;
    query.captionNormal = meta->captionNormal;
    return query;
}

} // namespace PlasmaZones
