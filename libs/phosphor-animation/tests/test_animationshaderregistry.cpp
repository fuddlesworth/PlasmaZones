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

    // ── translateAnimationParams ─────────────────────────────────────
    //
    // Slot allocation contract: parameters fill `customParams<N>_<x|y|z|w>`
    // in metadata declaration order, with N starting at 1 and components
    // x/y/z/w within a vec4. Both runtimes consume the resulting map; a
    // shader.frag reads the corresponding `customParams[N-1].xyz` slot
    // via the `#define` macros each effect.frag declares. A regression
    // here would silently break parameter delivery on both render
    // execution sites.

    void testTranslateAnimationParamsSlotAllocation()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("slide");
        eff.fragmentShaderPath = QStringLiteral("effect.frag");
        // Two float params — should land in customParams1_x, customParams1_y.
        AnimationShaderEffect::ParameterInfo p1;
        p1.id = QStringLiteral("direction");
        p1.type = QStringLiteral("int");
        p1.defaultValue = 0;
        AnimationShaderEffect::ParameterInfo p2;
        p2.id = QStringLiteral("parallax");
        p2.type = QStringLiteral("float");
        p2.defaultValue = 0.0;
        eff.parameters = {p1, p2};

        const QVariantMap friendly{{QStringLiteral("direction"), 1}, {QStringLiteral("parallax"), 0.2}};
        const QVariantMap translated = AnimationShaderRegistry::translateAnimationParams(eff, friendly);

        QCOMPARE(translated.size(), 2);
        QCOMPARE(translated.value(QStringLiteral("customParams1_x")).toInt(), 1);
        QCOMPARE(translated.value(QStringLiteral("customParams1_y")).toDouble(), 0.2);
        QVERIFY(!translated.contains(QStringLiteral("direction"))); // friendly keys dropped
    }

    void testTranslateAnimationParamsFallsBackToDeclaredDefaults()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("dissolve");
        eff.fragmentShaderPath = QStringLiteral("effect.frag");
        AnimationShaderEffect::ParameterInfo grain;
        grain.id = QStringLiteral("grain");
        grain.type = QStringLiteral("float");
        grain.defaultValue = 0.05;
        AnimationShaderEffect::ParameterInfo softness;
        softness.id = QStringLiteral("softness");
        softness.type = QStringLiteral("float");
        softness.defaultValue = 0.1;
        eff.parameters = {grain, softness};

        // Friendly map empty → both slots get metadata defaults.
        const QVariantMap translated = AnimationShaderRegistry::translateAnimationParams(eff, QVariantMap());
        QCOMPARE(translated.value(QStringLiteral("customParams1_x")).toDouble(), 0.05);
        QCOMPARE(translated.value(QStringLiteral("customParams1_y")).toDouble(), 0.1);
    }

    void testTranslateAnimationParamsBoolCoerced()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("test-bool");
        eff.fragmentShaderPath = QStringLiteral("effect.frag");
        AnimationShaderEffect::ParameterInfo p;
        p.id = QStringLiteral("flag");
        p.type = QStringLiteral("bool");
        p.defaultValue = false;
        eff.parameters = {p};

        const QVariantMap translatedTrue =
            AnimationShaderRegistry::translateAnimationParams(eff, {{QStringLiteral("flag"), true}});
        QCOMPARE(translatedTrue.value(QStringLiteral("customParams1_x")).toFloat(), 1.0f);

        const QVariantMap translatedFalse =
            AnimationShaderRegistry::translateAnimationParams(eff, {{QStringLiteral("flag"), false}});
        QCOMPARE(translatedFalse.value(QStringLiteral("customParams1_x")).toFloat(), 0.0f);
    }

    void testTranslateAnimationParamsCrossesVec4Boundary()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("test-five");
        eff.fragmentShaderPath = QStringLiteral("effect.frag");
        // Five float params: 0..3 fill customParams1_x..customParams1_w,
        // the fifth lands in customParams2_x.
        for (int i = 0; i < 5; ++i) {
            AnimationShaderEffect::ParameterInfo p;
            p.id = QStringLiteral("p%1").arg(i);
            p.type = QStringLiteral("float");
            p.defaultValue = double(i);
            eff.parameters.append(p);
        }
        const QVariantMap translated = AnimationShaderRegistry::translateAnimationParams(eff, QVariantMap());
        QCOMPARE(translated.value(QStringLiteral("customParams1_x")).toDouble(), 0.0);
        QCOMPARE(translated.value(QStringLiteral("customParams1_y")).toDouble(), 1.0);
        QCOMPARE(translated.value(QStringLiteral("customParams1_z")).toDouble(), 2.0);
        QCOMPARE(translated.value(QStringLiteral("customParams1_w")).toDouble(), 3.0);
        QCOMPARE(translated.value(QStringLiteral("customParams2_x")).toDouble(), 4.0);
    }

    void testTranslateAnimationParamsEmptyForInvalidEffect()
    {
        AnimationShaderEffect invalid; // no id, no frag — isValid() == false
        const QVariantMap translated =
            AnimationShaderRegistry::translateAnimationParams(invalid, {{QStringLiteral("foo"), 1.0}});
        QVERIFY(translated.isEmpty());
    }

    // ── slotKey ──────────────────────────────────────────────────────
    //
    // Both the registry encoder and the two decoders (kwin-effect's
    // per-transition pack, ShaderEffect::setShaderParams in phosphor-rendering)
    // build customParams keys via this single helper. Pin the format —
    // any drift here would fragment the contract across three call sites.

    void testSlotKey()
    {
        using PhosphorAnimationShaders::AnimationShaderContract::slotKey;
        QCOMPARE(slotKey(0, 'x'), QStringLiteral("customParams1_x"));
        QCOMPARE(slotKey(0, 'y'), QStringLiteral("customParams1_y"));
        QCOMPARE(slotKey(0, 'z'), QStringLiteral("customParams1_z"));
        QCOMPARE(slotKey(0, 'w'), QStringLiteral("customParams1_w"));
        QCOMPARE(slotKey(1, 'x'), QStringLiteral("customParams2_x"));
        QCOMPARE(slotKey(7, 'w'), QStringLiteral("customParams8_w"));
    }

    /// Flat-slot overload covers the in-range path the registry encoder
    /// actually uses (translateAnimationParams calls `slotKey(floatSlot)`),
    /// plus the boundary policy: out-of-range slots return an empty
    /// QString rather than wrapping around to a valid in-range key.
    /// Wrap-around would silently collide with another slot and corrupt
    /// the decoder's UBO upload — pin the empty-on-OOB contract here.
    void testSlotKeyFlatIndex()
    {
        using PhosphorAnimationShaders::AnimationShaderContract::slotKey;
        // In-range — same outputs as the (vec, comp) overload.
        QCOMPARE(slotKey(0), QStringLiteral("customParams1_x"));
        QCOMPARE(slotKey(3), QStringLiteral("customParams1_w"));
        QCOMPARE(slotKey(4), QStringLiteral("customParams2_x"));
        QCOMPARE(slotKey(31), QStringLiteral("customParams8_w"));
        // Out-of-range — empty, not a wrapped-around collision.
        QVERIFY(slotKey(-1).isEmpty());
        QVERIFY(slotKey(32).isEmpty());
        QVERIFY(slotKey(100).isEmpty());
    }

    // ── colorKey ─────────────────────────────────────────────────────
    //
    // Sibling helper to `CustomParams::slotKey` for the `customColors[N]`
    // region. Three call sites consume keys produced here:
    // `ShaderEffect::setShaderParams`'s color-decoder branch,
    // `AnimationShaderRegistry::translateAnimationParams`'s color-param
    // encoder, and the kwin-effect's per-transition `customColorsValues`
    // pack. Pin the format so a future drift fragments at most this
    // single test, not all three call sites.

    void testColorKey()
    {
        using PhosphorShaders::CustomColors::colorKey;
        // 0-based input → 1-based output, matching the GLSL author
        // convention (`customColor1` is the first slot).
        QCOMPARE(colorKey(0), QStringLiteral("customColor1"));
        QCOMPARE(colorKey(1), QStringLiteral("customColor2"));
        QCOMPARE(colorKey(15), QStringLiteral("customColor16"));
        // Out-of-range — empty, mirroring `slotKey(int)`'s graceful-
        // degradation contract. Wrap-around would silently collide with a
        // valid in-range key.
        QVERIFY(colorKey(-1).isEmpty());
        QVERIFY(colorKey(16).isEmpty());
        QVERIFY(colorKey(100).isEmpty());
    }

    // ── translateAnimationParams: color parameter routing ────────────
    //
    // Color-typed parameters route to `customColor<N>` slots, sibling to
    // the float/int/bool `customParams<N>_<x|y|z|w>` allocator. The two
    // allocators advance independently — a color param does NOT consume
    // a float sub-slot. Pin the encoder so a regression that accidentally
    // collapsed the two allocators back into one (or dropped color
    // routing altogether, as the predecessor revision did) surfaces here
    // rather than as a silent black-default at runtime.
    void testColorParamTranslation()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("test-color");
        eff.fragmentShaderPath = QStringLiteral("/dummy/effect.frag");
        AnimationShaderEffect::ParameterInfo tint;
        tint.id = QStringLiteral("tint");
        tint.type = QStringLiteral("color");
        tint.defaultValue = QColor(Qt::red);
        AnimationShaderEffect::ParameterInfo speed;
        speed.id = QStringLiteral("speed");
        speed.type = QStringLiteral("float");
        speed.defaultValue = 1.5;
        eff.parameters = {tint, speed};

        const QVariantMap result = AnimationShaderRegistry::translateAnimationParams(eff, {});
        QVERIFY(result.contains(QStringLiteral("customColor1")));
        QVERIFY(result.contains(QStringLiteral("customParams1_x")));
        const QColor c = result.value(QStringLiteral("customColor1")).value<QColor>();
        QCOMPARE(c, QColor(Qt::red));
        QCOMPARE(result.value(QStringLiteral("customParams1_x")).toDouble(), 1.5);
    }

    /// Independence of the float and color allocators. A
    /// `[color, float, color, float]` declaration must produce
    /// `customColor1 + customParams1_x + customColor2 + customParams1_y`,
    /// NOT `customColor1 + customParams1_y + customColor2 +
    /// customParams2_x`. The allocators advance separately; a regression
    /// that collapsed them back into a single counter (or that
    /// accidentally bumped floatSlot for color params) would silently
    /// corrupt the float-slot delivery for any pack mixing the two
    /// types.
    void testColorAndFloatAllocatorsAdvanceIndependently()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("test-mixed");
        eff.fragmentShaderPath = QStringLiteral("/dummy/effect.frag");
        const auto makeParam = [](const QString& id, const QString& type, const QVariant& def) {
            AnimationShaderEffect::ParameterInfo p;
            p.id = id;
            p.type = type;
            p.defaultValue = def;
            return p;
        };
        eff.parameters = {
            makeParam(QStringLiteral("c0"), QStringLiteral("color"), QColor(Qt::red)),
            makeParam(QStringLiteral("f0"), QStringLiteral("float"), 0.1),
            makeParam(QStringLiteral("c1"), QStringLiteral("color"), QColor(Qt::green)),
            makeParam(QStringLiteral("f1"), QStringLiteral("float"), 0.2),
        };

        const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(eff, {});
        // Color allocator: c0 → customColor1, c1 → customColor2.
        QCOMPARE(r.value(QStringLiteral("customColor1")).value<QColor>(), QColor(Qt::red));
        QCOMPARE(r.value(QStringLiteral("customColor2")).value<QColor>(), QColor(Qt::green));
        // Float allocator advances independently: f0 → customParams1_x
        // (NOT customParams1_y, which would happen if the color params
        // had bumped floatSlot), f1 → customParams1_y.
        QCOMPARE(r.value(QStringLiteral("customParams1_x")).toDouble(), 0.1);
        QCOMPARE(r.value(QStringLiteral("customParams1_y")).toDouble(), 0.2);
        // Pin the negative side too — neither customParams2_x nor a
        // stray entry from a collapsed allocator should appear.
        QVERIFY(!r.contains(QStringLiteral("customParams2_x")));
    }

    /// String hex codes from a friendlyParams map (e.g. user-edited
    /// config that wrote `"#ff8800"`) coerce to QColor at the registry
    /// boundary. Without this coercion, the kwin-effect's
    /// `value<QColor>()` decoder would receive an invalid QColor and
    /// silently render the slot as black.
    void testColorParamCoercesStringHex()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("test-color-string");
        eff.fragmentShaderPath = QStringLiteral("/dummy/effect.frag");
        AnimationShaderEffect::ParameterInfo tint;
        tint.id = QStringLiteral("tint");
        tint.type = QStringLiteral("color");
        // Note no defaultValue — friendly map is the only source.
        eff.parameters = {tint};

        const QVariantMap result = AnimationShaderRegistry::translateAnimationParams(
            eff, {{QStringLiteral("tint"), QStringLiteral("#ff8800")}});
        QVERIFY(result.contains(QStringLiteral("customColor1")));
        const QColor c = result.value(QStringLiteral("customColor1")).value<QColor>();
        QVERIFY(c.isValid());
        QCOMPARE(c, QColor(0xff, 0x88, 0x00));
    }

    /// Alpha channel survives the QColor → QVariantMap → kwin-effect
    /// QVector4D round-trip. Without an explicit assertion, an encoder
    /// regression that dropped the alpha (e.g. `setRgb()` instead of
    /// `setRgba()`) would silently slip past `testColorParamTranslation`
    /// because `QColor(Qt::red)` and a `setRgb()`-only result both
    /// compare equal under `QColor::operator==` when alpha defaults to
    /// 255.
    void testColorParamPreservesAlpha()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("test-color-alpha");
        eff.fragmentShaderPath = QStringLiteral("/dummy/effect.frag");
        AnimationShaderEffect::ParameterInfo tint;
        tint.id = QStringLiteral("tint");
        tint.type = QStringLiteral("color");
        eff.parameters = {tint};

        // Friendly map carries an explicit non-255 alpha — pin that the
        // encoder threads it through to the customColor1 slot. Wrapped
        // in a scope block so a future maintainer adding a third case
        // can't accidentally have a `result` shadow the prior block's.
        {
            const QColor source(0xff, 0x00, 0x00, 0x80);
            const QVariantMap result =
                AnimationShaderRegistry::translateAnimationParams(eff, {{QStringLiteral("tint"), source}});
            QVERIFY(result.contains(QStringLiteral("customColor1")));
            const QColor c = result.value(QStringLiteral("customColor1")).value<QColor>();
            QVERIFY(c.isValid());
            QCOMPARE(c.alpha(), 0x80);
            QCOMPARE(c, source);
        }

        // Qt's QColor accepts 8-char hex in `#AARRGGBB` form (alpha
        // first), per `QColor::QColor(QString)` — NOT the CSS-style
        // `#RRGGBBAA` convention. Pin that the encoder threads alpha
        // through whichever form Qt's parser actually accepts.
        {
            const QVariantMap fromHex = AnimationShaderRegistry::translateAnimationParams(
                eff, {{QStringLiteral("tint"), QStringLiteral("#80ff0000")}});
            const QColor cHex = fromHex.value(QStringLiteral("customColor1")).value<QColor>();
            QVERIFY(cHex.isValid());
            QCOMPARE(cHex.alpha(), 0x80);
            QCOMPARE(cHex.red(), 0xff);
        }
    }

    /// Coercion-edge contract for the friendlyParams → customColor<N>
    /// path. The translator's coerce lambda accepts QColor, QString
    /// (any QColor-parseable form including SVG names), and falls back
    /// to the declared default — ultimately to `Qt::transparent` if no
    /// path resolves. Pin every edge so a regression in the lambda
    /// surfaces here rather than as a silent black-default at runtime.
    void testColorParamCoercionEdges()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("test-color-edges");
        eff.fragmentShaderPath = QStringLiteral("/dummy/effect.frag");
        AnimationShaderEffect::ParameterInfo tint;
        tint.id = QStringLiteral("tint");
        tint.type = QStringLiteral("color");
        tint.defaultValue = QColor(Qt::blue);
        eff.parameters = {tint};

        // SVG colour name resolves via QColor's string ctor.
        {
            const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(
                eff, {{QStringLiteral("tint"), QStringLiteral("red")}});
            QCOMPARE(r.value(QStringLiteral("customColor1")).value<QColor>(), QColor(Qt::red));
        }
        // Invalid hex string falls back to the declared default (blue).
        {
            const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(
                eff, {{QStringLiteral("tint"), QStringLiteral("#zzzzzz")}});
            QCOMPARE(r.value(QStringLiteral("customColor1")).value<QColor>(), QColor(Qt::blue));
        }
        // Empty string falls back to the declared default (blue).
        {
            const QVariantMap r =
                AnimationShaderRegistry::translateAnimationParams(eff, {{QStringLiteral("tint"), QString()}});
            QCOMPARE(r.value(QStringLiteral("customColor1")).value<QColor>(), QColor(Qt::blue));
        }
        // Wrong-type variant (int) falls back to the declared default
        // (blue) — int → QColor cannot resolve, int → QString gives
        // "42", which is not a valid colour, so the chain bottoms out
        // on the declared default.
        {
            const QVariantMap r =
                AnimationShaderRegistry::translateAnimationParams(eff, {{QStringLiteral("tint"), 42}});
            QCOMPARE(r.value(QStringLiteral("customColor1")).value<QColor>(), QColor(Qt::blue));
        }

        // No defaultValue + no friendlyParams entry → ultimate fallback
        // to Qt::transparent (the chain's documented sentinel).
        AnimationShaderEffect noDefault;
        noDefault.id = QStringLiteral("test-color-no-default");
        noDefault.fragmentShaderPath = QStringLiteral("/dummy/effect.frag");
        AnimationShaderEffect::ParameterInfo bare;
        bare.id = QStringLiteral("tint");
        bare.type = QStringLiteral("color");
        noDefault.parameters = {bare};
        const QVariantMap rNoSrc = AnimationShaderRegistry::translateAnimationParams(noDefault, {});
        QCOMPARE(rNoSrc.value(QStringLiteral("customColor1")).value<QColor>(), QColor(Qt::transparent));
    }

    /// Lower-digit hex forms accepted by `QColor::QColor(QString)` survive
    /// the friendlyParams → customColor coercion. The encoder routes any
    /// QString-shaped value through Qt's parser, so 3-digit, 12-bit, and
    /// 16-bit forms all work alongside the canonical 6/8-digit forms
    /// covered above. A future encoder tightening that forced a strict
    /// 6-digit-only check would silently break user configs containing
    /// any of these — pin every shape Qt actually accepts here. Note
    /// Qt does NOT accept 4-digit `#argb` or `#rgba` forms — only the
    /// shapes asserted below.
    void testColorParamCoercesShortHex()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("test-color-short-hex");
        eff.fragmentShaderPath = QStringLiteral("/dummy/effect.frag");
        AnimationShaderEffect::ParameterInfo tint;
        tint.id = QStringLiteral("tint");
        tint.type = QStringLiteral("color");
        eff.parameters = {tint};

        // 3-digit `#rgb` expands to `#rrggbb` per Qt's parser.
        {
            const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(
                eff, {{QStringLiteral("tint"), QStringLiteral("#f80")}});
            const QColor c = r.value(QStringLiteral("customColor1")).value<QColor>();
            QVERIFY(c.isValid());
            QCOMPARE(c, QColor(0xff, 0x88, 0x00));
        }
        // 12-bit `#rrrgggbbb` accepted by Qt — extra precision is
        // truncated to 8 bits per channel. Just assert validity here;
        // Qt's exact truncation rule isn't part of this contract.
        {
            const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(
                eff, {{QStringLiteral("tint"), QStringLiteral("#fff888000")}});
            const QColor c = r.value(QStringLiteral("customColor1")).value<QColor>();
            QVERIFY(c.isValid());
        }
        // 16-bit `#rrrrggggbbbb` accepted by Qt.
        {
            const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(
                eff, {{QStringLiteral("tint"), QStringLiteral("#ffff88880000")}});
            const QColor c = r.value(QStringLiteral("customColor1")).value<QColor>();
            QVERIFY(c.isValid());
        }
        // The `"transparent"` keyword resolves to (0,0,0,0).
        {
            const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(
                eff, {{QStringLiteral("tint"), QStringLiteral("transparent")}});
            const QColor c = r.value(QStringLiteral("customColor1")).value<QColor>();
            QVERIFY(c.isValid());
            QCOMPARE(c, QColor(Qt::transparent));
        }
        // 4-digit forms are NOT supported — pin negative coverage so a
        // future regression that "added" 4-digit support without going
        // through the registry's strict default fallback surfaces here.
        // (Without a declared default, an unparseable string falls
        // through to Qt::transparent — the documented sentinel.)
        AnimationShaderEffect noDefault;
        noDefault.id = QStringLiteral("test-color-4digit-rejected");
        noDefault.fragmentShaderPath = QStringLiteral("/dummy/effect.frag");
        AnimationShaderEffect::ParameterInfo bare;
        bare.id = QStringLiteral("tint");
        bare.type = QStringLiteral("color");
        noDefault.parameters = {bare};
        const QVariantMap rReject = AnimationShaderRegistry::translateAnimationParams(
            noDefault, {{QStringLiteral("tint"), QStringLiteral("#8f80")}});
        QCOMPARE(rReject.value(QStringLiteral("customColor1")).value<QColor>(), QColor(Qt::transparent));
    }

    /// Parameter declarations beyond the customParams flat-slot budget
    /// (`kMaxParameterSlots` = 32) are silently dropped at the
    /// `translateAnimationParams` boundary with a `qCWarning`. Without
    /// this test the encoder could regress to wrap-around indexing into
    /// slot 0 (corrupting the first parameter) or to writing a phantom
    /// `customParams9_x` key that consumers never look at. Pin the
    /// budget cap by declaring 33 float params and asserting only 32
    /// land in the result.
    void testTranslateAnimationParamsFloatBudgetOverflow()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("test-overflow-float");
        eff.fragmentShaderPath = QStringLiteral("/dummy/effect.frag");
        constexpr int kBudget = PhosphorAnimationShaders::AnimationShaderContract::kMaxParameterSlots;
        for (int i = 0; i < kBudget + 1; ++i) {
            AnimationShaderEffect::ParameterInfo p;
            p.id = QStringLiteral("p%1").arg(i);
            p.type = QStringLiteral("float");
            p.defaultValue = double(i);
            eff.parameters.append(p);
        }

        // Expect ONE budget-overflow qCWarning for the 33rd parameter.
        // Using QTest::ignoreMessage so it's scoped to this test only —
        // unlike QLoggingCategory::setFilterRules which mutates global
        // state and would persist if a downstream QVERIFY failed before
        // the restore call. Wrap the literal substrings in
        // QRegularExpression::escape for consistency with the
        // path-traversal-guard tests below — only the `.*` between
        // the escaped halves is treated as a regex metasequence.
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QRegularExpression::escape(QStringLiteral("translateAnimationParams"))
                                                + QStringLiteral(".*")
                                                + QRegularExpression::escape(QStringLiteral("budget"))));
        const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(eff, {});

        // Last in-budget slot (`customParams8_w` per the 1-based +
        // x/y/z/w encoder) populated.
        const QString lastSlot = PhosphorAnimationShaders::AnimationShaderContract::slotKey(kBudget - 1);
        QVERIFY(r.contains(lastSlot));
        // The 33rd param must NOT have produced any key — neither a
        // wrap-around nor a phantom customParams9_x.
        QVERIFY(!r.contains(QStringLiteral("customParams9_x")));
        // Total entries should not exceed the budget.
        QVERIFY(r.size() <= kBudget);
    }

    /// Color declarations beyond the customColors slot budget
    /// (`kMaxCustomColors` = 16) are silently dropped with a `qCWarning`.
    /// Symmetric coverage to the float-budget test above.
    void testTranslateAnimationParamsColorBudgetOverflow()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("test-overflow-color");
        eff.fragmentShaderPath = QStringLiteral("/dummy/effect.frag");
        constexpr int kBudget = PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors;
        for (int i = 0; i < kBudget + 1; ++i) {
            AnimationShaderEffect::ParameterInfo p;
            p.id = QStringLiteral("c%1").arg(i);
            p.type = QStringLiteral("color");
            p.defaultValue = QColor(Qt::red);
            eff.parameters.append(p);
        }

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QRegularExpression::escape(QStringLiteral("translateAnimationParams"))
                                                + QStringLiteral(".*")
                                                + QRegularExpression::escape(QStringLiteral("budget"))));
        const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(eff, {});

        // Last in-budget slot is `customColor16` (1-based).
        QVERIFY(r.contains(QStringLiteral("customColor%1").arg(kBudget)));
        // The 17th color must NOT have produced any key — neither a
        // wrap-around nor a phantom customColor17.
        QVERIFY(!r.contains(QStringLiteral("customColor%1").arg(kBudget + 1)));
        QVERIFY(r.size() <= kBudget);
    }

    // ── User-texture pipeline (kMaxUserTextureSlots = 3) ──────────────
    //
    // Pin the texture-slot surface introduced by the user-texture
    // unification: schema cap, JSON round-trip, equality discrimination,
    // translateAnimationParams emit (pack defaults + runtime overrides),
    // and the texture-only effect case (no float/color params).
    //
    // The earlier audit found this surface had ZERO test coverage. Each
    // case below exists because a regression there has caused observable
    // user-visible breakage at least once during PR #399 development.

    void testTextureSlotCapDropsSurplus()
    {
        // metadata.json declares 4 textures; AnimationShaderContract caps
        // at 3 (one per user-facing iChannel/uTexture slot). Surplus
        // entries are silently dropped at fromJson time.
        QJsonArray texArr;
        for (int i = 0; i < 5; ++i) {
            QJsonObject t;
            t.insert(QLatin1String("path"), QStringLiteral("tex%1.png").arg(i));
            t.insert(QLatin1String("wrap"), QStringLiteral("clamp"));
            texArr.append(t);
        }
        QJsonObject obj;
        obj.insert(QLatin1String("id"), QStringLiteral("over-cap"));
        obj.insert(QLatin1String("fragmentShader"), QStringLiteral("e.frag"));
        obj.insert(QLatin1String("textures"), texArr);

        const auto eff = AnimationShaderEffect::fromJson(obj);
        QCOMPARE(eff.textures.size(), PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots);
        // Surviving entries must be the FIRST kMax entries — preserving
        // declaration order so the effect's slot↔file mapping is stable.
        QCOMPARE(eff.textures[0].path, QStringLiteral("tex0.png"));
        QCOMPARE(eff.textures[2].path, QStringLiteral("tex2.png"));
    }

    void testTextureSlotEmptyPathDropped()
    {
        // An author writing `{"path": "", "wrap": "repeat"}` produces a
        // slot with no bound texture. fromJson drops it rather than
        // emitting a wrap-only override that would attach a wrap mode
        // to an unbound sampler.
        QJsonArray texArr;
        QJsonObject empty;
        empty.insert(QLatin1String("path"), QString());
        empty.insert(QLatin1String("wrap"), QStringLiteral("repeat"));
        QJsonObject good;
        good.insert(QLatin1String("path"), QStringLiteral("real.png"));
        texArr.append(empty);
        texArr.append(good);

        QJsonObject obj;
        obj.insert(QLatin1String("id"), QStringLiteral("empty-path"));
        obj.insert(QLatin1String("fragmentShader"), QStringLiteral("e.frag"));
        obj.insert(QLatin1String("textures"), texArr);

        const auto eff = AnimationShaderEffect::fromJson(obj);
        QCOMPARE(eff.textures.size(), 1);
        QCOMPARE(eff.textures[0].path, QStringLiteral("real.png"));
    }

    void testTextureSlotJsonRoundTrip()
    {
        AnimationShaderEffect e;
        e.id = QStringLiteral("rt");
        e.fragmentShaderPath = QStringLiteral("e.frag");
        AnimationShaderEffect::TextureSlot t1{QStringLiteral("a.png"), QStringLiteral("clamp")};
        AnimationShaderEffect::TextureSlot t2{QStringLiteral("b.svg"), QStringLiteral("repeat")};
        e.textures.append(t1);
        e.textures.append(t2);

        const auto restored = AnimationShaderEffect::fromJson(e.toJson());
        QCOMPARE(restored.textures.size(), 2);
        QCOMPARE(restored.textures[0].path, QStringLiteral("a.png"));
        QCOMPARE(restored.textures[0].wrap, QStringLiteral("clamp"));
        QCOMPARE(restored.textures[1].path, QStringLiteral("b.svg"));
        QCOMPARE(restored.textures[1].wrap, QStringLiteral("repeat"));
    }

    void testTextureSlotEqualityDifferentiates()
    {
        AnimationShaderEffect a;
        a.id = QStringLiteral("eq");
        a.fragmentShaderPath = QStringLiteral("e.frag");
        a.textures.append({QStringLiteral("noise.png"), QStringLiteral("repeat")});

        AnimationShaderEffect b = a;
        QVERIFY(a == b);

        // Path differs.
        b.textures[0].path = QStringLiteral("other.png");
        QVERIFY(a != b);
        b = a;

        // Wrap differs.
        b.textures[0].wrap = QStringLiteral("clamp");
        QVERIFY(a != b);
        b = a;

        // Slot count differs.
        b.textures.append({QStringLiteral("more.png"), QStringLiteral("clamp")});
        QVERIFY(a != b);
    }

    void testTranslateAnimationParamsEmitsPackTextureDefaults()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("texdefault");
        eff.fragmentShaderPath = QStringLiteral("/abs/e.frag");
        eff.sourceDir = QStringLiteral("/abs");
        eff.textures.append({QStringLiteral("/abs/tex.png"), QStringLiteral("repeat")});

        const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(eff, {});
        // Slot 0 → uTexture1 (uTexture0 reserved for surface).
        QCOMPARE(r.value(QStringLiteral("uTexture1")).toString(), QStringLiteral("/abs/tex.png"));
        QCOMPARE(r.value(QStringLiteral("uTexture1_wrap")).toString(), QStringLiteral("repeat"));
    }

    void testTranslateAnimationParamsRuntimeOverrideWinsOverPackDefault()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("texoverride");
        eff.fragmentShaderPath = QStringLiteral("/abs/e.frag");
        eff.sourceDir = QStringLiteral("/abs");
        eff.textures.append({QStringLiteral("/abs/pack.png"), QStringLiteral("clamp")});

        const QVariantMap friendly{
            {QStringLiteral("uTexture1"), QStringLiteral("/user/override.png")},
            {QStringLiteral("uTexture1_wrap"), QStringLiteral("mirror")},
        };
        const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(eff, friendly);
        QCOMPARE(r.value(QStringLiteral("uTexture1")).toString(), QStringLiteral("/user/override.png"));
        QCOMPARE(r.value(QStringLiteral("uTexture1_wrap")).toString(), QStringLiteral("mirror"));
    }

    void testTranslateAnimationParamsTextureOnlyEffect()
    {
        // A texture-only effect (no parameters declared) used to early-
        // return from translateAnimationParams before the texture loop
        // ran. Pin that the relaxation correctly emits texture keys
        // even when `effect.parameters.isEmpty()`.
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("texonly");
        eff.fragmentShaderPath = QStringLiteral("/abs/e.frag");
        eff.sourceDir = QStringLiteral("/abs");
        eff.textures.append({QStringLiteral("/abs/atlas.png"), QString()});

        const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(eff, {});
        QVERIFY(r.contains(QStringLiteral("uTexture1")));
        QCOMPARE(r.value(QStringLiteral("uTexture1")).toString(), QStringLiteral("/abs/atlas.png"));
        // Empty wrap on the slot — translateAnimationParams omits the
        // `_wrap` key entirely so a consumer's "missing key = no
        // change" semantic preserves whatever default the runtime uses
        // (clamp on both runtimes).
        QVERIFY(!r.contains(QStringLiteral("uTexture1_wrap")));
    }

    void testTranslateAnimationParamsSkipsEmptyPathSlots()
    {
        // No pack default, no override — the slot key must NOT appear in
        // the output map. Otherwise downstream consumers would see an
        // empty `uTexture1` and wipe a previously-bound texture.
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("noslot");
        eff.fragmentShaderPath = QStringLiteral("e.frag");
        // No textures at all.

        const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(eff, {});
        QVERIFY(!r.contains(QStringLiteral("uTexture1")));
        QVERIFY(!r.contains(QStringLiteral("uTexture2")));
        QVERIFY(!r.contains(QStringLiteral("uTexture3")));
    }

    /// Empty-string `uTextureN` override is treated as an explicit
    /// clear: the slot is dropped from the result map entirely. The
    /// pack-default `wrap` value MUST NOT survive into the result —
    /// emitting a wrap-only key would attach a wrap mode to an
    /// unbound sampler, violating the downstream contract. Pin the
    /// exact behaviour: neither `uTexture1` nor `uTexture1_wrap`
    /// appears in the output even though the pack supplied both.
    void testTranslateAnimationParamsEmptyOverrideClearsPackWrap()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("texclear");
        eff.fragmentShaderPath = QStringLiteral("/abs/e.frag");
        eff.sourceDir = QStringLiteral("/abs");
        // Pack default supplies BOTH path and wrap.
        eff.textures.append({QStringLiteral("/abs/pack.png"), QStringLiteral("repeat")});

        const QVariantMap friendly{
            {QStringLiteral("uTexture1"), QString()}, // explicit empty-string clear
        };
        const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(eff, friendly);

        // (a) explicit empty-string override clears the texture slot.
        QVERIFY(!r.contains(QStringLiteral("uTexture1")));
        // (b) pack's wrap value does not appear in the result `wrap` slot.
        QVERIFY(!r.contains(QStringLiteral("uTexture1_wrap")));
    }

    /// Dual-key edge case for the empty-string override + companion
    /// wrap-override interaction. A caller that supplies BOTH
    /// `uTexture1 = ""` (explicit clear) AND `uTexture1_wrap = "repeat"`
    /// in the same friendlyParams map must end up with NEITHER key in
    /// the result: the empty path triggers the "skip both keys" branch
    /// at the end of the texture loop, which dominates the wrap
    /// override's reassignment. Pin this so a future edit that adds an
    /// early `wrap.clear()` in the empty-path branch (which would be
    /// silently overwritten by the wrap override below it) doesn't
    /// accidentally regress the orphan-wrap-emit guard.
    void testTranslateAnimationParamsEmptyOverridePlusWrapOverrideDropsBoth()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("texdualclear");
        eff.fragmentShaderPath = QStringLiteral("/abs/e.frag");
        eff.sourceDir = QStringLiteral("/abs");
        eff.textures.append({QStringLiteral("/abs/pack.png"), QStringLiteral("clamp")});

        const QVariantMap friendly{
            {QStringLiteral("uTexture1"), QString()}, // explicit clear
            {QStringLiteral("uTexture1_wrap"), QStringLiteral("repeat")}, // companion wrap override
        };
        const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(eff, friendly);

        // BOTH keys absent — the empty-path skip below the wrap-
        // override dominates regardless of QVariantMap iteration order.
        QVERIFY(!r.contains(QStringLiteral("uTexture1")));
        QVERIFY(!r.contains(QStringLiteral("uTexture1_wrap")));
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

    void testTranslateAnimationParamsWrapOnlyOverrideRequiresPath()
    {
        // friendlyParams contains `uTexture1_wrap` but neither the pack
        // nor friendlyParams supplies a path → wrap key is silently
        // skipped (orphan wrap on an unbound sampler is a contract
        // violation downstream).
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("orphanwrap");
        eff.fragmentShaderPath = QStringLiteral("e.frag");

        const QVariantMap friendly{
            {QStringLiteral("uTexture1_wrap"), QStringLiteral("repeat")},
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
