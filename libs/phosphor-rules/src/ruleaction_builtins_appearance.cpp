// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Built-in action descriptor table, per-window appearance / gap / autotile-param
// / scrolling-param half. Split from ruleaction.cpp for file-size;
// registerBuiltins() calls registerBuiltinsEngine(), then this, then
// registerBuiltinsIndicators() (the tab- and drop-indicator families, split off
// here for the same reason), in that order. Shared param validators and slot
// helpers live in ruleaction_builtins_p.h.

#include <PhosphorRules/RuleAction.h>

#include "ruleaction_builtins_p.h"

#include <QJsonObject>
#include <QJsonValue>

namespace PhosphorRules {

using namespace detail;

void ActionRegistry::registerBuiltinsAppearance()
{
    using P = ParamSchema;

    // RestorePosition is window-domain but NOT a border/appearance slot — it is
    // consumed daemon-side (both engines' restore-position predicate), not by the
    // effect. Unlike the border bools below it seeds FALSE: the per-engine
    // `*RestoreFloatedWindowsOnLogin` settings default ON, so a fresh rule that
    // re-asserted true would be a no-op the user has to flip. Seeding false lands
    // the rule on its only meaningful value — opt this window OUT of restore.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::RestorePosition),
        .slotFor = constantSlot(ActionSlot::RestorePosition),
        .validate =
            [](const QJsonObject& p) {
                return hasBool(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("bool"), .defaultDisplay = 0.0}},
        .category = QStringLiteral("windowManagement"),
        // 9, not 5: ExcludePlacement already holds 5 in this category
        // (ruleaction_builtins_engine.cpp), and displayOrder is meant to be
        // unique per category.
        .displayOrder = 9,
    });

    // Three more per-window snap-policy overrides, same bool shape as
    // RestorePosition, consumed daemon-side (the managed-restore predicate /
    // the drag-out unsnap paths / the unfloat-fallback predicate), not the
    // effect. The SEED differs per row: the two restore policies seed FALSE
    // (their governing settings default ON, so the meaningful fresh rule is
    // "opt this window OUT"), while the unfloat fallback seeds TRUE (its
    // global `snapUnfloatFallbackToZone` defaults OFF, so the meaningful
    // fresh rule is "opt this window IN").
    struct RestorePolicy
    {
        QLatin1StringView type;
        QLatin1StringView slot;
        int order;
        double seed;
    };
    for (const RestorePolicy& rp : {
             RestorePolicy{ActionType::SetRestoreToZoneOnLogin, ActionSlot::RestoreToZoneOnLogin, 6, 0.0},
             RestorePolicy{ActionType::SetRestoreSizeOnUnsnap, ActionSlot::RestoreSizeOnUnsnap, 7, 0.0},
             RestorePolicy{ActionType::SetUnfloatFallbackToZone, ActionSlot::UnfloatFallbackToZone, 10, 1.0},
         }) {
        const QString slot = QString(rp.slot);
        registerAction(ActionDescriptor{
            .type = QString(rp.type),
            .slotFor =
                [slot](const QJsonObject&) {
                    return slot;
                },
            .validate =
                [](const QJsonObject& p) {
                    return hasBool(p, ActionParam::Value);
                },
            .terminal = false,
            .allowedKeys = {QString(ActionParam::Value)},
            .domain = ActionDomain::Window,
            .params = {P{.key = QString(ActionParam::Value),
                         .kind = QStringLiteral("bool"),
                         .defaultDisplay = rp.seed}},
            .category = QStringLiteral("windowManagement"),
            .displayOrder = rp.order,
        });
    }

    // Per-window stacking-layer override. Effect-consumed (Tag::Effect admits it
    // into the effect's rule set): reconcileRuleWindowLayer maps the token onto
    // KWin's keepAbove/keepBelow pair, snapshotting the pre-rule flags so a rule
    // that stops matching restores the user's own layer state. `above` is listed
    // first so a fresh rule seeds the headline use case (floating windows above
    // tiled windows, paired with an IsFloating match).
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetWindowLayer),
        .slotFor = constantSlot(ActionSlot::WindowLayer),
        .validate =
            [](const QJsonObject& p) {
                const QString v = p.value(ActionParam::Value).toString();
                return v == WindowLayerToken::Above || v == WindowLayerToken::Normal || v == WindowLayerToken::Below;
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("enum"),
                     .enumWireValues = {QString(WindowLayerToken::Above), QString(WindowLayerToken::Normal),
                                        QString(WindowLayerToken::Below)}}},
        .category = QStringLiteral("windowManagement"),
        .displayOrder = 8,
        .tags = {QString(Tag::Effect)},
    });

    // Per-window scroll-speed multiplier (niri's scroll-factor window rule).
    // Effect-consumed: the input filter rescales axis events in place while
    // the pointer hovers the matched window. Wayland sessions only.
    //
    // Tag::EffectVerdict, not Tag::Effect: this is a one-shot input verdict,
    // not an appearance override, and a user's "no animations" rule must not
    // cancel it — see the Tag::EffectVerdict doc in RuleAction.h.
    //
    // The wire value is the MULTIPLIER itself ([MinScrollFactor,
    // MaxScrollFactor], a fraction below 1), so the editor renders it as a
    // PERCENT with the usual 0.01 scale — the split-ratio / column-width
    // shape. A plain `number` kind would hand the fractional range to the
    // integer spin box, where every value below 1 is unreachable and the
    // floored 0 it saves instead fails this validator on the next load.
    //
    // Category stays "windowManagement" even though its three niri
    // window-rule siblings (OpenFullscreen / OpenFocused / OpenMaximized) are
    // "layoutEngine": the settings picker's layoutEngine branch buckets by
    // explicit type lists and its fall-through lands in the CONTEXT-domain
    // "Engine" group, so moving this window-domain action there without the
    // matching settings-side row would put it on the wrong side of the
    // picker's context/window divider.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::ScrollFactor),
        .slotFor = constantSlot(ActionSlot::ScrollFactor),
        .validate =
            [](const QJsonObject& p) {
                return hasNumberInSignedRange(p, ActionParam::Value, kMinScrollFactor, kMaxScrollFactor);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        // defaultDisplay 100 %: a fresh rule starts at "no visible change" and
        // the user deliberately moves off it — SetOpacity's 100% rationale.
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("percent"),
                     .min = kMinScrollFactorPercent,
                     .max = kMaxScrollFactorPercent,
                     .scale = 0.01,
                     .defaultDisplay = 100.0}},
        .category = QStringLiteral("windowManagement"),
        .displayOrder = 11,
        .tags = {QString(Tag::EffectVerdict)},
    });

    // ── per-window border / title-bar appearance slots (domain Window) ──
    // One slot per property so independent rules cascade per-property. The
    // effect (resolveWindowAppearance) reads these slots and merges them over
    // the global snap/autotile border state for ANY matched window. Bool seeds
    // (defaultDisplay 1.0 = true) land a fresh "hide title bars" / "show
    // border" rule in its on state — the user adds the action to turn it on.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetHideTitleBar),
        .slotFor = constantSlot(ActionSlot::HideTitleBar),
        .validate =
            [](const QJsonObject& p) {
                return hasBool(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("bool"), .defaultDisplay = 1.0}},
        .category = QStringLiteral("borderAppearance"),
        .displayOrder = 0,
        .tags = {QString(Tag::Border), QString(Tag::Effect)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetBorderVisible),
        .slotFor = constantSlot(ActionSlot::BorderVisible),
        .validate =
            [](const QJsonObject& p) {
                return hasBool(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("bool"), .defaultDisplay = 1.0}},
        .category = QStringLiteral("borderAppearance"),
        .displayOrder = 1,
        .tags = {QString(Tag::Border), QString(Tag::Effect)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetBorderWidth),
        .slotFor = constantSlot(ActionSlot::BorderWidth),
        .validate =
            [](const QJsonObject& p) {
                return hasNumberInRange(p, ActionParam::Value, MaxBorderWidth);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("number"),
                     .min = 0.0,
                     .max = MaxBorderWidth,
                     .defaultDisplay = 2.0}},
        .category = QStringLiteral("borderAppearance"),
        .displayOrder = 2,
        .tags = {QString(Tag::Border), QString(Tag::Effect)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetBorderRadius),
        .slotFor = constantSlot(ActionSlot::BorderRadius),
        .validate =
            [](const QJsonObject& p) {
                return hasNumberInRange(p, ActionParam::Value, MaxBorderRadius);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("number"),
                     .min = 0.0,
                     .max = MaxBorderRadius,
                     .defaultDisplay = 8.0}},
        .category = QStringLiteral("borderAppearance"),
        .displayOrder = 3,
        .tags = {QString(Tag::Border), QString(Tag::Effect)},
    });
    // Two single-colour border actions, one per focus state, each its own slot.
    // The colour param is keyed ActionParam::Value: a hex shape or the accent
    // sentinel. Internal active/inactive naming matches KWin and the effect's
    // activeColor/inactiveColor; user-facing labels say focused/unfocused.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetBorderColorActive),
        .slotFor = constantSlot(ActionSlot::BorderColorActive),
        .validate =
            [](const QJsonObject& p) {
                return hasHexColorOrAccent(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("color")}},
        .category = QStringLiteral("borderAppearance"),
        .displayOrder = 4,
        .tags = {QString(Tag::Border), QString(Tag::Effect)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetBorderColorInactive),
        .slotFor = constantSlot(ActionSlot::BorderColorInactive),
        .validate =
            [](const QJsonObject& p) {
                return hasHexColorOrAccent(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("color")}},
        .category = QStringLiteral("borderAppearance"),
        .displayOrder = 5,
        .tags = {QString(Tag::Border), QString(Tag::Effect)},
    });
    // Per-window opacity+tint layer slots, feeding the plain layer's reserved
    // "opacity-tint" pack the way the border slots feed "border". Visible
    // mirrors SetBorderVisible (an engaged true turns the layer on for the
    // matched window even when the global toggle is off; false forces it
    // off). Strength is wire-encoded [0.0, 1.0] like SetOpacity (percent in
    // the editor); the colour accepts a hex shape or the accent sentinel. The
    // layer's opacity itself stays on the SetOpacity slot above — when the
    // layer renders, that rule's value folds into the pack's opacity param
    // (rule wins over config); custom chains do not honour it (packs dim
    // through their own parameters, e.g. frost/glass contentOpacity).
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetOpacityTintVisible),
        .slotFor = constantSlot(ActionSlot::OpacityTintVisible),
        .validate =
            [](const QJsonObject& p) {
                return hasBool(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("bool"), .defaultDisplay = 1.0}},
        .category = QStringLiteral("appearance"),
        .displayOrder = 0,
        .tags = {QString(Tag::Effect)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetTintStrength),
        .slotFor = constantSlot(ActionSlot::TintStrength),
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
                     .defaultDisplay = 30.0}},
        .category = QStringLiteral("appearance"),
        .displayOrder = 1,
        .tags = {QString(Tag::Effect)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetTintColor),
        .slotFor = constantSlot(ActionSlot::TintColor),
        .validate =
            [](const QJsonObject& p) {
                return hasHexColorOrAccent(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("color")}},
        .category = QStringLiteral("appearance"),
        .displayOrder = 2,
        .tags = {QString(Tag::Effect)},
    });
    // Decoration-chain override: an ordered surface-pack list (empty array =
    // "no decoration" sentinel, so `Chain` must be PRESENT and an array but
    // may be empty) plus an optional per-pack params object riding the shared
    // `Params` key out-of-band, exactly like OverrideAnimationShader's
    // uniform map (the editor writes it; it is not in the params schema).
    registerAction(ActionDescriptor{
        .type = QString(ActionType::OverrideDecorationChain),
        .slotFor = constantSlot(ActionSlot::DecorationChain),
        .validate =
            [](const QJsonObject& p) {
                return p.contains(ActionParam::Chain) && p.value(ActionParam::Chain).isArray();
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Chain), QString(ActionParam::Params)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Chain), .kind = QStringLiteral("decorationChain")}},
        .category = QStringLiteral("borderAppearance"),
        .displayOrder = 6,
        .tags = {QString(Tag::Border), QString(Tag::Effect)},
    });

    // ── decoration-exclude slot — the decoration mirror of ExcludeAnimations.
    // A rule with `ExcludeDecorations` suppresses the border + surface-pack
    // chain for matched windows via the effect's shouldDecorateWindow gate,
    // which binds the Exclude ∪ ExcludeDecorations slice
    // (ExclusionRules::excludeDecorationsRulesFrom). Terminal for the same
    // reason ExcludeAnimations is; deliberately NOT Tag::Effect so it never
    // enters the effect's animation rule set (whose any-match gate
    // force-animates — wrong for a decoration opt-out). Tag::Border is
    // classification only (no in-tree tag reader).
    registerAction(ActionDescriptor{
        .type = QString(ActionType::ExcludeDecorations),
        .slotFor = constantSlot(ActionSlot::DecorationExclude),
        .validate = &acceptAny,
        .terminal = true,
        .allowedKeys = {},
        .domain = ActionDomain::Window,
        .category = QStringLiteral("borderAppearance"),
        .displayOrder = 9,
        .tags = {QString(Tag::Border)},
    });

    // ── per-context gap slots (domain Context) ──
    // Resolved daemon-side at zone-geometry time (DaemonGeometryResolver) as
    // the highest-precedence gap layer. Per-property to mirror the
    // PerScreenKeys gap set; the resolver maps these slots into a
    // per-screen-shaped override map and reuses the existing per-side logic.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetInnerGap),
        .slotFor = constantSlot(ActionSlot::InnerGap),
        .validate =
            [](const QJsonObject& p) {
                return hasNumberInRange(p, ActionParam::Value, kMaxGap);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("number"),
                     .min = 0.0,
                     .max = kMaxGap,
                     .defaultDisplay = 8.0}},
        .category = QStringLiteral("gap"),
        .displayOrder = 0,
        .tags = {QString(Tag::Gap)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetOuterGap),
        .slotFor = constantSlot(ActionSlot::OuterGap),
        .validate =
            [](const QJsonObject& p) {
                return hasNumberInRange(p, ActionParam::Value, kMaxGap);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("number"),
                     .min = 0.0,
                     .max = kMaxGap,
                     .defaultDisplay = 8.0}},
        .category = QStringLiteral("gap"),
        .displayOrder = 1,
        .tags = {QString(Tag::Gap)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetUsePerSideOuterGap),
        .slotFor = constantSlot(ActionSlot::UsePerSideOuterGap),
        .validate =
            [](const QJsonObject& p) {
                return hasBool(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("bool"), .defaultDisplay = 0.0}},
        .category = QStringLiteral("gap"),
        .displayOrder = 2,
        .tags = {QString(Tag::Gap)},
    });
    // Each per-side gap maps to its own slot. The {type, slot} pairs live in a
    // single table so type and slot stay in lockstep per row — adding a side is
    // one row, with no parallel mapping to keep in sync and no silent
    // fall-through to the wrong slot if a row is mistyped.
    struct PerSideGap
    {
        QLatin1StringView type;
        QLatin1StringView slot;
        int order;
    };
    for (const PerSideGap& perSide : {
             PerSideGap{ActionType::SetOuterGapTop, ActionSlot::OuterGapTop, 3},
             PerSideGap{ActionType::SetOuterGapBottom, ActionSlot::OuterGapBottom, 4},
             PerSideGap{ActionType::SetOuterGapLeft, ActionSlot::OuterGapLeft, 5},
             PerSideGap{ActionType::SetOuterGapRight, ActionSlot::OuterGapRight, 6},
         }) {
        const QString slot = QString(perSide.slot);
        registerAction(ActionDescriptor{
            .type = QString(perSide.type),
            .slotFor =
                [slot](const QJsonObject&) {
                    return slot;
                },
            .validate =
                [](const QJsonObject& p) {
                    return hasNumberInRange(p, ActionParam::Value, kMaxGap);
                },
            .terminal = false,
            .allowedKeys = {QString(ActionParam::Value)},
            .domain = ActionDomain::Context,
            .params = {P{.key = QString(ActionParam::Value),
                         .kind = QStringLiteral("number"),
                         .min = 0.0,
                         .max = kMaxGap,
                         .defaultDisplay = 8.0}},
            .category = QStringLiteral("gap"),
            .displayOrder = perSide.order,
            .tags = {QString(Tag::Gap)},
        });
    }

    // ── per-context autotile parameter slots (domain Context) ──
    // Resolved daemon-side by LayoutRegistry::resolveContextTilingParams and
    // layered onto the per-screen autotile override map (config stays the base;
    // the rule wins where present). Category "layoutEngine" — these configure the
    // tiling engine for the matched context.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetMaxWindows),
        .slotFor = constantSlot(ActionSlot::MaxWindows),
        .validate =
            [](const QJsonObject& p) {
                // 1..12, integral; the consumer re-clamps to AutotileDefaults, so
                // this only enforces a sanity floor of 1 (0 and negatives are rejected
                // as grossly malformed) and the upper bound. Reject non-integral like
                // the sibling count validators (SnapToZone / RouteToDesktop) rather
                // than silently truncating a hand-edited fractional count.
                const QJsonValue v = p.value(ActionParam::Value);
                if (!v.isDouble()) {
                    return false;
                }
                const double d = v.toDouble();
                if (d < 1.0 || d > kMaxTiledWindows) {
                    return false;
                }
                return static_cast<double>(static_cast<int>(d)) == d;
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("number"),
                     .min = 1.0,
                     .max = kMaxTiledWindows,
                     .defaultDisplay = 5.0}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 10,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetSplitRatio),
        .slotFor = constantSlot(ActionSlot::SplitRatio),
        .validate =
            [](const QJsonObject& p) {
                // Wire is the [kMinSplitRatio, kMaxSplitRatio] ratio; edited as a percent.
                return hasNumberInSignedRange(p, ActionParam::Value, kMinSplitRatio, kMaxSplitRatio);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("percent"),
                     .min = kMinSplitPercent,
                     .max = kMaxSplitPercent,
                     .scale = 0.01,
                     .defaultDisplay = 50.0}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 11,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetMasterCount),
        .slotFor = constantSlot(ActionSlot::MasterCount),
        .validate =
            [](const QJsonObject& p) {
                // 1..kMaxMasterCount, integral (mirrors SetMaxWindows / SnapToZone).
                const QJsonValue v = p.value(ActionParam::Value);
                if (!v.isDouble()) {
                    return false;
                }
                const double d = v.toDouble();
                if (d < 1.0 || d > kMaxMasterCount) {
                    return false;
                }
                return static_cast<double>(static_cast<int>(d)) == d;
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("number"),
                     .min = 1.0,
                     .max = kMaxMasterCount,
                     .defaultDisplay = 1.0}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 12,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetInsertPosition),
        .slotFor = constantSlot(ActionSlot::InsertPosition),
        .validate =
            [](const QJsonObject& p) {
                const QString v = p.value(ActionParam::Value).toString();
                return v == InsertPositionToken::End || v == InsertPositionToken::AfterFocused
                    || v == InsertPositionToken::AsMaster;
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("enum"),
                     .enumWireValues = {QString(InsertPositionToken::End), QString(InsertPositionToken::AfterFocused),
                                        QString(InsertPositionToken::AsMaster)}}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 13,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetOverflowBehavior),
        .slotFor = constantSlot(ActionSlot::OverflowBehavior),
        .validate =
            [](const QJsonObject& p) {
                const QString v = p.value(ActionParam::Value).toString();
                return v == OverflowBehaviorToken::Float || v == OverflowBehaviorToken::Unlimited;
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{
            .key = QString(ActionParam::Value),
            .kind = QStringLiteral("enum"),
            .enumWireValues = {QString(OverflowBehaviorToken::Float), QString(OverflowBehaviorToken::Unlimited)}}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 14,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetDragBehavior),
        .slotFor = constantSlot(ActionSlot::DragBehavior),
        .validate =
            [](const QJsonObject& p) {
                const QString v = p.value(ActionParam::Value).toString();
                return v == DragBehaviorToken::Float || v == DragBehaviorToken::Reorder;
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("enum"),
                     .enumWireValues = {QString(DragBehaviorToken::Float), QString(DragBehaviorToken::Reorder)}}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 15,
        .tags = {QString(Tag::LayoutEngine)},
    });
    // SetAlgorithmParam mirrors OverrideOverlayShader: a picker param (the target
    // algorithm) plus a free-form `params` blob (the custom-parameter values,
    // validated against the algorithm's declared schema at apply time via
    // hasCustomParam — so the wire validator only requires the algorithm token).
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetAlgorithmParam),
        .slotFor = constantSlot(ActionSlot::AlgorithmParams),
        .validate =
            [](const QJsonObject& p) {
                return hasNonEmptyString(p, ActionParam::Algorithm);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Algorithm), QString(ActionParam::Params)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Algorithm), .kind = QStringLiteral("tilingAlgorithm")}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 16,
        .tags = {QString(Tag::LayoutEngine)},
    });

    // ── per-context scrolling parameter slots (domain Context) ──
    // Resolved daemon-side by LayoutRegistry::resolveContextScrollingParams and
    // layered onto the scrolling engine's per-screen parameters, exactly as the
    // autotile family above is layered onto the tiling override map.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetScrollDefaultColumnWidth),
        .slotFor = constantSlot(ActionSlot::ScrollDefaultColumnWidth),
        .validate =
            [](const QJsonObject& p) {
                // Wire is the [kMinColumnWidthRatio, kMaxColumnWidthRatio] fraction of
                // the work area; edited as a percent (mirrors SetSplitRatio).
                return hasNumberInSignedRange(p, ActionParam::Value, kMinColumnWidthRatio, kMaxColumnWidthRatio);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("percent"),
                     .min = kMinColumnWidthPercent,
                     .max = kMaxColumnWidthPercent,
                     .scale = 0.01,
                     .defaultDisplay = 50.0}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 17,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetCenterFocusedColumn),
        .slotFor = constantSlot(ActionSlot::CenterFocusedColumn),
        .validate =
            [](const QJsonObject& p) {
                const QString v = p.value(ActionParam::Value).toString();
                return v == CenterFocusedColumnToken::Never || v == CenterFocusedColumnToken::Always
                    || v == CenterFocusedColumnToken::OnOverflow;
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("enum"),
                     .enumWireValues = {QString(CenterFocusedColumnToken::Never),
                                        QString(CenterFocusedColumnToken::Always),
                                        QString(CenterFocusedColumnToken::OnOverflow)}}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 18,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetScrollDefaultColumnDisplay),
        .slotFor = constantSlot(ActionSlot::ScrollDefaultColumnDisplay),
        .validate =
            [](const QJsonObject& p) {
                const QString v = p.value(ActionParam::Value).toString();
                return v == ColumnDisplayToken::Normal || v == ColumnDisplayToken::Tabbed;
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("enum"),
                     .enumWireValues = {QString(ColumnDisplayToken::Normal), QString(ColumnDisplayToken::Tabbed)}}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 19,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetScrollInsertPosition),
        .slotFor = constantSlot(ActionSlot::ScrollInsertPosition),
        .validate =
            [](const QJsonObject& p) {
                const QString v = p.value(ActionParam::Value).toString();
                return v == ScrollInsertPositionToken::RightOfActive || v == ScrollInsertPositionToken::LeftOfActive
                    || v == ScrollInsertPositionToken::First || v == ScrollInsertPositionToken::Last
                    || v == ScrollInsertPositionToken::IntoActiveColumn;
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("enum"),
                     .enumWireValues = {QString(ScrollInsertPositionToken::RightOfActive),
                                        QString(ScrollInsertPositionToken::LeftOfActive),
                                        QString(ScrollInsertPositionToken::First),
                                        QString(ScrollInsertPositionToken::Last),
                                        QString(ScrollInsertPositionToken::IntoActiveColumn)}}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 23,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetScrollDefaultWindowHeight),
        .slotFor = constantSlot(ActionSlot::ScrollDefaultWindowHeight),
        .validate =
            [](const QJsonObject& p) {
                // Same wire shape as the width pair — a work-area fraction
                // sharing the same shared bounds (a height may take the
                // whole column, so the 1.0 ceiling is right here too).
                return hasNumberInSignedRange(p, ActionParam::Value, kMinColumnWidthRatio, kMaxColumnWidthRatio);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("percent"),
                     .min = kMinColumnWidthPercent,
                     .max = kMaxColumnWidthPercent,
                     .scale = 0.01,
                     .defaultDisplay = 50.0}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 24,
        .tags = {QString(Tag::LayoutEngine)},
    });

    // ── per-context scrolling BEHAVIOUR overrides (domain Context) ──
    // The six boolean toggles that had no rule seam until now. Same shape as
    // the sizing slots above: five ride the per-screen override map and the
    // engine reads each through an `effective*` accessor falling back to the
    // global config value, while focus-follows-mouse is resolved per screen
    // and pushed to the compositor instead (its consumer lives there).
    // Each seeds the polarity a user reaches for: the three whose global
    // default is ON seed FALSE (the meaningful rule is "turn it off here"),
    // and the three whose global default is OFF — cropStraddlers,
    // alwaysCenterSingleColumn and focusFollowsMouse — seed TRUE.
    struct ScrollBehaviourToggle
    {
        QLatin1StringView type;
        QLatin1StringView slot;
        int order;
        double seed;
    };
    for (const ScrollBehaviourToggle& t : {
             ScrollBehaviourToggle{ActionType::SetScrollAlwaysCenterSingleColumn,
                                   ActionSlot::ScrollAlwaysCenterSingleColumn, 30, 1.0},
             ScrollBehaviourToggle{ActionType::SetScrollCenterShortColumns, ActionSlot::ScrollCenterShortColumns, 29,
                                   1.0},
             ScrollBehaviourToggle{ActionType::SetScrollRespectMinimumSize, ActionSlot::ScrollRespectMinimumSize, 31,
                                   0.0},
             ScrollBehaviourToggle{ActionType::SetScrollCropStraddlers, ActionSlot::ScrollCropStraddlers, 32, 1.0},
             ScrollBehaviourToggle{ActionType::SetScrollFocusNewWindows, ActionSlot::ScrollFocusNewWindows, 33, 0.0},
             ScrollBehaviourToggle{ActionType::SetScrollSmartGaps, ActionSlot::ScrollSmartGaps, 34, 0.0},
             // Effect-consumed rather than engine-consumed, but structurally
             // the same context bool, so it registers with its neighbours.
             ScrollBehaviourToggle{ActionType::SetScrollFocusFollowsMouse, ActionSlot::ScrollFocusFollowsMouse, 36,
                                   1.0},
         }) {
        const QString slot = QString(t.slot);
        registerAction(ActionDescriptor{
            .type = QString(t.type),
            .slotFor =
                [slot](const QJsonObject&) {
                    return slot;
                },
            .validate =
                [](const QJsonObject& p) {
                    return hasBool(p, ActionParam::Value);
                },
            .terminal = false,
            .allowedKeys = {QString(ActionParam::Value)},
            .domain = ActionDomain::Context,
            .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("bool"), .defaultDisplay = t.seed}},
            .category = QStringLiteral("layoutEngine"),
            .displayOrder = t.order,
            .tags = {QString(Tag::LayoutEngine)},
        });
    }
    // The cap on the toggle above. A percent slot rather than a bool, so it
    // registers on its own instead of joining the loop.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetScrollFocusFollowsMouseMaxScroll),
        .slotFor = constantSlot(ActionSlot::ScrollFocusFollowsMouseMaxScroll),
        .validate =
            [](const QJsonObject& p) {
                // Wire is the fraction of the viewport's extent along the
                // strip; edited as a percent, like the column-width slot. The
                // ZERO-floored helper: a negative cap is not a stricter one,
                // it is a malformed payload.
                return hasNumberInRange(p, ActionParam::Value, kMaxFfmMaxScrollRatio);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("percent"),
                     .min = kMinFfmMaxScrollPercent,
                     .max = kMaxFfmMaxScrollPercent,
                     .scale = 0.01,
                     // Seeds a cap that actually caps: the global default is
                     // 100 (no cap), so a rule reaching for this slot wants a
                     // limit, and half a viewport is the useful middle.
                     .defaultDisplay = 50.0}},
        .category = QStringLiteral("layoutEngine"),
        // 35 through 37 are taken (sticky handling, the toggle this caps, the
        // strip axis), so the cap takes the next free slot rather than sitting
        // beside its toggle.
        .displayOrder = 38,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetScrollStickyWindowHandling),
        .slotFor = constantSlot(ActionSlot::ScrollStickyWindowHandling),
        .validate =
            [](const QJsonObject& p) {
                const QString v = p.value(ActionParam::Value).toString();
                return v == StickyWindowHandlingToken::TreatAsNormal || v == StickyWindowHandlingToken::RestoreOnly
                    || v == StickyWindowHandlingToken::IgnoreAll;
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("enum"),
                     .enumWireValues = {QString(StickyWindowHandlingToken::TreatAsNormal),
                                        QString(StickyWindowHandlingToken::RestoreOnly),
                                        QString(StickyWindowHandlingToken::IgnoreAll)}}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 35,
        .tags = {QString(Tag::LayoutEngine)},
    });
    // The strip axis: a closed three-token vocabulary in the config INTENT
    // space (auto resolves from the work-area shape at relayout, so a rule
    // can put a pinned monitor back on shape-matching for one context).
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetScrollStripAxis),
        .slotFor = constantSlot(ActionSlot::ScrollStripAxis),
        .validate =
            [](const QJsonObject& p) {
                const QString v = p.value(ActionParam::Value).toString();
                return v == StripAxisToken::Auto || v == StripAxisToken::Horizontal || v == StripAxisToken::Vertical;
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("enum"),
                     // Vertical FIRST, deliberately out of config-intent
                     // order: the editor seeds a fresh enum action with the
                     // first wire value, and Auto is the global default, so
                     // an Auto-seeded rule looked like it did nothing on any
                     // setup without a per-screen pin to override. Forcing
                     // an axis (portrait monitors are the feature's whole
                     // point) is what a fresh axis rule is for; Auto stays
                     // available as the put-it-back-on-shape-matching arm.
                     .enumWireValues = {QString(StripAxisToken::Vertical), QString(StripAxisToken::Horizontal),
                                        QString(StripAxisToken::Auto)}}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 37,
        .tags = {QString(Tag::LayoutEngine)},
    });

    // ── per-window scrolling open overrides (domain Window) ──
    // Read on the open path for the matched window and layered over the context /
    // config defaults above, so one application opens wide, tabbed, or into the
    // focused column without changing the engine's defaults.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::OpenColumnWidth),
        .slotFor = constantSlot(ActionSlot::OpenColumnWidth),
        .validate =
            [](const QJsonObject& p) {
                // Same wire shape as SetScrollDefaultColumnWidth — a work-area fraction.
                return hasNumberInSignedRange(p, ActionParam::Value, kMinColumnWidthRatio, kMaxColumnWidthRatio);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("percent"),
                     .min = kMinColumnWidthPercent,
                     .max = kMaxColumnWidthPercent,
                     .scale = 0.01,
                     .defaultDisplay = 50.0}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 20,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::OpenTabbed),
        .slotFor = constantSlot(ActionSlot::OpenTabbed),
        .validate =
            [](const QJsonObject& p) {
                return hasBool(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("bool"), .defaultDisplay = 1.0}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 21,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::OpenColumnPlacement),
        .slotFor = constantSlot(ActionSlot::OpenColumnPlacement),
        .validate =
            [](const QJsonObject& p) {
                const QString v = p.value(ActionParam::Value).toString();
                return v == ColumnPlacementToken::NewColumn || v == ColumnPlacementToken::Consume;
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{
            .key = QString(ActionParam::Value),
            .kind = QStringLiteral("enum"),
            .enumWireValues = {QString(ColumnPlacementToken::NewColumn), QString(ColumnPlacementToken::Consume)}}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 22,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::OpenWindowHeight),
        .slotFor = constantSlot(ActionSlot::OpenWindowHeight),
        .validate =
            [](const QJsonObject& p) {
                // Same wire shape as the width slots — a work-area fraction
                // against the shared column-width bounds, applied to the height.
                return hasNumberInSignedRange(p, ActionParam::Value, kMinColumnWidthRatio, kMaxColumnWidthRatio);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("percent"),
                     .min = kMinColumnWidthPercent,
                     .max = kMaxColumnWidthPercent,
                     .scale = 0.01,
                     .defaultDisplay = 50.0}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 25,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::OpenMaximized),
        .slotFor = constantSlot(ActionSlot::OpenMaximized),
        .validate =
            [](const QJsonObject& p) {
                return hasBool(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("bool"), .defaultDisplay = 1.0}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 26,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::OpenFocused),
        .slotFor = constantSlot(ActionSlot::OpenFocused),
        .validate =
            [](const QJsonObject& p) {
                return hasBool(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        // defaultDisplay 0.0: the global focus-new-windows setting defaults ON,
        // so the meaningful fresh-rule value is "do not steal focus".
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("bool"), .defaultDisplay = 0.0}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 27,
        .tags = {QString(Tag::LayoutEngine)},
    });
    // Effect-consumed, unlike its Open* siblings: Tag::EffectVerdict (not
    // LayoutEngine) admits the rule into the KWin effect's rule set, where
    // the open-time fullscreen flip lives. The VERDICT tag rather than
    // Tag::Effect because this is a one-shot open verdict, not an appearance
    // override, and an ExcludeAnimations rule must not cancel it — see the
    // Tag::EffectVerdict doc in RuleAction.h. See also the ActionType doc.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::OpenFullscreen),
        .slotFor = constantSlot(ActionSlot::OpenFullscreen),
        .validate =
            [](const QJsonObject& p) {
                return hasBool(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("bool"), .defaultDisplay = 1.0}},
        .category = QStringLiteral("layoutEngine"),
        .displayOrder = 28,
        .tags = {QString(Tag::EffectVerdict)},
    });
}

} // namespace PhosphorRules
