// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QSignalSpy>

#include "config/settings.h"
#include "settings/animationsettingscontroller.h"
#include "../helpers/IsolatedConfigGuard.h"

#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimationShaders/AnimationShaderRegistry.h>
#include <PhosphorAnimationShaders/ShaderProfile.h>
#include <PhosphorAnimationShaders/ShaderProfileTree.h>

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;
using PhosphorAnimationShaders::AnimationShaderRegistry;

class TestAnimationSettingsController : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ─── Construction ────────────────────────────────────────────────────

    void construct_withNullRegistry_doesNotCrash()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationSettingsController ctrl(&settings, nullptr);
        QCOMPARE(ctrl.availableTransitionEffects().size(), 0);
        QCOMPARE(ctrl.shaderParameters(QStringLiteral("dissolve")).size(), 0);
        QCOMPARE(ctrl.shaderDefaults(QStringLiteral("dissolve")).size(), 0);
        QVERIFY(ctrl.effectMetadata(QStringLiteral("dissolve")).isEmpty());
    }

    void construct_withNullSettings_doesNotCrash()
    {
        AnimationShaderRegistry registry;
        AnimationSettingsController ctrl(nullptr, &registry);
        QVERIFY(ctrl.effectForPath(QStringLiteral("zone.snapIn")).isEmpty());
        QVERIFY(ctrl.parametersForPath(QStringLiteral("zone.snapIn")).isEmpty());
        QVERIFY(ctrl.inheritSummaryForEvent(QStringLiteral("zone.snapIn")).isEmpty());
    }

    // ─── eventPaths ──────────────────────────────────────────────────────

    void eventPaths_returnsAllBuiltInPaths()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationSettingsController ctrl(&settings, nullptr);
        const auto paths = ctrl.eventPaths();
        QVERIFY(paths.size() > 10);
        QVERIFY(paths.contains(PhosphorAnimation::ProfilePaths::ZoneSnapIn));
        QVERIFY(paths.contains(PhosphorAnimation::ProfilePaths::OsdShow));
        QVERIFY(paths.contains(PhosphorAnimation::ProfilePaths::ZoneHighlight));
    }

    // ─── effectForPath / setEffectForPath / clearEffectForPath ────────────

    void setEffectForPath_persistsToSettings()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationSettingsController ctrl(&settings, nullptr);

        ctrl.setEffectForPath(QStringLiteral("zone.snapIn"), QStringLiteral("dissolve"));
        QCOMPARE(ctrl.effectForPath(QStringLiteral("zone.snapIn")), QStringLiteral("dissolve"));

        const auto tree = settings.shaderProfileTree();
        const auto profile = tree.resolve(QStringLiteral("zone.snapIn"));
        QCOMPARE(profile.effectiveEffectId(), QStringLiteral("dissolve"));
    }

    void clearEffectForPath_removesOverride()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationSettingsController ctrl(&settings, nullptr);

        ctrl.setEffectForPath(QStringLiteral("zone.snapIn"), QStringLiteral("dissolve"));
        QCOMPARE(ctrl.effectForPath(QStringLiteral("zone.snapIn")), QStringLiteral("dissolve"));

        ctrl.clearEffectForPath(QStringLiteral("zone.snapIn"));
        QVERIFY(ctrl.effectForPath(QStringLiteral("zone.snapIn")).isEmpty());
    }

    // ─── setParameterForPath ─────────────────────────────────────────────

    void setParameterForPath_storesValue()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationSettingsController ctrl(&settings, nullptr);

        ctrl.setEffectForPath(QStringLiteral("zone.snapIn"), QStringLiteral("dissolve"));
        ctrl.setParameterForPath(QStringLiteral("zone.snapIn"), QStringLiteral("grain"), 0.5);

        const auto params = ctrl.parametersForPath(QStringLiteral("zone.snapIn"));
        QVERIFY(params.contains(QStringLiteral("grain")));
        QCOMPARE(params.value(QStringLiteral("grain")).toDouble(), 0.5);
    }

    void setParameterForPath_withoutEffect_impliesCurrentEffect()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationSettingsController ctrl(&settings, nullptr);

        ctrl.setEffectForPath(QStringLiteral("zone.snapIn"), QStringLiteral("dissolve"));
        ctrl.setParameterForPath(QStringLiteral("zone.snapIn"), QStringLiteral("softness"), 0.8);

        QCOMPARE(ctrl.effectForPath(QStringLiteral("zone.snapIn")), QStringLiteral("dissolve"));
    }

    // ─── parentChainForEvent ─────────────────────────────────────────────

    void parentChainForEvent_containsTranslatedNames()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationSettingsController ctrl(&settings, nullptr);

        const auto chain = ctrl.parentChainForEvent(QStringLiteral("zone.snapIn"));
        QVERIFY(chain.contains(QStringLiteral("Zone")));
        QVERIFY(chain.contains(QStringLiteral("Snap In")));
        QVERIFY(chain.contains(QString::fromUtf8("→")));
    }

    void parentChainForEvent_handlesNewPaths()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationSettingsController ctrl(&settings, nullptr);

        const auto chain = ctrl.parentChainForEvent(QStringLiteral("zone.snapResize"));
        QVERIFY(chain.contains(QStringLiteral("Snap Resize")));

        const auto chain2 = ctrl.parentChainForEvent(QStringLiteral("osd.dim"));
        QVERIFY(chain2.contains(QStringLiteral("Dim")));
    }

    // ─── inheritSummaryForEvent ──────────────────────────────────────────

    void inheritSummaryForEvent_emptyTree_returnsNoEffect()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationSettingsController ctrl(&settings, nullptr);

        const auto summary = ctrl.inheritSummaryForEvent(QStringLiteral("zone.snapIn"));
        QVERIFY(!summary.isEmpty());
    }

    void inheritSummaryForEvent_withEffect_returnsEffectId()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationSettingsController ctrl(&settings, nullptr);

        ctrl.setEffectForPath(QStringLiteral("zone.snapIn"), QStringLiteral("dissolve"));
        const auto summary = ctrl.inheritSummaryForEvent(QStringLiteral("zone.snapIn"));
        QCOMPARE(summary, QStringLiteral("dissolve"));
    }

    // ─── Multiple overrides don't interfere ──────────────────────────────

    void multipleOverrides_areIndependent()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        AnimationSettingsController ctrl(&settings, nullptr);

        ctrl.setEffectForPath(QStringLiteral("zone.snapIn"), QStringLiteral("dissolve"));
        ctrl.setEffectForPath(QStringLiteral("zone.snapOut"), QStringLiteral("slide"));
        ctrl.setEffectForPath(QStringLiteral("osd.show"), QStringLiteral("glitch"));

        QCOMPARE(ctrl.effectForPath(QStringLiteral("zone.snapIn")), QStringLiteral("dissolve"));
        QCOMPARE(ctrl.effectForPath(QStringLiteral("zone.snapOut")), QStringLiteral("slide"));
        QCOMPARE(ctrl.effectForPath(QStringLiteral("osd.show")), QStringLiteral("glitch"));

        ctrl.clearEffectForPath(QStringLiteral("zone.snapOut"));
        QCOMPARE(ctrl.effectForPath(QStringLiteral("zone.snapIn")), QStringLiteral("dissolve"));
        QVERIFY(ctrl.effectForPath(QStringLiteral("zone.snapOut")).isEmpty());
        QCOMPARE(ctrl.effectForPath(QStringLiteral("osd.show")), QStringLiteral("glitch"));
    }
};

QTEST_MAIN(TestAnimationSettingsController)
#include "test_animation_settings_controller.moc"
