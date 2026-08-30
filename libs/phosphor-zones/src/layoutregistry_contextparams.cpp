// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// The two UNCACHED per-engine context parameter resolvers for LayoutRegistry:
// resolveContextTilingParams (autotile) and resolveContextScrollingParams
// (the scrolling strip, its tab / drop indicators and its behaviour toggles).
// Split out of layoutregistry_contextresolve.cpp on the natural seam: that file
// answers the CACHED, mode-agnostic cascade questions (assignment, gaps, lock,
// default assignment, OSD, overlay), while these two answer a single engine's
// parameter block. They run off screen / layout changes rather than the
// per-cursor hot path, which is why they can stamp the active layout onto their
// query without folding it into any cache key.
//
// The rule-shape guards are shared with the cascade resolvers: see
// layoutregistry_contextresolve.cpp for the polarity rationale behind the
// structural field exclusions and the slot-carrying admit gate, and
// layoutregistry_assignments.cpp for the resolution-model overview.

#include <PhosphorZones/LayoutRegistry.h>

#include "layoutregistry_rulehelpers_p.h"

#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/WindowQuery.h>
#include <PhosphorRules/Rule.h>

#include <QJsonValue>
#include <QSet>

#include <optional>

namespace PhosphorZones {

namespace PWR = PhosphorRules;

using namespace RuleHelpers;

namespace {

/// True if @p s is one of the three hex shapes PhosphorRules' `hasHexColor`
/// admits: `#RGB`, `#RRGGBB` or `#AARRGGBB`. Kept as a local shape test rather
/// than `QColor::isValidColorName`, which is wider (SVG keywords, and the
/// longer `#RRRGGGBBB` forms) and would let "transparent" through to a QML
/// `color` property as an invisible indicator.
bool isHexColorString(const QString& s)
{
    if ((s.size() != 4 && s.size() != 7 && s.size() != 9) || s.at(0) != QLatin1Char('#')) {
        return false;
    }
    for (int i = 1; i < s.size(); ++i) {
        const QChar c = s.at(i);
        const bool hex = (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
            || (c >= QLatin1Char('a') && c <= QLatin1Char('f')) || (c >= QLatin1Char('A') && c <= QLatin1Char('F'));
        if (!hex) {
            return false;
        }
    }
    return true;
}

} // namespace

ContextTilingParams LayoutRegistry::resolveContextTilingParams(const QString& screenId, int virtualDesktop,
                                                               const QString& activity) const
{
    // Per-slot read (mirrors resolveContextGaps), but NOT cached: this runs on
    // screen / layout changes via the daemon's updateEngineScreens, not the hot
    // per-cursor path. Being uncached lets us stamp the active layout AND the
    // screen orientation onto the query without folding either into a cache key
    // (no cached entry to go stale). Safe from recursion: rulesVisibleActiveLayoutId
    // routes through resolveAssignmentEntry, which never calls this resolver.
    // Mode IS stamped (same rationale as resolveContextScrollingParams): the
    // resolver only runs for autotile screens, and a user rule pinning
    // `Mode Equals "tiling"` alongside a tiling-param action would silently
    // never fire against an unstamped query.
    PWR::WindowQuery query = makeContextQuery(screenId, virtualDesktop, activity, QString(PWR::ModeToken::Tiling));
    stampScreenOrientation(query, screenId);
    stampColorScheme(query);
    query.activeLayout = rulesVisibleActiveLayoutId(screenId, virtualDesktop, activity);
    // Filtered resolve, but with NO managed catch-all exclusion (unlike
    // resolveContextGaps'): the baseline rule carries only gap/default slots,
    // never tiling params, so there is no catch-all to exclude here. The
    // slot-carrying gate below is resolveContextGaps' though, and load-bearing
    // for the same reason: the walk STOPS at the first admitted rule carrying
    // an in-scope terminal action (Exclude), so admitting a rule that fills
    // none of these slots lets a higher-priority context Exclude drop every
    // tiling-param override below it.
    static const QSet<QString> tilingSlots = {
        QString(PWR::ActionSlot::MaxWindows),       QString(PWR::ActionSlot::SplitRatio),
        QString(PWR::ActionSlot::MasterCount),      QString(PWR::ActionSlot::InsertPosition),
        QString(PWR::ActionSlot::OverflowBehavior), QString(PWR::ActionSlot::DragBehavior),
        QString(PWR::ActionSlot::AlgorithmParams),
    };
    const PWR::ActionRegistry& registry = PWR::ActionRegistry::instance();
    const PWR::ResolvedActions resolved = m_evaluator->resolveFiltered(query, [&registry](const PWR::Rule& r) {
        // TiledWindowCount is not stamped here either, and an absent field
        // makes a leaf false, so a negated leaf on it would match every
        // context. Mode IS stamped above, so it stays admitted. Window-sourced
        // fields carry the negation-scoped guard (positive leaves stay inert
        // by design; a `none{}` leaf inverts on absence).
        if (r.match.referencesAnyField({PWR::Field::TiledWindowCount})
            || r.match.negatesAnyField(PWR::windowSourcedFields())) {
            return false;
        }
        for (const PWR::RuleAction& a : r.actions) {
            if (tilingSlots.contains(registry.slotFor(a))) {
                return true;
            }
        }
        return false;
    });

    ContextTilingParams params;
    // No defense-in-depth clamps here, unlike the scrolling resolver's
    // width bound: the tile engine re-clamps every one of these on
    // consumption (maxWindows/masterCount floors, split-ratio bounds), so a
    // second clamp would only duplicate its policy. The scrolling width is
    // clamped at THIS layer because the strip consumes it raw.
    // Type-gated reads, the REJECT AND FALL THROUGH policy the scrolling twin's
    // readFraction applies: a hand-edited rules.json carrying a string (or no)
    // Value would read back 0 and APPLY an override the user never wrote (a
    // maxWindows of 0, a split ratio of 0.0), which is the opposite of falling
    // through to the configured value. And qRound(toDouble()) for the ints, not
    // toInt(): QJsonValue::toInt() returns its default for a non-whole number,
    // so a `"value": 3.5` would resolve 0 rather than 3 (the same trap the gap
    // resolver's readInt documents).
    // Reject-and-fall-through on both the type AND the integrality, matching
    // what the descriptors themselves enforce: SetMaxWindows and SetMasterCount
    // both refuse a non-integral payload rather than truncating it, so a
    // resolver that rounded here would resolve a count the validator would
    // never have admitted. QJsonValue::toInt()'s zero default is the thing
    // being avoided — an unset slot must read as "config wins", never as 0.
    const auto readParamInt = [&resolved](QLatin1StringView slot, std::optional<int>& out) {
        const auto action = resolved.slot(QString(slot));
        if (!action) {
            return;
        }
        const QJsonValue v = action->params.value(PWR::ActionParam::Value);
        if (!v.isDouble()) {
            return;
        }
        const double d = v.toDouble();
        if (static_cast<double>(static_cast<int>(d)) != d) {
            return;
        }
        out = static_cast<int>(d);
    };
    readParamInt(PWR::ActionSlot::MaxWindows, params.maxWindows);
    readParamInt(PWR::ActionSlot::MasterCount, params.masterCount);
    if (const auto action = resolved.slot(QString(PWR::ActionSlot::SplitRatio))) {
        if (const QJsonValue v = action->params.value(PWR::ActionParam::Value); v.isDouble()) {
            params.splitRatio = v.toDouble();
        }
    }
    if (const auto action = resolved.slot(QString(PWR::ActionSlot::InsertPosition))) {
        // Wire token → AutotileInsertPosition int (End 0 / AfterFocused 1 / AsMaster 2),
        // the same value the per-screen config store holds.
        const QString token = action->params.value(PWR::ActionParam::Value).toString();
        if (token == PWR::InsertPositionToken::End) {
            params.insertPosition = 0;
        } else if (token == PWR::InsertPositionToken::AfterFocused) {
            params.insertPosition = 1;
        } else if (token == PWR::InsertPositionToken::AsMaster) {
            params.insertPosition = 2;
        }
    }
    if (const auto action = resolved.slot(QString(PWR::ActionSlot::OverflowBehavior))) {
        // Wire token → AutotileOverflowBehavior int (Float 0 / Unlimited 1).
        const QString token = action->params.value(PWR::ActionParam::Value).toString();
        if (token == PWR::OverflowBehaviorToken::Float) {
            params.overflowBehavior = 0;
        } else if (token == PWR::OverflowBehaviorToken::Unlimited) {
            params.overflowBehavior = 1;
        }
    }
    if (const auto action = resolved.slot(QString(PWR::ActionSlot::DragBehavior))) {
        // Wire token → AutotileDragBehavior int (Float 0 / Reorder 1).
        const QString token = action->params.value(PWR::ActionParam::Value).toString();
        if (token == PWR::DragBehaviorToken::Float) {
            params.dragBehavior = 0;
        } else if (token == PWR::DragBehaviorToken::Reorder) {
            params.dragBehavior = 1;
        }
    }
    if (const auto action = resolved.slot(QString(PWR::ActionSlot::AlgorithmParams))) {
        // Target algorithm token + free-form custom-param blob (mirrors the
        // overlay shader-uniform override). The daemon applies the params only
        // when the target matches the screen's effective algorithm.
        params.algorithmParamTarget = action->params.value(PWR::ActionParam::Algorithm).toString();
        params.algorithmParams = action->params.value(PWR::ActionParam::Params).toObject().toVariantMap();
    }
    return params;
}

ContextScrollingParams LayoutRegistry::resolveContextScrollingParams(const QString& screenId, int virtualDesktop,
                                                                     const QString& activity) const
{
    // Per-slot read, uncached for the same reasons resolveContextTilingParams is:
    // it runs on screen / layout changes rather than the hot per-cursor path, which
    // lets the query carry the active layout and the screen orientation without
    // folding either into a cache key.
    // Field::Mode IS stamped: this resolver only runs for screens the
    // cascade already put in Scrolling mode, so the stamp costs nothing —
    // but a user rule that pins `Mode Equals "scrolling"` alongside a
    // scroll-param action (a redundant-but-legal spelling) would silently
    // never fire against an unstamped query.
    PWR::WindowQuery query = makeContextQuery(screenId, virtualDesktop, activity, QString(PWR::ModeToken::Scrolling));
    stampScreenOrientation(query, screenId);
    stampColorScheme(query);
    query.activeLayout = rulesVisibleActiveLayoutId(screenId, virtualDesktop, activity);
    // Filtered resolve with no managed catch-all exclusion: same baseline-slot
    // rationale as the tiling-param resolver above. The slot-carrying gate is
    // the same one too, and load-bearing for the same reason: the walk STOPS at
    // the first admitted rule carrying an in-scope terminal action (Exclude),
    // so a rule filling none of these slots must never be admitted.
    static const QSet<QString> scrollingSlots = {
        QString(PWR::ActionSlot::ScrollDefaultColumnWidth),
        QString(PWR::ActionSlot::CenterFocusedColumn),
        QString(PWR::ActionSlot::ScrollDefaultColumnDisplay),
        QString(PWR::ActionSlot::ScrollInsertPosition),
        QString(PWR::ActionSlot::ScrollDefaultWindowHeight),
        QString(PWR::ActionSlot::TabIndicatorEnabled),
        QString(PWR::ActionSlot::TabIndicatorHideWhenSingleTab),
        QString(PWR::ActionSlot::TabIndicatorPlaceWithinColumn),
        QString(PWR::ActionSlot::TabIndicatorGap),
        QString(PWR::ActionSlot::TabIndicatorWidth),
        QString(PWR::ActionSlot::TabIndicatorGapsBetweenTabs),
        QString(PWR::ActionSlot::TabIndicatorCornerRadius),
        QString(PWR::ActionSlot::TabIndicatorActiveColor),
        QString(PWR::ActionSlot::TabIndicatorInactiveColor),
        QString(PWR::ActionSlot::TabIndicatorUrgentColor),
        QString(PWR::ActionSlot::TabIndicatorLength),
        QString(PWR::ActionSlot::TabIndicatorStyle),
        QString(PWR::ActionSlot::TabIndicatorPosition),
        QString(PWR::ActionSlot::TabIndicatorFontFamily),
        QString(PWR::ActionSlot::TabIndicatorFontWeight),
        QString(PWR::ActionSlot::TabIndicatorFontItalic),
        QString(PWR::ActionSlot::TabIndicatorFontUnderline),
        QString(PWR::ActionSlot::TabIndicatorFontStrikeout),
        QString(PWR::ActionSlot::DropIndicatorEnabled),
        QString(PWR::ActionSlot::DropIndicatorColor),
        QString(PWR::ActionSlot::DropIndicatorBorderColor),
        QString(PWR::ActionSlot::DropIndicatorBorderWidth),
        QString(PWR::ActionSlot::DropIndicatorBorderRadius),
        QString(PWR::ActionSlot::DropIndicatorOpacity),
        QString(PWR::ActionSlot::ScrollAlwaysCenterSingleColumn),
        QString(PWR::ActionSlot::ScrollRespectMinimumSize),
        QString(PWR::ActionSlot::ScrollCropStraddlers),
        QString(PWR::ActionSlot::ScrollFocusNewWindows),
        QString(PWR::ActionSlot::ScrollSmartGaps),
        QString(PWR::ActionSlot::ScrollFocusFollowsMouse),
        QString(PWR::ActionSlot::ScrollFocusFollowsMouseMaxScroll),
        QString(PWR::ActionSlot::ScrollStickyWindowHandling),
        QString(PWR::ActionSlot::ScrollStripAxis),
    };
    const PWR::ActionRegistry& registry = PWR::ActionRegistry::instance();
    const PWR::ResolvedActions resolved = m_evaluator->resolveFiltered(query, [&registry](const PWR::Rule& r) {
        // TiledWindowCount is not stamped here, and an absent field makes a
        // leaf false, so a negated leaf on it would match every context. Mode
        // IS stamped above, so it stays admitted. Window-sourced fields carry
        // the negation-scoped guard, same as the tiling-param twin.
        if (r.match.referencesAnyField({PWR::Field::TiledWindowCount})
            || r.match.negatesAnyField(PWR::windowSourcedFields())) {
            return false;
        }
        for (const PWR::RuleAction& a : r.actions) {
            if (scrollingSlots.contains(registry.slotFor(a))) {
                return true;
            }
        }
        return false;
    });

    ContextScrollingParams params;
    // Defense in depth for the two fraction slots (the descriptor validator
    // already rejects out-of-range payloads at load). The policy is REJECT AND
    // FALL THROUGH, not clamp, matching the open path in
    // WindowTrackingAdaptor::scrollOpenRuleParams: a hand-edited rules.json
    // carrying a non-numeric Value would toDouble() to 0.0 and CLAMP UP to the
    // 5% minimum, and a 50.0 would saturate to full width — both of them
    // applying an override the user never wrote. Left unset, the field falls
    // through to the configured default instead. The bounds are the installed
    // PhosphorRules constants, the same pair the descriptor validator checks —
    // which is why the bounds are per call rather than baked in: the tab
    // indicator's length and the drop indicator's opacity are read through this
    // same helper further down against their own descriptors' pairs. The two
    // column fractions share one pair: it is named for column WIDTH but bounds
    // the window HEIGHT fraction too (see the Min/MaxColumnWidthRatio doc).
    const auto readFraction = [&resolved](QLatin1StringView slot, std::optional<double>& out, double lo, double hi) {
        const auto action = resolved.slot(QString(slot));
        if (!action) {
            return;
        }
        const QJsonValue v = action->params.value(PWR::ActionParam::Value);
        const double fraction = v.toDouble();
        if (v.isDouble() && fraction >= lo && fraction <= hi) {
            out = fraction;
        }
    };
    readFraction(PWR::ActionSlot::ScrollDefaultColumnWidth, params.defaultColumnWidth, PWR::MinColumnWidthRatio,
                 PWR::MaxColumnWidthRatio);
    if (const auto action = resolved.slot(QString(PWR::ActionSlot::CenterFocusedColumn))) {
        // Wire token → the centering int (never 0 / always 1 / on overflow 2), the
        // same value the config store holds.
        const QString token = action->params.value(PWR::ActionParam::Value).toString();
        if (token == PWR::CenterFocusedColumnToken::Never) {
            params.centerFocusedColumn = 0;
        } else if (token == PWR::CenterFocusedColumnToken::Always) {
            params.centerFocusedColumn = 1;
        } else if (token == PWR::CenterFocusedColumnToken::OnOverflow) {
            params.centerFocusedColumn = 2;
        }
    }
    if (const auto action = resolved.slot(QString(PWR::ActionSlot::ScrollDefaultColumnDisplay))) {
        // Wire token → the column display int (normal 0 / tabbed 1).
        const QString token = action->params.value(PWR::ActionParam::Value).toString();
        if (token == PWR::ColumnDisplayToken::Normal) {
            params.defaultColumnDisplay = 0;
        } else if (token == PWR::ColumnDisplayToken::Tabbed) {
            params.defaultColumnDisplay = 1;
        }
    }
    if (const auto action = resolved.slot(QString(PWR::ActionSlot::ScrollInsertPosition))) {
        // Wire token → the ScrollInsertPosition int the engine consumes.
        const QString token = action->params.value(PWR::ActionParam::Value).toString();
        if (token == PWR::ScrollInsertPositionToken::RightOfActive) {
            params.insertPosition = 0;
        } else if (token == PWR::ScrollInsertPositionToken::LeftOfActive) {
            params.insertPosition = 1;
        } else if (token == PWR::ScrollInsertPositionToken::First) {
            params.insertPosition = 2;
        } else if (token == PWR::ScrollInsertPositionToken::Last) {
            params.insertPosition = 3;
        } else if (token == PWR::ScrollInsertPositionToken::IntoActiveColumn) {
            params.insertPosition = 4;
        }
    }
    readFraction(PWR::ActionSlot::ScrollDefaultWindowHeight, params.defaultWindowHeight, PWR::MinColumnWidthRatio,
                 PWR::MaxColumnWidthRatio);

    // ── tab indicator (niri's `tab-indicator` layout block) ──
    // Same REJECT AND FALL THROUGH policy as readFraction above: a hand-edited
    // rules.json carrying the wrong JSON type must leave the field unset so it
    // falls through to the configured value, never coerce to 0 and apply an
    // override the user did not write. The descriptor validators already
    // reject these at load; this is the defence in depth for what bypasses
    // them.
    const auto readBool = [&resolved](QLatin1StringView slot, std::optional<bool>& out) {
        const auto action = resolved.slot(QString(slot));
        if (!action) {
            return;
        }
        const QJsonValue v = action->params.value(PWR::ActionParam::Value);
        if (v.isBool()) {
            out = v.toBool();
        }
    };
    // Bounds are the descriptors' own, so a value the validator accepts is a
    // value this resolver accepts.
    const auto readInt = [&resolved](QLatin1StringView slot, std::optional<int>& out, double lo, double hi) {
        const auto action = resolved.slot(QString(slot));
        if (!action) {
            return;
        }
        const QJsonValue v = action->params.value(PWR::ActionParam::Value);
        const double d = v.toDouble();
        if (v.isDouble() && d >= lo && d <= hi) {
            out = qRound(d);
        }
    };
    // Free-string slots. Stores an EMPTY string rather than treating it as
    // unset, matching the descriptor's hasStringAllowingEmpty: for the label's
    // font family, empty is the user asking for the system font, and is the
    // only way a rule walks one screen back to the default after a global
    // family was picked. Do not "tidy" this into an isEmpty() guard.
    const auto readString = [&resolved](QLatin1StringView slot, std::optional<QString>& out) {
        const auto action = resolved.slot(QString(slot));
        if (!action) {
            return;
        }
        const QJsonValue v = action->params.value(PWR::ActionParam::Value);
        if (v.isString()) {
            out = v.toString();
        }
    };
    const auto readColor = [&resolved](QLatin1StringView slot, std::optional<QString>& out) {
        const auto action = resolved.slot(QString(slot));
        if (!action) {
            return;
        }
        const QJsonValue v = action->params.value(PWR::ActionParam::Value);
        // Hex shapes only, matching the descriptors' hasHexColor exactly.
        // This helper passes the string through verbatim to a QML `color`
        // property, so a store that bypassed the loader's validation must not
        // get its string through here either. Deliberately NOT
        // QColor::isValidColorName, which is WIDER than the descriptor: it
        // also admits SVG keywords, and "transparent" would reach the overlay
        // as a fully invisible indicator while every setting reported it on.
        if (v.isString() && isHexColorString(v.toString())) {
            out = v.toString();
        }
    };

    readBool(PWR::ActionSlot::TabIndicatorEnabled, params.tabIndicatorEnabled);
    readBool(PWR::ActionSlot::TabIndicatorHideWhenSingleTab, params.tabIndicatorHideWhenSingleTab);
    readBool(PWR::ActionSlot::TabIndicatorPlaceWithinColumn, params.tabIndicatorPlaceWithinColumn);
    readInt(PWR::ActionSlot::TabIndicatorGap, params.tabIndicatorGap, PWR::MinTabIndicatorGap, PWR::MaxTabIndicatorGap);
    readInt(PWR::ActionSlot::TabIndicatorWidth, params.tabIndicatorWidth, PWR::MinTabIndicatorWidth,
            PWR::MaxTabIndicatorWidth);
    readInt(PWR::ActionSlot::TabIndicatorGapsBetweenTabs, params.tabIndicatorGapsBetweenTabs, 0,
            PWR::MaxTabIndicatorGap);
    readInt(PWR::ActionSlot::TabIndicatorCornerRadius, params.tabIndicatorCornerRadius,
            PWR::TabIndicatorCornerRadiusPill, PWR::MaxTabIndicatorCornerRadius);
    readColor(PWR::ActionSlot::TabIndicatorActiveColor, params.tabIndicatorActiveColor);
    readColor(PWR::ActionSlot::TabIndicatorInactiveColor, params.tabIndicatorInactiveColor);
    readColor(PWR::ActionSlot::TabIndicatorUrgentColor, params.tabIndicatorUrgentColor);
    // The label font. No size slot to read: the painter fits the label to the
    // pill thickness, so there is nothing for a size to change.
    readString(PWR::ActionSlot::TabIndicatorFontFamily, params.tabIndicatorFontFamily);
    readInt(PWR::ActionSlot::TabIndicatorFontWeight, params.tabIndicatorFontWeight, PWR::MinTabIndicatorFontWeight,
            PWR::MaxTabIndicatorFontWeight);
    readBool(PWR::ActionSlot::TabIndicatorFontItalic, params.tabIndicatorFontItalic);
    readBool(PWR::ActionSlot::TabIndicatorFontUnderline, params.tabIndicatorFontUnderline);
    readBool(PWR::ActionSlot::TabIndicatorFontStrikeout, params.tabIndicatorFontStrikeout);

    // Drop indicator. Same per-property cascade as the tab indicator above, so
    // a theme rule can set the colours while a separate rule turns it off.
    readBool(PWR::ActionSlot::DropIndicatorEnabled, params.dropIndicatorEnabled);
    readColor(PWR::ActionSlot::DropIndicatorColor, params.dropIndicatorColor);
    readColor(PWR::ActionSlot::DropIndicatorBorderColor, params.dropIndicatorBorderColor);
    readInt(PWR::ActionSlot::DropIndicatorBorderWidth, params.dropIndicatorBorderWidth,
            PWR::MinDropIndicatorBorderWidth, PWR::MaxDropIndicatorBorderWidth);
    readInt(PWR::ActionSlot::DropIndicatorBorderRadius, params.dropIndicatorBorderRadius,
            PWR::MinDropIndicatorBorderRadius, PWR::MaxDropIndicatorBorderRadius);
    // Fractions, not ints, so they go through readFraction with their own
    // descriptors' bounds — the same pair the validator checks, so a
    // hand-edited rule cannot smuggle an out-of-range value past the authoring
    // UI.
    readFraction(PWR::ActionSlot::DropIndicatorOpacity, params.dropIndicatorOpacity, PWR::MinDropIndicatorOpacity,
                 PWR::MaxDropIndicatorOpacity);
    readFraction(PWR::ActionSlot::TabIndicatorLength, params.tabIndicatorLength, PWR::MinTabIndicatorLengthRatio,
                 PWR::MaxTabIndicatorLengthRatio);
    if (const auto action = resolved.slot(QString(PWR::ActionSlot::TabIndicatorStyle))) {
        // Wire token → the style int (chips 0 / bar 1).
        const QString token = action->params.value(PWR::ActionParam::Value).toString();
        if (token == PWR::TabIndicatorStyleToken::Chips) {
            params.tabIndicatorStyle = 0;
        } else if (token == PWR::TabIndicatorStyleToken::Bar) {
            params.tabIndicatorStyle = 1;
        }
    }
    if (const auto action = resolved.slot(QString(PWR::ActionSlot::TabIndicatorPosition))) {
        // Wire token → TabIndicatorPosition (left 0 / right 1 / top 2 / bottom 3).
        const QString token = action->params.value(PWR::ActionParam::Value).toString();
        if (token == PWR::TabIndicatorPositionToken::Left) {
            params.tabIndicatorPosition = 0;
        } else if (token == PWR::TabIndicatorPositionToken::Right) {
            params.tabIndicatorPosition = 1;
        } else if (token == PWR::TabIndicatorPositionToken::Top) {
            params.tabIndicatorPosition = 2;
        } else if (token == PWR::TabIndicatorPositionToken::Bottom) {
            params.tabIndicatorPosition = 3;
        }
    }

    // ── scrolling behaviour toggles ──
    // Read last so they share readBool's reject-and-fall-through policy: a
    // hand-edited non-bool leaves the field unset and the engine keeps its
    // configured value, rather than coercing to false and silently disabling
    // a behaviour the user never turned off.
    readBool(PWR::ActionSlot::ScrollAlwaysCenterSingleColumn, params.alwaysCenterSingleColumn);
    readBool(PWR::ActionSlot::ScrollCenterShortColumns, params.centerShortColumns);
    readBool(PWR::ActionSlot::ScrollRespectMinimumSize, params.respectMinimumSize);
    readBool(PWR::ActionSlot::ScrollCropStraddlers, params.cropStraddlers);
    readBool(PWR::ActionSlot::ScrollFocusNewWindows, params.focusNewWindows);
    readBool(PWR::ActionSlot::ScrollSmartGaps, params.smartGaps);
    readBool(PWR::ActionSlot::ScrollFocusFollowsMouse, params.focusFollowsMouse);
    readFraction(PWR::ActionSlot::ScrollFocusFollowsMouseMaxScroll, params.focusFollowsMouseMaxScroll,
                 PWR::MinFocusFollowsMouseMaxScrollRatio, PWR::MaxFocusFollowsMouseMaxScrollRatio);
    if (const auto action = resolved.slot(QString(PWR::ActionSlot::ScrollStickyWindowHandling))) {
        // Wire token → the StickyWindowHandling int the config store holds
        // (treatAsNormal 0 / restoreOnly 1 / ignoreAll 2).
        const QString token = action->params.value(PWR::ActionParam::Value).toString();
        if (token == PWR::StickyWindowHandlingToken::TreatAsNormal) {
            params.stickyWindowHandling = 0;
        } else if (token == PWR::StickyWindowHandlingToken::RestoreOnly) {
            params.stickyWindowHandling = 1;
        } else if (token == PWR::StickyWindowHandlingToken::IgnoreAll) {
            params.stickyWindowHandling = 2;
        }
    }
    if (const auto action = resolved.slot(QString(PWR::ActionSlot::ScrollStripAxis))) {
        // Wire token → the Scrolling.StripAxis config int (auto 0 /
        // horizontal 1 / vertical 2). Same closed-vocabulary fall-through as
        // its neighbours: an unrecognized token leaves the field unset.
        const QString token = action->params.value(PWR::ActionParam::Value).toString();
        if (token == PWR::StripAxisToken::Auto) {
            params.stripAxis = 0;
        } else if (token == PWR::StripAxisToken::Horizontal) {
            params.stripAxis = 1;
        } else if (token == PWR::StripAxisToken::Vertical) {
            params.stripAxis = 2;
        }
    }
    return params;
}

} // namespace PhosphorZones
