// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorProtocol/AutotileTypes.h>
#include <PhosphorProtocol/BridgeTypes.h>
#include <PhosphorProtocol/DragTypes.h>

#include <QDebug>
#include <QLatin1String>
#include <QLoggingCategory>

namespace PhosphorProtocol {

namespace {
// Wire-format strings for DragBypassReason. Kept as named constants so THIS
// enum's two converters (toWireString / bypassReasonFromWireString) can never
// drift from each other. (Other wire sentinels in this file — REJECTED, the
// stacking directions — are validated inline against literals defined at
// their producer sites; unifying those is a separate, wider change.)
constexpr QLatin1String kBypassAutotileScreen("autotile_screen");
constexpr QLatin1String kBypassSnappingDisabled("snapping_disabled");
constexpr QLatin1String kBypassContextDisabled("context_disabled");
constexpr QLatin1String kBypassLayoutSuppressed("layout_suppressed");

// File-local category sharing the library's "phosphor.protocol" name (same
// name = same filter rules). ClientHelpers' accessor lives in the
// DBus-linked sibling target, which this QtCore-only types target cannot
// reach.
const QLoggingCategory& lcProtocolTypes()
{
    static const QLoggingCategory category("phosphor.protocol", QtInfoMsg);
    return category;
}
} // namespace

QString toWireString(DragBypassReason r)
{
    switch (r) {
    case DragBypassReason::None:
        return {};
    case DragBypassReason::EngineOwnedScreen:
        return kBypassAutotileScreen;
    case DragBypassReason::SnappingDisabled:
        return kBypassSnappingDisabled;
    case DragBypassReason::ContextDisabled:
        return kBypassContextDisabled;
    case DragBypassReason::LayoutSuppressed:
        return kBypassLayoutSuppressed;
    }
    return {};
}

DragBypassReason bypassReasonFromWireString(const QString& s)
{
    if (s.isEmpty()) {
        return DragBypassReason::None;
    }
    if (s == kBypassAutotileScreen) {
        return DragBypassReason::EngineOwnedScreen;
    }
    if (s == kBypassSnappingDisabled) {
        return DragBypassReason::SnappingDisabled;
    }
    if (s == kBypassContextDisabled) {
        return DragBypassReason::ContextDisabled;
    }
    if (s == kBypassLayoutSuppressed) {
        return DragBypassReason::LayoutSuppressed;
    }
    // Categorized and expected under version skew: an old effect .so (not
    // reloaded until logout) decodes any newer reason string through here on
    // every policy carrying it.
    qCWarning(lcProtocolTypes()) << "bypassReasonFromWireString: unknown wire value" << s << "— mapping to None";
    return DragBypassReason::None;
}

QString TileRequestEntry::validationError() const
{
    if (windowId.isEmpty()) {
        return QStringLiteral("TileRequestEntry: empty windowId");
    }
    if (screenId.isEmpty()) {
        return QStringLiteral("TileRequestEntry: empty screenId (windowId=%1)").arg(windowId);
    }
    if (width < 0 || height < 0) {
        return QStringLiteral("TileRequestEntry: negative size (windowId=%1 w=%2 h=%3)")
            .arg(windowId)
            .arg(width)
            .arg(height);
    }
    // Floating tile requests legitimately carry zero size — the plugin
    // resolves geometry from the current frame. Tiled requests must have
    // concrete geometry or the plugin has nothing to apply.
    if (!floating && (width == 0 || height == 0)) {
        return QStringLiteral("TileRequestEntry: tiled request requires non-zero size (windowId=%1)").arg(windowId);
    }
    // stacking is optional (empty = non-overlap layout). A non-empty value
    // must be one of the two declared directions: the effect engages its
    // overlap restack on ANY non-empty value and treats unknown strings as
    // lastOnTop, so a garbled or spoofed value would silently force a
    // restack. Reject it here instead, where every unmarshal site already
    // checks.
    if (!stacking.isEmpty() && stacking != QLatin1String("firstOnTop") && stacking != QLatin1String("lastOnTop")) {
        return QStringLiteral("TileRequestEntry: invalid stacking '%1' (windowId=%2)").arg(stacking, windowId);
    }
    // scrollEdge is optional (empty = not a scrolling placement). Same
    // reasoning as stacking: the effect re-anchors a window's animation origin
    // on any non-empty value, so an unrecognised string would silently move
    // the window's apparent entry side. Reject it at the unmarshal boundary.
    if (!scrollEdge.isEmpty() && scrollEdge != QLatin1String("left") && scrollEdge != QLatin1String("right")) {
        return QStringLiteral("TileRequestEntry: invalid scrollEdge '%1' (windowId=%2)").arg(scrollEdge, windowId);
    }
    // viewDeltaX is deliberately NOT validated here, unlike its neighbours.
    // It is a motion hint, not a placement input: the committed rect stands on
    // its own whatever the delta says, and an absurd value costs one wild slide
    // that rings out to the correct position on its own. Rejecting the entry
    // would instead drop a perfectly good placement over a cosmetic field. The
    // consumer clamps it when it starts the spring.
    return {};
}

QString BridgeRegistrationResult::validationError() const
{
    // REJECTED is a legitimate sentinel the daemon sends when peer version
    // is incompatible. Surface it as valid so the caller can branch on it
    // before calling any other methods on the result.
    if (sessionId == QLatin1String("REJECTED")) {
        return {};
    }
    if (apiVersion.isEmpty()) {
        return QStringLiteral("BridgeRegistrationResult: empty apiVersion");
    }
    bool ok = false;
    apiVersion.toInt(&ok);
    if (!ok) {
        return QStringLiteral("BridgeRegistrationResult: apiVersion not an integer: '%1'").arg(apiVersion);
    }
    if (bridgeName.isEmpty()) {
        return QStringLiteral("BridgeRegistrationResult: empty bridgeName");
    }
    if (sessionId.isEmpty()) {
        return QStringLiteral("BridgeRegistrationResult: empty sessionId");
    }
    return {};
}

QString DragPolicy::validationError() const
{
    // The only strong invariant: an EngineOwnedScreen bypass must carry the
    // owning engine's screen id, because the effect uses it to scope retroactive
    // bypass state and the post-drag float target. Every other bypass reason
    // (SnappingDisabled, ContextDisabled, LayoutSuppressed) may be emitted
    // with an empty screenId when beginDrag was called with an empty
    // startScreenId — the producer code in drag_protocol.cpp deliberately
    // tolerates that.
    if (bypassReason == DragBypassReason::EngineOwnedScreen && screenId.isEmpty()) {
        return QStringLiteral("DragPolicy: EngineOwnedScreen bypass requires non-empty screenId");
    }
    return {};
}

QString DragOutcome::validationError() const
{
    // Wire-boundary range check: the unmarshal path casts a raw int into the
    // typed Action field, so this defends against a peer built at a different
    // revision (or memory corruption) — local misassignment is prevented by
    // the field's type.
    if (action < NoOp || action > NotifyDragOutUnsnap) {
        return QStringLiteral("DragOutcome: action out of range: %1").arg(static_cast<int>(action));
    }

    // windowId is required for every action that does real work.
    if (action != NoOp && windowId.isEmpty()) {
        return QStringLiteral("DragOutcome: windowId required for action=%1").arg(static_cast<int>(action));
    }

    switch (action) {
    case NoOp:
    case CancelSnap:
        // No further cross-field requirements.
        break;
    case ApplyFloat:
        // ApplyFloat carries cursor position; (0,0) is a legitimate drop
        // location at top-left. targetScreenId is optional — the plugin
        // resolves it from the cursor.
        break;
    case ApplySnap:
        if (zoneId.isEmpty()) {
            return QStringLiteral("DragOutcome: ApplySnap requires non-empty zoneId (windowId=%1)").arg(windowId);
        }
        if (width <= 0 || height <= 0) {
            return QStringLiteral("DragOutcome: ApplySnap requires non-zero size (windowId=%1 w=%2 h=%3)")
                .arg(windowId)
                .arg(width)
                .arg(height);
        }
        break;
    case RestoreSize:
        if (width <= 0 || height <= 0) {
            return QStringLiteral("DragOutcome: RestoreSize requires non-zero size (windowId=%1 w=%2 h=%3)")
                .arg(windowId)
                .arg(width)
                .arg(height);
        }
        break;
    case NotifyDragOutUnsnap:
        // windowId (checked above) is the only requirement.
        break;
    }
    return {};
}

QDebug operator<<(QDebug debug, DragBypassReason r)
{
    QDebugStateSaver saver(debug);
    debug.nospace();
    switch (r) {
    case DragBypassReason::None:
        debug << "DragBypassReason::None";
        break;
    case DragBypassReason::EngineOwnedScreen:
        debug << "DragBypassReason::EngineOwnedScreen";
        break;
    case DragBypassReason::SnappingDisabled:
        debug << "DragBypassReason::SnappingDisabled";
        break;
    case DragBypassReason::ContextDisabled:
        debug << "DragBypassReason::ContextDisabled";
        break;
    case DragBypassReason::LayoutSuppressed:
        debug << "DragBypassReason::LayoutSuppressed";
        break;
    }
    return debug;
}

} // namespace PhosphorProtocol
