// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWindowRule/RuleAction.h>

#include <QJsonObject>
#include <QTest>

using namespace PhosphorWindowRule;

namespace {

RuleAction makeAction(QLatin1StringView type, const QJsonObject& params = {})
{
    RuleAction a;
    a.type = QString(type);
    a.params = params;
    return a;
}

} // namespace

// `ActionRegistry` is a process-global singleton. It now exposes
// `unregisterAction` (added in the audit pass), so symmetric cleanup is
// possible — but this test suite predates that API and `testRegisterCustomAction`
// does not yet RAII-clean up its sentinel. To keep cross-test independence
// without rewriting every test, no test here asserts an *absolute*
// `registeredTypes().size()` — that count is not stable across the suite.
// Builtins are asserted individually instead. `testBuiltinsRegistered` must
// stay declared FIRST so it observes the registry before any test mutates it.
class TestActionRegistry : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testBuiltinsRegistered()
    {
        const ActionRegistry& reg = ActionRegistry::instance();
        // Assert each builtin individually — never an absolute
        // `registeredTypes().size()`, see the singleton-pollution note above.
        QVERIFY(reg.isRegistered(QString(ActionType::SetEngineMode)));
        QVERIFY(reg.isRegistered(QString(ActionType::SetSnappingLayout)));
        QVERIFY(reg.isRegistered(QString(ActionType::SetTilingAlgorithm)));
        QVERIFY(reg.isRegistered(QString(ActionType::DisableEngine)));
        QVERIFY(reg.isRegistered(QString(ActionType::Exclude)));
        QVERIFY(reg.isRegistered(QString(ActionType::Float)));
        QVERIFY(reg.isRegistered(QString(ActionType::OverrideAnimationShader)));
        QVERIFY(reg.isRegistered(QString(ActionType::OverrideAnimationTiming)));
        QVERIFY(reg.isRegistered(QString(ActionType::OverrideAnimationCurve)));
        QVERIFY(reg.isRegistered(QString(ActionType::SetOpacity)));
        QVERIFY(reg.isRegistered(QString(ActionType::RestorePosition)));
    }

    void testSlots()
    {
        const ActionRegistry& reg = ActionRegistry::instance();

        QJsonObject mode;
        mode.insert(QStringLiteral("mode"), QStringLiteral("autotile"));
        QCOMPARE(reg.slotFor(makeAction(ActionType::SetEngineMode, mode)), QString(ActionSlot::EngineMode));

        QJsonObject layout;
        layout.insert(QStringLiteral("layoutId"), QStringLiteral("{x}"));
        QCOMPARE(reg.slotFor(makeAction(ActionType::SetSnappingLayout, layout)), QString(ActionSlot::Layout));

        QJsonObject algo;
        algo.insert(QStringLiteral("algorithm"), QStringLiteral("dwindle"));
        // setSnappingLayout and setTilingAlgorithm share the `layout` slot.
        QCOMPARE(reg.slotFor(makeAction(ActionType::SetTilingAlgorithm, algo)), QString(ActionSlot::Layout));

        QCOMPARE(reg.slotFor(makeAction(ActionType::Float)), QString(ActionSlot::Float));
        QCOMPARE(reg.slotFor(makeAction(ActionType::Exclude)), QString(ActionSlot::Manage));
    }

    void testAnimationSlotsAreEventScoped()
    {
        const ActionRegistry& reg = ActionRegistry::instance();

        QJsonObject open;
        open.insert(QStringLiteral("event"), QStringLiteral("window.open"));
        QJsonObject close;
        close.insert(QStringLiteral("event"), QStringLiteral("window.close"));

        const QString openShaderSlot = reg.slotFor(makeAction(ActionType::OverrideAnimationShader, open));
        const QString closeShaderSlot = reg.slotFor(makeAction(ActionType::OverrideAnimationShader, close));
        QCOMPARE(openShaderSlot, QStringLiteral("anim-shader:window.open"));
        QCOMPARE(closeShaderSlot, QStringLiteral("anim-shader:window.close"));
        QVERIFY(openShaderSlot != closeShaderSlot);

        // Shader and timing axes stay independent even for the same event.
        const QString openTimingSlot = reg.slotFor(makeAction(ActionType::OverrideAnimationTiming, open));
        QCOMPARE(openTimingSlot, QStringLiteral("anim-timing:window.open"));
        QVERIFY(openTimingSlot != openShaderSlot);
    }

    void testTerminalFlag()
    {
        const ActionRegistry& reg = ActionRegistry::instance();
        // Exclude and ExcludeAnimations are the terminal builtins — every other
        // builtin must be non-terminal, so evaluation continues past a match.
        QVERIFY(reg.isTerminal(makeAction(ActionType::Exclude)));
        QVERIFY(reg.isTerminal(makeAction(ActionType::ExcludeAnimations)));
        QVERIFY(!reg.isTerminal(makeAction(ActionType::Float)));
        QVERIFY(!reg.isTerminal(makeAction(ActionType::SetEngineMode)));
        QVERIFY(!reg.isTerminal(makeAction(ActionType::SetSnappingLayout)));
        QVERIFY(!reg.isTerminal(makeAction(ActionType::SetTilingAlgorithm)));
        QVERIFY(!reg.isTerminal(makeAction(ActionType::DisableEngine)));
        QVERIFY(!reg.isTerminal(makeAction(ActionType::OverrideAnimationShader)));
        QVERIFY(!reg.isTerminal(makeAction(ActionType::OverrideAnimationTiming)));
        QVERIFY(!reg.isTerminal(makeAction(ActionType::OverrideAnimationCurve)));
        QVERIFY(!reg.isTerminal(makeAction(ActionType::SetOpacity)));
        QVERIFY(!reg.isTerminal(makeAction(ActionType::RestorePosition)));
    }

    void testValidateAcceptsWellFormedRegisteredAction()
    {
        const ActionRegistry& reg = ActionRegistry::instance();
        // A registered type with a params payload its descriptor accepts.
        QJsonObject mode;
        mode.insert(QStringLiteral("mode"), QStringLiteral("autotile"));
        QVERIFY(reg.validate(makeAction(ActionType::SetEngineMode, mode)));

        QJsonObject opacity;
        opacity.insert(QStringLiteral("value"), 0.5);
        QVERIFY(reg.validate(makeAction(ActionType::SetOpacity, opacity)));
    }

    void testValidateRejectsRegisteredTypeWithBadParams()
    {
        const ActionRegistry& reg = ActionRegistry::instance();
        // Registered type, but the params fail the descriptor's predicate:
        // setEngineMode requires a non-empty `mode` string.
        QVERIFY(!reg.validate(makeAction(ActionType::SetEngineMode)));
        QJsonObject emptyMode;
        emptyMode.insert(QStringLiteral("mode"), QString());
        QVERIFY(!reg.validate(makeAction(ActionType::SetEngineMode, emptyMode)));

        // setOpacity requires a numeric `value` in [0, 1] — out of range fails.
        QJsonObject badOpacity;
        badOpacity.insert(QStringLiteral("value"), 1.5);
        QVERIFY(!reg.validate(makeAction(ActionType::SetOpacity, badOpacity)));
    }

    void testRestorePositionAction()
    {
        const ActionRegistry& reg = ActionRegistry::instance();
        QVERIFY(reg.isRegistered(QString(ActionType::RestorePosition)));

        QJsonObject on;
        on.insert(QStringLiteral("value"), true);
        QJsonObject off;
        off.insert(QStringLiteral("value"), false);

        // Window-domain boolean action filling the dedicated restore-position slot.
        QCOMPARE(reg.slotFor(makeAction(ActionType::RestorePosition, on)), QString(ActionSlot::RestorePosition));
        QCOMPARE(reg.domainFor(makeAction(ActionType::RestorePosition, on)), ActionDomain::Window);
        QVERIFY(!reg.isTerminal(makeAction(ActionType::RestorePosition, on)));

        // Requires a boolean `value`; both true and false are well-formed.
        QVERIFY(reg.validate(makeAction(ActionType::RestorePosition, on)));
        QVERIFY(reg.validate(makeAction(ActionType::RestorePosition, off)));
        QVERIFY2(!reg.validate(makeAction(ActionType::RestorePosition)), "missing value must fail validation");
        QJsonObject notBool;
        notBool.insert(QStringLiteral("value"), 1);
        QVERIFY2(!reg.validate(makeAction(ActionType::RestorePosition, notBool)), "non-bool value must fail");
    }

    void testRegisterCustomAction()
    {
        ActionRegistry& reg = ActionRegistry::instance();
        // `_zz_` prefix so the type-id sorts to the end if anyone iterates
        // `registeredTypes()` in lexicographic order, and the underscore-led
        // name visually separates it from the production wire identifiers.
        // This test predates the `unregisterAction` API; the sentinel
        // remains registered for the rest of the test binary's lifetime —
        // see the file-level comment for the cross-test independence
        // pattern.
        const QString customType = QStringLiteral("_zz_pwrTestCustomAction");
        QVERIFY(!reg.isRegistered(customType));

        reg.registerAction(ActionDescriptor{.type = customType,
                                            .slotFor =
                                                [](const QJsonObject&) {
                                                    return QStringLiteral("custom-slot");
                                                },
                                            .validate =
                                                [](const QJsonObject&) {
                                                    return true;
                                                },
                                            .terminal = false});
        QVERIFY(reg.isRegistered(customType));
        QCOMPARE(reg.slotFor(makeAction(QLatin1StringView("_zz_pwrTestCustomAction"))), QStringLiteral("custom-slot"));
    }

    void testValidateRejectsUnregistered()
    {
        const ActionRegistry& reg = ActionRegistry::instance();
        QVERIFY(!reg.validate(makeAction(QLatin1StringView("notRegistered"))));
    }
};

QTEST_GUILESS_MAIN(TestActionRegistry)
#include "test_actionregistry.moc"
