// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Effect-local shims for the per-window animation cascade on top of
// PhosphorRules::RuleEvaluator. They walk the event-scoped slots
// (`anim-shader:`, `anim-timing:`, `anim-curve:`) filled by Rules
// carrying OverrideAnimation* actions, with the duration clamp, the curve
// `tryCreate` fallback, the engaged-empty `effectId` sentinel, and the
// empty-input short-circuits localised here so the evaluator stays generic.
//
// SPDX is GPL-3 because this TU lives in the effect tree, but it deliberately
// touches no KWin type — it operates purely on the LGPL rule engine and
// animation library value types, so it could be promoted to an LGPL library
// in the future without source changes. Per-window context is threaded in
// via PhosphorRules::WindowQuery built KWin-side by
// `ruleQueryFor()` (window_query.h).

#include "shader_resolve.h"

#include <PhosphorAnimation/AnimationLimits.h>
#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/RuleEvaluator.h>
#include <PhosphorRules/WindowQuery.h>

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QtGlobal>

namespace PlasmaZones {

namespace {

// OverrideAnimation* action param keys consumed by the resolvers below come
// from the shared PhosphorRules::ActionParam vocabulary so the resolver,
// the action-registry validators in ruleaction.cpp, the rule-editor UI, and
// the v3→v4 migration in configmigration.cpp::buildAnimationAppRule all read
// from one source of truth. A future rename of any of these wire keys flows
// to every consumer in a single edit.
namespace ActionParam = PhosphorRules::ActionParam;

/// The event-scoped shader slot id for @p eventPath. Concatenation through
/// the `QLatin1StringView + QString` overload allocates a single QString
/// directly rather than materialising the prefix into its own QString first.
QString shaderSlotFor(const QString& eventPath)
{
    return PhosphorRules::ActionSlot::AnimShaderPrefix + eventPath;
}

/// The event-scoped timing slot id for @p eventPath.
QString timingSlotFor(const QString& eventPath)
{
    return PhosphorRules::ActionSlot::AnimTimingPrefix + eventPath;
}

/// The event-scoped curve-override slot id for @p eventPath. Curve and timing
/// (duration) are independent overrides — a rule can fill one without the
/// other so the user can change just the easing or just the duration.
QString curveSlotFor(const QString& eventPath)
{
    return PhosphorRules::ActionSlot::AnimCurvePrefix + eventPath;
}

} // namespace

ResolvedShaderProfile resolveAnimationShaderProfile(const PhosphorRules::RuleEvaluator& evaluator,
                                                    const PhosphorAnimationShaders::ShaderProfileTree& tree,
                                                    const QString& windowId, const PhosphorRules::WindowQuery& query,
                                                    const QString& eventPath)
{
    // Empty-input short-circuit — preserves the standalone resolvers' header
    // contract for both a windowless query and an empty eventPath, and
    // avoids consuming a cache slot for an evaluator walk that cannot match
    // anything (no window attribute can satisfy any rule predicate).
    if (!query.hasWindow() || eventPath.isEmpty()) {
        return ResolvedShaderProfile{tree.resolve(eventPath)};
    }
    // ONE cached evaluator walk feeds both slot lookups. The historical
    // pair of standalone shader-profile + duration resolvers did two
    // independent `resolve()` walks per shader event — same query, same
    // priority-order traversal, both bypassing the per-window match
    // cache. `resolveCached` keyed by the composite windowId reuses the
    // result across both slot reads AND across subsequent shader events
    // for the same window until the rule set's revision changes.
    const PhosphorRules::ResolvedActions resolved =
        windowId.isEmpty() ? evaluator.resolve(query) : evaluator.resolveCached(windowId, query);

    // Shader slot: rule wins verbatim (engaged-empty effectId is the
    // user's "block tree fallthrough for this app/event" sentinel and
    // is preserved by ShaderProfile's own contract). Unfilled slot →
    // fall through to the per-event tree.
    PhosphorAnimationShaders::ShaderProfile profile;
    bool shaderSlotFromRule = false;
    if (const auto action = resolved.slot(shaderSlotFor(eventPath))) {
        profile.effectId = action->params.value(ActionParam::EffectId).toString();
        profile.parameters = action->params.value(ActionParam::Params).toObject().toVariantMap();
        shaderSlotFromRule = true;
    } else {
        profile = tree.resolve(eventPath);
    }

    // The Rule TIMING slot is deliberately not read here — resolveEventMotionProfile
    // owns it, reads it once, and clamps it once for both the animator and shader
    // legs of the same rule. Reading it again here (as this used to) worked only
    // because both sites spelled an identical qBound; change one envelope and the
    // two legs of one user-facing rule would silently run different durations.
    return ResolvedShaderProfile{std::move(profile), shaderSlotFromRule};
}

PhosphorAnimation::Profile resolveAnimationMotionProfile(const PhosphorRules::RuleEvaluator& evaluator,
                                                         const PhosphorAnimation::Profile& base,
                                                         const PhosphorRules::WindowQuery& query,
                                                         const QString& eventPath, const QString& windowId,
                                                         const PhosphorAnimation::CurveRegistry& curveRegistry)
{
    if (!query.hasWindow() || eventPath.isEmpty()) {
        return base;
    }
    // Use the cached path so motion + shader + opacity lookups all share a
    // single per-window cascade walk. The previous uncached call did its
    // own resolve() against the same query as the sister combined
    // function — wasted work even though motion-profile lookups are
    // per-event, not per-frame.
    const PhosphorRules::ResolvedActions resolved =
        windowId.isEmpty() ? evaluator.resolve(query) : evaluator.resolveCached(windowId, query);
    const auto curveAction = resolved.slot(curveSlotFor(eventPath));
    const auto timingAction = resolved.slot(timingSlotFor(eventPath));
    if (!curveAction && !timingAction) {
        return base;
    }
    PhosphorAnimation::Profile out = base;
    // Curve cascade: prefer the dedicated `anim-curve:` slot. Fall through to
    // the legacy `anim-timing:` slot's curve field so rules authored before
    // the curve/timing split (including those produced by the v3→v4
    // migration in configmigration.cpp::animationAppRuleToRule) still
    // resolve.
    QString curve = curveAction ? curveAction->params.value(ActionParam::Curve).toString() : QString();
    if (curve.isEmpty() && timingAction) {
        curve = timingAction->params.value(ActionParam::Curve).toString();
    }
    if (!curve.isEmpty()) {
        // tryCreate (NOT create) — a malformed curve string stays on the
        // base curve rather than silently coercing to OutCubic and swapping
        // out the user's configured global curve.
        if (auto compiled = curveRegistry.tryCreate(curve)) {
            out.curve = std::move(compiled);
        }
    }
    if (timingAction) {
        const int durationMs = timingAction->params.value(ActionParam::DurationMs).toInt(0);
        if (durationMs > 0) {
            // The ONE place the Rule timing slot is READ. Both legs of a
            // user-facing timing rule — the animator's geometry animation and the
            // shader transition — take their duration from this result, so they
            // cannot drift apart. The clamp here bounds the rule's own value;
            // resolveEventMotionProfile re-clamps the MERGED profile afterwards
            // (idempotent for this path — that clamp exists for the motion-tree
            // path, which reaches it unbounded).
            out.duration = static_cast<qreal>(qBound(PhosphorAnimation::Limits::MinAnimationDurationMs, durationMs,
                                                     PhosphorAnimation::Limits::MaxAnimationDurationMs));
        }
    }
    return out;
}

std::optional<qreal> resolveWindowOpacity(const PhosphorRules::ResolvedActions& resolved)
{
    // The opacity-slot id is a constant; hoist its QString materialisation
    // out of the per-paint hot path so each `resolveWindowOpacity` call
    // reuses the same heap allocation. `static const` is thread-safe under
    // C++11+ [stmt.dcl] without further synchronisation.
    static const QString kOpacitySlot = QString(PhosphorRules::ActionSlot::Opacity);
    const auto action = resolved.slot(kOpacitySlot);
    if (!action) {
        return std::nullopt;
    }
    // The action descriptor's validator in ruleaction.cpp already rejected
    // rules whose `value` falls outside [0.0, 1.0] at load time, but a
    // future loader path (legacy migration, hand-edited JSON) could land an
    // out-of-range value here. Validate at the consumer too — defence in
    // depth keeps the paint pipeline from setting a negative opacity (which
    // KWin renders as "invisible window the user can still focus through").
    //
    // Gate on the QVariant conversion path because the QJsonObject may
    // have been populated programmatically (rule editor builder, future
    // migration porter) with a `QVariant(int)` or numeric-string payload
    // that round-trips through `QJsonValue` as a non-Double type. JSON-
    // parsed numeric literals all surface as `QJsonValue::Double` (Qt6
    // collapses int/float at parse time), but construction paths that
    // bypass the parser don't.
    //
    // Extraction MUST go through `toVariant().toDouble(&ok)` rather than
    // `raw.toDouble()` — the latter is the QJsonValue method, which
    // returns 0.0 for any non-Double JSON type. A `"value": "0.5"`
    // payload would pass the canConvert gate but `raw.toDouble()` would
    // return 0.0, silently rendering the window completely invisible.
    const QJsonValue raw = action->params.value(PhosphorRules::ActionParam::Value);
    if (raw.isNull() || raw.isUndefined()) {
        return std::nullopt;
    }
    // QVariant::toDouble on a bool returns 1.0/0.0 with ok=true — surfacing
    // a programmatically-built `"value": true` rule as a fully-visible /
    // completely-invisible window. The action descriptor's load-time
    // validator already rejects bools (it requires `isDouble`); reject
    // them at the consumer too so a runtime-constructed payload that
    // bypasses the parser can't silently change opacity behaviour.
    const QVariant v = raw.toVariant();
    if (v.typeId() == QMetaType::Bool) {
        return std::nullopt;
    }
    bool ok = false;
    const double value = v.toDouble(&ok);
    if (!ok) {
        return std::nullopt;
    }
    if (value < 0.0 || value > 1.0) {
        return std::nullopt;
    }
    return value;
}

std::optional<ResolvedWindowAppearance> resolveWindowAppearance(const PhosphorRules::ResolvedActions& resolved,
                                                                const QColor& accentColor, const QColor& inactiveColor)
{
    // Each reader re-validates the param type even though the load-time
    // descriptor validators already did — defence in depth against a
    // programmatically-built / hand-edited payload that bypassed the parser
    // (see the equivalent rationale in resolveWindowOpacity).
    const auto boolSlot = [&resolved](QLatin1StringView slot) -> std::optional<bool> {
        const auto action = resolved.slot(QString(slot));
        if (!action) {
            return std::nullopt;
        }
        const QJsonValue v = action->params.value(PhosphorRules::ActionParam::Value);
        if (!v.isBool()) {
            return std::nullopt;
        }
        return v.toBool();
    };
    // Upper bounds are the SHARED PhosphorRules::MaxBorderWidth / MaxBorderRadius
    // constants that the load-time descriptor validators (ruleaction.cpp) also
    // use, so this consumer re-validation is genuinely symmetric — a
    // programmatically-built / hand-edited payload with width:5000 is rejected
    // here, not drawn — and the two boundaries can never silently drift.
    const auto intSlot = [&resolved](QLatin1StringView slot, double maxValue) -> std::optional<int> {
        const auto action = resolved.slot(QString(slot));
        if (!action) {
            return std::nullopt;
        }
        const QJsonValue v = action->params.value(PhosphorRules::ActionParam::Value);
        if (!v.isDouble()) {
            return std::nullopt;
        }
        const double d = v.toDouble();
        if (d < 0.0 || d > maxValue) {
            return std::nullopt;
        }
        return static_cast<int>(d);
    };
    // The focused and unfocused colours live on two separate single-colour slots
    // (SetBorderColorActive / SetBorderColorInactive), each carrying its colour in
    // the `value` param. The accent sentinel resolves to the matching system
    // colour passed in @p systemColor (the accent/highlight for the active slot,
    // the inactive colour for the inactive slot); anything else parses as a hex
    // colour.
    const auto borderColorSlot = [&resolved](QLatin1StringView slot,
                                             const QColor& systemColor) -> std::optional<QColor> {
        const auto action = resolved.slot(QString(slot));
        if (!action) {
            return std::nullopt;
        }
        const QJsonValue v = action->params.value(PhosphorRules::ActionParam::Value);
        if (!v.isString()) {
            return std::nullopt;
        }
        const QString s = v.toString();
        if (s == PhosphorRules::BorderColorToken::Accent) {
            return systemColor.isValid() ? std::optional<QColor>(systemColor) : std::nullopt;
        }
        const QColor color(s);
        if (!color.isValid()) {
            return std::nullopt;
        }
        return color;
    };

    // [0.0, 1.0] double slot (SetTintStrength). Same defence-in-depth re-read
    // as the other slots; the range mirrors the load-time validator.
    const auto unitDoubleSlot = [&resolved](QLatin1StringView slot) -> std::optional<double> {
        const auto action = resolved.slot(QString(slot));
        if (!action) {
            return std::nullopt;
        }
        const QJsonValue v = action->params.value(PhosphorRules::ActionParam::Value);
        if (!v.isDouble()) {
            return std::nullopt;
        }
        const double d = v.toDouble();
        if (d < 0.0 || d > 1.0) {
            return std::nullopt;
        }
        return d;
    };

    ResolvedWindowAppearance out;
    out.hideTitleBar = boolSlot(PhosphorRules::ActionSlot::HideTitleBar);
    out.showBorder = boolSlot(PhosphorRules::ActionSlot::BorderVisible);
    out.borderWidth = intSlot(PhosphorRules::ActionSlot::BorderWidth, PhosphorRules::MaxBorderWidth);
    out.borderRadius = intSlot(PhosphorRules::ActionSlot::BorderRadius, PhosphorRules::MaxBorderRadius);
    out.activeColor = borderColorSlot(PhosphorRules::ActionSlot::BorderColorActive, accentColor);
    out.inactiveColor = borderColorSlot(PhosphorRules::ActionSlot::BorderColorInactive, inactiveColor);
    // Plain opacity+tint layer slots. The tint colour's accent sentinel
    // resolves to the system accent (focus-independent, so the active colour
    // is the right system fallback). `out.opacity` stays unset here — it is
    // config-only, filled by resolveEffectiveWindowAppearance.
    out.showOpacityTint = boolSlot(PhosphorRules::ActionSlot::OpacityTintVisible);
    out.tintStrength = unitDoubleSlot(PhosphorRules::ActionSlot::TintStrength);
    out.tintColor = borderColorSlot(PhosphorRules::ActionSlot::TintColor, accentColor);
    // An omitted `inactive` mirrors the active colour, matching the retired
    // global behaviour where a window with no distinct inactive colour kept its
    // active border when unfocused.
    if (!out.inactiveColor) {
        out.inactiveColor = out.activeColor;
    }
    if (!out.any()) {
        return std::nullopt;
    }
    return out;
}

std::optional<ResolvedDecorationChain> resolveDecorationChain(const PhosphorRules::ResolvedActions& resolved)
{
    // Constant slot id — same static-hoist rationale as resolveWindowOpacity.
    static const QString kChainSlot = QString(PhosphorRules::ActionSlot::DecorationChain);
    const auto action = resolved.slot(kChainSlot);
    if (!action) {
        return std::nullopt;
    }
    // The load-time validator guarantees `chain` is present and an array;
    // re-check here (defence in depth against programmatic construction
    // paths) and drop non-string entries plus the reserved config/rule-owned
    // ids: "border" (SetBorderVisible governs it) and "opacity-tint"
    // (SetOpacityTintVisible) — a hand-edited rule must not inject a second
    // plain layer into the fold, nor flip the window into custom mode with a
    // reserved pack running on baked defaults.
    const QJsonValue chainVal = action->params.value(PhosphorRules::ActionParam::Chain);
    if (!chainVal.isArray()) {
        return std::nullopt;
    }
    ResolvedDecorationChain out;
    const QJsonArray arr = chainVal.toArray();
    for (const QJsonValue& entry : arr) {
        const QString id = entry.toString();
        if (id.isEmpty() || id == QLatin1String("border") || id == QLatin1String("opacity-tint")) {
            continue;
        }
        out.chain.append(id);
    }
    // Optional per-pack params override, same nested-object shape the tree
    // profile uses ({packId -> {paramId -> value}}); mirrors how the
    // animation override reads ActionParam::Params in
    // resolveAnimationShaderProfile. Drop the reserved ids for symmetry with
    // the chain filter above — the plain layers' params come from the
    // resolved appearance (SetBorder* / SetOpacity / SetTint*), never the
    // chain.
    out.params = action->params.value(PhosphorRules::ActionParam::Params).toObject().toVariantMap();
    out.params.remove(QStringLiteral("border"));
    out.params.remove(QStringLiteral("opacity-tint"));
    return out;
}

std::optional<QString> resolveWindowLayer(const PhosphorRules::ResolvedActions& resolved)
{
    // Constant slot id — same static-hoist rationale as resolveWindowOpacity.
    static const QString kLayerSlot = QString(PhosphorRules::ActionSlot::WindowLayer);
    const auto action = resolved.slot(kLayerSlot);
    if (!action) {
        return std::nullopt;
    }
    // Re-validate against the closed vocabulary even though the load-time
    // descriptor validator already did — defence in depth against a
    // programmatically-built / hand-edited payload that bypassed the parser
    // (see the equivalent rationale in resolveWindowOpacity). An unknown token
    // must resolve to "no override", never to an implicit Normal (which would
    // clear a user's own keepAbove).
    const QJsonValue v = action->params.value(PhosphorRules::ActionParam::Value);
    if (!v.isString()) {
        return std::nullopt;
    }
    const QString token = v.toString();
    if (token != PhosphorRules::WindowLayerToken::Above && token != PhosphorRules::WindowLayerToken::Normal
        && token != PhosphorRules::WindowLayerToken::Below) {
        return std::nullopt;
    }
    return token;
}

} // namespace PlasmaZones
