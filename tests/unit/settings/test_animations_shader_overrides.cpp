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

    /// `setShaderOverride(path, "")` — the empty-effectId disable shorthand
    /// — MUST emit pendingChangesChanged. Behavior change: empty-effectId
    /// now writes the engaged-empty disable sentinel (blocks inheritance
    /// from a parent shader) rather than removing the override entry. The
    /// raw profile is therefore NOT empty after the write — it carries an
    /// engaged-empty `effectId`.
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
        QVERIFY2(spy.count() >= 1, "setShaderOverride('','') MUST emit pendingChangesChanged on disable");
        // Engaged-empty sentinel: `effectId` is engaged but the value is
        // an empty string, signalling "explicitly no shader" (blocks
        // parent-inheritance cascade).
        const QVariantMap raw = c.rawShaderProfile(QStringLiteral("osd.show"));
        QVERIFY2(!raw.isEmpty(), "engaged-empty disable sentinel must persist a profile entry");
        QCOMPARE(raw.value(QStringLiteral("effectId")).toString(), QString());
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
                 qPrintable(QStringLiteral("expected >=2 emissions, got ") + QString::number(spy.count())));
    }

    /// User-reported scenario: parent ("All Window Events" / "All
    /// Popups" / etc.) has an explicit shader. The user wants to override
    /// a CHILD event with a different shader. Each step's resolution
    /// must reflect the latest write — otherwise the picker appears to
    /// "reject" the user's selection because the resolved value the QML
    /// reads back is still the inherited parent shader.
    void setShaderOverride_childOverridesParentInheritance()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        PhosphorAnimationShaders::AnimationShaderRegistry registry;
        AnimationsPageController c(&registry, &settings);

        // Parent ("All Window Events") set to matrix
        QVERIFY(c.setShaderOverride(QStringLiteral("window"), QStringLiteral("matrix"), {}));
        QCOMPARE(c.resolvedShaderProfile(QStringLiteral("window")).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("matrix"));
        // Child inherits matrix.
        QCOMPARE(c.resolvedShaderProfile(QStringLiteral("window.appearance.open"))
                     .value(QStringLiteral("effectId"))
                     .toString(),
                 QStringLiteral("matrix"));
        // Child has NO direct override yet.
        QVERIFY(c.rawShaderProfile(QStringLiteral("window.appearance.open")).isEmpty());

        // Child overrides to a DIFFERENT shader.
        QVERIFY(c.setShaderOverride(QStringLiteral("window.appearance.open"), QStringLiteral("dissolve"), {}));

        // Child must resolve to dissolve (direct override wins over
        // parent's matrix). Parent's window resolution is unchanged.
        QCOMPARE(c.resolvedShaderProfile(QStringLiteral("window.appearance.open"))
                     .value(QStringLiteral("effectId"))
                     .toString(),
                 QStringLiteral("dissolve"));
        QCOMPARE(c.resolvedShaderProfile(QStringLiteral("window")).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("matrix"));
        // Direct override is now visible at the child path.
        QCOMPARE(
            c.rawShaderProfile(QStringLiteral("window.appearance.open")).value(QStringLiteral("effectId")).toString(),
            QStringLiteral("dissolve"));

        // Switch the child to a third shader — must take effect with no
        // residual influence from the prior child override.
        QVERIFY(c.setShaderOverride(QStringLiteral("window.appearance.open"), QStringLiteral("glitch"), {}));
        QCOMPARE(c.resolvedShaderProfile(QStringLiteral("window.appearance.open"))
                     .value(QStringLiteral("effectId"))
                     .toString(),
                 QStringLiteral("glitch"));
    }

    /// Repeated identical OFF toggles MUST short-circuit after the first
    /// write — the engaged-empty disable sentinel is materially the same
    /// state on disk, so a no-op second call should not light the Save
    /// button. Pins the comparison contract documented next to the
    /// engaged-empty branch in `setShaderOverride`: `ShaderProfile::operator==`
    /// is engaged-state-sensitive, and the constructed `disabledProfile`
    /// matches the disk-loaded round-trip shape (parameters = nullopt).
    void setShaderOverride_repeatedDisableIsNoOp()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        PhosphorAnimationShaders::AnimationShaderRegistry registry;
        AnimationsPageController c(&registry, &settings);

        const QString path = QStringLiteral("popup");

        // First disable write — fires pendingChangesChanged once.
        QSignalSpy spy(&c, &AnimationsPageController::pendingChangesChanged);
        QVERIFY(c.setShaderOverride(path, QString(), {}));
        QCOMPARE(spy.count(), 1);

        // Second identical disable write — must short-circuit, no extra
        // signal. If this fires a second time, the compare-and-skip path
        // diverged from the engaged-state-sensitive equality contract.
        QVERIFY(c.setShaderOverride(path, QString(), {}));
        QCOMPARE(spy.count(), 1);
        // Pin tree state too — a future regression that returns true while
        // quietly mutating the tree (e.g. clearing the engaged-empty
        // sentinel, or engaging the parameters optional) wouldn't trip
        // the spy-count check, but would flip `effectId` from engaged-
        // empty back to nullopt-on-read OR add a `parameters` key here.
        QCOMPARE(c.rawShaderProfile(path).value(QStringLiteral("effectId")).toString(), QString());
        QVERIFY(!c.rawShaderProfile(path).contains(QStringLiteral("parameters")));

        // Third identical disable write — same invariant.
        QVERIFY(c.setShaderOverride(path, QString(), {}));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(c.rawShaderProfile(path).value(QStringLiteral("effectId")).toString(), QString());
        QVERIFY(!c.rawShaderProfile(path).contains(QStringLiteral("parameters")));
    }

    // The descendant-coverage tests below each construct a fresh
    // `Settings` under an `IsolatedConfigGuard`, so the guard redirects
    // `QStandardPaths` away from the user's real `~/.config` for the
    // lifetime of the test. Each case is therefore hermetic — no state
    // leaks between tests, no dependency on Qt Test's invocation order,
    // and no risk of clobbering the developer's actual config.

    /// `shaderOverrideDescendantCount` must count strict descendants only —
    /// dot-bounded prefix match, not the path itself, not siblings with
    /// shared character prefix. Pin both the non-zero and zero branches
    /// plus a deeply-nested chain.
    void shaderOverrideDescendantCount_strictDescendantsOnly()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        PhosphorAnimationShaders::AnimationShaderRegistry registry;
        AnimationsPageController c(&registry, &settings);

        // Empty tree → count is zero for any path.
        QCOMPARE(c.shaderOverrideDescendantCount(QStringLiteral("popup")), 0);
        QCOMPARE(c.shaderOverrideDescendantCount(QStringLiteral("popup.layoutPicker")), 0);

        // Self-only override at popup.layoutPicker — descendants of it: 0.
        QVERIFY(c.setShaderOverride(QStringLiteral("popup.layoutPicker"), QStringLiteral("dissolve"), {}));
        QCOMPARE(c.shaderOverrideDescendantCount(QStringLiteral("popup.layoutPicker")), 0);
        // Descendants of `popup` includes `popup.layoutPicker`: 1.
        QCOMPARE(c.shaderOverrideDescendantCount(QStringLiteral("popup")), 1);

        // Add a deeply-nested descendant.
        QVERIFY(c.setShaderOverride(QStringLiteral("popup.layoutPicker.show"), QStringLiteral("pixelate"), {}));
        // popup sees both descendants now.
        QCOMPARE(c.shaderOverrideDescendantCount(QStringLiteral("popup")), 2);
        // popup.layoutPicker sees the leaf only (itself excluded by strict prefix).
        QCOMPARE(c.shaderOverrideDescendantCount(QStringLiteral("popup.layoutPicker")), 1);
        // The leaf itself has no descendants.
        QCOMPARE(c.shaderOverrideDescendantCount(QStringLiteral("popup.layoutPicker.show")), 0);

        // Empty path → zero (defensive: collectShaderOverrideDescendants
        // bails on empty path so the prefix isn't a bare ".").
        QCOMPARE(c.shaderOverrideDescendantCount(QString()), 0);
    }

    /// `clearShaderOverrideDescendants` removes every strict descendant,
    /// preserves the path-itself override (parent-card "Clear shadowing
    /// children" semantics), and is a no-op when count is zero.
    void clearShaderOverrideDescendants_clearsStrictDescendantsOnly()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        PhosphorAnimationShaders::AnimationShaderRegistry registry;
        AnimationsPageController c(&registry, &settings);

        // Build: parent override + two descendants at different depths.
        QVERIFY(c.setShaderOverride(QStringLiteral("popup"), QStringLiteral("dissolve"), {}));
        QVERIFY(c.setShaderOverride(QStringLiteral("popup.layoutPicker.show"), QStringLiteral("pixelate"), {}));
        QVERIFY(c.setShaderOverride(QStringLiteral("popup.zoneSelector.show"), QStringLiteral("matrix"), {}));
        QCOMPARE(c.shaderOverrideDescendantCount(QStringLiteral("popup")), 2);

        // Clear from popup — both leaves go, popup itself remains
        // (it's the parent the user is keeping intact).
        QSignalSpy spy(&c, &AnimationsPageController::pendingChangesChanged);
        QCOMPARE(c.clearShaderOverrideDescendants(QStringLiteral("popup")), 2);
        QVERIFY2(spy.count() >= 1,
                 "clearShaderOverrideDescendants MUST emit pendingChangesChanged when it cleared anything");
        // Verify count is now zero.
        QCOMPARE(c.shaderOverrideDescendantCount(QStringLiteral("popup")), 0);
        // Parent override survived.
        QCOMPARE(c.rawShaderProfile(QStringLiteral("popup")).value(QStringLiteral("effectId")).toString(),
                 QStringLiteral("dissolve"));
        // Leaf overrides really gone.
        QVERIFY(c.rawShaderProfile(QStringLiteral("popup.layoutPicker.show")).isEmpty());
        QVERIFY(c.rawShaderProfile(QStringLiteral("popup.zoneSelector.show")).isEmpty());

        // No-op call — count already zero.
        QSignalSpy spy2(&c, &AnimationsPageController::pendingChangesChanged);
        QCOMPARE(c.clearShaderOverrideDescendants(QStringLiteral("popup")), 0);
        QCOMPARE(spy2.count(), 0);
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
