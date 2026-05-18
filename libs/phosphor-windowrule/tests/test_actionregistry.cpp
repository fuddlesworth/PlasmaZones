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

// Singleton-pollution hazard: `ActionRegistry` is a process-global singleton
// and exposes no `unregisterAction`. `testRegisterCustomAction` permanently
// adds a custom type for the lifetime of this test process. Consequently no
// test here may assert an *absolute* `registeredTypes().size()` — that count
// is not stable across the suite. Builtins are asserted individually instead.
// `testBuiltinsRegistered` must stay declared FIRST so it observes the
// registry before any test mutates it.
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
        QVERIFY(reg.isRegistered(QString(ActionType::SetOpacity)));
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
        QVERIFY(reg.isTerminal(makeAction(ActionType::Exclude)));
        QVERIFY(!reg.isTerminal(makeAction(ActionType::Float)));
        QVERIFY(!reg.isTerminal(makeAction(ActionType::SetEngineMode)));
    }

    void testRegisterCustomAction()
    {
        ActionRegistry& reg = ActionRegistry::instance();
        const QString customType = QStringLiteral("pwrTestCustomAction");
        QVERIFY(!reg.isRegistered(customType));

        reg.registerAction(ActionDescriptor{customType,
                                            [](const QJsonObject&) {
                                                return QStringLiteral("custom-slot");
                                            },
                                            [](const QJsonObject&) {
                                                return true;
                                            },
                                            false});
        QVERIFY(reg.isRegistered(customType));
        QCOMPARE(reg.slotFor(makeAction(QLatin1StringView("pwrTestCustomAction"))), QStringLiteral("custom-slot"));
    }

    void testValidateRejectsUnregistered()
    {
        const ActionRegistry& reg = ActionRegistry::instance();
        QVERIFY(!reg.validate(makeAction(QLatin1StringView("notRegistered"))));
    }
};

QTEST_MAIN(TestActionRegistry)
#include "test_actionregistry.moc"
