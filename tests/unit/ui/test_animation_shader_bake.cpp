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
            if (sub.startsWith(QLatin1Char('_'))) {
                continue; // _shared/ holds the canonical UBO include, not a pack
            }
            const QString frag = animationsDir + QLatin1Char('/') + sub + QStringLiteral("/effect.frag");
            if (QFileInfo::exists(frag)) {
                QTest::newRow(qPrintable(sub)) << frag;
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
        // ShaderCompiler::loadAndExpand resolves `#include "..."` directives
        // relative to the file's directory, so the canonical
        // `../_shared/animation_uniforms.glsl` include is inlined before
        // the SPIR-V bake. Empty includePaths is sufficient — animation
        // shaders only include the one header sitting one directory up.
        const auto result = PhosphorRendering::ShaderCompiler::compileFromFile(path, QStringList());
        QVERIFY2(
            result.success,
            qPrintable(QStringLiteral("Animation shader bake failed: ") + path + QStringLiteral(" — ") + result.error));
    }
};

QTEST_MAIN(TestAnimationShaderBake)
#include "test_animation_shader_bake.moc"
