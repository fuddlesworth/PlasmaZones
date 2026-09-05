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
/// both whenever a screen is stamped, so they are excluded only where the
/// screen itself is unstamped (see screenDerivedContextFields /
/// admitNothingStamped).
inline const QSet<PhosphorRules::Field>& neverStampedFields()
{
    static const QSet<PhosphorRules::Field> fields = {
        PhosphorRules::Field::TiledWindowCount,
    };
    return fields;
}

/// The context fields stampScreenContext derives FROM the screen: engaged and
/// meaningful exactly when a screen was stamped, unanswerable otherwise. A
/// resolver that stamped no screen must exclude rules referencing any of them
/// (both polarities — the negated-leaf inversion documented above).
inline const QSet<PhosphorRules::Field>& screenDerivedContextFields()
{
    static const QSet<PhosphorRules::Field> fields = {
        PhosphorRules::Field::ScreenId,
        PhosphorRules::Field::ActiveLayout,
        PhosphorRules::Field::ScreenOrientation,
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
/// up empty): the screen-derived trio (ScreenId, ActiveLayout,
/// ScreenOrientation) joins the exclusions, because valueForField answers
/// an ENGAGED empty for each and a `None{ScreenId Equals …}` group would then
/// match for every window — the same inversion documented above. The
/// unanswerable-window-field negation guard rides in from admitScreenStamped.
///
/// The name is about the CONTEXT stamps only: the window fields
/// buildRuleQueryForWindow does engage (appId, title, type, the state flags)
/// are answerable on all of these paths and stay admitted. VirtualDesktop,
/// Activity and ColorScheme are stamped by the builder itself, so they are
/// admitted here too — the fields this test additionally excludes over
/// admitScreenStamped are the screen-derived trio and (via that test) Mode.
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

/// Bind @p base to the ColorScheme guard @p query needs.
///
/// buildRuleQueryForWindow stamps the colour-scheme token from the injected
/// settings, but the token comes up EMPTY in a process with no GUI application (or off
/// the GUI thread, which the accessor refuses). An engaged empty makes the leaf
/// fail, so `None{ColorScheme Equals dark}` would invert and fire for every
/// window — the same defect the field sets above close, except that here the
/// answer depends on the process rather than the field, so it cannot live in a
/// static set.
///
/// The emptiness is a property of the PROCESS (does it have a GUI application),
/// not of the individual resolver, so every one of the six shared-memo callers
/// computes the same value and resolveCachedFiltered's equivalent-admit
/// precondition still holds.
inline std::function<bool(const PhosphorRules::Rule&)> admitWith(bool (*base)(const PhosphorRules::Rule&),
                                                                 const PhosphorRules::WindowQuery& query)
{
    if (!query.colorScheme.isEmpty()) {
        return base;
    }
    return [base](const PhosphorRules::Rule& rule) {
        return base(rule) && !rule.match.negatesAnyField({PhosphorRules::Field::ColorScheme});
    };
}

} // namespace PlasmaZones::RuleAdmission
