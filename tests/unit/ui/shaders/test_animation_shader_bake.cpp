// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pins the VERTEX shader of every built-in animation pack
// against `ShaderCompiler::compileFromFile` (qsb / glslang for SPIR-V + GLSL
// bake targets). Catches the "non-opaque uniforms outside a block" qsb
// rejection class — the regression that motivated the canonical
// `animation_uniforms.glsl` UBO — and any future drift that breaks the
// daemon's overlay-surface execution site. Compositor-only packs bake here
// too since the shared transition includes grew UBO branches; their
// kwin-dialect coverage remains test_animation_shader_kwin_bake (which
// QSKIPs headless). Fragment-stage coverage moved to
// `test_animation_shader_preamble_bake`, which bakes each daemon-eligible
// effect.frag through the FULL runtime assembly (T1.4/T1.5 entry scaffold +
// T1.1 param preamble + include expansion); a raw compileFromFile here would
// reject an entry-only pack that defines pTransition / pIn+pOut instead of
// main().

#include <PhosphorAnimation/AnimationShaderEffect.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorRendering/ShaderCompiler.h>
#include <PhosphorShaders/ShaderParamPreamble.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

class TestAnimationShaderBake : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testEveryAnimationShaderBakes_data()
    {
        QTest::addColumn<QString>("path");
        const QString animationsDir = QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/animations");
        QDir dir(animationsDir);
        if (!dir.exists()) {
            QSKIP("data/animations not found — running outside source tree");
        }
        const QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        bool any = false;
        QStringList metadataless;
        for (const QString& sub : subdirs) {
            if (sub == QLatin1String("shared")) {
                continue; // shared/ holds the canonical UBO include + default vert, not a pack
            }
            // Compositor-only packs bake here too: since the shared
            // transition includes grew UBO branches (the transition tail of
            // animation_uniforms.glsl plus the uTexture aliases), every
            // pack's sources compile under the strict SPIR-V target. Their
            // kwin-dialect coverage remains test_animation_shader_kwin_bake;
            // only ATTACHING them to a daemon surface is still refused.
            const QString metaPath = animationsDir + QLatin1Char('/') + sub + QStringLiteral("/metadata.json");
            // A subdir carrying shader sources but NO metadata.json is invisible
            // to every other gate — the CLI's isPackDir() and the preamble bake
            // both require the file before a directory counts as a pack — so it
            // would ship with zero compile coverage. Fail loudly here rather
            // than skipping.
            const bool hasSources =
                QFileInfo::exists(animationsDir + QLatin1Char('/') + sub + QStringLiteral("/effect.frag"))
                || QFileInfo::exists(animationsDir + QLatin1Char('/') + sub + QStringLiteral("/effect.vert"));
            if (hasSources && !QFileInfo::exists(metaPath)) {
                // Collect rather than QFAIL here: QFAIL expands to `return`,
                // which would drop every row for packs sorting after the
                // offender and turn one bad directory into a near-empty suite.
                metadataless << sub;
                continue;
            }
            // Bake the pack's vertex shader if it ships one. Per the AnimationShaderEffect
            // contract, packs that ship their own `effect.vert` must compile under
            // the same UBO contract as the fragment side. Without this row the
            // first vert-driven effect to land would only get bake coverage by
            // hand at first install.
            const QString vert = animationsDir + QLatin1Char('/') + sub + QStringLiteral("/effect.vert");
            if (QFileInfo::exists(vert)) {
                QTest::newRow(qPrintable(sub + QStringLiteral(":vert"))) << vert;
                any = true;
            }
        }
        // The shared default vertex stage — auto-assigned by SurfaceAnimator
        // to every anchor-extent effect that ships no effect.vert — is a
        // real compilable shader, not an include fragment. The shared/
        // directory is skipped wholesale above, so bake it explicitly:
        // a GLSL error here breaks every daemon anchor-extent transition.
        const QString sharedVert = animationsDir + QStringLiteral("/shared/animation.vert");
        if (QFileInfo::exists(sharedVert)) {
            QTest::newRow("shared/animation:vert") << sharedVert;
            any = true;
        }
        if (!metadataless.isEmpty()) {
            QFAIL(qPrintable(QStringLiteral("pack directories have shader sources but no metadata.json: ")
                             + metadataless.join(QStringLiteral(", "))));
        }
        if (!any) {
            QSKIP("no animation shaders found to bake-check");
        }
    }

    void testEveryAnimationShaderBakes()
    {
        QFETCH(QString, path);
        const QStringList includePaths = {QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/animations/shared")};
        // Splice the pack's p_<id> preamble exactly as the runtime vertex
        // compile does (ShaderNodeRhi splices m_paramPreamble into the
        // vertex stage; the kwin path splices one preamble for both
        // stages). Geometry packs read p_ params in their verts, so a raw
        // compile would reject sources the runtime accepts. The shared
        // default vert row has no pack directory and gets no preamble,
        // which matches its runtime load.
        QString preamble;
        const QString metaPath = QFileInfo(path).dir().filePath(QStringLiteral("metadata.json"));
        QFile meta(metaPath);
        const bool isSharedVertRow = QFileInfo(path).dir().dirName() == QLatin1String("shared");
        if (meta.open(QIODevice::ReadOnly)) {
            const auto eff = PhosphorAnimationShaders::AnimationShaderEffect::fromJson(
                QJsonDocument::fromJson(meta.readAll()).object());
            preamble = PhosphorAnimationShaders::AnimationShaderRegistry::paramPreamble(eff);
        } else {
            // Only the shared-vert row legitimately has no metadata.json —
            // the _data() walk fails loudly on a metadata-less PACK, so an
            // unreadable file here would otherwise degrade to an empty
            // preamble and a p_-reading vert would fail with an
            // undeclared-identifier error that hides the real cause.
            QVERIFY2(isSharedVertRow,
                     qPrintable(QStringLiteral("cannot read metadata.json beside ") + path
                                + QStringLiteral(" — the pack's p_ preamble cannot be reproduced")));
        }
        QString err;
        QString source = PhosphorRendering::ShaderCompiler::loadAndExpand(path, includePaths, &err);
        QVERIFY2(!source.isEmpty(), qPrintable(QStringLiteral("failed to load ") + path + QStringLiteral(" — ") + err));
        source = PhosphorShaders::spliceAfterVersion(source, preamble);
        const auto result = PhosphorRendering::ShaderCompiler::compile(source.toUtf8(), QShader::VertexStage);
        QVERIFY2(
            result.success,
            qPrintable(QStringLiteral("Animation shader bake failed: ") + path + QStringLiteral(" — ") + result.error));
    }
};

QTEST_MAIN(TestAnimationShaderBake)
#include "test_animation_shader_bake.moc"
