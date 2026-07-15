// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/AnimationShaderRegistry.h>

#include <PhosphorFsLoader/WatchedDirectorySet.h>

#include <QDir>
#include <QFile>
#include <QTest>

using PhosphorAnimationShaders::AnimationShaderEffect;
using PhosphorAnimationShaders::AnimationShaderRegistry;

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
        const QString dataDir = QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/animations");
        if (!QDir(dataDir).exists())
            QSKIP("data/animations not found — running outside source tree");

        AnimationShaderRegistry registry;
        // addSearchPath now runs a synchronous initial scan via the
        // underlying WatchedDirectorySet — no separate refresh() needed.
        registry.addSearchPath(dataDir, PhosphorFsLoader::LiveReload::Off);

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
        const QString dataDir = QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/animations");
        if (!QDir(dataDir).exists())
            QSKIP("data/animations not found — running outside source tree");

        AnimationShaderRegistry registry;
        // addSearchPath now runs a synchronous initial scan via the
        // underlying WatchedDirectorySet — no separate refresh() needed.
        registry.addSearchPath(dataDir, PhosphorFsLoader::LiveReload::Off);

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
        const QString dataDir = QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/animations");
        if (!QDir(dataDir).exists())
            QSKIP("data/animations not found — running outside source tree");

        AnimationShaderRegistry registry;
        registry.addSearchPath(dataDir, PhosphorFsLoader::LiveReload::Off);

        const QStringList desktopPacks = {
            QStringLiteral("peek-recede"),      QStringLiteral("peek-blinds"),      QStringLiteral("phosphor-peek"),
            QStringLiteral("desktop-fade"),     QStringLiteral("desktop-slide"),    QStringLiteral("desktop-slidefade"),
            QStringLiteral("desktop-wipe"),     QStringLiteral("desktop-circle"),   QStringLiteral("desktop-dissolve"),
            QStringLiteral("desktop-pixelate"), QStringLiteral("desktop-cube"),     QStringLiteral("desktop-crosszoom"),
            QStringLiteral("desktop-aretha"),   QStringLiteral("desktop-phosphor"),
        };

        for (const QString& id : desktopPacks) {
            QVERIFY2(registry.hasEffect(id), qPrintable(QStringLiteral("Missing desktop pack: ") + id));
            const AnimationShaderEffect e = registry.effect(id);
            QVERIFY2(e.appliesTo.contains(QStringLiteral("desktop")),
                     qPrintable(QStringLiteral("Pack ") + id
                                + QStringLiteral(" does not declare appliesTo \"desktop\"; it would be refused on "
                                                 "every desktop event path")));
        }
    }

    void testDissolveHasExpectedParameters()
    {
        const QString dataDir = QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/animations");
        if (!QDir(dataDir).exists())
            QSKIP("data/animations not found — running outside source tree");

        AnimationShaderRegistry registry;
        // addSearchPath now runs a synchronous initial scan via the
        // underlying WatchedDirectorySet — no separate refresh() needed.
        registry.addSearchPath(dataDir, PhosphorFsLoader::LiveReload::Off);

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
