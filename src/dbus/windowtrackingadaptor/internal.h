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
#include "core/interfaces/isettings.h"
#include "core/types/constants.h"
#include "core/utils/utils.h"
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorRules/WindowQuery.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include "core/platform/logging.h"

namespace PlasmaZones {
namespace WindowTrackingInternal {

/// Release @p windowId from @p source (when given and distinct from the
/// destination) and receive it into @p dest, VERIFYING adoption.
/// handoffReceive can silently refuse (screen no longer in the engine's set,
/// no state, duplicate, insert failure), and by then the source has already
/// released — an unguarded release→receive strands the window tracked by NO
/// engine. On refusal this re-homes the window into the source engine on
/// @p recoverScreenId / @p recoverDesktop (when available) so it stays
/// managed; pass the window's SOURCE desktop for a cross-desktop crossing or
/// the recovery lands on the source screen's current desktop instead.
/// Returns true when the destination adopted the window.
///
/// Pre-tracked destination (source == dest is the common shape: a floating
/// window moving between two screens of the SAME mode): isWindowTracked
/// answers true unconditionally there, so adoption is instead verified as
/// "the tracked screen now matches the requested destination".
///
/// PRECONDITION for that pre-tracked test: an engine must record an accepted
/// arrival under a screen id that screensMatch() equates with the ctx.toScreenId
/// it was handed. screensMatch absorbs connector-name / EDID-id spelling and the
/// "/vs:" virtual-screen suffix, but an engine that re-resolved the arrival onto
/// a genuinely DIFFERENT screen id (say, redirecting to a fallback output) would
/// read here as a refusal — this helper would then re-home a window the
/// destination actually adopted. No engine does that today; one that starts to
/// must report the id it stored, not silently redirect.
inline bool guardedHandoff(::PhosphorEngine::IPlacementEngine* source, ::PhosphorEngine::IPlacementEngine* dest,
                           ::PhosphorEngine::IPlacementEngine::HandoffContext ctx, const QString& recoverScreenId,
                           int recoverDesktop = 0)
{
    const bool destPreTracked = dest->isWindowTracked(ctx.windowId);
    // The source's OWN slot, snapshotted before the release wipes it. The
    // incoming ctx.sourceZoneIds describe the DESTINATION landing (empty for
    // a scrolling or autotile target), so a re-home that reused them would
    // hand the source engine no zones — and a snap source receiving a
    // non-floating arrival with no zones lands it as a plain FREE window,
    // i.e. tracked by nobody. Re-homing with the window's real source zones
    // restores it where it was.
    QStringList sourceZoneIds;
    if (source && source != dest) {
        if (const auto captured = source->capturePlacement(ctx.windowId)) {
            sourceZoneIds = captured->slotFor(source->engineId()).zoneIds;
        }
        source->handoffRelease(ctx.windowId);
    }
    dest->handoffReceive(ctx);
    const bool adopted = destPreTracked
        ? ::PhosphorScreens::ScreenIdentity::screensMatch(dest->screenForTrackedWindow(ctx.windowId), ctx.toScreenId)
        : dest->isWindowTracked(ctx.windowId);
    if (adopted) {
        return true;
    }
    qCWarning(lcDbusWindow) << "guardedHandoff:" << dest->engineId() << "refused" << ctx.windowId;
    if (source && source != dest && !recoverScreenId.isEmpty()) {
        ::PhosphorEngine::IPlacementEngine::HandoffContext back = ctx;
        back.toScreenId = recoverScreenId;
        back.toDesktop = recoverDesktop;
        back.fromEngineId = dest->engineId();
        back.sourceZoneIds = sourceZoneIds;
        source->handoffReceive(back);
        if (source->isWindowTracked(ctx.windowId)) {
            qCInfo(lcDbusWindow) << "guardedHandoff: re-homed" << ctx.windowId << "into" << source->engineId() << "on"
                                 << recoverScreenId;
        } else {
            // Double refusal: the window is now tracked by NO engine. Loud,
            // so the log distinguishes recovered from stranded.
            qCWarning(lcDbusWindow) << "guardedHandoff: re-home REFUSED too -" << ctx.windowId
                                    << "is tracked by no engine";
        }
    } else if (!destPreTracked) {
        qCWarning(lcDbusWindow) << "guardedHandoff: no recovery available -" << ctx.windowId
                                << "is tracked by no engine";
    }
    return false;
}

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
// metadata is tracked (the caller falls back to its own default). Shared by the
// RestorePosition and Float resolvers so the metadata→query derivation lives in
// one place. windowClass is not tracked daemon-side (the compositor reports
// appId, which is class-derived), so rules match on appId / title / role / type
// / desktop / pid plus the recorded desktop / activity context; screenId stays
// empty (a window-domain rule does not pin a screen). The extended window
// properties (state flags, geometry, accessory flags, captionNormal) are carried
// straight through from the effect's snapshot (setWindowMetadata's a{sv}), so a
// Float / RestorePosition rule keyed on e.g. IsModal or Width matches the same
// values the effect path resolves live. Placement state (IsFloating / IsSnapped /
// IsTiled / Zone) is deliberately absent: these resolvers run at window-open,
// before any placement exists. A POSITIVE predicate over one of them stays
// inert, which is the design; a NEGATED one would invert and match every
// window, so the admission tests in rules.cpp refuse rules that negate any of
// those four plus WindowClass (unanswerableWindowFields).
//
// @p settings supplies the session colour scheme. It is the caller's INJECTED
// ISettings, not Settings' static, so a test double substitutes a scheme like
// any other setting; a null @p settings leaves the field empty, which the
// resolvers' admission tests already treat as unanswerable.
inline std::optional<PhosphorRules::WindowQuery>
buildRuleQueryForWindow(const QPointer<PhosphorEngine::WindowRegistry>& registry, const QString& windowId,
                        const ISettings* settings)
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
    // Consequence, stated so it is not read as an oversight: for a window whose
    // appId the compositor never reported, `None{AppId Equals X}` inverts and
    // matches. That cannot be closed by a blanket holdout the way
    // unanswerableWindowFields closes WindowClass and the placement fields,
    // because appId IS answerable for every other window and excluding
    // appId-negating rules outright would drop the common `everything except
    // this app` rule. The effect-side builder has the identical shape, so the
    // two paths at least agree.
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
    // Colour scheme is session-wide (not window metadata), read through the
    // injected settings so a `ColorScheme == dark` clause composes with any
    // window predicate on the daemon open path. The EFFECT-side query leaves
    // it unstamped and its admission filter holds such rules out there
    // (effectNeverStampedFields), so pairing ColorScheme with an
    // effect-consumed action is inert by design; the daemon-resolved actions
    // honour it.
    //
    // The token is EMPTY in a process with no GUI application (and off the GUI
    // thread), which engages the field with a value no leaf can match — inert
    // for a positive leaf, INVERTING for a negated one. rules.cpp's admitWith
    // binds the ColorScheme negation guard on exactly that condition, so an
    // empty token here is handled rather than merely tolerated.
    if (settings) {
        query.colorScheme = settings->colorSchemeToken();
    }
    // Screen-derived context fields (ScreenId, Mode, ScreenOrientation, ActiveLayout)
    // are intentionally NOT stamped here: the window metadata does not carry the
    // window's screen geometry / active layout, and this daemon-side query feeds the
    // open-path Float / Restore / placement resolvers only. The effect's live
    // per-window query (ruleQueryFor) stamps ScreenId / Mode / ScreenOrientation, so
    // a rule pairing one of those with a window property resolves there but not on
    // this path. Callers that DO know more pin what they know on top of the
    // query this builds. Stamping the screen-derived trio (ScreenId,
    // ActiveLayout, ScreenOrientation — via stampScreenContext):
    // placementZonesByRule, applyOpenDesktopRouting, applyOpenScreenRouting,
    // applyOpenRoutingForTiling. Stamping the trio AND the derived Mode:
    // shouldFloatByRule,
    // scrollOpenRuleParams, shouldRestoreSizeOnUnsnap and
    // shouldUnfloatFallbackToZone, all four of which resolve UNCACHED for that
    // reason (resolveCached is keyed on windowId and rule revision alone, so a
    // hit discards the freshly stamped query). Each is documented at its own
    // site.
    //
    // KNOWN GAP, stated so it is not mistaken for a deliberate design: no
    // resolveCached-path resolver stamps Mode. There are SIX resolveCached
    // callers in rules.cpp — four stamp ScreenId (placementZonesByRule,
    // applyOpenScreenRouting, applyOpenDesktopRouting,
    // applyOpenRoutingForTiling) and two stamp nothing at all
    // (shouldRestoreFloatedPosition, shouldRestoreToZoneOnLogin), relying on
    // the ordering invariant that a stamper seeds the memo first. So a
    // user-authored rule pairing `Mode == "scrolling"` (or tiling/snapping)
    // with SnapToZone, RouteToScreen or RouteToDesktop cannot resolve
    // correctly on the open path, even though the rules editor offers exactly
    // that pairing. Closing the gap for the cached six means giving up their
    // shared memo.
    //
    // BOTH POLARITIES FAIL, and they fail differently. A POSITIVE leaf on an
    // unstamped field (`Mode Equals scrolling`) reads the engaged empty string
    // and evaluates FALSE, so the rule is silently inert — it never fires. A
    // NEGATED leaf (`None{Mode Equals scrolling}`, `None{ScreenId Equals X}`)
    // matches precisely BECAUSE its inner leaf failed, so it INVERTS and the
    // rule fires for EVERY window — the far worse half, and one the editor
    // lets users author as a none-group. Neither is left to the empty-value
    // coincidence: each resolver in rules.cpp passes a structural admission
    // test (admitScreenStamped / admitScreenAndModeStamped /
    // admitNothingStamped, bound through admitWith) that drops any rule
    // referencing a field that resolver does not stamp, mirroring the
    // zones-layer predicate at seven sites in
    // layoutregistry_contextresolve.cpp. The gap above is therefore about
    // which rules can WIN, not about a negated rule wrongly firing.
    //
    // Two of those exclusions are NEGATION-scoped rather than blanket, because
    // the field is answerable for most windows and a blanket exclusion would
    // drop legitimate rules: the five window fields this builder cannot answer
    // (rules.cpp's unanswerableWindowFields) and ColorScheme when the process
    // has no palette to classify (rules.cpp's admitWith).
    //
    // ActiveLayout resolves through the ONE definition every producer shares
    // (assignmentIdForScreen for the screen's current desktop and activity —
    // the id the windowless context cascade stamps and the daemon publishes
    // to the KWin effect), so an `ActiveLayout Equals <uuid>` leaf means the
    // same thing on a context rule, a daemon-resolved window rule, and an
    // effect-resolved appearance rule. Extended
    // properties: an optional→optional copy preserves engagement exactly, so
    // a field the effect could not observe stays disengaged and inert here
    // too.
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

/// Keys of the QVariantMap seam between scrollOpenRuleParams (rules.cpp,
/// producer) and the setOpenParamsResolver lambda (enginewiring.cpp,
/// consumer). One namespace so a typo on either side is a compile error,
/// not a silently dropped rule.
namespace ScrollOpenKeys {
inline QString widthFraction()
{
    return QStringLiteral("widthFraction");
}
inline QString tabbed()
{
    return QStringLiteral("tabbed");
}
inline QString consume()
{
    return QStringLiteral("consume");
}
inline QString heightFraction()
{
    return QStringLiteral("heightFraction");
}
inline QString maximized()
{
    return QStringLiteral("maximized");
}
inline QString focused()
{
    return QStringLiteral("focused");
}
} // namespace ScrollOpenKeys

/// Keys of the per-window colour-override maps: tabColorRuleParams (consumed by
/// the daemon's scroll tab-strip enrichment) and dropIndicatorRuleParams
/// (consumed by the overlay service's drop-indicator slot). Same rationale as
/// ScrollOpenKeys above — the seam was five hand-repeated literals across four
/// files, where a typo silently drops an override instead of failing to build.
///
/// These are also the QML property names the overlay items expose, so the
/// producer, the layering step and the property write all have to agree on the
/// same spelling; that is exactly what makes one home worth having.
namespace WindowColorKeys {
inline QString activeColor()
{
    return QStringLiteral("activeColor");
}
inline QString inactiveColor()
{
    return QStringLiteral("inactiveColor");
}
inline QString urgentColor()
{
    return QStringLiteral("urgentColor");
}
inline QString indicatorColor()
{
    return QStringLiteral("indicatorColor");
}
inline QString indicatorBorderColor()
{
    return QStringLiteral("indicatorBorderColor");
}
} // namespace WindowColorKeys

} // namespace PlasmaZones
