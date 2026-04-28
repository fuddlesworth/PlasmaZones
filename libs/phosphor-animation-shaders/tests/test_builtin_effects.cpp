// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimationShaders/AnimationShaderRegistry.h>

#include <QDir>
#include <QFile>
#include <QTest>

using PhosphorAnimationShaders::AnimationShaderEffect;
using PhosphorAnimationShaders::AnimationShaderRegistry;

class TestBuiltinEffects : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testAllSevenEffectsDiscovered()
    {
        const QString dataDir = QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/animations");
        if (!QDir(dataDir).exists())
            QSKIP("data/animations not found — running outside source tree");

        AnimationShaderRegistry registry;
        registry.addSearchPath(dataDir);
        registry.refresh();

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
        registry.addSearchPath(dataDir);
        registry.refresh();

        const auto effects = registry.availableEffects();
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

    void testDissolveHasExpectedParameters()
    {
        const QString dataDir = QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/animations");
        if (!QDir(dataDir).exists())
            QSKIP("data/animations not found — running outside source tree");

        AnimationShaderRegistry registry;
        registry.addSearchPath(dataDir);
        registry.refresh();

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
