// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Admission tests shared by the WindowTrackingAdaptor rule resolvers (rules.cpp:
// the restore predicates, the open-float gate, the tab-colour / drop-indicator /
// scrolling-open param builders; rules_placement.cpp: placement-zone
// resolution and screen/desktop open-routing). Every resolveCachedFiltered
// caller in both TUs must pass an EQUIVALENT admission for the same stamping
// shape, because the evaluator memo is keyed on (windowId, revision) only and
// the first resolver to touch a window seeds the verdict the others reuse. Keeping
// the tests in one header is what keeps that precondition checkable.

#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/WindowQuery.h>

#include <QSet>

#include <functional>
#include <utility>

namespace PlasmaZones::RuleAdmission {

/// Structural admission tests for the open-path resolvers, one per stamping
/// shape. Every one of them is the same guard the zones-layer context resolvers
/// apply (layoutregistry_contextresolve.cpp), for the same reason.
///
/// An UNSTAMPED context field is not merely inert. WindowQuery::valueForField
/// returns an ENGAGED empty string for the string-valued context fields, so a
/// POSITIVE leaf (`Mode Equals scrolling`) correctly never matches — but a
/// NEGATED one (`None{Mode Equals scrolling}`) matches precisely BECAUSE the
/// inner leaf failed, and the rule then fires for EVERY window. Excluding rules
/// that reference an unstamped field closes both polarities instead of relying
/// on the empty value to coincide with a non-match.
///
/// TiledWindowCount is stamped by NO resolver on this path (it is a
/// context-cascade field), so it is excluded everywhere here. ActiveLayout and
/// ScreenOrientation used to sit beside it, but stampScreenContext now stamps
/// the six screen-derived fields whenever a screen is stamped, so they are
/// excluded only where the screen itself is unstamped (see
/// screenDerivedContextFields / admitNothingStamped). The four shape fields
/// can still be unknown on a stamped screen, which admitWith guards.
inline const QSet<PhosphorRules::Field>& neverStampedFields()
{
    static const QSet<PhosphorRules::Field> fields = {
        PhosphorRules::Field::TiledWindowCount,
    };
    return fields;
}

/// The context fields stampScreenContext derives FROM the screen. With no
/// screen stamped none of them can be answered, so a resolver that stamped no
/// screen must exclude rules referencing any of them (both polarities — the
/// negated-leaf inversion documented above). With a screen stamped, ScreenId
/// and ActiveLayout are always answered, but the four shape fields only once
/// the screen's shape is known: the size, and the aspect ratio derived from
/// it, need the compositor's report or the last session's record of it, and
/// the orientation needs that or the screen's geometry. unknownScreenShapeFields
/// names the ones a given query leaves unanswered.
inline const QSet<PhosphorRules::Field>& screenDerivedContextFields()
{
    static const QSet<PhosphorRules::Field> fields = {
        PhosphorRules::Field::ScreenId,    PhosphorRules::Field::ActiveLayout, PhosphorRules::Field::ScreenOrientation,
        PhosphorRules::Field::ScreenWidth, PhosphorRules::Field::ScreenHeight, PhosphorRules::Field::ScreenAspectRatio,
    };
    return fields;
}

/// The WINDOW-sourced fields buildRuleQueryForWindow cannot answer, so a
/// negated leaf on one of them would invert and fire for every window.
///
/// Deliberately NOT PhosphorRules::windowSourcedFields(): unlike the windowless
/// context resolvers, this query DOES engage most window fields (appId, title,
/// role, type, pid, the state / geometry / accessory flags, captionNormal), and
/// excluding a rule that merely negates one of those would drop legitimate
/// user rules. Only these five are unanswerable here — WindowClass is not
/// tracked daemon-side at all (the compositor reports appId), and the four
/// placement fields describe a placement that does not exist yet on the open
/// path, which is exactly what the buildRuleQueryForWindow doc calls "inert".
/// Inert holds for a POSITIVE leaf only; this closes the negated half.
inline const QSet<PhosphorRules::Field>& unanswerableWindowFields()
{
    static const QSet<PhosphorRules::Field> fields = {
        PhosphorRules::Field::WindowClass, PhosphorRules::Field::IsFloating, PhosphorRules::Field::IsSnapped,
        PhosphorRules::Field::Zone,        PhosphorRules::Field::IsTiled,
    };
    return fields;
}

/// Admission test for a resolver that stamps ScreenId but NOT Mode — the shape
/// every resolveCached caller on this path shares. Passed identically by all
/// six so the memo they share stays coherent (see resolveCachedFiltered's
/// precondition).
///
/// CONSEQUENCE FOR RULE AUTHORS: the open-routing channels
/// (applyOpenDesktopRouting, applyOpenScreenRouting, applyOpenRoutingForTiling)
/// resolve under this test, so a rule whose match references Field::Mode never
/// applies its RouteToScreen / RouteToDesktop / RouteToWorkspace actions. The
/// exclusion is not a local oversight: all six cached callers must pass an
/// EQUIVALENT admit or the memo they share stops being coherent, so lifting it
/// for three of them is not a valid change. Making Mode-scoped routing work
/// means stamping Mode on all six (they do not all have the desktop and
/// activity a mode lookup needs) or splitting the memo — a rules-library
/// change, not a call-site one.
inline bool admitScreenStamped(const PhosphorRules::Rule& rule)
{
    return !rule.match.referencesAnyField(neverStampedFields())
        && !rule.match.referencesAnyField({PhosphorRules::Field::Mode})
        && !rule.match.negatesAnyField(unanswerableWindowFields());
}

/// Admission test for a resolver that stamps BOTH ScreenId and Mode
/// (shouldFloatByRule, scrollOpenRuleParams, shouldRestoreSizeOnUnsnap). Mode
/// stays admitted; only the never-stamped context fields are excluded, plus
/// the negated-only guard on the window fields this query cannot answer.
inline bool admitScreenAndModeStamped(const PhosphorRules::Rule& rule)
{
    return !rule.match.referencesAnyField(neverStampedFields())
        && !rule.match.negatesAnyField(unanswerableWindowFields());
}

/// Admission test for a query that stamps NO context field at all (the
/// tab-colour and drop-indicator paths, and any stamper whose inputs came
/// up empty): the screen-derived fields (ScreenId, ActiveLayout and the four
/// shape fields) join the exclusions, because valueForField answers an
/// ENGAGED empty for the string-valued ones and an absent value for the
/// sizes, and a `None{ScreenId Equals …}` group would then match for every
/// window — the same inversion documented above. The unanswerable-window-field
/// negation guard rides in from admitScreenStamped.
///
/// The name is about the CONTEXT stamps only: the window fields
/// buildRuleQueryForWindow does engage (appId, title, type, the state flags)
/// are answerable on all of these paths and stay admitted. VirtualDesktop,
/// Activity and ColorScheme are stamped by the builder itself, so they are
/// admitted here too — the fields this test additionally excludes over
/// admitScreenStamped are the screen-derived fields and (via that test) Mode.
inline bool admitNothingStamped(const PhosphorRules::Rule& rule)
{
    return admitScreenStamped(rule) && !rule.match.referencesAnyField(screenDerivedContextFields());
}

/// The admission test matching what the stampers actually LEFT on @p query,
/// rather than what the caller intended to stamp: an empty screen id returns
/// without stamping anything (stampScreenContext / stampScreenAndMode), and a
/// caller that stamped the screen but not Mode (stampScreenContext alone)
/// leaves Mode empty — admitting rules that reference an unstamped field
/// re-opens the negated-leaf inversion for that field. The layout registry is a
/// fatal-guarded constructor dependency, so "registry missing" is not a case
/// this has to cover.
inline auto admissionForStamped(const PhosphorRules::WindowQuery& query)
{
    if (query.screenId.isEmpty()) {
        return &admitNothingStamped;
    }
    return query.mode.isEmpty() ? &admitScreenStamped : &admitScreenAndModeStamped;
}

/// The screen-shape fields @p query leaves unanswered: ScreenOrientation and
/// ScreenAspectRatio while their token is empty, ScreenWidth and ScreenHeight
/// while disengaged. Mirrors PhosphorZones::RuleHelpers::unknownScreenShapeFields
/// (layoutregistry_rulehelpers_p.h, private to phosphor-zones, so it cannot be
/// called from here), which every context resolver there applies, so a window
/// rule and a context rule refuse the same shape leaves on the same screen.
inline QSet<PhosphorRules::Field> unknownScreenShapeFields(const PhosphorRules::WindowQuery& query)
{
    QSet<PhosphorRules::Field> unknown;
    if (query.screenOrientation.isEmpty()) {
        unknown.insert(PhosphorRules::Field::ScreenOrientation);
    }
    if (!query.screenWidth) {
        unknown.insert(PhosphorRules::Field::ScreenWidth);
    }
    if (!query.screenHeight) {
        unknown.insert(PhosphorRules::Field::ScreenHeight);
    }
    if (query.screenAspectRatio.isEmpty()) {
        unknown.insert(PhosphorRules::Field::ScreenAspectRatio);
    }
    return unknown;
}

/// Bind @p base to the guards @p query needs beyond its stamping shape.
///
/// ColorScheme: buildRuleQueryForWindow stamps the colour-scheme token from the
/// injected settings, but the token comes up EMPTY in a process with no GUI
/// application (or off the GUI thread, which the accessor refuses). An engaged
/// empty makes the leaf fail, so `None{ColorScheme Equals dark}` would invert
/// and fire for every window — the same defect the field sets above close,
/// except that here the answer depends on the process rather than the field,
/// so it cannot live in a static set.
///
/// The screen shape: a stamped screen whose size the daemon has not learned
/// yet (no compositor report and no record from the last session) leaves the
/// size and aspect fields unanswered, and the orientation too when its
/// geometry is unknown. A positive leaf on one of them is false, the inert
/// outcome, but `None{ScreenWidth Equals 5120}` would invert and match every
/// window, so a rule negating an unanswered shape field is refused. Once the
/// shape is known a negated shape rule is legitimate and admitted. On a query
/// with no screen stamped every shape field is unanswered, which only repeats
/// what admitNothingStamped already excludes.
///
/// Both guards follow from the query rather than from the resolver: the
/// colour-scheme emptiness is a property of the PROCESS, and the shape is a
/// property of the SCREEN at the moment of the resolve. The shared-memo
/// callers that stamp a screen stamp the same one for a window in the same
/// turn of its open, so they compute the same guard and
/// resolveCachedFiltered's equivalent-admit precondition still holds. The two
/// that stamp nothing read the memo only after a stamper has seeded it (the
/// ordering invariant documented above shouldRestoreFloatedPosition), so their
/// wider shape guard is never the admission the memo keeps.
inline std::function<bool(const PhosphorRules::Rule&)> admitWith(bool (*base)(const PhosphorRules::Rule&),
                                                                 const PhosphorRules::WindowQuery& query)
{
    const bool guardColorScheme = query.colorScheme.isEmpty();
    QSet<PhosphorRules::Field> unknownShape = unknownScreenShapeFields(query);
    if (!guardColorScheme && unknownShape.isEmpty()) {
        return base;
    }
    return [base, guardColorScheme, unknownShape = std::move(unknownShape)](const PhosphorRules::Rule& rule) {
        if (!base(rule)) {
            return false;
        }
        if (guardColorScheme && rule.match.negatesAnyField({PhosphorRules::Field::ColorScheme})) {
            return false;
        }
        return unknownShape.isEmpty() || !rule.match.negatesAnyField(unknownShape);
    };
}

} // namespace PlasmaZones::RuleAdmission
