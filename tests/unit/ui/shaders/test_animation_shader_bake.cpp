// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pins the VERTEX shader of every daemon-eligible built-in animation pack
// against `ShaderCompiler::compileFromFile` (qsb / glslang for SPIR-V + GLSL
// bake targets). Catches the "non-opaque uniforms outside a block" qsb
// rejection class — the regression that motivated the canonical
// `animation_uniforms.glsl` UBO — and any future drift that breaks the
// daemon's overlay-surface execution site. Compositor-only packs are excluded
// (their source is kwin classic-GL by design); test_animation_shader_kwin_bake
// covers them WHERE a desktop-GL 4.5 context exists — it QSKIPs headless, so a
// GPU-less CI run leaves those packs without compile coverage. Fragment-stage
// coverage moved to
// `test_animation_shader_preamble_bake`, which bakes each daemon-eligible
// effect.frag through the FULL runtime assembly (T1.4/T1.5 entry scaffold +
// T1.1 param preamble + include expansion); a raw compileFromFile here would
// reject an entry-only pack that defines pTransition / pIn+pOut instead of
// main().

#include <PhosphorAnimation/AnimationShaderEffect.h>
#include <PhosphorRendering/ShaderCompiler.h>

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
        for (const QString& sub : subdirs) {
            if (sub == QLatin1String("shared")) {
                continue; // shared/ holds the canonical UBO include + default vert, not a pack
            }
            // Compositor-only packs (desktop / geometry / move classes) are
            // authored against the kwin classic-GL dialect with no daemon
            // branch — the strict SPIR-V target rejects their default-block
            // uniforms by design. Their compile coverage is
            // test_animation_shader_kwin_bake.
            QFile meta(animationsDir + QLatin1Char('/') + sub + QStringLiteral("/metadata.json"));
            if (!meta.open(QIODevice::ReadOnly)) {
                // Do NOT fall through: an unreadable metadata.json would
                // silently reclassify a compositor-only pack as
                // daemon-eligible and bake a vert that is not meant for the
                // SPIR-V target. Skip and let the pack-level gates complain.
                continue;
            }
            {
                const auto eff = PhosphorAnimationShaders::AnimationShaderEffect::fromJson(
                    QJsonDocument::fromJson(meta.readAll()).object());
                if (PhosphorAnimationShaders::shaderEffectIsCompositorOnly(eff)) {
                    continue;
                }
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
        if (!any) {
            QSKIP("no animation shaders found to bake-check");
        }
    }

    void testEveryAnimationShaderBakes()
    {
        QFETCH(QString, path);
        const QStringList includePaths = {QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/animations/shared")};
        const auto result = PhosphorRendering::ShaderCompiler::compileFromFile(path, includePaths);
        QVERIFY2(
            result.success,
            qPrintable(QStringLiteral("Animation shader bake failed: ") + path + QStringLiteral(" — ") + result.error));
    }
};

QTEST_MAIN(TestAnimationShaderBake)
#include "test_animation_shader_bake.moc"
