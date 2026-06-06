// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// End-to-end proof for T1.5 on the animation side: an entry-only transition
// shader — the author writes ONLY `pTransition` (symmetric) or `pIn`+`pOut`
// (asymmetric), no #version / include / in-out / main() and no direction code —
// assembles through the harness scaffold, expands `#include
// <animation_uniforms.glsl>`, and bakes against the real animation UBO. Pins
// that the generated direction dispatch (legProgress / p_reversed) and the
// prologue compile, that a half-defined pair (pIn without pOut) does NOT get a
// dangling-call main, and that a traditional main() pack is passed through.

#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorRendering/ShaderCompiler.h>
#include <PhosphorShaders/ShaderEntryPoint.h>
#include <PhosphorShaders/ShaderIncludeResolver.h>

#include <QTest>

using PhosphorAnimationShaders::AnimationShaderRegistry;

class TestAnimationEntryScaffold : public QObject
{
    Q_OBJECT

    static QString assemble(const QString& rawBody)
    {
        return PhosphorShaders::assembleEntryPoint(rawBody, AnimationShaderRegistry::animationEntryPrologue(),
                                                   AnimationShaderRegistry::animationEntryCandidates());
    }

    static bool bakeBody(const QString& rawBody, QString* errOut)
    {
        const QString assembled = assemble(rawBody);
        const QStringList includePaths = {QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/animations/shared")};
        QString err;
        const QString expanded = PhosphorShaders::ShaderIncludeResolver::expandIncludes(
            assembled, QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/animations/shared"), includePaths, &err);
        if (expanded.isEmpty()) {
            if (errOut) {
                *errOut = QStringLiteral("include expand failed: ") + err;
            }
            return false;
        }
        const auto result = PhosphorRendering::ShaderCompiler::compile(expanded.toUtf8(), QShader::FragmentStage);
        if (!result.success && errOut) {
            *errOut = result.error;
        }
        return result.success;
    }

private Q_SLOTS:

    void testSymmetricPTransitionBakes()
    {
        // The whole author file: one symmetric entry, `t` is raw iTime.
        const QString body = QStringLiteral(
            "vec4 pTransition(vec2 uv, float t) {\n"
            "    return surfaceColor(uv) * smoothstep(0.0, 1.0, t);\n"
            "}\n");
        QString err;
        QVERIFY2(bakeBody(body, &err), qPrintable(err));
        // Generated main calls pTransition with raw iTime (no legProgress un-flip).
        const QString assembled = assemble(body);
        QVERIFY2(assembled.contains(QStringLiteral("pTransition(vTexCoord, iTime)")), qPrintable(assembled));
    }

    void testAsymmetricPInPOutBakes()
    {
        // Two entries; the harness dispatches by direction and feeds forward 0→1 t.
        const QString body = QStringLiteral(
            "vec4 pIn(vec2 uv, float t)  { return surfaceColor(uv) * t; }\n"
            "vec4 pOut(vec2 uv, float t) { return surfaceColor(uv) * (1.0 - t); }\n");
        QString err;
        QVERIFY2(bakeBody(body, &err), qPrintable(err));
        const QString assembled = assemble(body);
        // The generated main un-flips via legProgress and dispatches on p_reversed.
        QVERIFY2(assembled.contains(QStringLiteral("legProgress()")), qPrintable(assembled));
        QVERIFY2(assembled.contains(QStringLiteral("p_reversed ? pOut(vTexCoord")), qPrintable(assembled));
    }

    void testLonePInFallsThrough()
    {
        // pIn without its pOut companion must NOT generate a main that calls
        // the missing pOut — the candidate is skipped, so no dispatch is added.
        const QString body = QStringLiteral("vec4 pIn(vec2 uv, float t) { return surfaceColor(uv) * t; }\n");
        const QString assembled = assemble(body);
        QVERIFY2(!assembled.contains(QStringLiteral("p_reversed ? pOut")), qPrintable(assembled));
        QVERIFY2(!assembled.contains(QStringLiteral("void main()")), qPrintable(assembled));
    }

    void testTraditionalMainPassedThrough()
    {
        const QString full = QStringLiteral(
            "#version 450\n"
            "#include <animation_uniforms.glsl>\n"
            "layout(location = 0) in vec2 vTexCoord;\n"
            "layout(location = 0) out vec4 fragColor;\n"
            "void main() { fragColor = surfaceColor(vTexCoord); }\n");
        QCOMPARE(assemble(full), full);
    }
};

QTEST_MAIN(TestAnimationEntryScaffold)
#include "test_animation_entry_scaffold.moc"
