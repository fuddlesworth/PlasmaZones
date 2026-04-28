// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimationShaders/AnimationShaderRegistry.h>

#include <PhosphorFsLoader/WatchedDirectorySet.h>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using PhosphorAnimationShaders::AnimationShaderEffect;
using PhosphorAnimationShaders::AnimationShaderRegistry;
using PhosphorFsLoader::LiveReload;

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
        // addSearchPath now runs a synchronous scan via the underlying
        // WatchedDirectorySet — no separate refresh() needed.
        registry.addSearchPath(tmp.path(), LiveReload::Off);

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
        registry.addSearchPath(tmp.path(), LiveReload::Off);

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
        // Caller order is system-first / user-last per the public
        // contract; the strategy reverse-iterates so user wins on
        // collision. Single batched call avoids the N-rescans-on-startup
        // amplification of a per-path loop.
        registry.addSearchPaths({system.path(), user.path()}, LiveReload::Off);

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
        registry.addSearchPath(tmp.path(), LiveReload::Off);

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
        registry.addSearchPath(tmp.path(), LiveReload::Off);

        QVERIFY(registry.availableEffects().isEmpty());
    }

    void testSkipsMissingDirectory()
    {
        AnimationShaderRegistry registry;
        registry.addSearchPath(QStringLiteral("/nonexistent/path/that/does/not/exist"), LiveReload::Off);

        QVERIFY(registry.availableEffects().isEmpty());
    }

    void testRefreshDetectsNewEffect()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        AnimationShaderRegistry registry;
        registry.addSearchPath(tmp.path(), LiveReload::Off);
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
        registry.addSearchPath(tmp.path(), LiveReload::Off);
        QVERIFY(registry.hasEffect(QStringLiteral("dissolve")));

        QDir(tmp.path() + QStringLiteral("/dissolve")).removeRecursively();
        registry.refresh();

        QVERIFY(!registry.hasEffect(QStringLiteral("dissolve")));
    }

    /// `effectsChanged` fires only on actual content change, not on
    /// every rescan. The bespoke registry guarded its emit with
    /// `if (newEffects != m_effects)`; the WatchedDirectorySet-backed
    /// implementation preserves that contract — every rescan calls into
    /// `performScan`, but the diff is what gates the emit.
    void testEffectsChangedSignal()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        AnimationShaderRegistry registry;
        // Initial registration with an empty dir — m_effects starts
        // empty and stays empty, no diff, no emit. Spy attaches to
        // observe subsequent transitions only.
        registry.addSearchPath(tmp.path(), LiveReload::Off);
        QVERIFY(registry.availableEffects().isEmpty());

        QSignalSpy spy(&registry, &AnimationShaderRegistry::effectsChanged);

        // First content-change: an effect appears. Diff is non-empty,
        // emit fires.
        writeMetadata(tmp.path() + QStringLiteral("/dissolve"), QStringLiteral("dissolve"));
        registry.refresh();
        QCOMPARE(spy.count(), 1);

        // Second refresh with the same on-disk content. Diff is empty,
        // no emit.
        registry.refresh();
        QCOMPARE(spy.count(), 1);
    }

    void testAddDuplicateSearchPathIsNoop()
    {
        AnimationShaderRegistry registry;
        registry.addSearchPath(QStringLiteral("/tmp/test-path"), LiveReload::Off);
        registry.addSearchPath(QStringLiteral("/tmp/test-path"), LiveReload::Off);
        QCOMPARE(registry.searchPaths().size(), 1);
    }

    /// `setUserShaderPath` classifies a registered dir as user vs system
    /// for the `isUserEffect` flag. Order-independent: works whether
    /// called before OR after `addSearchPaths`. The "after" case
    /// triggers a synchronous reclassification rescan; the "before"
    /// case bakes the right classification into the initial scan.
    void testIsUserEffectFromUserShaderPath()
    {
        QTemporaryDir system;
        QTemporaryDir user;
        QVERIFY(system.isValid());
        QVERIFY(user.isValid());

        writeMetadata(system.path() + QStringLiteral("/dissolve"), QStringLiteral("dissolve"));
        writeMetadata(user.path() + QStringLiteral("/morph"), QStringLiteral("morph"));

        AnimationShaderRegistry registry;
        // setUserShaderPath BEFORE addSearchPaths — initial scan sees
        // the right classification.
        registry.setUserShaderPath(user.path());
        registry.addSearchPaths({system.path(), user.path()}, LiveReload::Off);

        QVERIFY(!registry.effect(QStringLiteral("dissolve")).isUserEffect);
        QVERIFY(registry.effect(QStringLiteral("morph")).isUserEffect);
    }

    /// Order-independence pinned: `setUserShaderPath` AFTER
    /// `addSearchPaths` triggers a reclassification rescan so already-
    /// discovered effects pick up the new flag.
    void testSetUserShaderPath_reclassifiesAfterAddSearchPaths()
    {
        QTemporaryDir system;
        QTemporaryDir user;
        QVERIFY(system.isValid());
        QVERIFY(user.isValid());

        writeMetadata(system.path() + QStringLiteral("/dissolve"), QStringLiteral("dissolve"));
        writeMetadata(user.path() + QStringLiteral("/morph"), QStringLiteral("morph"));

        AnimationShaderRegistry registry;
        registry.addSearchPaths({system.path(), user.path()}, LiveReload::Off);

        // Before the user-path is set, every effect is system.
        QVERIFY(!registry.effect(QStringLiteral("dissolve")).isUserEffect);
        QVERIFY(!registry.effect(QStringLiteral("morph")).isUserEffect);

        // Set the user path AFTER registration — rescan reclassifies.
        registry.setUserShaderPath(user.path());

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
        registry.addSearchPath(tmp.path(), LiveReload::Off);

        const AnimationShaderEffect e = registry.effect(QStringLiteral("slide"));
        QCOMPARE(e.fragmentShaderPath, effectDir + QStringLiteral("/slide.frag"));
    }

    /// `availableEffects()` and `effectIds()` return entries sorted by
    /// id. QHash iteration order is intentionally randomised in Qt6, so
    /// without an explicit sort downstream consumers (settings UI, QML
    /// pickers, snapshot tests) would see different ordering on every
    /// process launch. The sister `PhosphorShell::ShaderRegistry`
    /// applies the same sort for the same reason.
    void testAvailableEffects_isSortedById()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        // Write effects whose dir-iteration order (alphabetic by
        // subdir name) differs from the desired id-sort order. Effect
        // ids alphabetise as "alpha", "kappa", "zulu"; subdir names
        // sort as "one-zulu", "three-alpha", "two-kappa" — matching by
        // id catches a hypothetical regression where the registry
        // accidentally returned dir-iteration order instead.
        writeMetadata(tmp.path() + QStringLiteral("/one-zulu"), QStringLiteral("zulu"));
        writeMetadata(tmp.path() + QStringLiteral("/two-kappa"), QStringLiteral("kappa"));
        writeMetadata(tmp.path() + QStringLiteral("/three-alpha"), QStringLiteral("alpha"));

        AnimationShaderRegistry registry;
        registry.addSearchPath(tmp.path(), LiveReload::Off);

        const QList<AnimationShaderEffect> effects = registry.availableEffects();
        QCOMPARE(effects.size(), 3);
        QCOMPARE(effects.at(0).id, QStringLiteral("alpha"));
        QCOMPARE(effects.at(1).id, QStringLiteral("kappa"));
        QCOMPARE(effects.at(2).id, QStringLiteral("zulu"));

        const QStringList ids = registry.effectIds();
        QCOMPARE(ids.size(), 3);
        QCOMPARE(ids.at(0), QStringLiteral("alpha"));
        QCOMPARE(ids.at(1), QStringLiteral("kappa"));
        QCOMPARE(ids.at(2), QStringLiteral("zulu"));
    }
};

QTEST_MAIN(TestAnimationShaderRegistry)
#include "test_animationshaderregistry.moc"
