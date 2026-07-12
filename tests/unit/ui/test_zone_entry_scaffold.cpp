// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// End-to-end proof for T1.4 scope B on the zone side: an entry-only fragment
// shader — the author writes ONLY `vec4 pZone(ZoneCtx)` / `vec4 pImage(vec2)`,
// no #version / include / in-out / main() — must assemble through the harness
// scaffold, expand its generated `#include <common.glsl>`, and bake against the
// real zone UBO. This pins that the generated dispatch (ZoneCtx fill, the loop,
// blendOver/clampFragColor) and the prologue compile — the same assembler the
// live bake layer applies (ZoneShaderItem::updatePaintNode → setEntryScaffold).
// Also pins that a traditional main() pack is passed through untouched.

#include "../../../src/daemon/rendering/zoneentryscaffold.h"

#include <PhosphorRendering/ShaderCompiler.h>
#include <PhosphorShaders/ShaderIncludeResolver.h>
#include <PhosphorShaders/ShaderParamPreamble.h>
#include <PhosphorShaders/ShaderRegistry.h>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

class TestZoneEntryScaffold : public QObject
{
    Q_OBJECT

    static bool bakeAssembled(const QString& rawBody, QString* errOut)
    {
        const QString assembled = PlasmaZones::assembleZoneEntrySource(rawBody);
        const QStringList includePaths = {QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/overlays/shared"),
                                          QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/overlays")};
        QString err;
        const QString expanded = PhosphorShaders::ShaderIncludeResolver::expandIncludes(
            assembled, QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/overlays/shared"), includePaths, &err);
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

    void testEntryOnlyPZoneAssemblesAndBakes()
    {
        // The whole author file — no #version, include, in/out, or main().
        const QString body = QStringLiteral(
            "vec4 pZone(ZoneCtx z) {\n"
            "    vec4 c = z.fillColor;\n"
            "    if (z.isHighlighted) c.rgb *= 1.5;\n"
            "    // continuous-field globals stay readable:\n"
            "    c.rgb += 0.01 * sin(iTime + float(z.index));\n"
            "    return c;\n"
            "}\n");
        QString err;
        QVERIFY2(bakeAssembled(body, &err), qPrintable(err));
    }

    void testEntryOnlyPImageAssemblesAndBakes()
    {
        const QString body = QStringLiteral(
            "vec4 pImage(vec2 fragCoord) {\n"
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
        const QString body = QStringLiteral("vec4 pZone(ZoneCtx z) { return z.fillColor; }\n");
        const QString assembled = PlasmaZones::assembleZoneEntrySource(body);
        QVERIFY(assembled != body);
        QVERIFY2(assembled.startsWith(QStringLiteral("#version 450")), qPrintable(assembled));
        QVERIFY2(assembled.contains(QStringLiteral("void main()")), qPrintable(assembled));
    }

    // Every bundled zone pack's effect.frag must bake through the same assembly
    // the runtime applies (read raw → assembleZoneEntrySource → expand →
    // compile): traditional main() packs pass through, migrated pZone packs get
    // wrapped. This is the regression net for migrating packs to the entry API.
    void testEveryBundledZoneFragBakes_data()
    {
        QTest::addColumn<QString>("fragPath");
        const QString root = QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/overlays");
        QDir dir(root);
        if (!dir.exists()) {
            QSKIP("data/overlays not found — running outside source tree");
        }
        bool any = false;
        for (const QString& sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            if (sub == QLatin1String("shared") || sub == QLatin1String("none")) {
                continue;
            }
            const QString frag = root + QLatin1Char('/') + sub + QStringLiteral("/effect.frag");
            if (QFileInfo::exists(frag)) {
                QTest::newRow(qPrintable(sub)) << frag;
                any = true;
            }
        }
        if (!any) {
            QSKIP("no bundled zone shaders found");
        }
    }

    void testEveryBundledZoneFragBakes()
    {
        QFETCH(QString, fragPath);
        QFile f(fragPath);
        QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(fragPath));
        const QString raw = QString::fromUtf8(f.readAll());

        // Build the param preamble from the pack's metadata, exactly as the
        // runtime does — so a pack migrated to p_<id> names finds its defines.
        // Parse metadata.json's parameters[] into a ShaderInfo (id/type/slot) and
        // run the production ShaderRegistry::paramPreamble.
        PhosphorShaders::ShaderRegistry::ShaderInfo info;
        QFile metaFile(QFileInfo(fragPath).absolutePath() + QStringLiteral("/metadata.json"));
        if (metaFile.open(QIODevice::ReadOnly)) {
            const QJsonObject root = QJsonDocument::fromJson(metaFile.readAll()).object();
            for (const QJsonValue& pv : root.value(QStringLiteral("parameters")).toArray()) {
                const QJsonObject po = pv.toObject();
                PhosphorShaders::ShaderRegistry::ParameterInfo pi;
                pi.id = po.value(QStringLiteral("id")).toString();
                pi.type = po.value(QStringLiteral("type")).toString();
                pi.slot = po.value(QStringLiteral("slot")).toInt(-1);
                info.parameters.append(pi);
            }
        }
        const QString preamble = PhosphorShaders::ShaderRegistry::paramPreamble(info);

        const QString assembled = PlasmaZones::assembleZoneEntrySource(raw);
        const QStringList includePaths = {QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/overlays/shared"),
                                          QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/overlays")};
        QString err;
        QString expanded = PhosphorShaders::ShaderIncludeResolver::expandIncludes(
            assembled, QFileInfo(fragPath).absolutePath(), includePaths, &err);
        QVERIFY2(!expanded.isEmpty(),
                 qPrintable(QStringLiteral("expand failed: ") + fragPath + QStringLiteral(" — ") + err));
        expanded = PhosphorShaders::spliceAfterVersion(expanded, preamble);

        const auto result = PhosphorRendering::ShaderCompiler::compile(expanded.toUtf8(), QShader::FragmentStage);
        QVERIFY2(result.success,
                 qPrintable(QStringLiteral("bake failed: ") + fragPath + QStringLiteral(" — ") + result.error));
    }
};

QTEST_MAIN(TestZoneEntryScaffold)
#include "test_zone_entry_scaffold.moc"
