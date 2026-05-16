// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pins every built-in animation shader against `ShaderCompiler::compileFromFile`
// (qsb / glslang for SPIR-V + GLSL bake targets). Catches the
// "non-opaque uniforms outside a block" qsb rejection class — the
// regression that motivated the canonical `animation_uniforms.glsl`
// UBO in the first place — and any future drift that breaks the
// daemon's overlay-surface execution site for a built-in animation.

#include <PhosphorRendering/ShaderCompiler.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
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
            const QString frag = animationsDir + QLatin1Char('/') + sub + QStringLiteral("/effect.frag");
            if (QFileInfo::exists(frag)) {
                QTest::newRow(qPrintable(sub + QStringLiteral(":frag"))) << frag;
                any = true;
            }
            // Also bake the optional vertex shader. Per the AnimationShaderEffect
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
