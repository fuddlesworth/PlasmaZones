// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "RuleTestHelpers.h"

#include <PhosphorRules/RuleAction.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QTest>

using namespace PhosphorRules;

namespace {
// Local alias for the shared `TestHelpers::engineMode(QString)` helper —
// the test body reads cleaner with a one-word name and a local namespace
// alias keeps the call sites unchanged. Replaces the prior duplicate
// helper that bypassed the canonical `ActionParam::Mode` constant.
inline RuleAction engineModeAction(const QString& mode)
{
    return PhosphorRules::TestHelpers::engineMode(mode);
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
    // Default-assignment override — context-domain, resolved during the
    // screen/desktop/activity pass (LayoutRegistry::resolveContextDefaultAssignment),
    // mode-agnostic like LockContext.
    ActionType::DefaultLayoutAssignment,
    // Gap overrides are context-domain — resolved during the
    // screen/desktop/activity pass, never per-window.
    ActionType::SetInnerGap,
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
    ActionType::SnapToZone,
    // Open-routing actions are window-domain — resolved per window on the
    // daemon open path, applicable to any matched window.
    ActionType::RouteToScreen,
    ActionType::RouteToDesktop,
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
    ActionType::SetBorderColorActive,
    ActionType::SetBorderColorInactive,
    ActionType::OverrideDecorationChain,
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

    void testOverrideDecorationChain_roundTripAndSentinels()
    {
        // Full payload: ordered chain array + nested per-pack params object
        // (the OverrideAnimationShader Params precedent) round-trip intact.
        QJsonObject params;
        params.insert(QString(ActionParam::Chain), QJsonArray{QStringLiteral("frosted-glass"), QStringLiteral("glow")});
        QJsonObject glowParams;
        glowParams.insert(QStringLiteral("glowSize"), 32);
        QJsonObject packParams;
        packParams.insert(QStringLiteral("glow"), glowParams);
        params.insert(QString(ActionParam::Params), packParams);
        const RuleAction a{QString(ActionType::OverrideDecorationChain), params};
        const auto reloaded = RuleAction::fromJson(a.toJson());
        QVERIFY(reloaded.has_value());
        QCOMPARE(*reloaded, a);

        // An EMPTY chain array is the valid "no decoration" sentinel.
        QJsonObject emptyChain;
        emptyChain.insert(QString(ActionParam::Chain), QJsonArray{});
        const RuleAction sentinel{QString(ActionType::OverrideDecorationChain), emptyChain};
        QVERIFY(RuleAction::fromJson(sentinel.toJson()).has_value());

        // A missing or non-array chain fails the descriptor validator.
        const RuleAction missing{QString(ActionType::OverrideDecorationChain), QJsonObject()};
        QVERIFY(!RuleAction::fromJson(missing.toJson()).has_value());
        QJsonObject wrongShape;
        wrongShape.insert(QString(ActionParam::Chain), QStringLiteral("frosted-glass"));
        const RuleAction nonArray{QString(ActionType::OverrideDecorationChain), wrongShape};
        QVERIFY(!RuleAction::fromJson(nonArray.toJson()).has_value());
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

    void testJson_routeToScreen()
    {
        QJsonObject o;
        o.insert(QStringLiteral("type"), QString(ActionType::RouteToScreen));
        // Missing / empty target screen id is rejected.
        QVERIFY(!RuleAction::fromJson(o).has_value());
        o.insert(QString(ActionParam::TargetScreenId), QString());
        QVERIFY(!RuleAction::fromJson(o).has_value());
        // A non-empty canonical screen id is accepted (not validated against
        // live screen state — a route to a currently-absent monitor is legal).
        o.insert(QString(ActionParam::TargetScreenId), QStringLiteral("LG Electronics:38GN950:688325"));
        const auto loaded = RuleAction::fromJson(o);
        QVERIFY(loaded.has_value());
        QCOMPARE(ActionRegistry::instance().slotFor(*loaded), QString(ActionSlot::RouteScreen));
        // Unknown param key is rejected by the strict loader.
        o.insert(QStringLiteral("bogus"), 1);
        QVERIFY(!RuleAction::fromJson(o).has_value());
    }

    void testJson_routeToDesktop()
    {
        QJsonObject o;
        o.insert(QStringLiteral("type"), QString(ActionType::RouteToDesktop));
        // Missing desktop is rejected.
        QVERIFY(!RuleAction::fromJson(o).has_value());
        // Desktops are 1-based: 0 and negatives are rejected.
        o.insert(QString(ActionParam::TargetDesktop), 0);
        QVERIFY(!RuleAction::fromJson(o).has_value());
        o.insert(QString(ActionParam::TargetDesktop), -1);
        QVERIFY(!RuleAction::fromJson(o).has_value());
        // Non-integral is rejected.
        o.insert(QString(ActionParam::TargetDesktop), 2.5);
        QVERIFY(!RuleAction::fromJson(o).has_value());
        // Out-of-range (above the cap) is rejected.
        o.insert(QString(ActionParam::TargetDesktop), MaxVirtualDesktopOrdinal + 1);
        QVERIFY(!RuleAction::fromJson(o).has_value());
        // A valid 1-based desktop loads and resolves to the route-desktop slot.
        o.insert(QString(ActionParam::TargetDesktop), 3);
        const auto loaded = RuleAction::fromJson(o);
        QVERIFY(loaded.has_value());
        QCOMPARE(ActionRegistry::instance().slotFor(*loaded), QString(ActionSlot::RouteDesktop));
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
        // hand-edited rules.json that names a mode the daemon does
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
        // SetBorderColorActive and SetBorderColorInactive each carry a single
        // `value` colour: a hex shape or the accent sentinel. Both share the
        // validator, so exercise both type strings.
        for (const QLatin1StringView type : {ActionType::SetBorderColorActive, ActionType::SetBorderColorInactive}) {
            QJsonObject o;
            o.insert(QStringLiteral("type"), QString::fromLatin1(type));
            // Missing `value` — rejected (the colour is required).
            QVERIFY2(!RuleAction::fromJson(o).has_value(), type.data());
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
                QVERIFY2(RuleAction::fromJson(o).has_value(), qPrintable(good));
            }
            // The accent sentinel is accepted.
            o.insert(QStringLiteral("value"), QString(BorderColorToken::Accent));
            const auto reloaded = RuleAction::fromJson(o);
            QVERIFY2(reloaded.has_value(), type.data());
            // Genuine round-trip: serialise the parsed action back out and re-parse,
            // asserting toJson→fromJson is stable.
            const auto roundTripped = RuleAction::fromJson(reloaded->toJson());
            QVERIFY2(roundTripped.has_value(), type.data());
            QCOMPARE(*roundTripped, *reloaded);
        }
    }

    // ── gap actions (context-domain) ──

    void testGapNumberActions_range()
    {
        for (const QLatin1StringView type :
             {ActionType::SetInnerGap, ActionType::SetOuterGap, ActionType::SetOuterGapTop,
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
        // The border-colour actions key their colour on `value`, so the
        // value-shaped helper covers them with a valid hex colour.
        rejectsStray(ActionType::SetBorderColorActive, QJsonValue(QStringLiteral("#FF0000")));
        rejectsStray(ActionType::SetBorderColorInactive, QJsonValue(QStringLiteral("#FF0000")));
        rejectsStray(ActionType::SetOuterGapTop, QJsonValue(8));
        rejectsStray(ActionType::SetUsePerSideOuterGap, QJsonValue(true));
        rejectsStray(ActionType::LockContext, QJsonValue(true));
        rejectsStray(ActionType::DefaultLayoutAssignment, QJsonValue(true));
        // OverrideOverlayStyle also declares allowedKeys = {Value}.
        rejectsStray(ActionType::OverrideOverlayStyle, QJsonValue(QString(OverlayStyleToken::Preview)));
    }

    void testOverrideOverlay_fromJson()
    {
        // The two context-domain overlay actions validate through the public
        // fromJson boundary. OverrideOverlayShader requires a non-empty effectId
        // (open shader-id vocabulary, like OverrideAnimationShader);
        // OverrideOverlayStyle is a CLOSED enum — only the OverlayStyleToken
        // vocabulary survives load, so a hand-edited rules.json naming an
        // unknown style is dropped. Pins both validators against a widening
        // regression in registerBuiltins (which the resolution-layer tests in
        // test_rule_cascade_fidelity.cpp would not catch).

        // ── OverrideOverlayShader ──
        QJsonObject missingId;
        missingId.insert(QStringLiteral("type"), QString(ActionType::OverrideOverlayShader));
        QVERIFY(!RuleAction::fromJson(missingId).has_value()); // no effectId

        QJsonObject emptyId;
        emptyId.insert(QStringLiteral("type"), QString(ActionType::OverrideOverlayShader));
        emptyId.insert(QStringLiteral("effectId"), QString());
        QVERIFY(!RuleAction::fromJson(emptyId).has_value()); // empty effectId rejected

        QJsonObject shader;
        shader.insert(QStringLiteral("type"), QString(ActionType::OverrideOverlayShader));
        shader.insert(QStringLiteral("effectId"), QStringLiteral("plasma-glow"));
        QVERIFY(RuleAction::fromJson(shader).has_value());

        // The optional params object is in allowedKeys; a populated one validates.
        QJsonObject shaderParams = shader;
        QJsonObject params;
        params.insert(QStringLiteral("intensity"), 0.5);
        shaderParams.insert(QString(ActionParam::Params), params);
        QVERIFY(RuleAction::fromJson(shaderParams).has_value());

        // A key outside allowedKeys ({effectId, params}) is rejected.
        QJsonObject shaderStray = shader;
        shaderStray.insert(QStringLiteral("mode"), QStringLiteral("snapping"));
        QVERIFY(!RuleAction::fromJson(shaderStray).has_value());

        // ── OverrideOverlayStyle ──
        QJsonObject unknownStyle;
        unknownStyle.insert(QStringLiteral("type"), QString(ActionType::OverrideOverlayStyle));
        unknownStyle.insert(QStringLiteral("value"), QLatin1String("grid"));
        QVERIFY(!RuleAction::fromJson(unknownStyle).has_value()); // not in vocabulary

        for (const QLatin1StringView token : {OverlayStyleToken::Rectangles, OverlayStyleToken::Preview}) {
            QJsonObject ok;
            ok.insert(QStringLiteral("type"), QString(ActionType::OverrideOverlayStyle));
            ok.insert(QStringLiteral("value"), QString(token));
            QVERIFY2(RuleAction::fromJson(ok).has_value(), token.data());
        }
    }

    void testSnapToZone_fromJson()
    {
        // SnapToZone is a window-domain placement action whose `zones` param is a
        // non-empty JSON array of 1-based integer ordinals. Pin the validator at
        // the public fromJson boundary: it is the single line of defence against
        // a hand-edited rules.json carrying a malformed ordinal list, and a
        // widening regression in registerBuiltins would otherwise slip past.
        const auto withZones = [](const QJsonValue& zones) {
            QJsonObject o;
            o.insert(QStringLiteral("type"), QString(ActionType::SnapToZone));
            o.insert(QString(ActionParam::Zones), zones);
            return o;
        };

        // Missing / wrong-typed / empty → rejected.
        QJsonObject missing;
        missing.insert(QStringLiteral("type"), QString(ActionType::SnapToZone));
        QVERIFY(!RuleAction::fromJson(missing).has_value());
        QVERIFY(!RuleAction::fromJson(withZones(QJsonValue(2))).has_value()); // not an array
        QVERIFY(!RuleAction::fromJson(withZones(QJsonArray{})).has_value()); // empty
        QVERIFY(!RuleAction::fromJson(withZones(QJsonArray{0})).has_value()); // 0 not 1-based
        QVERIFY(!RuleAction::fromJson(withZones(QJsonArray{-1})).has_value()); // negative
        QVERIFY(!RuleAction::fromJson(withZones(QJsonArray{1.5})).has_value()); // non-integral
        QVERIFY(!RuleAction::fromJson(withZones(QJsonArray{QStringLiteral("1")})).has_value()); // string
        QVERIFY(!RuleAction::fromJson(withZones(QJsonArray{65})).has_value()); // above the ordinal cap (64)
        // A double far beyond int range must be rejected by the bound BEFORE any
        // narrowing cast (an out-of-range float-to-int cast is UB) — must not crash.
        QVERIFY(!RuleAction::fromJson(withZones(QJsonArray{1e18})).has_value());

        // Single zone, span, and the inclusive cap boundary are all accepted.
        QVERIFY(RuleAction::fromJson(withZones(QJsonArray{1})).has_value());
        QVERIFY(RuleAction::fromJson(withZones(QJsonArray{1, 2})).has_value());
        QVERIFY(RuleAction::fromJson(withZones(QJsonArray{64})).has_value());

        // A key outside allowedKeys ({zones}) is rejected.
        QJsonObject stray = withZones(QJsonArray{1});
        stray.insert(QStringLiteral("value"), 3);
        QVERIFY(!RuleAction::fromJson(stray).has_value());
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

    void testDefaultLayoutAssignment_fromJsonRoundTrip()
    {
        // DefaultLayoutAssignment is a context-domain boolean (per-context override
        // of the global suppress setting): it must validate through the public
        // fromJson boundary (not just the registry), require a bool `value`, and
        // round-trip both true (allow / force the default through) and false
        // (suppress this context) losslessly. Mirrors testLockContext_fromJsonRoundTrip.
        QJsonObject bad;
        bad.insert(QStringLiteral("type"), QString(ActionType::DefaultLayoutAssignment));
        bad.insert(QStringLiteral("value"), 1); // a number is not a bool
        QVERIFY(!RuleAction::fromJson(bad).has_value());

        for (const bool v : {true, false}) {
            QJsonObject o;
            o.insert(QStringLiteral("type"), QString(ActionType::DefaultLayoutAssignment));
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
