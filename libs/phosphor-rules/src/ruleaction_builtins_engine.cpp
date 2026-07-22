// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Built-in action descriptor table, engine/window-management/animation/overlay
// half. Split from ruleaction.cpp for file-size; registerBuiltins() calls this
// then registerBuiltinsAppearance() in that order. Shared param validators and
// slot helpers live in ruleaction_builtins_p.h.

#include <PhosphorRules/RuleAction.h>

#include "ruleaction_builtins_p.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

namespace PhosphorRules {

using namespace detail;

void ActionRegistry::registerBuiltinsEngine()
{
    using P = ParamSchema;

    // ── engine-mode slot ──
    // SetEngineMode intentionally validates only that the `mode` param is
    // non-empty — vocabulary validation lives at consumers, NOT at load.
    // Two reasons: (a) the rule-evaluator test suites use arbitrary mode
    // strings as per-rule identifiers (`test_ruleevaluator.cpp` uses
    // "alpha"/"beta"/"gamma"; `test_ruleevaluator_cascade.cpp` uses
    // "window-rule"), so tightening to a closed set would force those
    // tests to re-route through a different action type; (b) the open
    // vocabulary mirrors the bridge's `disableRuleMode` contract — the
    // wire token round-trips verbatim, and the assignment-side consumer
    // `PhosphorZones::RuleHelpers::entryFromRuleMatchActions` maps it
    // via `PhosphorZones::modeFromWireString`, which returns nullopt
    // for unknown tokens and leaves the entry on its Snapping default.
    // DisableEngine is the asymmetric case below: it gates on the
    // closed set to fail malformed disable rules early at load
    // (per-mode disable lists never use synthetic identifiers).
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetEngineMode),
        .slotFor = constantSlot(ActionSlot::EngineMode),
        .validate =
            [](const QJsonObject& p) {
                return hasNonEmptyString(p, ActionParam::Mode);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Mode)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Mode),
                     .kind = QStringLiteral("enum"),
                     .enumWireValues = engineModeOptions()}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 0,
        .tags = {QString(Tag::LayoutEngine)},
    });

    // ── layout slot — both layout-shaping actions share it ──
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetSnappingLayout),
        .slotFor = constantSlot(ActionSlot::Layout),
        .validate =
            [](const QJsonObject& p) {
                return hasNonEmptyString(p, ActionParam::LayoutId);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::LayoutId)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::LayoutId), .kind = QStringLiteral("snappingLayout")}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 1,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetTilingAlgorithm),
        .slotFor = constantSlot(ActionSlot::Layout),
        .validate =
            [](const QJsonObject& p) {
                return hasNonEmptyString(p, ActionParam::Algorithm);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Algorithm)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Algorithm), .kind = QStringLiteral("tilingAlgorithm")}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 2,
        .tags = {QString(Tag::LayoutEngine)},
    });

    // ── engine-enable slot ──
    // `mode` records which engine the rule disables. The recognised tokens
    // are the wire vocabulary `PhosphorZones::modeFromWireString` accepts —
    // listed verbatim here because the LGPL PhosphorRules lib does not
    // depend on PhosphorZones, and depending on it just for the string
    // vocabulary would couple the two libs over a stable wire format. New
    // tokens added here MUST mirror the Mode enum extension in
    // libs/phosphor-zones/include/PhosphorZones/AssignmentEntry.h; the
    // round-trip tests pin the contract.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::DisableEngine),
        .slotFor = constantSlot(ActionSlot::EngineEnable),
        .validate =
            [](const QJsonObject& p) {
                return engineModeOptions().contains(p.value(ActionParam::Mode).toString());
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Mode)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Mode),
                     .kind = QStringLiteral("enum"),
                     .enumWireValues = engineModeOptions()}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 3,
        .tags = {QString(Tag::LayoutEngine)},
    });

    // ── locked slot — context-domain layout lock ──
    // A matched context rule pins the active layout for its screen/desktop/
    // activity so it can't be switched, mirroring the manual ToggleLayoutLock
    // shortcut. Boolean `value`: true locks (the meaningful default for a
    // freshly-authored action, hence `defaultDisplay = 1.0`); false fills the
    // Locked slot with not-locked — a no-op against the manual lock store (the
    // daemon ORs, so it never unlocks a manual lock), but as a single-winner
    // slot a higher-priority false rule overrides a lower-priority true one.
    // Mode-agnostic — the rule query ignores the Mode
    // axis, so the same lock surfaces for whichever engine mode is asked — and
    // live-resolved: the daemon ORs it with (never replaces) the manual
    // ToggleLayoutLock store, so rule locks and manual toggles do not fight.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::LockContext),
        .slotFor = constantSlot(ActionSlot::Locked),
        .validate =
            [](const QJsonObject& p) {
                return hasBool(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("bool"), .defaultDisplay = 1.0}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 4,
        .tags = {QString(Tag::LayoutEngine)},
    });

    // ── default-assignment slot — context-domain override of the global
    //    "suppress default layout assignment" setting. A matched context rule
    //    flips the synthesized level-1 default for its screen/desktop/activity:
    //    value == false suppresses it (no engine activates until the user
    //    explicitly assigns one), value == true forces it through even when the
    //    global suppress setting is on. Mode-agnostic (the level-1 default is a
    //    single mode-carrying AssignmentEntry) and live-resolved per-slot at
    //    cascade-miss by LayoutRegistry::resolveContextDefaultAssignment —
    //    carries no SetEngineMode action, so it never wins the single-rule
    //    assignment cascade. Seeds FALSE: the global setting defaults OFF (every
    //    context gets a default), so the only meaningful per-context rule a user
    //    adds is "suppress on this monitor" — defaultDisplay 0.0 lands the fresh
    //    action on that value.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::DefaultLayoutAssignment),
        .slotFor = constantSlot(ActionSlot::DefaultAssignment),
        .validate =
            [](const QJsonObject& p) {
                return hasBool(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("bool"), .defaultDisplay = 0.0}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 5,
        .tags = {QString(Tag::LayoutEngine)},
    });

    // ── manage slot — terminal. Exclude is intentionally free-form: an empty
    //    `allowedKeys` opts out of the strict-key check so a future Exclude
    //    reason/scope param can be added without a schema bump. ──
    registerAction(ActionDescriptor{
        .type = QString(ActionType::Exclude),
        .slotFor = constantSlot(ActionSlot::Manage),
        .validate = &acceptAny,
        .terminal = true,
        .allowedKeys = {},
        .domain = ActionDomain::Window,
        .category = QStringLiteral("windowManagement"),
        .displayOrder = 0,
    });

    // ── float slot — intentionally free-form (future float-geometry hints);
    //    empty `allowedKeys` opts out of the strict-key check. ──
    registerAction(ActionDescriptor{
        .type = QString(ActionType::Float),
        .slotFor = constantSlot(ActionSlot::Float),
        .validate = &acceptAny,
        .terminal = false,
        .allowedKeys = {},
        .domain = ActionDomain::Window,
        .category = QStringLiteral("windowManagement"),
        .displayOrder = 1,
    });

    registerAction(ActionDescriptor{
        .type = QString(ActionType::SnapToZone),
        .slotFor = constantSlot(ActionSlot::Placement),
        .validate =
            [](const QJsonObject& p) {
                // A non-empty array of positive integer (1-based) zone ordinals.
                const QJsonValue v = p.value(ActionParam::Zones);
                if (!v.isArray()) {
                    return false;
                }
                const QJsonArray arr = v.toArray();
                if (arr.isEmpty()) {
                    return false;
                }
                for (const QJsonValue& e : arr) {
                    if (!e.isDouble()) {
                        return false;
                    }
                    const double d = e.toDouble();
                    // Bound on the DOUBLE before narrowing — a float-to-int cast
                    // out of int's range is undefined behaviour. Reject < 1
                    // (ordinals are 1-based) and an absurd upper value first; only
                    // then is the cast for the integrality check well-defined.
                    if (d < 1.0 || d > MaxZoneOrdinal) {
                        return false;
                    }
                    if (static_cast<double>(static_cast<int>(d)) != d) {
                        return false; // non-integral
                    }
                }
                return true;
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Zones)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Zones), .kind = QStringLiteral("zoneOrdinals")}},
        // No tags: SnapToZone is daemon-placement only (consumed by the SnapEngine
        // open path), not an Effect / Border / Animation / Overlay action.
        .category = QStringLiteral("windowManagement"),
        .displayOrder = 2,
    });

    // ── open-routing: send a matched window to a target monitor / desktop ──
    registerAction(ActionDescriptor{
        .type = QString(ActionType::RouteToScreen),
        .slotFor = constantSlot(ActionSlot::RouteScreen),
        .validate =
            [](const QJsonObject& p) {
                // A non-empty canonical screen id (physical or virtual). The id
                // form is not validated against live screen state here — this lib
                // has no view of connected outputs, and a rule targeting a
                // currently-absent monitor is legitimate (it fires when that
                // monitor returns). The daemon's placement path no-ops a route
                // whose target screen is not currently resolvable.
                return hasNonEmptyString(p, ActionParam::TargetScreenId);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::TargetScreenId)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::TargetScreenId), .kind = QStringLiteral("screenId")}},
        // No tags: RouteToScreen is daemon open-path routing only, not an
        // Effect / Border / Animation / Overlay / Gap action.
        .category = QStringLiteral("windowManagement"),
        .displayOrder = 3,
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::RouteToDesktop),
        .slotFor = constantSlot(ActionSlot::RouteDesktop),
        .validate =
            [](const QJsonObject& p) {
                // A single 1-based virtual-desktop number. Bound the DOUBLE before
                // narrowing (a float-to-int cast out of int's range is UB), then
                // require integrality — mirrors the SnapToZone ordinal check.
                const QJsonValue v = p.value(ActionParam::TargetDesktop);
                if (!v.isDouble()) {
                    return false;
                }
                const double d = v.toDouble();
                if (d < 1.0 || d > MaxVirtualDesktopOrdinal) {
                    return false;
                }
                return static_cast<double>(static_cast<int>(d)) == d;
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::TargetDesktop)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::TargetDesktop),
                     .kind = QStringLiteral("virtualDesktop"),
                     .min = 1.0,
                     .max = static_cast<double>(MaxVirtualDesktopOrdinal)}},
        .category = QStringLiteral("windowManagement"),
        .displayOrder = 4,
    });

    // ── animation slots — event-scoped: "anim-shader:<event>" ──
    registerAction(ActionDescriptor{
        .type = QString(ActionType::OverrideAnimationShader),
        .slotFor = [](const QJsonObject& p) -> QString {
            const QString event = p.value(ActionParam::Event).toString();
            if (event.isEmpty()) {
                return QString();
            }
            return QString(ActionSlot::AnimShaderPrefix) + event;
        },
        .validate =
            [](const QJsonObject& p) {
                return hasNonEmptyString(p, ActionParam::Event);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Event), QString(ActionParam::EffectId), QString(ActionParam::Params)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Event), .kind = QStringLiteral("animationEvent")},
                   P{.key = QString(ActionParam::EffectId), .kind = QStringLiteral("shaderEffect")}},
        .category = QStringLiteral("animation"),
        .displayOrder = 0,
        .tags = {QString(Tag::Animation), QString(Tag::Effect)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::OverrideAnimationTiming),
        .slotFor = [](const QJsonObject& p) -> QString {
            const QString event = p.value(ActionParam::Event).toString();
            if (event.isEmpty()) {
                return QString();
            }
            return QString(ActionSlot::AnimTimingPrefix) + event;
        },
        .validate =
            [](const QJsonObject& p) {
                return hasNonEmptyString(p, ActionParam::Event);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Event), QString(ActionParam::Curve), QString(ActionParam::DurationMs)},
        .domain = ActionDomain::Window,
        .params =
            {P{.key = QString(ActionParam::Event), .kind = QStringLiteral("animationEvent")},
             P{.key = QString(ActionParam::DurationMs), .kind = QStringLiteral("number"), .min = 0.0, .max = 60000.0}},
        .category = QStringLiteral("animation"),
        .displayOrder = 1,
        .tags = {QString(Tag::Animation), QString(Tag::Effect)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::OverrideAnimationCurve),
        .slotFor = [](const QJsonObject& p) -> QString {
            const QString event = p.value(ActionParam::Event).toString();
            if (event.isEmpty()) {
                return QString();
            }
            return QString(ActionSlot::AnimCurvePrefix) + event;
        },
        .validate =
            [](const QJsonObject& p) {
                return hasNonEmptyString(p, ActionParam::Event);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Event), QString(ActionParam::Curve)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Event), .kind = QStringLiteral("animationEvent")},
                   P{.key = QString(ActionParam::Curve), .kind = QStringLiteral("curveEditor")}},
        .category = QStringLiteral("animation"),
        .displayOrder = 2,
        .tags = {QString(Tag::Animation), QString(Tag::Effect)},
    });

    // ── anim-exclude slot — terminal within the animation pipeline ──
    // A rule with `ExcludeAnimations` suppresses every animation override
    // for matched windows, regardless of event. Single event-agnostic
    // slot (vs. the per-event AnimShader/AnimTiming/AnimCurve slots) so
    // ONE rule covers the window's full animation surface. Terminal so
    // an ExcludeAnimations action on the same rule as an
    // OverrideAnimation* action wins. Migrated from the legacy
    // animationExcludedApplications / animationExcludedWindowClasses
    // lists by the v3→v4 chain; user-authored via Rules going
    // forward.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::ExcludeAnimations),
        .slotFor = constantSlot(ActionSlot::AnimExclude),
        .validate = &acceptAny,
        .terminal = true,
        .allowedKeys = {},
        .domain = ActionDomain::Window,
        .category = QStringLiteral("animation"),
        .displayOrder = 3,
        .tags = {QString(Tag::Animation)},
    });

    // ── opacity slot ──
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetOpacity),
        .slotFor = constantSlot(ActionSlot::Opacity),
        .validate =
            [](const QJsonObject& p) {
                return hasNumberInRange(p, ActionParam::Value, 1.0);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("percent"),
                     .min = 0.0,
                     .max = 100.0,
                     .scale = 0.01,
                     .defaultDisplay = 100.0}},
        .category = QStringLiteral("appearance"),
        .displayOrder = -1,
        .tags = {QString(Tag::Effect)},
    });

    // ── overlay-property slots — context-domain overrides of the active
    //    layout's zone-overlay shader / style. Daemon-side only
    //    (LayoutRegistry::resolveContextOverlay → OverlayService); no Tag::Effect.
    //    Shader-id vocabulary validation lives at the consumer (the overlay
    //    service falls back to the layout default for an unknown id), mirroring
    //    SetEngineMode's open-vocabulary rationale above.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::OverrideOverlayShader),
        .slotFor = constantSlot(ActionSlot::OverlayShader),
        .validate =
            [](const QJsonObject& p) {
                return hasNonEmptyString(p, ActionParam::EffectId);
            },
        .terminal = false,
        // Params carries the optional shader-uniform overrides, mirroring
        // OverrideAnimationShader; the inline ParameterEditor writes it.
        .allowedKeys = {QString(ActionParam::EffectId), QString(ActionParam::Params)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::EffectId), .kind = QStringLiteral("overlayShader")}},
        .category = QStringLiteral("overlay"),
        .displayOrder = 0,
        .tags = {QString(Tag::Overlay)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::OverrideOverlayStyle),
        .slotFor = constantSlot(ActionSlot::OverlayStyle),
        .validate =
            [](const QJsonObject& p) {
                const QString v = p.value(ActionParam::Value).toString();
                return v == OverlayStyleToken::Rectangles || v == OverlayStyleToken::Preview;
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("enum"),
                     .enumWireValues = {QString(OverlayStyleToken::Rectangles), QString(OverlayStyleToken::Preview)}}},
        .category = QStringLiteral("overlay"),
        .displayOrder = 1,
        .tags = {QString(Tag::Overlay)},
    });

    // ── per-context overlay-APPEARANCE slots (domain Context) ──
    // Colours / opacities / border dimensions / zone-number visibility that
    // override the global Snapping.Zones.* config for a matched context. One
    // slot per property so independent rules cascade. Resolved daemon-side by
    // LayoutRegistry::resolveContextOverlay; an unset property falls through to
    // the global config value (config stays authoritative). No Tag::Effect —
    // these are overlay-service reads, not KWin-effect window appearance.
    //
    // The three colour actions share a shape, so a small table keeps type and
    // slot in lockstep per row (mirroring the per-side gap loop). Colours use
    // the plain hex validator — NO accent sentinel, since the overlay consumer
    // resolves no token.
    struct OverlayColor
    {
        QLatin1StringView type;
        QLatin1StringView slot;
        int order;
    };
    for (const OverlayColor& c : {
             OverlayColor{ActionType::SetOverlayHighlightColor, ActionSlot::OverlayHighlightColor, 2},
             OverlayColor{ActionType::SetOverlayInactiveColor, ActionSlot::OverlayInactiveColor, 3},
             OverlayColor{ActionType::SetOverlayBorderColor, ActionSlot::OverlayBorderColor, 4},
         }) {
        const QString slot = QString(c.slot);
        registerAction(ActionDescriptor{
            .type = QString(c.type),
            .slotFor =
                [slot](const QJsonObject&) {
                    return slot;
                },
            .validate =
                [](const QJsonObject& p) {
                    return hasHexColor(p, ActionParam::Value);
                },
            .terminal = false,
            .allowedKeys = {QString(ActionParam::Value)},
            .domain = ActionDomain::Context,
            .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("color")}},
            .category = QStringLiteral("overlay"),
            .displayOrder = c.order,
            .tags = {QString(Tag::Overlay)},
        });
    }
    // Active / inactive overlay opacity — [0, 1] double on the wire, edited as a
    // percent (scale 0.01), mirroring SetOpacity. Defaults match the global
    // config (activeOpacity 0.5, inactiveOpacity 0.3).
    struct OverlayOpacity
    {
        QLatin1StringView type;
        QLatin1StringView slot;
        double defaultDisplay;
        int order;
    };
    for (const OverlayOpacity& o : {
             OverlayOpacity{ActionType::SetOverlayActiveOpacity, ActionSlot::OverlayActiveOpacity, 50.0, 5},
             OverlayOpacity{ActionType::SetOverlayInactiveOpacity, ActionSlot::OverlayInactiveOpacity, 30.0, 6},
         }) {
        const QString slot = QString(o.slot);
        registerAction(ActionDescriptor{
            .type = QString(o.type),
            .slotFor =
                [slot](const QJsonObject&) {
                    return slot;
                },
            .validate =
                [](const QJsonObject& p) {
                    return hasNumberInRange(p, ActionParam::Value, 1.0);
                },
            .terminal = false,
            .allowedKeys = {QString(ActionParam::Value)},
            .domain = ActionDomain::Context,
            .params = {P{.key = QString(ActionParam::Value),
                         .kind = QStringLiteral("percent"),
                         .min = 0.0,
                         .max = 100.0,
                         .scale = 0.01,
                         .defaultDisplay = o.defaultDisplay}},
            .category = QStringLiteral("overlay"),
            .displayOrder = o.order,
            .tags = {QString(Tag::Overlay)},
        });
    }
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetOverlayBorderWidth),
        .slotFor = constantSlot(ActionSlot::OverlayBorderWidth),
        .validate =
            [](const QJsonObject& p) {
                return hasNumberInRange(p, ActionParam::Value, MaxBorderWidth);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("number"),
                     .min = 0.0,
                     .max = MaxBorderWidth,
                     .defaultDisplay = 2.0}},
        .category = QStringLiteral("overlay"),
        .displayOrder = 7,
        .tags = {QString(Tag::Overlay)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetOverlayBorderRadius),
        .slotFor = constantSlot(ActionSlot::OverlayBorderRadius),
        .validate =
            [](const QJsonObject& p) {
                return hasNumberInRange(p, ActionParam::Value, kMaxOverlayBorderRadius);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("number"),
                     .min = 0.0,
                     .max = kMaxOverlayBorderRadius,
                     .defaultDisplay = 8.0}},
        .category = QStringLiteral("overlay"),
        .displayOrder = 8,
        .tags = {QString(Tag::Overlay)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetOverlayShowZoneNumbers),
        .slotFor = constantSlot(ActionSlot::OverlayShowZoneNumbers),
        .validate =
            [](const QJsonObject& p) {
                return hasBool(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("bool"), .defaultDisplay = 1.0}},
        .category = QStringLiteral("overlay"),
        .displayOrder = 9,
        .tags = {QString(Tag::Overlay)},
    });
}

} // namespace PhosphorRules
