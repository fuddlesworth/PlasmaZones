// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_animations_shader_overrides.cpp
 * @brief AnimationsPageController shader-override tests.
 *
 * Split from test_animations_page_controller.cpp to keep each test file
 * under the project's <800-line guideline. Pinned behaviour:
 *   - setShaderOverride round-trip through Settings/ShaderProfileTree
 *   - setShaderOverride emits pendingChangesChanged on every mutation
 *     (set, clear, repeated picks, empty-effect-clear shorthand)
 *   - No-op short-circuit when the request matches the existing tree
 *     state (prevents the QML two-way binding from re-dirtying on every
 *     refresh)
 */

#include <QSignalSpy>
#include <QTest>

#include <PhosphorAnimation/AnimationShaderRegistry.h>

#include "config/settings.h"
#include "settings/animationspagecontroller.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestAnimationsShaderOverrides : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// Simulates the full AnimationEventCard combo flow:
    ///   user clicks pixelate
    ///     → setShaderOverride(path, "pixelate", {})
    ///   refresh fires
    ///     → resolvedShaderProfile(path) → expect effectId="pixelate"
    /// If resolvedShaderProfile returns empty effectId here, the QML
    /// combo would snap back to "None" — mirrors the user's symptom.
    void setShaderOverride_resolveReturnsTheJustWrittenEffect()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        PhosphorAnimationShaders::AnimationShaderRegistry registry;
        AnimationsPageController c(&registry, &settings);

        const QString path = QStringLiteral("osd.show");

        QVERIFY(c.setShaderOverride(path, QStringLiteral("pixelate"), {}));

        const QVariantMap raw = c.rawShaderProfile(path);
        const QVariantMap resolved = c.resolvedShaderProfile(path);

        // Raw read MUST contain effectId=pixelate.
        QCOMPARE(raw.value(QStringLiteral("effectId")).toString(), QStringLiteral("pixelate"));
        // Resolved read (what the QML refresh path uses) MUST too.
        QCOMPARE(resolved.value(QStringLiteral("effectId")).toString(), QStringLiteral("pixelate"));
    }

    /// Symptom-mirroring test: pick A, then immediately pick B.
    /// Both reads must return the corresponding effect.
    void setShaderOverride_repeatedPicksPreserveLatest()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        PhosphorAnimationShaders::AnimationShaderRegistry registry;
        AnimationsPageController c(&registry, &settings);

        const QString path = QStringLiteral("osd.show");

        QVERIFY(c.setShaderOverride(path, QStringLiteral("pixelate"), {}));
        QCOMPARE(c.resolvedShaderProfile(path).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("pixelate"));

        QVERIFY(c.setShaderOverride(path, QStringLiteral("dissolve"), {}));
        QCOMPARE(c.resolvedShaderProfile(path).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("dissolve"));

        QVERIFY(c.setShaderOverride(path, QStringLiteral("glitch"), {}));
        QCOMPARE(c.resolvedShaderProfile(path).value(QStringLiteral("effectId")).toString(), QStringLiteral("glitch"));
    }

    /// Pre-fix, setShaderOverride and clearShaderOverride mutated
    /// settings but never emitted pendingChangesChanged, so the Save
    /// button never lit up for shader edits.
    void setShaderOverride_emitsPendingChangesChanged()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        PhosphorAnimationShaders::AnimationShaderRegistry registry;
        AnimationsPageController c(&registry, &settings);

        QSignalSpy spy(&c, &AnimationsPageController::pendingChangesChanged);
        QVERIFY(c.setShaderOverride(QStringLiteral("osd.show"), QStringLiteral("pixelate"), {}));
        QVERIFY2(spy.count() >= 1, "setShaderOverride MUST emit pendingChangesChanged");
    }

    /// `setShaderOverride(path, "")` — the empty-effectId clear shorthand
    /// — MUST also emit pendingChangesChanged. Pre-fix it routed to
    /// clearShaderOverride which silently mutated.
    void setShaderOverride_emptyEffectClearsAndEmits()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        PhosphorAnimationShaders::AnimationShaderRegistry registry;
        AnimationsPageController c(&registry, &settings);

        // First write something so there's state to clear.
        QVERIFY(c.setShaderOverride(QStringLiteral("osd.show"), QStringLiteral("pixelate"), {}));

        QSignalSpy spy(&c, &AnimationsPageController::pendingChangesChanged);
        QVERIFY(c.setShaderOverride(QStringLiteral("osd.show"), QString(), {}));
        QVERIFY2(spy.count() >= 1, "setShaderOverride('','') MUST emit pendingChangesChanged on clear");
        QVERIFY(c.rawShaderProfile(QStringLiteral("osd.show")).isEmpty());
    }

    /// Set then explicit clear → both calls fire pendingChangesChanged.
    void setShaderOverride_thenClear_emitsTwice()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        PhosphorAnimationShaders::AnimationShaderRegistry registry;
        AnimationsPageController c(&registry, &settings);

        QSignalSpy spy(&c, &AnimationsPageController::pendingChangesChanged);
        QVERIFY(c.setShaderOverride(QStringLiteral("osd.show"), QStringLiteral("pixelate"), {}));
        QVERIFY(c.clearShaderOverride(QStringLiteral("osd.show")));
        QVERIFY2(spy.count() >= 2,
                 qPrintable(QStringLiteral("expected ≥2 emissions, got ") + QString::number(spy.count())));
    }

    /// `setShaderOverride` short-circuits when the request matches the
    /// existing tree state. Without this, the QML two-way binding round-
    /// trip (combo activation → controller → settings → tree → QML
    /// refresh → re-emit) would dirty the page on every reload.
    void setShaderOverride_noOpWhenTreeAlreadyMatches()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        PhosphorAnimationShaders::AnimationShaderRegistry registry;
        AnimationsPageController c(&registry, &settings);

        QVERIFY(c.setShaderOverride(QStringLiteral("osd.show"), QStringLiteral("pixelate"), {}));

        QSignalSpy pendingSpy(&c, &AnimationsPageController::pendingChangesChanged);
        // Same effectId + same (empty) parameters — must early-return.
        QVERIFY(c.setShaderOverride(QStringLiteral("osd.show"), QStringLiteral("pixelate"), {}));
        QCOMPARE(pendingSpy.count(), 0);
    }
};

QTEST_MAIN(TestAnimationsShaderOverrides)
#include "test_animations_shader_overrides.moc"
