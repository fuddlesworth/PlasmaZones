// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// End-to-end proof for T1.4 scope B on the zone side: an entry-only fragment
// shader — the author writes ONLY `vec4 pzZone(ZoneCtx)` / `vec4 pzImage(vec2)`,
// no #version / include / in-out / main() — must assemble through the harness
// scaffold, expand its generated `#include <common.glsl>`, and bake against the
// real zone UBO. This pins that the generated dispatch (ZoneCtx fill, the loop,
// blendOver/clampFragColor) and the prologue compile, BEFORE the assembler is
// threaded into the live bake layer. Also pins that a traditional main() pack is
// passed through untouched.

#include "../../../src/daemon/rendering/zoneentryscaffold.h"

#include <PhosphorRendering/ShaderCompiler.h>
#include <PhosphorShaders/ShaderIncludeResolver.h>

#include <QTest>

class TestZoneEntryScaffold : public QObject
{
    Q_OBJECT

    static bool bakeAssembled(const QString& rawBody, QString* errOut)
    {
        const QString assembled = PlasmaZones::assembleZoneEntrySource(rawBody);
        const QStringList includePaths = {QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/shaders/shared"),
                                          QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/shaders")};
        QString err;
        const QString expanded = PhosphorShaders::ShaderIncludeResolver::expandIncludes(
            assembled, QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/shaders/shared"), includePaths, &err);
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

    void testEntryOnlyPzZoneAssemblesAndBakes()
    {
        // The whole author file — no #version, include, in/out, or main().
        const QString body = QStringLiteral(
            "vec4 pzZone(ZoneCtx z) {\n"
            "    vec4 c = z.fillColor;\n"
            "    if (z.isHighlighted) c.rgb *= 1.5;\n"
            "    // continuous-field globals stay readable:\n"
            "    c.rgb += 0.01 * sin(iTime + float(z.index));\n"
            "    return c;\n"
            "}\n");
        QString err;
        QVERIFY2(bakeAssembled(body, &err), qPrintable(err));
    }

    void testEntryOnlyPzImageAssemblesAndBakes()
    {
        const QString body = QStringLiteral(
            "vec4 pzImage(vec2 fragCoord) {\n"
            "    vec2 uv = fragCoord / iResolution;\n"
            "    return vec4(uv, 0.5 + 0.5 * sin(iTime), 1.0);\n"
            "}\n");
        QString err;
        QVERIFY2(bakeAssembled(body, &err), qPrintable(err));
    }

    void testTraditionalMainPassedThroughUnchanged()
    {
        // A pack with its own #version + main() is the traditional form and
        // must be returned byte-identical (no scaffold, no wrapper).
        const QString full = QStringLiteral(
            "#version 450\n"
            "#include <common.glsl>\n"
            "layout(location = 1) in vec2 vFragCoord;\n"
            "layout(location = 0) out vec4 fragColor;\n"
            "void main() { fragColor = vec4(1.0); }\n");
        QCOMPARE(PlasmaZones::assembleZoneEntrySource(full), full);
    }

    void testEntryOnlyIsNotPassedThrough()
    {
        // Sanity: an entry-only body must actually change (scaffold added),
        // otherwise the bake above would be testing nothing.
        const QString body = QStringLiteral("vec4 pzZone(ZoneCtx z) { return z.fillColor; }\n");
        const QString assembled = PlasmaZones::assembleZoneEntrySource(body);
        QVERIFY(assembled != body);
        QVERIFY2(assembled.startsWith(QStringLiteral("#version 450")), qPrintable(assembled));
        QVERIFY2(assembled.contains(QStringLiteral("void main()")), qPrintable(assembled));
    }
};

QTEST_MAIN(TestZoneEntryScaffold)
#include "test_zone_entry_scaffold.moc"
