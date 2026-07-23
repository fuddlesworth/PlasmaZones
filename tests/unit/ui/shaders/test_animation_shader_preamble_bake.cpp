// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// End-to-end proof for T1.1 on the animation side: for every daemon-eligible
// built-in animation shader, generate its `#define p_<id> ...` preamble from
// metadata.json (via the production AnimationShaderRegistry::paramPreamble),
// splice it after #version into the include-expanded source, and bake the
// result through ShaderCompiler. This pins that the GENERATED accessors
// actually compile against the real animation UBO (animation_uniforms.glsl) —
// catching any naming, slot, or UBO-mismatch regression before the preamble
// is wired into the live runtimes. Compositor-only packs are excluded (their
// source is kwin classic-GL by design); test_animation_shader_kwin_bake
// covers their preamble compile WHERE a desktop-GL 4.5 context exists — it
// QSKIPs headless, so a GPU-less CI run leaves those packs uncovered.
//
// Also asserts the p_<id> macro for at least one known param resolves to the
// SAME lane the runtime's translateAnimationParams uploads to, so a future
// allocation drift between the two can't pass silently.

#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorRendering/ShaderCompiler.h>
#include <PhosphorShaders/ShaderEntryPoint.h>
#include <PhosphorShaders/ShaderParamPreamble.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using PhosphorAnimationShaders::AnimationShaderEffect;
using PhosphorAnimationShaders::AnimationShaderRegistry;

class TestAnimationShaderPreambleBake : public QObject
{
    Q_OBJECT

    static AnimationShaderEffect loadEffect(const QString& dir)
    {
        QFile f(dir + QStringLiteral("/metadata.json"));
        if (!f.open(QIODevice::ReadOnly)) {
            return {};
        }
        const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
        AnimationShaderEffect eff = AnimationShaderEffect::fromJson(obj);
        eff.sourceDir = dir;
        eff.fragmentShaderPath = dir + QStringLiteral("/effect.frag");
        return eff;
    }

private Q_SLOTS:

    void testEveryAnimationShaderBakesWithPreamble_data()
    {
        QTest::addColumn<QString>("dir");
        const QString animationsDir = QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/animations");
        QDir dir(animationsDir);
        if (!dir.exists()) {
            QSKIP("data/animations not found — running outside source tree");
        }
        bool any = false;
        for (const QString& sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            if (sub == QLatin1String("shared")) {
                continue;
            }
            const QString packDir = animationsDir + QLatin1Char('/') + sub;
            if (QFileInfo::exists(packDir + QStringLiteral("/effect.frag"))
                && QFileInfo::exists(packDir + QStringLiteral("/metadata.json"))) {
                // Compositor-only packs (desktop / geometry / move classes)
                // are authored against the kwin classic-GL dialect with no
                // daemon branch — the strict SPIR-V target rejects their
                // default-block uniforms by design. Their compile coverage
                // is test_animation_shader_kwin_bake.
                if (PhosphorAnimationShaders::shaderEffectIsCompositorOnly(loadEffect(packDir))) {
                    continue;
                }
                QTest::newRow(qPrintable(sub)) << packDir;
                any = true;
            }
        }
        if (!any) {
            QSKIP("no animation shaders found");
        }
    }

    void testEveryAnimationShaderBakesWithPreamble()
    {
        QFETCH(QString, dir);
        const AnimationShaderEffect eff = loadEffect(dir);
        QVERIFY2(eff.isValid(), qPrintable(QStringLiteral("failed to load effect: ") + dir));

        const QString preamble = AnimationShaderRegistry::paramPreamble(eff);

        // Mirror the runtime fragment-load pipeline exactly (loadFragmentShader):
        // read raw → apply the T1.4/T1.5 entry assembly (so an entry-only pack
        // authored as pTransition / pIn+pOut bakes, and a traditional main()
        // pack passes through) → expand includes → splice the param preamble →
        // compile. This bakes EVERY pack the way both runtimes do.
        QFile frag(eff.fragmentShaderPath);
        QVERIFY2(frag.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(eff.fragmentShaderPath));
        const QString raw = QString::fromUtf8(frag.readAll());
        const QString assembled =
            PhosphorShaders::assembleEntryPoint(raw, AnimationShaderRegistry::animationEntryPrologue(),
                                                AnimationShaderRegistry::animationEntryCandidates());

        const QStringList includePaths = {QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/animations/shared")};
        QString err;
        QString src = PhosphorRendering::ShaderCompiler::expandSource(
            assembled, QFileInfo(eff.fragmentShaderPath).absolutePath(), includePaths, &err);
        QVERIFY2(!src.isEmpty(),
                 qPrintable(QStringLiteral("include expand failed: ") + dir + QStringLiteral(" — ") + err));

        src = PhosphorShaders::spliceAfterVersion(src, preamble);

        const auto result = PhosphorRendering::ShaderCompiler::compile(src.toUtf8(), QShader::FragmentStage);
        QVERIFY2(result.success,
                 qPrintable(QStringLiteral("bake failed: ") + dir + QStringLiteral(" — ") + result.error));
    }

    // The generated macro and the runtime uploader must agree on the lane.
    // bounce declares one float param "bounces" → customParams[0].x and
    // translateAnimationParams keys it customParams1_x (1-based uniform key for
    // the same lane). Pin both so a future split between the two is caught.
    void testPreambleLaneMatchesRuntimeUpload()
    {
        const QString dir = QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/animations/bounce");
        // Hard-fail rather than QSKIP: this is the ONLY pin that the generated
        // p_<id> macro and translateAnimationParams agree on a lane, so a
        // rename or removal of the fixture pack must break the build, not
        // silently evaporate the cross-check. (A missing source tree entirely
        // is already handled by the _data() functions above.)
        QVERIFY2(QFileInfo::exists(dir + QStringLiteral("/metadata.json")),
                 "the bounce pack is this test's lane-agreement fixture and must exist");
        const AnimationShaderEffect eff = loadEffect(dir);
        QVERIFY(eff.isValid());

        const QString preamble = AnimationShaderRegistry::paramPreamble(eff);
        QVERIFY2(preamble.contains(QStringLiteral("#define p_bounces customParams[0].x")), qPrintable(preamble));

        // Runtime uploads "bounces" to the customParams1_x key — the 1-based
        // uniform-key form of the same 0-based customParams[0].x lane.
        const QVariantMap uniforms = AnimationShaderRegistry::translateAnimationParams(eff, {});
        QVERIFY2(uniforms.contains(QStringLiteral("customParams1_x")),
                 "runtime did not key bounces to customParams1_x");
    }
};

QTEST_MAIN(TestAnimationShaderPreambleBake)
#include "test_animation_shader_preamble_bake.moc"
