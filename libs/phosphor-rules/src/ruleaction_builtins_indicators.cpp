// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Built-in action descriptor table, INDICATOR half — the tab indicator and the
// drop indicator, context knobs and per-window colours alike. Split out of
// ruleaction_builtins_appearance.cpp for file size; registerBuiltins() calls
// registerBuiltinsEngine(), then registerBuiltinsAppearance(), then this, in
// that order. Shared param validators, bounds and slot helpers live in
// ruleaction_builtins_p.h.
//
// The seam is a concern, not an arbitrary cut: these two families are the
// niri-parity INDICATOR surface — twenty-four registrations that are all paint
// or geometry for a widget the engine draws beside a column, none of which the
// appearance half's per-window border / gap / engine-parameter descriptors
// interact with.

#include <PhosphorRules/RuleAction.h>

#include "ruleaction_builtins_p.h"

#include <QJsonObject>

namespace PhosphorRules {

using namespace detail;

void ActionRegistry::registerBuiltinsIndicators()
{
    using P = ParamSchema;

    // ── per-context tab-indicator slots (domain Context) ──
    // niri's `tab-indicator` layout block. One action per property, so a
    // layout rule that sets the position and a theme rule that sets the
    // colours compose instead of clobbering each other. The GEOMETRY half is
    // layered onto the scrolling engine's per-screen override map (the
    // ScrollPerScreenKeys::tabIndicator* keys); the PAINT half never reaches
    // that library and is applied to the overlay daemon-side.
    //
    // Their own category, not layoutEngine: sixteen more rows (the thirteen
    // context knobs below plus the three per-window tab colours further down)
    // would swamp that group's list, and the indicator is one coherent feature
    // a user reaches for as a unit.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetTabIndicatorEnabled),
        .slotFor = constantSlot(ActionSlot::TabIndicatorEnabled),
        .validate =
            [](const QJsonObject& p) {
                return hasBool(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("bool"), .defaultDisplay = 1.0}},
        .category = QStringLiteral("tabIndicator"),
        .displayOrder = 1,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetTabIndicatorStyle),
        .slotFor = constantSlot(ActionSlot::TabIndicatorStyle),
        .validate =
            [](const QJsonObject& p) {
                const QString v = p.value(ActionParam::Value).toString();
                return v == TabIndicatorStyleToken::Chips || v == TabIndicatorStyleToken::Bar;
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("enum"),
                     .enumWireValues = {QString(TabIndicatorStyleToken::Chips), QString(TabIndicatorStyleToken::Bar)}}},
        .category = QStringLiteral("tabIndicator"),
        .displayOrder = 2,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetTabIndicatorPosition),
        .slotFor = constantSlot(ActionSlot::TabIndicatorPosition),
        .validate =
            [](const QJsonObject& p) {
                const QString v = p.value(ActionParam::Value).toString();
                return v == TabIndicatorPositionToken::Left || v == TabIndicatorPositionToken::Right
                    || v == TabIndicatorPositionToken::Top || v == TabIndicatorPositionToken::Bottom;
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{
            .key = QString(ActionParam::Value),
            .kind = QStringLiteral("enum"),
            .enumWireValues = {QString(TabIndicatorPositionToken::Left), QString(TabIndicatorPositionToken::Right),
                               QString(TabIndicatorPositionToken::Top), QString(TabIndicatorPositionToken::Bottom)}}},
        .category = QStringLiteral("tabIndicator"),
        .displayOrder = 3,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetTabIndicatorHideWhenSingleTab),
        .slotFor = constantSlot(ActionSlot::TabIndicatorHideWhenSingleTab),
        .validate =
            [](const QJsonObject& p) {
                return hasBool(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("bool"), .defaultDisplay = 1.0}},
        .category = QStringLiteral("tabIndicator"),
        .displayOrder = 4,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetTabIndicatorPlaceWithinColumn),
        .slotFor = constantSlot(ActionSlot::TabIndicatorPlaceWithinColumn),
        .validate =
            [](const QJsonObject& p) {
                return hasBool(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("bool"), .defaultDisplay = 1.0}},
        .category = QStringLiteral("tabIndicator"),
        .displayOrder = 5,
        .tags = {QString(Tag::LayoutEngine)},
    });
    // Signed range: a negative gap draws the indicator over the window, which
    // is the whole point of allowing it (niri parity).
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetTabIndicatorGap),
        .slotFor = constantSlot(ActionSlot::TabIndicatorGap),
        .validate =
            [](const QJsonObject& p) {
                return hasNumberInSignedRange(p, ActionParam::Value, kMinTabIndicatorGap, kMaxTabIndicatorGap);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("number"),
                     .min = kMinTabIndicatorGap,
                     .max = kMaxTabIndicatorGap,
                     .defaultDisplay = 5.0}},
        .category = QStringLiteral("tabIndicator"),
        .displayOrder = 6,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetTabIndicatorWidth),
        .slotFor = constantSlot(ActionSlot::TabIndicatorWidth),
        .validate =
            [](const QJsonObject& p) {
                return hasNumberInSignedRange(p, ActionParam::Value, kMinTabIndicatorWidth, kMaxTabIndicatorWidth);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("number"),
                     .min = kMinTabIndicatorWidth,
                     .max = kMaxTabIndicatorWidth,
                     .defaultDisplay = 4.0}},
        .category = QStringLiteral("tabIndicator"),
        .displayOrder = 7,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetTabIndicatorLength),
        .slotFor = constantSlot(ActionSlot::TabIndicatorLength),
        .validate =
            [](const QJsonObject& p) {
                return hasNumberInSignedRange(p, ActionParam::Value, kMinTabIndicatorLengthRatio,
                                              kMaxTabIndicatorLengthRatio);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        // Stored as a fraction, edited as a percent — the SetScrollDefaultColumnWidth shape.
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("percent"),
                     .min = kMinTabIndicatorLengthPercent,
                     .max = kMaxTabIndicatorLengthPercent,
                     .scale = 0.01,
                     .defaultDisplay = 50.0}},
        .category = QStringLiteral("tabIndicator"),
        .displayOrder = 8,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetTabIndicatorGapsBetweenTabs),
        .slotFor = constantSlot(ActionSlot::TabIndicatorGapsBetweenTabs),
        .validate =
            [](const QJsonObject& p) {
                return hasNumberInRange(p, ActionParam::Value, kMaxTabIndicatorGap);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("number"),
                     .min = 0.0,
                     .max = kMaxTabIndicatorGap,
                     .defaultDisplay = 0.0}},
        .category = QStringLiteral("tabIndicator"),
        .displayOrder = 9,
        .tags = {QString(Tag::LayoutEngine)},
    });
    // Signed like the gap, but for a different reason: -1 is the config
    // layer's "fully rounded" sentinel, not a real negative radius.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetTabIndicatorCornerRadius),
        .slotFor = constantSlot(ActionSlot::TabIndicatorCornerRadius),
        .validate =
            [](const QJsonObject& p) {
                return hasNumberInSignedRange(p, ActionParam::Value, kTabIndicatorCornerRadiusPill,
                                              kMaxTabIndicatorCornerRadius);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("number"),
                     .min = kTabIndicatorCornerRadiusPill,
                     .max = kMaxTabIndicatorCornerRadius,
                     // Square, the shipped default. The pill sentinel is a
                     // value the user opts into, not one a fresh rule seeds.
                     .defaultDisplay = 0.0}},
        .category = QStringLiteral("tabIndicator"),
        .displayOrder = 10,
        .tags = {QString(Tag::LayoutEngine)},
    });
    // HEX ONLY, no accent sentinel — the same contract the overlay colour
    // actions carry and for the same reason: no consumer on either the context
    // or the per-window path resolves the token. Both readColor helpers
    // (layoutregistry_contextresolve.cpp, windowtrackingadaptor/rules.cpp) pass
    // the string through verbatim to a QML colour property, so an accepted
    // "accent" would reach the overlay as an unparseable colour. Only the
    // border/tint family has a resolver for it.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetTabIndicatorActiveColor),
        .slotFor = constantSlot(ActionSlot::TabIndicatorActiveColor),
        .validate =
            [](const QJsonObject& p) {
                return hasHexColor(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("color")}},
        .category = QStringLiteral("tabIndicator"),
        .displayOrder = 11,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetTabIndicatorInactiveColor),
        .slotFor = constantSlot(ActionSlot::TabIndicatorInactiveColor),
        .validate =
            [](const QJsonObject& p) {
                return hasHexColor(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("color")}},
        .category = QStringLiteral("tabIndicator"),
        .displayOrder = 12,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetTabIndicatorUrgentColor),
        .slotFor = constantSlot(ActionSlot::TabIndicatorUrgentColor),
        .validate =
            [](const QJsonObject& p) {
                return hasHexColor(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("color")}},
        .category = QStringLiteral("tabIndicator"),
        .displayOrder = 13,
        .tags = {QString(Tag::LayoutEngine)},
    });

    // ── per-window tab colours (domain Window) ──
    // niri's `tab-indicator` WINDOW rule: recolours only the matched window's
    // own tab, so one app can be marked out inside a shared column. Resolved
    // per tab when the daemon builds the indicator model, where they outrank
    // the per-context colours registered above.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::TabColorActive),
        .slotFor = constantSlot(ActionSlot::TabColorActive),
        .validate =
            [](const QJsonObject& p) {
                return hasHexColor(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("color")}},
        .category = QStringLiteral("tabIndicator"),
        .displayOrder = 14,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::TabColorInactive),
        .slotFor = constantSlot(ActionSlot::TabColorInactive),
        .validate =
            [](const QJsonObject& p) {
                return hasHexColor(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("color")}},
        .category = QStringLiteral("tabIndicator"),
        .displayOrder = 15,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::TabColorUrgent),
        .slotFor = constantSlot(ActionSlot::TabColorUrgent),
        .validate =
            [](const QJsonObject& p) {
                return hasHexColor(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("color")}},
        .category = QStringLiteral("tabIndicator"),
        .displayOrder = 16,
        .tags = {QString(Tag::LayoutEngine)},
    });

    // ── per-context drop-indicator overrides (domain Context) ──
    // Its own category, not folded into tabIndicator: the two indicators are
    // separate features a user reaches for independently, and the drop
    // indicator is armed by a drag while the tab indicator is a property of a
    // tabbed column. Every action here is PAINT — there is no geometry half,
    // because the rect comes from the engine's layout math and cannot be
    // positioned independently of where the drop lands.
    // Master switch, the peer of SetTabIndicatorEnabled.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetDropIndicatorEnabled),
        .slotFor = constantSlot(ActionSlot::DropIndicatorEnabled),
        .validate =
            [](const QJsonObject& p) {
                return hasBool(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("bool"), .defaultDisplay = 1.0}},
        .category = QStringLiteral("dropIndicator"),
        .displayOrder = 1,
        .tags = {QString(Tag::LayoutEngine)},
    });
    // Fill colour. A rule cannot express the EMPTY "follow the colour scheme" sentinel; not setting the action leaves
    // it in place.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetDropIndicatorColor),
        .slotFor = constantSlot(ActionSlot::DropIndicatorColor),
        .validate =
            [](const QJsonObject& p) {
                return hasHexColor(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("color")}},
        .category = QStringLiteral("dropIndicator"),
        .displayOrder = 2,
        .tags = {QString(Tag::LayoutEngine)},
    });
    // Border colour. Drawn opaque whatever alpha the value carries, matching the paint site.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetDropIndicatorBorderColor),
        .slotFor = constantSlot(ActionSlot::DropIndicatorBorderColor),
        .validate =
            [](const QJsonObject& p) {
                return hasHexColor(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("color")}},
        .category = QStringLiteral("dropIndicator"),
        .displayOrder = 3,
        .tags = {QString(Tag::LayoutEngine)},
    });
    // Fill opacity as a stored fraction, edited as a percent. Zero is legal and means an outline with no fill.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetDropIndicatorOpacity),
        .slotFor = constantSlot(ActionSlot::DropIndicatorOpacity),
        .validate =
            [](const QJsonObject& p) {
                return hasNumberInRange(p, ActionParam::Value, kMaxDropIndicatorOpacity);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("percent"),
                     .min = kMinDropIndicatorOpacityPercent,
                     .max = kMaxDropIndicatorOpacityPercent,
                     .scale = 0.01,
                     .defaultDisplay = 25.0}},
        .category = QStringLiteral("dropIndicator"),
        .displayOrder = 4,
        .tags = {QString(Tag::LayoutEngine)},
    });
    // Border thickness in px. Floors at 0, which is a fill with no edge.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetDropIndicatorBorderWidth),
        .slotFor = constantSlot(ActionSlot::DropIndicatorBorderWidth),
        .validate =
            [](const QJsonObject& p) {
                return hasNumberInRange(p, ActionParam::Value, kMaxDropIndicatorBorderWidth);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("number"),
                     .min = kMinDropIndicatorBorderWidth,
                     .max = kMaxDropIndicatorBorderWidth,
                     .defaultDisplay = 2.0}},
        .category = QStringLiteral("dropIndicator"),
        .displayOrder = 5,
        .tags = {QString(Tag::LayoutEngine)},
    });
    // Corner radius in px. UNSIGNED, unlike the tab indicator: 0 is square, not a pill sentinel.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::SetDropIndicatorBorderRadius),
        .slotFor = constantSlot(ActionSlot::DropIndicatorBorderRadius),
        .validate =
            [](const QJsonObject& p) {
                return hasNumberInRange(p, ActionParam::Value, kMaxDropIndicatorBorderRadius);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Context,
        .params = {P{.key = QString(ActionParam::Value),
                     .kind = QStringLiteral("number"),
                     .min = kMinDropIndicatorBorderRadius,
                     .max = kMaxDropIndicatorBorderRadius,
                     .defaultDisplay = 8.0}},
        .category = QStringLiteral("dropIndicator"),
        .displayOrder = 6,
        .tags = {QString(Tag::LayoutEngine)},
    });

    // ── per-window drop-indicator colours (domain Window) ──
    // Keyed on the DRAGGED window, resolved once at drag start. The only
    // per-window slice of this family with a coherent referent: exactly one
    // window is dragged at a time. They outrank the per-context colours above,
    // which outrank the config, which falls back to the theme — the same order
    // the tab colours use.
    registerAction(ActionDescriptor{
        .type = QString(ActionType::DropIndicatorColor),
        .slotFor = constantSlot(ActionSlot::DragDropIndicatorColor),
        .validate =
            [](const QJsonObject& p) {
                return hasHexColor(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("color")}},
        .category = QStringLiteral("dropIndicator"),
        .displayOrder = 7,
        .tags = {QString(Tag::LayoutEngine)},
    });
    registerAction(ActionDescriptor{
        .type = QString(ActionType::DropIndicatorBorderColor),
        .slotFor = constantSlot(ActionSlot::DragDropIndicatorBorderColor),
        .validate =
            [](const QJsonObject& p) {
                return hasHexColor(p, ActionParam::Value);
            },
        .terminal = false,
        .allowedKeys = {QString(ActionParam::Value)},
        .domain = ActionDomain::Window,
        .params = {P{.key = QString(ActionParam::Value), .kind = QStringLiteral("color")}},
        .category = QStringLiteral("dropIndicator"),
        .displayOrder = 8,
        .tags = {QString(Tag::LayoutEngine)},
    });
}

} // namespace PhosphorRules
