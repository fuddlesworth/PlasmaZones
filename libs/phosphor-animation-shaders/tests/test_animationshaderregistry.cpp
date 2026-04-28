// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimationShaders/AnimationShaderRegistry.h>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using PhosphorAnimationShaders::AnimationShaderEffect;
using PhosphorAnimationShaders::AnimationShaderRegistry;

namespace {

void writeMetadata(const QString& dir, const QString& id, const QString& fragShader = QStringLiteral("effect.frag"))
{
    QDir().mkpath(dir);
    QJsonObject obj;
    obj.insert(QLatin1String("id"), id);
    obj.insert(QLatin1String("name"), id);
    obj.insert(QLatin1String("fragmentShader"), fragShader);
    obj.insert(QLatin1String("category"), QStringLiteral("Test"));

    QFile file(dir + QStringLiteral("/metadata.json"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(obj).toJson());
    file.close();

    QFile frag(dir + QLatin1Char('/') + fragShader);
    QVERIFY(frag.open(QIODevice::WriteOnly));
    frag.write("void main() {}");
    frag.close();
}

} // namespace

class TestAnimationShaderRegistry : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testEmptyRegistryHasNoEffects()
    {
        AnimationShaderRegistry registry;
        QVERIFY(registry.availableEffects().isEmpty());
        QVERIFY(registry.effectIds().isEmpty());
        QVERIFY(!registry.hasEffect(QStringLiteral("dissolve")));
    }

    void testDiscoversSingleEffect()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QString effectDir = tmp.path() + QStringLiteral("/dissolve");
        writeMetadata(effectDir, QStringLiteral("dissolve"));

        AnimationShaderRegistry registry;
        registry.addSearchPath(tmp.path());
        registry.refresh();

        QVERIFY(registry.hasEffect(QStringLiteral("dissolve")));
        const AnimationShaderEffect e = registry.effect(QStringLiteral("dissolve"));
        QCOMPARE(e.id, QStringLiteral("dissolve"));
        QVERIFY(e.fragmentShaderPath.endsWith(QStringLiteral("effect.frag")));
        QCOMPARE(e.sourceDir, effectDir);
    }

    void testDiscoversMultipleEffects()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        writeMetadata(tmp.path() + QStringLiteral("/dissolve"), QStringLiteral("dissolve"));
        writeMetadata(tmp.path() + QStringLiteral("/slide"), QStringLiteral("slide"));
        writeMetadata(tmp.path() + QStringLiteral("/glitch"), QStringLiteral("glitch"));

        AnimationShaderRegistry registry;
        registry.addSearchPath(tmp.path());
        registry.refresh();

        QCOMPARE(registry.availableEffects().size(), 3);
        QVERIFY(registry.hasEffect(QStringLiteral("dissolve")));
        QVERIFY(registry.hasEffect(QStringLiteral("slide")));
        QVERIFY(registry.hasEffect(QStringLiteral("glitch")));
    }

    void testLaterPathOverridesEarlier()
    {
        QTemporaryDir system;
        QTemporaryDir user;
        QVERIFY(system.isValid());
        QVERIFY(user.isValid());

        writeMetadata(system.path() + QStringLiteral("/dissolve"), QStringLiteral("dissolve"),
                      QStringLiteral("system.frag"));
        writeMetadata(user.path() + QStringLiteral("/dissolve"), QStringLiteral("dissolve"),
                      QStringLiteral("user.frag"));

        AnimationShaderRegistry registry;
        registry.addSearchPath(system.path());
        registry.addSearchPath(user.path());
        registry.refresh();

        QCOMPARE(registry.availableEffects().size(), 1);
        const AnimationShaderEffect e = registry.effect(QStringLiteral("dissolve"));
        QVERIFY(e.fragmentShaderPath.endsWith(QStringLiteral("user.frag")));
    }

    void testSkipsMissingId()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QString effectDir = tmp.path() + QStringLiteral("/broken");
        QDir().mkpath(effectDir);
        QJsonObject obj;
        obj.insert(QLatin1String("name"), QStringLiteral("broken"));
        QFile file(effectDir + QStringLiteral("/metadata.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(obj).toJson());

        AnimationShaderRegistry registry;
        registry.addSearchPath(tmp.path());
        registry.refresh();

        QVERIFY(registry.availableEffects().isEmpty());
    }

    void testSkipsMalformedJson()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QString effectDir = tmp.path() + QStringLiteral("/bad");
        QDir().mkpath(effectDir);
        QFile file(effectDir + QStringLiteral("/metadata.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{ not valid json");

        AnimationShaderRegistry registry;
        registry.addSearchPath(tmp.path());
        registry.refresh();

        QVERIFY(registry.availableEffects().isEmpty());
    }

    void testSkipsMissingDirectory()
    {
        AnimationShaderRegistry registry;
        registry.addSearchPath(QStringLiteral("/nonexistent/path/that/does/not/exist"));
        registry.refresh();

        QVERIFY(registry.availableEffects().isEmpty());
    }

    void testRefreshDetectsNewEffect()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        AnimationShaderRegistry registry;
        registry.addSearchPath(tmp.path());
        registry.refresh();
        QVERIFY(registry.availableEffects().isEmpty());

        writeMetadata(tmp.path() + QStringLiteral("/morph"), QStringLiteral("morph"));
        registry.refresh();

        QVERIFY(registry.hasEffect(QStringLiteral("morph")));
    }

    void testRefreshDetectsRemovedEffect()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        writeMetadata(tmp.path() + QStringLiteral("/dissolve"), QStringLiteral("dissolve"));

        AnimationShaderRegistry registry;
        registry.addSearchPath(tmp.path());
        registry.refresh();
        QVERIFY(registry.hasEffect(QStringLiteral("dissolve")));

        QDir(tmp.path() + QStringLiteral("/dissolve")).removeRecursively();
        registry.refresh();

        QVERIFY(!registry.hasEffect(QStringLiteral("dissolve")));
    }

    void testEffectsChangedSignal()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        writeMetadata(tmp.path() + QStringLiteral("/dissolve"), QStringLiteral("dissolve"));

        AnimationShaderRegistry registry;
        registry.addSearchPath(tmp.path());

        QSignalSpy spy(&registry, &AnimationShaderRegistry::effectsChanged);
        registry.refresh();
        QCOMPARE(spy.count(), 1);

        registry.refresh();
        QCOMPARE(spy.count(), 1);
    }

    void testRemoveSearchPath()
    {
        AnimationShaderRegistry registry;
        registry.addSearchPath(QStringLiteral("/tmp/test-path"));
        QCOMPARE(registry.searchPaths().size(), 1);

        registry.removeSearchPath(QStringLiteral("/tmp/test-path"));
        QVERIFY(registry.searchPaths().isEmpty());
    }

    void testAddDuplicateSearchPathIsNoop()
    {
        AnimationShaderRegistry registry;
        registry.addSearchPath(QStringLiteral("/tmp/test-path"));
        registry.addSearchPath(QStringLiteral("/tmp/test-path"));
        QCOMPARE(registry.searchPaths().size(), 1);
    }

    void testIsUserEffectFromExplicitFlag()
    {
        QTemporaryDir system;
        QTemporaryDir user;
        QVERIFY(system.isValid());
        QVERIFY(user.isValid());

        writeMetadata(system.path() + QStringLiteral("/dissolve"), QStringLiteral("dissolve"));
        writeMetadata(user.path() + QStringLiteral("/morph"), QStringLiteral("morph"));

        AnimationShaderRegistry registry;
        registry.addSearchPath(system.path(), false);
        registry.addSearchPath(user.path(), true);
        registry.refresh();

        QVERIFY(!registry.effect(QStringLiteral("dissolve")).isUserEffect);
        QVERIFY(registry.effect(QStringLiteral("morph")).isUserEffect);
    }

    void testResolvesAbsoluteShaderPaths()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QString effectDir = tmp.path() + QStringLiteral("/slide");
        writeMetadata(effectDir, QStringLiteral("slide"), QStringLiteral("slide.frag"));

        AnimationShaderRegistry registry;
        registry.addSearchPath(tmp.path());
        registry.refresh();

        const AnimationShaderEffect e = registry.effect(QStringLiteral("slide"));
        QCOMPARE(e.fragmentShaderPath, effectDir + QStringLiteral("/slide.frag"));
    }
};

QTEST_MAIN(TestAnimationShaderRegistry)
#include "test_animationshaderregistry.moc"
