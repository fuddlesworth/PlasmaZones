// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "RuleTestHelpers.h"

#include <PhosphorWindowRules/RuleAction.h>

#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QTest>

using namespace PhosphorWindowRules;

namespace {
// Local alias for the shared `TestHelpers::engineMode(QString)` helper —
// the test body reads cleaner with a one-word name and a local namespace
// alias keeps the call sites unchanged. Replaces the prior duplicate
// helper that bypassed the canonical `ActionParam::Mode` constant.
inline RuleAction engineModeAction(const QString& mode)
{
    return PhosphorWindowRules::TestHelpers::engineMode(mode);
}

// Single source for the domain pins: the per-domain assertion tests AND the
// completeness canary iterate these same lists, so a registered type can
// only pass the canary by actually being domain-asserted somewhere.
const QList<QLatin1StringView> kContextDomainTypes = {
    ActionType::SetEngineMode,
    ActionType::SetSnappingLayout,
    ActionType::SetTilingAlgorithm,
    ActionType::DisableEngine,
    // Layout lock — context-domain, resolved during the screen/desktop/
    // activity pass (mode-agnostic) like the other context actions.
    ActionType::LockContext,
    // Gap overrides are context-domain — resolved during the
    // screen/desktop/activity pass, never per-window.
    ActionType::SetZonePadding,
    ActionType::SetOuterGap,
    ActionType::SetUsePerSideOuterGap,
    ActionType::SetOuterGapTop,
    ActionType::SetOuterGapBottom,
    ActionType::SetOuterGapLeft,
    ActionType::SetOuterGapRight,
    // Overlay-property overrides are context-domain — resolved during the
    // screen/desktop/activity pass (LayoutRegistry::resolveContextOverlay),
    // never per-window.
    ActionType::OverrideOverlayShader,
    ActionType::OverrideOverlayStyle,
};
const QList<QLatin1StringView> kWindowDomainTypes = {
    ActionType::Exclude,
    ActionType::Float,
    ActionType::OverrideAnimationShader,
    ActionType::OverrideAnimationTiming,
    ActionType::OverrideAnimationCurve,
    ActionType::ExcludeAnimations,
    ActionType::SetOpacity,
    // Border / title-bar overrides are window-domain — resolved per
    // window in the effect, applicable to any matched window.
    ActionType::SetHideTitleBar,
    ActionType::SetBorderVisible,
    ActionType::SetBorderWidth,
    ActionType::SetBorderRadius,
    ActionType::SetBorderColor,
};
} // namespace

class TestRuleAction : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testJson_roundTrip()
    {
        const RuleAction a = engineModeAction(QStringLiteral("autotile"));
        const auto reloaded = RuleAction::fromJson(a.toJson());
        QVERIFY(reloaded.has_value());
        QCOMPARE(*reloaded, a);
    }

    void testJson_typeWrittenInline()
    {
        const RuleAction a = engineModeAction(QStringLiteral("snapping"));
        const QJsonObject o = a.toJson();
        QCOMPARE(o.value(QStringLiteral("type")).toString(), QString(ActionType::SetEngineMode));
        QCOMPARE(o.value(QStringLiteral("mode")).toString(), QStringLiteral("snapping"));
    }

    void testJson_rejectsMissingType()
    {
        QJsonObject o;
        o.insert(QStringLiteral("mode"), QStringLiteral("autotile"));
        QVERIFY(!RuleAction::fromJson(o).has_value());
    }

    void testJson_rejectsUnregisteredType()
    {
        QJsonObject o;
        o.insert(QStringLiteral("type"), QStringLiteral("notARealAction"));
        QVERIFY(!RuleAction::fromJson(o).has_value());
    }

    void testJson_rejectsRemovedInactiveBorderColor()
    {
        // `setInactiveBorderColor` was a valid action type until focus-dependent
        // colour was folded into the IsFocused match condition (a `WHEN NOT
        // focused ⇒ setBorderColor` rule replaces it). It is now unregistered,
        // so a saved rule carrying it must drop the action on load — even with
        // an otherwise-valid hex payload. Pins the removal's drop-on-load
        // contract to the specific retired token, not just the generic
        // unknown-type path (testJson_rejectsUnregisteredType).
        QJsonObject o;
        o.insert(QStringLiteral("type"), QStringLiteral("setInactiveBorderColor"));
        o.insert(QStringLiteral("value"), QStringLiteral("#FF0000"));
        QVERIFY(!RuleAction::fromJson(o).has_value());
    }

    void testJson_rejectsInvalidParams()
    {
        // setEngineMode requires a non-empty `mode` string.
        QJsonObject o;
        o.insert(QStringLiteral("type"), QString(ActionType::SetEngineMode));
        QVERIFY(!RuleAction::fromJson(o).has_value());
        o.insert(QStringLiteral("mode"), QString());
        QVERIFY(!RuleAction::fromJson(o).has_value());
    }

    void testJson_animationActionRequiresEvent()
    {
        QJsonObject o;
        o.insert(QStringLiteral("type"), QString(ActionType::OverrideAnimationShader));
        o.insert(QStringLiteral("effectId"), QStringLiteral("dissolve"));
        // No `event` — must be rejected (the slot is event-scoped).
        QVERIFY(!RuleAction::fromJson(o).has_value());
        o.insert(QStringLiteral("event"), QStringLiteral("window.open"));
        QVERIFY(RuleAction::fromJson(o).has_value());
    }

    void testJson_opacityRange()
    {
        QJsonObject o;
        o.insert(QStringLiteral("type"), QString(ActionType::SetOpacity));
        o.insert(QStringLiteral("value"), 1.5);
        QVERIFY(!RuleAction::fromJson(o).has_value());
        // Below-minimum is rejected too; 0.0 itself is a legal wire value
        // (fully transparent — the 0.0–1.0 range is inclusive).
        o.insert(QStringLiteral("value"), -0.1);
        QVERIFY(!RuleAction::fromJson(o).has_value());
        o.insert(QStringLiteral("value"), 0.0);
        QVERIFY(RuleAction::fromJson(o).has_value());
        o.insert(QStringLiteral("value"), 0.95);
        QVERIFY(RuleAction::fromJson(o).has_value());
    }

    void testEquality()
    {
        QVERIFY(engineModeAction(QStringLiteral("autotile")) == engineModeAction(QStringLiteral("autotile")));
        QVERIFY(engineModeAction(QStringLiteral("autotile")) != engineModeAction(QStringLiteral("snapping")));
    }

    void testActionDomain_contextActions()
    {
        // The context-domain actions all fill slots consumed during the
        // screen/desktop/activity resolution pass. They must report
        // ActionDomain::Context so the validator can flag mismatched matches.
        // (kContextDomainTypes is shared with the completeness canary so
        // pinning is structural, not copy-discipline.)
        for (const QLatin1StringView type : kContextDomainTypes) {
            const auto descriptor = ActionRegistry::instance().descriptor(QString::fromLatin1(type));
            QVERIFY2(descriptor.has_value(), type.data());
            QCOMPARE(descriptor->domain, ActionDomain::Context);
        }
    }

    void testActionDomain_windowActions()
    {
        // Window-domain actions cover the per-window evaluation pass — exclude,
        // float, animation overrides, opacity. They run against a WindowQuery
        // that carries both window and context fields, so any match shape is
        // valid for them. (kWindowDomainTypes is shared with the canary.)
        for (const QLatin1StringView type : kWindowDomainTypes) {
            const auto descriptor = ActionRegistry::instance().descriptor(QString::fromLatin1(type));
            QVERIFY2(descriptor.has_value(), type.data());
            QCOMPARE(descriptor->domain, ActionDomain::Window);
        }
    }

    void testActionDomain_unregisteredFallsBackToWindow()
    {
        // An unregistered action type is treated as window-domain — the
        // conservative default so an unknown action gets the looser
        // compatibility check rather than being incorrectly flagged.
        RuleAction unknown;
        unknown.type = QStringLiteral("notARealAction");
        QCOMPARE(ActionRegistry::instance().domainFor(unknown), ActionDomain::Window);
    }

    void testActionDomain_allBuiltinsCovered()
    {
        // Canary — every registered built-in must be either context or window
        // domain. The cast-back asserts the enum stayed two-valued; adding a
        // third domain without updating this test would still pass the
        // compiler.
        for (const QString& type : ActionRegistry::instance().registeredTypes()) {
            const auto descriptor = ActionRegistry::instance().descriptor(type);
            QVERIFY(descriptor.has_value());
            const int d = static_cast<int>(descriptor->domain);
            QVERIFY(d == static_cast<int>(ActionDomain::Context) || d == static_cast<int>(ActionDomain::Window));
        }

        // Completeness: every registered type must be domain-PINNED by the
        // SAME lists the assertion tests above iterate (file-scope
        // kContextDomainTypes / kWindowDomainTypes — shared, not copied, so
        // a type added here without a domain assertion is impossible), or by
        // the known-elsewhere set below.
        QSet<QString> pinned;
        // Domain pinned in another suite: RestorePosition (window) in
        // test_actionregistry.cpp.
        const QList<QLatin1StringView> pinnedElsewhere = {
            ActionType::RestorePosition,
        };
        for (const QLatin1StringView t : kContextDomainTypes) {
            pinned.insert(QString::fromLatin1(t));
        }
        for (const QLatin1StringView t : kWindowDomainTypes) {
            pinned.insert(QString::fromLatin1(t));
        }
        for (const QLatin1StringView t : pinnedElsewhere) {
            pinned.insert(QString::fromLatin1(t));
        }
        for (const QString& type : ActionRegistry::instance().registeredTypes()) {
            QVERIFY2(pinned.contains(type),
                     qPrintable(QStringLiteral("action type not domain-pinned by any list: ") + type));
        }
    }

    void testDisableEngine_rejectsUnknownModeToken()
    {
        // The `DisableEngine` validator must reject an unknown mode wire
        // token at load. The `ContextRuleBridge` is intentionally open-
        // vocabulary (it writes the token verbatim), so the load-side
        // descriptor validator is the single line of defence against a
        // hand-edited windowrules.json that names a mode the daemon does
        // not recognise. A widening regression (e.g. someone adds a
        // default-true branch to the mode predicate in `registerBuiltins`)
        // would silently survive the round-trip tests in
        // test_contextrulebridge.cpp — pinning the rejection here covers
        // that exact regression.
        QJsonObject obj;
        obj.insert(QLatin1StringView("type"), QString(ActionType::DisableEngine));
        obj.insert(QLatin1StringView("mode"), QLatin1String("bogus"));
        QVERIFY(!RuleAction::fromJson(obj).has_value());
    }

    void testDisableEngine_rejectsEmptyModeToken()
    {
        // The bridge tolerates an empty mode token (writes `mode:""`); the
        // load-side validator drops the rule. Mirrors the malformed-payload
        // guarantee documented in `ContextRuleBridge.h::makeDisableRule`.
        QJsonObject obj;
        obj.insert(QLatin1StringView("type"), QString(ActionType::DisableEngine));
        obj.insert(QLatin1StringView("mode"), QString());
        QVERIFY(!RuleAction::fromJson(obj).has_value());
    }

    void testDisableEngine_acceptsAllThreeModeTokens()
    {
        // Pin the complete recognised vocabulary so a future enum addition
        // forces an update here — the symmetric guard for the open-
        // vocabulary bridge in test_contextrulebridge.cpp.
        for (QLatin1StringView token :
             {QLatin1String("snapping"), QLatin1String("autotile"), QLatin1String("scrolling")}) {
            QJsonObject obj;
            obj.insert(QLatin1StringView("type"), QString(ActionType::DisableEngine));
            obj.insert(QLatin1StringView("mode"), QString::fromLatin1(token.data(), token.size()));
            QVERIFY2(RuleAction::fromJson(obj).has_value(), token.data());
        }
    }

    // ── border / title-bar appearance actions ──

    void testBorderBoolActions_requireBool()
    {
        for (const QLatin1StringView type : {ActionType::SetHideTitleBar, ActionType::SetBorderVisible}) {
            QJsonObject o;
            o.insert(QStringLiteral("type"), QString::fromLatin1(type));
            // A number is NOT a bool — must be rejected.
            o.insert(QStringLiteral("value"), 1);
            QVERIFY2(!RuleAction::fromJson(o).has_value(), type.data());
            o.insert(QStringLiteral("value"), true);
            QVERIFY2(RuleAction::fromJson(o).has_value(), type.data());
        }
    }

    void testSetHideTitleBarFalse_isValidAndPreserved()
    {
        // SetHideTitleBar is tri-state at the effect: rule absent = mode
        // decides, true = hide, FALSE = force the title bar visible (a veto
        // over mode hiding). An explicit false must therefore validate and
        // round-trip — it is a meaningful value, not "unset".
        QJsonObject o;
        o.insert(QStringLiteral("type"), QString(ActionType::SetHideTitleBar));
        o.insert(QStringLiteral("value"), false);
        const auto action = RuleAction::fromJson(o);
        QVERIFY(action.has_value());
        QCOMPARE(action->params.value(QStringLiteral("value")), QJsonValue(false));
        const auto roundTripped = RuleAction::fromJson(action->toJson());
        QVERIFY(roundTripped.has_value());
        QCOMPARE(roundTripped->params.value(QStringLiteral("value")), QJsonValue(false));
    }

    void testBorderNumberActions_range()
    {
        QJsonObject w;
        w.insert(QStringLiteral("type"), QString(ActionType::SetBorderWidth));
        w.insert(QStringLiteral("value"), 15); // > max 10
        QVERIFY(!RuleAction::fromJson(w).has_value());
        w.insert(QStringLiteral("value"), -1);
        QVERIFY(!RuleAction::fromJson(w).has_value());
        // A JSON bool is NOT a number — hasNumberInRange uses isDouble(), which
        // is false for a bool. Pin the rejection so a future widening of the
        // number validator (e.g. isDouble() || isBool()) fails the suite.
        w.insert(QStringLiteral("value"), true);
        QVERIFY(!RuleAction::fromJson(w).has_value());
        w.insert(QStringLiteral("value"), 4);
        QVERIFY(RuleAction::fromJson(w).has_value());

        QJsonObject r;
        r.insert(QStringLiteral("type"), QString(ActionType::SetBorderRadius));
        r.insert(QStringLiteral("value"), 25); // > max 20
        QVERIFY(!RuleAction::fromJson(r).has_value());
        r.insert(QStringLiteral("value"), 20);
        QVERIFY(RuleAction::fromJson(r).has_value());
    }

    void testBorderColorActions_requireHex()
    {
        // Single colour action since setInactiveBorderColor's removal (see the
        // header note above); kept loop-shaped so a future colour action only
        // adds an initializer entry.
        for (const QLatin1StringView type : {ActionType::SetBorderColor}) {
            QJsonObject o;
            o.insert(QStringLiteral("type"), QString::fromLatin1(type));
            o.insert(QStringLiteral("value"), QStringLiteral("red")); // named colour — hex-only boundary rejects
            QVERIFY2(!RuleAction::fromJson(o).has_value(), type.data());
            o.insert(QStringLiteral("value"), QStringLiteral("#ff00")); // length 5 ∉ {4,7,9}
            QVERIFY2(!RuleAction::fromJson(o).has_value(), type.data());
            o.insert(QStringLiteral("value"), QStringLiteral("#12345")); // length 6 ∉ {4,7,9}
            QVERIFY2(!RuleAction::fromJson(o).has_value(), type.data());
            o.insert(QStringLiteral("value"), QStringLiteral("#gg0000")); // non-hex digits
            QVERIFY2(!RuleAction::fromJson(o).has_value(), type.data());
            // The standard QColor hex shapes the consumer parses are all accepted:
            // #RGB (4), #RRGGBB (7), #AARRGGBB (9 — QColor reads 9-digit hex alpha-first).
            for (const QString& good :
                 {QStringLiteral("#abc"), QStringLiteral("#FF0000"), QStringLiteral("#80FF0000")}) {
                o.insert(QStringLiteral("value"), good);
                QVERIFY2(RuleAction::fromJson(o).has_value(),
                         qPrintable(QStringLiteral("%1 %2").arg(QString::fromLatin1(type), good)));
            }
            o.insert(QStringLiteral("value"), QStringLiteral("#FF0000"));
            const auto reloaded = RuleAction::fromJson(o);
            QVERIFY2(reloaded.has_value(), type.data());
            // Genuine round-trip: serialise the parsed action back out and
            // re-parse, asserting toJson→fromJson is stable (re-parsing the
            // same input `o` would only prove fromJson is deterministic).
            const auto roundTripped = RuleAction::fromJson(reloaded->toJson());
            QVERIFY2(roundTripped.has_value(), type.data());
            QCOMPARE(*roundTripped, *reloaded);
        }
    }

    // ── gap actions (context-domain) ──

    void testGapNumberActions_range()
    {
        for (const QLatin1StringView type :
             {ActionType::SetZonePadding, ActionType::SetOuterGap, ActionType::SetOuterGapTop,
              ActionType::SetOuterGapBottom, ActionType::SetOuterGapLeft, ActionType::SetOuterGapRight}) {
            QJsonObject o;
            o.insert(QStringLiteral("type"), QString::fromLatin1(type));
            o.insert(QStringLiteral("value"), -5);
            QVERIFY2(!RuleAction::fromJson(o).has_value(), type.data());
            o.insert(QStringLiteral("value"), 600); // > kMaxGap (500)
            QVERIFY2(!RuleAction::fromJson(o).has_value(), type.data());
            o.insert(QStringLiteral("value"), 0);
            QVERIFY2(RuleAction::fromJson(o).has_value(), type.data());
            o.insert(QStringLiteral("value"), 12);
            const auto reloaded = RuleAction::fromJson(o);
            QVERIFY2(reloaded.has_value(), type.data());
            QCOMPARE(reloaded->params.value(QStringLiteral("value")).toInt(), 12);
        }
    }

    void testGapPerSideToggle_requiresBool()
    {
        QJsonObject o;
        o.insert(QStringLiteral("type"), QString(ActionType::SetUsePerSideOuterGap));
        o.insert(QStringLiteral("value"), QStringLiteral("yes"));
        QVERIFY(!RuleAction::fromJson(o).has_value());
        o.insert(QStringLiteral("value"), false);
        QVERIFY(RuleAction::fromJson(o).has_value());
    }

    void testNewActions_rejectStrayKeys()
    {
        // The new border/gap family all declare `allowedKeys = {Value}`; pin
        // that an otherwise-valid payload carrying an unexpected extra key is
        // rejected, so the strict-key path is exercised for this family (not
        // just for the pre-existing SetEngineMode in testJson_rejectsInvalidParams).
        const auto rejectsStray = [](QLatin1StringView type, const QJsonValue& goodValue) {
            QJsonObject ok;
            ok.insert(QStringLiteral("type"), QString::fromLatin1(type));
            ok.insert(QStringLiteral("value"), goodValue);
            QVERIFY2(RuleAction::fromJson(ok).has_value(), type.data());
            QJsonObject stray = ok;
            stray.insert(QStringLiteral("width"), 4); // not in allowedKeys
            QVERIFY2(!RuleAction::fromJson(stray).has_value(), type.data());
        };
        rejectsStray(ActionType::SetBorderWidth, QJsonValue(4));
        rejectsStray(ActionType::SetHideTitleBar, QJsonValue(true));
        rejectsStray(ActionType::SetBorderColor, QJsonValue(QStringLiteral("#FF0000")));
        rejectsStray(ActionType::SetOuterGapTop, QJsonValue(8));
        rejectsStray(ActionType::SetUsePerSideOuterGap, QJsonValue(true));
        rejectsStray(ActionType::LockContext, QJsonValue(true));
    }

    void testLockContext_fromJsonRoundTrip()
    {
        // LockContext is a context-domain boolean: it must validate through the
        // public fromJson boundary (not just the registry), require a bool
        // `value`, and round-trip both true and the explicit-false no-op overlay
        // losslessly. Mirrors testSetHideTitleBarFalse_isValidAndPreserved.
        QJsonObject bad;
        bad.insert(QStringLiteral("type"), QString(ActionType::LockContext));
        bad.insert(QStringLiteral("value"), 1); // a number is not a bool
        QVERIFY(!RuleAction::fromJson(bad).has_value());

        for (const bool v : {true, false}) {
            QJsonObject o;
            o.insert(QStringLiteral("type"), QString(ActionType::LockContext));
            o.insert(QStringLiteral("value"), v);
            const auto action = RuleAction::fromJson(o);
            QVERIFY(action.has_value());
            QCOMPARE(action->params.value(QStringLiteral("value")), QJsonValue(v));
            const auto roundTripped = RuleAction::fromJson(action->toJson());
            QVERIFY(roundTripped.has_value());
            QCOMPARE(roundTripped->params.value(QStringLiteral("value")), QJsonValue(v));
        }
    }
};

QTEST_GUILESS_MAIN(TestRuleAction)
#include "test_ruleaction.moc"
