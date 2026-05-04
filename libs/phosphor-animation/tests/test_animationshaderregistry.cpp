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

    // ── rewriteCanonicalUboToDefaultBlock ────────────────────────────
    //
    // The kwin-effect's classic-GL path can't bind UBOs, so it rewrites
    // the canonical animation UBO into default-block uniforms before
    // compile. Pin the rewriter's behaviour against the canonical layout
    // plus the deliberate drops (qt_Matrix, qt_Opacity, _appField0/1).
    // Used to live in an anonymous namespace inside the kwin-effect plugin
    // where it was untestable; lifting it here makes it pinnable.

    void testRewriteCanonicalUboHappyPath()
    {
        const QString src = QStringLiteral(
            "#version 450\n"
            "layout(std140, binding = 0) uniform AnimationUniforms {\n"
            "    mat4 qt_Matrix;\n"
            "    float qt_Opacity;\n"
            "    float iTime;\n"
            "    vec2 iResolution;\n"
            "    int _appField0;\n"
            "    int _appField1;\n"
            "    vec4 customParams[8];\n"
            "};\n"
            "void main() {}\n");
        const QByteArray rewritten = AnimationShaderRegistry::rewriteCanonicalUboToDefaultBlock(src);
        const QString out = QString::fromUtf8(rewritten);
        // Header preserved.
        QVERIFY(out.contains(QStringLiteral("#version 450")));
        // Layout block opening / closing dropped.
        QVERIFY(!out.contains(QStringLiteral("layout(std140")));
        QVERIFY(!out.contains(QStringLiteral("};")));
        // Contract fields present as default-block uniforms.
        QVERIFY(out.contains(QStringLiteral("uniform float iTime;")));
        QVERIFY(out.contains(QStringLiteral("uniform vec2 iResolution;")));
        QVERIFY(out.contains(QStringLiteral("uniform vec4 customParams[8];")));
        // Drop list applied.
        QVERIFY(!out.contains(QStringLiteral("qt_Matrix")));
        QVERIFY(!out.contains(QStringLiteral("qt_Opacity")));
        QVERIFY(!out.contains(QStringLiteral("_appField0")));
        QVERIFY(!out.contains(QStringLiteral("_appField1")));
        // Body preserved.
        QVERIFY(out.contains(QStringLiteral("void main()")));
    }

    void testRewriteCanonicalUboToleratesTrailingCommentsInBlock()
    {
        const QString src = QStringLiteral(
            "layout(std140, binding = 0) uniform AnimationUniforms {\n"
            "    float iTime;     // animation progress\n"
            "    // pure-comment line\n"
            "    vec2 iResolution; // surface size\n"
            "};\n");
        const QString out = QString::fromUtf8(AnimationShaderRegistry::rewriteCanonicalUboToDefaultBlock(src));
        QVERIFY(out.contains(QStringLiteral("uniform float iTime;")));
        QVERIFY(out.contains(QStringLiteral("uniform vec2 iResolution;")));
        // Pure-comment lines inside the block are dropped (they have no
        // declaration to translate).
        QVERIFY(!out.contains(QStringLiteral("pure-comment")));
    }

    void testRewriteCanonicalUboToleratesTrailingCommentOnCloseBrace()
    {
        // `}; // close UBO` would break a naive `trimmed == "};"` check.
        // The rewriter strips line comments before that check.
        const QString src = QStringLiteral(
            "layout(std140, binding = 0) uniform AnimationUniforms {\n"
            "    float iTime;\n"
            "}; // close UBO\n"
            "void main() {}\n");
        const QString out = QString::fromUtf8(AnimationShaderRegistry::rewriteCanonicalUboToDefaultBlock(src));
        QVERIFY(out.contains(QStringLiteral("uniform float iTime;")));
        QVERIFY(!out.contains(QStringLiteral("};"))); // close-brace dropped despite the comment
        QVERIFY(out.contains(QStringLiteral("void main()")));
    }

    void testRewriteCanonicalUboIdempotentOnSourceWithoutBlock()
    {
        // A shader without the canonical block (e.g. an author wrote
        // their own classic-GL `uniform float iTime;` directly) passes
        // through unchanged.
        const QString src = QStringLiteral(
            "#version 450\n"
            "uniform float iTime;\n"
            "void main() {}\n");
        const QByteArray rewritten = AnimationShaderRegistry::rewriteCanonicalUboToDefaultBlock(src);
        QCOMPARE(QString::fromUtf8(rewritten).trimmed(), src.trimmed());
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
        // encoder threads it through to the customColor1 slot.
        const QColor source(0xff, 0x00, 0x00, 0x80);
        const QVariantMap result =
            AnimationShaderRegistry::translateAnimationParams(eff, {{QStringLiteral("tint"), source}});
        QVERIFY(result.contains(QStringLiteral("customColor1")));
        const QColor c = result.value(QStringLiteral("customColor1")).value<QColor>();
        QVERIFY(c.isValid());
        QCOMPARE(c.alpha(), 0x80);
        QCOMPARE(c, source);

        // Qt's QColor accepts 8-char hex in `#AARRGGBB` form (alpha
        // first), per `QColor::QColor(QString)` — NOT the CSS-style
        // `#RRGGBBAA` convention. Pin that the encoder threads alpha
        // through whichever form Qt's parser actually accepts.
        const QVariantMap fromHex = AnimationShaderRegistry::translateAnimationParams(
            eff, {{QStringLiteral("tint"), QStringLiteral("#80ff0000")}});
        const QColor cHex = fromHex.value(QStringLiteral("customColor1")).value<QColor>();
        QVERIFY(cHex.isValid());
        QCOMPARE(cHex.alpha(), 0x80);
        QCOMPARE(cHex.red(), 0xff);
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
};

QTEST_MAIN(TestAnimationShaderRegistry)
#include "test_animationshaderregistry.moc"
