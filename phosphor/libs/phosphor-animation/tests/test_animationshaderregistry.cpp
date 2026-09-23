// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/AnimationShaderContract.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorShaders/CustomParamsKey.h>

#include <PhosphorFsLoader/WatchedDirectorySet.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using PhosphorAnimationShaders::AnimationShaderEffect;
using PhosphorAnimationShaders::AnimationShaderRegistry;
using PhosphorFsLoader::LiveReload;

namespace {

/// Build a minimal metadata.json + effect.frag pair on disk for the
/// registry's scanner to discover. The optional @p extra QJsonObject
/// is merged into the metadata after the base fields, letting tests
/// exercise multipass / wallpaper / depth / parameters declarations
/// without duplicating the file-write boilerplate.
void writeMetadata(const QString& dir, const QString& id, const QString& fragShader = QStringLiteral("effect.frag"),
                   const QJsonObject& extra = {})
{
    QDir().mkpath(dir);
    QJsonObject obj;
    obj.insert(QLatin1String("id"), id);
    obj.insert(QLatin1String("name"), id);
    obj.insert(QLatin1String("fragmentShader"), fragShader);
    obj.insert(QLatin1String("category"), QStringLiteral("Test"));
    // Baseline fields required by animation-metadata.schema.json. Tests that
    // need parameters pass them via `extra` (insert below overrides the empty
    // default). Keeps every fixture a schema-valid pack now that the registry
    // validates metadata.json on load.
    obj.insert(QLatin1String("description"), QStringLiteral("Test effect"));
    obj.insert(QLatin1String("author"), QStringLiteral("Test"));
    obj.insert(QLatin1String("version"), QStringLiteral("1.0"));
    obj.insert(QLatin1String("parameters"), QJsonArray());
    for (auto it = extra.begin(); it != extra.end(); ++it) {
        obj.insert(it.key(), it.value());
    }

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

    /// Multipass / wallpaper / depth / buffer fields parsed and exposed
    /// through the registry's scanner end-to-end. Catches regressions
    /// in `parseEffect` that drop the new metadata fields when reading
    /// from disk (the unit-level `testJsonPreservesMultipassFields`
    /// covers the toJson↔fromJson round-trip; this one covers the
    /// disk-scan path through the scan-strategy machinery).
    void testDiscoversMultipassEffect()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QJsonObject extra;
        extra.insert(QLatin1String("multipass"), true);
        extra.insert(QLatin1String("bufferShaders"), QJsonArray{QStringLiteral("buffer-a.frag")});
        extra.insert(QLatin1String("bufferWraps"), QJsonArray{QStringLiteral("repeat")});
        extra.insert(QLatin1String("wallpaper"), true);
        extra.insert(QLatin1String("depthBuffer"), true);
        extra.insert(QLatin1String("bufferScale"), 0.5);
        writeMetadata(tmp.path() + QStringLiteral("/multipass"), QStringLiteral("multipass"),
                      QStringLiteral("effect.frag"), extra);
        // The buffer shader file must exist on disk too — registry's
        // parseEffect drops `bufferShaderPaths` entries that don't
        // resolve to a readable file, so a missing buffer shader would
        // make `isMultipass` collapse back to false.
        QFile bufFrag(tmp.path() + QStringLiteral("/multipass/buffer-a.frag"));
        QVERIFY(bufFrag.open(QIODevice::WriteOnly));
        bufFrag.write("void main() {}");
        bufFrag.close();

        AnimationShaderRegistry registry;
        registry.addSearchPath(tmp.path(), LiveReload::Off);

        const AnimationShaderEffect eff = registry.effect(QStringLiteral("multipass"));
        QVERIFY(eff.isValid());
        QCOMPARE(eff.isMultipass, true);
        QCOMPARE(eff.bufferShaderPaths.size(), 1);
        // A VALID multipass pack keeps its per-buffer overrides — the
        // single-pass coherence clear is gated on !isMultipass.
        QCOMPARE(eff.bufferWraps, QStringList{QStringLiteral("repeat")});
        QCOMPARE(eff.useWallpaper, true);
        QCOMPARE(eff.useDepthBuffer, true);
        QCOMPARE(eff.bufferScale, qreal(0.5));
    }

    void testSinglePassClearsOrphanBufferOverrides()
    {
        // A pack that never declares multipass but ships per-buffer
        // wrap/filter overrides must not carry them through parseEffect:
        // they claim positional alignment with an empty bufferShaderPaths,
        // survive toJson, and participate in operator==. Pins the
        // single-pass coherence clear (mirrors the surface registry test).
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QJsonObject extra;
        extra.insert(QLatin1String("bufferWraps"), QJsonArray{QStringLiteral("clamp")});
        extra.insert(QLatin1String("bufferFilters"), QJsonArray{QStringLiteral("nearest")});
        writeMetadata(tmp.path() + QStringLiteral("/orphan"), QStringLiteral("orphan"), QStringLiteral("effect.frag"),
                      extra);

        AnimationShaderRegistry registry;
        registry.addSearchPath(tmp.path(), LiveReload::Off);

        const AnimationShaderEffect eff = registry.effect(QStringLiteral("orphan"));
        QVERIFY(eff.isValid());
        QVERIFY(!eff.isMultipass);
        QVERIFY(eff.bufferWraps.isEmpty());
        QVERIFY(eff.bufferFilters.isEmpty());
    }

    void testFailClosedMultipassClearsOrphanBufferOverrides()
    {
        // multipass:true with a declared-but-missing buffer file fails closed
        // to single-pass; the orphaned wrap/filter overrides must be cleared
        // in the same normalization.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QJsonObject extra;
        extra.insert(QLatin1String("multipass"), true);
        extra.insert(QLatin1String("bufferShaders"), QJsonArray{QStringLiteral("missing.frag")});
        extra.insert(QLatin1String("bufferWraps"), QJsonArray{QStringLiteral("repeat")});
        writeMetadata(tmp.path() + QStringLiteral("/failclosed"), QStringLiteral("failclosed"),
                      QStringLiteral("effect.frag"), extra);
        // missing.frag intentionally NOT written to disk.

        AnimationShaderRegistry registry;
        registry.addSearchPath(tmp.path(), LiveReload::Off);

        const AnimationShaderEffect eff = registry.effect(QStringLiteral("failclosed"));
        QVERIFY(eff.isValid());
        QVERIFY(!eff.isMultipass);
        QVERIFY(eff.bufferShaderPaths.isEmpty());
        QVERIFY(eff.bufferWraps.isEmpty());
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

    /// `setUserPath` classifies a registered dir as user vs system
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
        // setUserPath BEFORE addSearchPaths — initial scan sees
        // the right classification.
        registry.setUserPath(user.path());
        registry.addSearchPaths({system.path(), user.path()}, LiveReload::Off);

        QVERIFY(!registry.effect(QStringLiteral("dissolve")).isUserEffect);
        QVERIFY(registry.effect(QStringLiteral("morph")).isUserEffect);
    }

    /// Order-independence pinned: `setUserPath` AFTER
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
        registry.setUserPath(user.path());

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
    /// process launch. The sister `PhosphorShaders::ShaderRegistry`
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

    /// In-memory effect (sourceDir empty) with a clean override path —
    /// the `pathHasNoTraversalSegments` branch must accept it and
    /// thread the path through to the result map. Pairs with the
    /// negative-coverage test below; together they pin the in-memory
    /// branch's accept/reject contract.
    void testInMemoryEffectAcceptsCleanOverridePath()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("inmem-clean");
        eff.fragmentShaderPath = QStringLiteral("e.frag");
        // sourceDir intentionally left empty — exercises the in-memory
        // factory branch that has no on-disk anchor.

        const QVariantMap friendly{
            {QStringLiteral("uTexture1"), QStringLiteral("/user/clean.png")},
        };
        const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(eff, friendly);
        QCOMPARE(r.value(QStringLiteral("uTexture1")).toString(), QStringLiteral("/user/clean.png"));
    }

    /// In-memory effect (sourceDir empty) with a `..`-traversal
    /// override path — the sourceDir-independent
    /// `pathHasNoTraversalSegments` guard rejects it, the slot stays
    /// empty in the result, and a "path traversal guard" warning
    /// surfaces in the journal so a malicious or buggy caller is
    /// noticed. Pairs with the positive case above.
    void testInMemoryEffectRejectsTraversalOverridePath()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("inmem-evil");
        eff.fragmentShaderPath = QStringLiteral("e.frag");
        // sourceDir intentionally left empty.

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QRegularExpression::escape(QStringLiteral("path traversal guard"))));

        const QVariantMap friendly{
            {QStringLiteral("uTexture1"), QStringLiteral("../../etc/passwd")},
        };
        const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(eff, friendly);
        QVERIFY(!r.contains(QStringLiteral("uTexture1")));
        QVERIFY(!r.contains(QStringLiteral("uTexture1_wrap")));
    }

    void testParseEffectRejectsTextureTraversalPath()
    {
        // A metadata.json with a `..`-traversal path must be rejected at
        // scan time so a malicious shipped pack can't read arbitrary
        // files via `QFile::exists` / `QImage::load`.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString effDir = tmp.path() + QStringLiteral("/evil");
        QJsonArray texArr;
        QJsonObject t;
        t.insert(QLatin1String("path"), QStringLiteral("../../../etc/passwd"));
        texArr.append(t);
        QJsonObject extra;
        extra.insert(QLatin1String("textures"), texArr);
        writeMetadata(effDir, QStringLiteral("evil"), QStringLiteral("e.frag"), extra);

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QRegularExpression::escape(QStringLiteral("path traversal guard"))));
        AnimationShaderRegistry registry;
        registry.addSearchPaths({tmp.path()}, LiveReload::Off);
        const auto effects = registry.availableEffects();
        QCOMPARE(effects.size(), 1);
        // Pack still loads. The offending texture slot is preserved in
        // the vector (slot-index stability) with BOTH path and wrap
        // cleared — downstream emit skips empty-path slots, and the
        // toJson round-trip drops them. Pin the exact shape: one slot,
        // both fields empty. An OR-clause that also accepted
        // `textures.isEmpty()` would silently pass a regression where
        // parseEffect started dropping the slot entirely, masking a
        // contract change.
        QCOMPARE(effects[0].textures.size(), 1);
        QVERIFY(effects[0].textures[0].path.isEmpty());
        QVERIFY(effects[0].textures[0].wrap.isEmpty());
    }

    void testParseEffectRejectsTextureSymlinkEscape()
    {
        // Symlink defence-in-depth: the lexical-`..` test above also passes
        // under the cleanPath-only fallback, so it doesn't actually exercise
        // the canonicalFilePath path that catches symlink escapes. Pin THAT
        // path: a symlink at `<effDir>/innocent.png` pointing to a file
        // outside the effect dir must be rejected even though the
        // metadata.json string itself looks benign (no `..` segments,
        // a plain filename relative to the effect dir).
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        // Create a file outside the effect dir for the symlink to target.
        const QString outsidePath = tmp.path() + QStringLiteral("/outside.png");
        QFile outside(outsidePath);
        QVERIFY(outside.open(QIODevice::WriteOnly));
        outside.write("\x89PNG\r\n\x1a\n", 8);
        outside.close();

        const QString effDir = tmp.path() + QStringLiteral("/symlink-evil");
        QVERIFY(QDir().mkpath(effDir));
        const QString symlinkPath = effDir + QStringLiteral("/innocent.png");
        // Skip rather than hard-fail on filesystems without symlink support
        // (FAT, restricted CI sandboxes, certain Docker storage drivers).
        // QFile::link returns false on the unsupported case; the
        // canonicalFilePath defence the test pins is filesystem-agnostic
        // logic, so an unverified run on tmpfs/ext4/btrfs/overlayfs
        // elsewhere is the right fallback.
        if (!QFile::link(outsidePath, symlinkPath)) {
            QSKIP("filesystem does not support symlinks — skipping symlink-escape coverage");
        }

        QJsonArray texArr;
        QJsonObject t;
        t.insert(QLatin1String("path"), QStringLiteral("innocent.png"));
        texArr.append(t);
        QJsonObject extra;
        extra.insert(QLatin1String("textures"), texArr);
        writeMetadata(effDir, QStringLiteral("symlink-evil"), QStringLiteral("e.frag"), extra);

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QRegularExpression::escape(QStringLiteral("path traversal guard"))));
        AnimationShaderRegistry registry;
        registry.addSearchPaths({tmp.path()}, LiveReload::Off);
        const auto effects = registry.availableEffects();
        QCOMPARE(effects.size(), 1);
        QCOMPARE(effects[0].textures.size(), 1);
        // Slot preserved with BOTH fields cleared (same shape as the
        // lexical-traversal case — defence in depth applies symmetrically
        // whether the escape was lexical or via symlink resolution).
        QVERIFY(effects[0].textures[0].path.isEmpty());
        QVERIFY(effects[0].textures[0].wrap.isEmpty());
    }

    void testParseEffectResolvesTexturePathRelativeToSourceDir()
    {
        // Symmetric positive case: a benign relative path resolves to
        // the absolute form once at scan time, so translateAnimationParams
        // doesn't pay a per-leg QFileInfo cost.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString effDir = tmp.path() + QStringLiteral("/ok");
        QDir().mkpath(effDir);
        QFile bitmap(effDir + QStringLiteral("/atlas.png"));
        QVERIFY(bitmap.open(QIODevice::WriteOnly));
        bitmap.write("\x89PNG\r\n\x1a\n", 8); // minimal PNG signature so QFile::exists passes
        bitmap.close();

        QJsonArray texArr;
        QJsonObject t;
        t.insert(QLatin1String("path"), QStringLiteral("atlas.png"));
        texArr.append(t);
        QJsonObject extra;
        extra.insert(QLatin1String("textures"), texArr);
        writeMetadata(effDir, QStringLiteral("ok"), QStringLiteral("e.frag"), extra);

        AnimationShaderRegistry registry;
        registry.addSearchPaths({tmp.path()}, LiveReload::Off);
        const auto effects = registry.availableEffects();
        QCOMPARE(effects.size(), 1);
        QCOMPARE(effects[0].textures.size(), 1);
        // Path is absolutised to the effect's sourceDir. Compare via
        // canonicalFilePath on BOTH sides so a temp-dir path that
        // traverses a symlink (macOS `/tmp` → `/private/tmp`, container
        // overlay mounts) doesn't false-fail the test — parseEffect now
        // stores the canonical-resolved form when both root and target
        // canonicalise, and the lexical-only `QDir(effDir).filePath(...)`
        // form would diverge from it on those systems.
        QCOMPARE(QFileInfo(effects[0].textures[0].path).canonicalFilePath(),
                 QFileInfo(QDir(effDir).filePath(QStringLiteral("atlas.png"))).canonicalFilePath());
    }
};

QTEST_MAIN(TestAnimationShaderRegistry)
#include "test_animationshaderregistry.moc"
