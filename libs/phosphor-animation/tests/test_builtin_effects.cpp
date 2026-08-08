// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/AnimationShaderRegistry.h>

#include <PhosphorFsLoader/WatchedDirectorySet.h>

#include <QDir>
#include <QFile>
#include <QTest>

using PhosphorAnimationShaders::AnimationShaderEffect;
using PhosphorAnimationShaders::AnimationShaderRegistry;

namespace {

/// Point @p registry at the bundled pack tree. Returns false when the source
/// tree is not present, which is the caller's cue to QSKIP.
///
/// QSKIP cannot live in here: it expands to a return, so it would abandon
/// this helper and let the slot carry on against an empty registry. Hence the
/// bool-and-skip-at-the-callsite shape rather than a void helper.
///
/// addSearchPath runs a synchronous initial scan via the underlying
/// WatchedDirectorySet, so no separate refresh() is needed.
bool openBundledPacks(AnimationShaderRegistry& registry)
{
    const QString dataDir = QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/animations");
    if (!QDir(dataDir).exists()) {
        return false;
    }
    registry.addSearchPath(dataDir, PhosphorFsLoader::LiveReload::Off);
    return true;
}

/// Every pack in @p registry declaring @p classToken, sorted. Pairs with the
/// hand-maintained per-class lists below to make them self-maintaining: the
/// list must equal this scan, so a new pack that forgets to register fails,
/// and so does an id left behind by a deleted pack.
QStringList packsDeclaring(const AnimationShaderRegistry& registry, const QString& classToken)
{
    QStringList ids;
    for (const AnimationShaderEffect& e : registry.availableEffects()) {
        if (e.appliesTo.contains(classToken)) {
            ids << e.id;
        }
    }
    ids.sort();
    return ids;
}

/// Assert that every id in @p expected exists, declares EXACTLY
/// {@p classToken}, and that no other pack declares that token.
///
/// Exact declaration rather than containment: an opt-in-class pack that grew
/// an "appearance" token would stop being compositor-only, and the daemon
/// would start warm-baking kwin classic-GL source that cannot compile on its
/// target. Collect-then-assert rather than QVERIFY2 in the loop, so one bad
/// pack does not hide the rest.
void verifyClassContract(const AnimationShaderRegistry& registry, const QString& classToken,
                         const QStringList& expected)
{
    QStringList missing;
    QStringList misdeclared;
    for (const QString& id : expected) {
        if (!registry.hasEffect(id)) {
            missing << id;
            continue;
        }
        const AnimationShaderEffect e = registry.effect(id);
        if (e.appliesTo != QStringList{classToken}) {
            misdeclared << (id + QStringLiteral(" → [") + e.appliesTo.join(QLatin1String(", ")) + QStringLiteral("]"));
        }
    }
    QVERIFY2(missing.isEmpty(),
             qPrintable(QStringLiteral("Missing %1 pack(s): ").arg(classToken) + missing.join(QLatin1String(", "))));
    QVERIFY2(misdeclared.isEmpty(),
             qPrintable(QStringLiteral("%1 packs must declare exactly [%1] (anything else changes their "
                                       "compositor-only classification): ")
                            .arg(classToken)
                        + misdeclared.join(QLatin1String("; "))));

    QStringList sortedExpected = expected;
    sortedExpected.sort();
    QCOMPARE(packsDeclaring(registry, classToken), sortedExpected);
}

} // namespace

class TestBuiltinEffects : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // The seven original per-window packs. The floor below is deliberately a
    // floor, not an equality: the bundled set grows freely, and pinning the
    // count would make every new pack a test edit. What matters is that these
    // seven keep resolving by id.
    void testCoreEffectsDiscovered()
    {
        AnimationShaderRegistry registry;
        if (!openBundledPacks(registry))
            QSKIP("data/animations not found — running outside source tree");

        const QStringList expected = {
            QStringLiteral("dissolve"),  QStringLiteral("glitch"), QStringLiteral("morph"),
            QStringLiteral("pixelate"),  QStringLiteral("popin"),  QStringLiteral("slide"),
            QStringLiteral("slidefade"),
        };

        for (const QString& id : expected) {
            QVERIFY2(registry.hasEffect(id), qPrintable(QStringLiteral("Missing effect: ") + id));
        }
        QVERIFY(registry.availableEffects().size() >= 7);
    }

    void testEachEffectHasValidMetadata()
    {
        AnimationShaderRegistry registry;
        if (!openBundledPacks(registry))
            QSKIP("data/animations not found — running outside source tree");

        const auto effects = registry.availableEffects();
        // Guard the loop: every assertion below lives inside it, so an empty
        // registry would pass this test vacuously rather than reporting that
        // discovery broke.
        QVERIFY(!effects.isEmpty());
        for (const AnimationShaderEffect& e : effects) {
            QVERIFY2(!e.id.isEmpty(), "Effect has empty id");
            QVERIFY2(!e.name.isEmpty(),
                     qPrintable(QStringLiteral("Effect ") + e.id + QStringLiteral(" has empty name")));
            QVERIFY2(!e.category.isEmpty(),
                     qPrintable(QStringLiteral("Effect ") + e.id + QStringLiteral(" has empty category")));
            QVERIFY2(QFile::exists(e.fragmentShaderPath),
                     qPrintable(QStringLiteral("Effect ") + e.id + QStringLiteral(" fragment shader not found: ")
                                + e.fragmentShaderPath));
        }
    }

    // Every desktop-class pack must DECLARE the desktop contract. `appliesTo`
    // is what `shaderEffectAppliesToEventPath` gates on (see
    // test_animationshadereffect), so a pack missing the "desktop" token is
    // refused on `desktop.switch` / `desktop.peek` and becomes silently
    // unselectable — while every other bundled-pack assertion here still
    // passes, since none of them read this field. Pinned per id rather than
    // by looping availableEffects(), so that DELETING the token from a
    // metadata.json fails instead of quietly shrinking the checked set.
    //
    // The list is the full desktop class, not just the peek packs: the hazard
    // is identical for the switch packs, and pinning only the newest three
    // would leave the same hole open on the other eleven. A new desktop pack
    // is expected to add its id here.
    void testDesktopPacksDeclareDesktopContract()
    {
        AnimationShaderRegistry registry;
        if (!openBundledPacks(registry))
            QSKIP("data/animations not found — running outside source tree");

        verifyClassContract(registry, QStringLiteral("desktop"),
                            {
                                QStringLiteral("peek-recede"),
                                QStringLiteral("peek-blinds"),
                                QStringLiteral("phosphor-peek"),
                                QStringLiteral("desktop-fade"),
                                QStringLiteral("desktop-slide"),
                                QStringLiteral("desktop-slidefade"),
                                QStringLiteral("desktop-wipe"),
                                QStringLiteral("desktop-circle"),
                                QStringLiteral("desktop-dissolve"),
                                QStringLiteral("desktop-pixelate"),
                                QStringLiteral("desktop-cube"),
                                QStringLiteral("desktop-crosszoom"),
                                QStringLiteral("desktop-aretha"),
                                QStringLiteral("desktop-phosphor"),
                            });
    }

    // Third opt-in class, same hazard: a move pack missing the "move" token
    // is refused on `window.movement.move`, and because that leaf takes no
    // inherited shader it then has no way to run at all. The move class
    // predates the desktop and strip lists above and was the only one of the
    // three left unpinned. A new drag-physics pack is expected to add its id
    // here.
    void testMovePacksDeclareMoveContract()
    {
        AnimationShaderRegistry registry;
        if (!openBundledPacks(registry))
            QSKIP("data/animations not found — running outside source tree");

        verifyClassContract(registry, QStringLiteral("move"),
                            {
                                QStringLiteral("wobble"),
                                QStringLiteral("phosphor-vortex"),
                            });
    }

    // Same hazard, strip class: a strip pack missing the "strip" token is
    // refused on `scrolling.view` and becomes silently unselectable.
    void testStripPacksDeclareStripContract()
    {
        AnimationShaderRegistry registry;
        if (!openBundledPacks(registry))
            QSKIP("data/animations not found — running outside source tree");

        verifyClassContract(registry, QStringLiteral("strip"),
                            {
                                QStringLiteral("strip-motion-blur"),
                                QStringLiteral("phosphor-gate"),
                                QStringLiteral("strip-chromatic"),
                                QStringLiteral("strip-jelly"),
                                QStringLiteral("strip-carousel"),
                            });
    }

    void testDissolveHasExpectedParameters()
    {
        AnimationShaderRegistry registry;
        if (!openBundledPacks(registry))
            QSKIP("data/animations not found — running outside source tree");

        const AnimationShaderEffect e = registry.effect(QStringLiteral("dissolve"));
        QCOMPARE(e.parameters.size(), 2);

        bool hasGrain = false;
        bool hasSoftness = false;
        for (const auto& p : e.parameters) {
            if (p.id == QStringLiteral("grain"))
                hasGrain = true;
            if (p.id == QStringLiteral("softness"))
                hasSoftness = true;
        }
        QVERIFY(hasGrain);
        QVERIFY(hasSoftness);
    }
};

QTEST_MAIN(TestBuiltinEffects)
#include "test_builtin_effects.moc"
