// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Built-in action descriptor table, per-window appearance / gap / autotile-param
// half. Split from ruleaction.cpp for file-size; registerBuiltins() calls
// registerBuiltinsEngine() then this, in that order. Shared param validators and
// slot helpers live in ruleaction_builtins_p.h.

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
        .displayOrder = 5,
    });

    // Two more per-window restore-policy overrides, same shape as RestorePosition
    // (bool, seeds FALSE because both governing settings default ON — the only
    // meaningful fresh-rule value is "opt this window OUT"). Consumed daemon-side
    // (the managed-restore predicate / the drag-out unsnap paths), not the effect.
    struct RestorePolicy
    {
        QLatin1StringView type;
        QLatin1StringView slot;
        int order;
    };
    for (const RestorePolicy& rp : {
             RestorePolicy{ActionType::SetRestoreToZoneOnLogin, ActionSlot::RestoreToZoneOnLogin, 6},
             RestorePolicy{ActionType::SetRestoreSizeOnUnsnap, ActionSlot::RestoreSizeOnUnsnap, 7},
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
            .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("bool"), .defaultDisplay = 0.0}},
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
                const QJsonValue v = p.value(ActionParam::Value);
                if (!v.isDouble()) {
                    return false;
                }
                const double d = v.toDouble();
                return d >= kMinSplitRatio && d <= kMaxSplitRatio;
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
}

} // namespace PhosphorRules
