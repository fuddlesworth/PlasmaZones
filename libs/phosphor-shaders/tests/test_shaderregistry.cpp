// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShaders/CustomParamsKey.h>
#include <PhosphorShaders/ShaderParamPreamble.h>
#include <PhosphorShaders/ShaderRegistry.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSize>
#include <QTemporaryDir>
#include <QTest>

using namespace PhosphorShaders;

/// In-lib coverage for ShaderRegistry::parsePackMetadata — the offline parse
/// entry point the pack validator uses, exercising the same parser the live
/// scan runs. Pins the pack-path containment guard, the auto-slot assignment
/// contract, the duplicate-id dedup, and the schema/size gates, so the lib
/// keeps its advertised standalone-build test coverage for its largest TU.
class TestShaderRegistry : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    QString makePack(const QJsonObject& meta, const QString& name = QStringLiteral("pack"))
    {
        const QString packDir = m_dir.filePath(name);
        QDir().mkpath(packDir);
        QFile f(packDir + QStringLiteral("/metadata.json"));
        if (!f.open(QIODevice::WriteOnly)) {
            return {};
        }
        f.write(QJsonDocument(meta).toJson());
        f.close();
        QFile frag(packDir + QStringLiteral("/effect.frag"));
        if (!frag.open(QIODevice::WriteOnly)) {
            return {};
        }
        frag.write("void main() {}\n");
        frag.close();
        return packDir;
    }

    static QJsonObject baseMeta()
    {
        QJsonObject meta;
        meta.insert(QLatin1String("id"), QStringLiteral("test-pack"));
        meta.insert(QLatin1String("name"), QStringLiteral("Test Pack"));
        meta.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        meta.insert(QLatin1String("parameters"), QJsonArray());
        return meta;
    }

    static QJsonObject param(const QString& id, const QString& type, int slot = -1)
    {
        QJsonObject p;
        p.insert(QLatin1String("id"), id);
        p.insert(QLatin1String("name"), id);
        p.insert(QLatin1String("group"), QStringLiteral("g"));
        p.insert(QLatin1String("type"), type);
        p.insert(QLatin1String("default"), 0.0);
        if (slot >= 0) {
            p.insert(QLatin1String("slot"), slot);
        }
        return p;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
    }

    void testParsesAMinimalPack()
    {
        const QString packDir = makePack(baseMeta(), QStringLiteral("minimal"));
        QString error;
        const auto info = ShaderRegistry::parsePackMetadata(packDir, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(info.name, QStringLiteral("Test Pack"));
        QCOMPARE(info.packDir, QDir(packDir).absolutePath());
        QVERIFY(info.sourcePath.endsWith(QLatin1String("/effect.frag")));
    }

    void testRefusesATraversalFragmentPath()
    {
        QJsonObject meta = baseMeta();
        meta.insert(QLatin1String("fragmentShader"), QStringLiteral("../../outside.frag"));
        const QString packDir = makePack(meta, QStringLiteral("traversal"));
        const auto info = ShaderRegistry::parsePackMetadata(packDir);
        // The guard clears the escaping path rather than resolving it.
        QVERIFY(info.sourcePath.isEmpty());
    }

    void testRefusesAnAbsoluteFragmentPath()
    {
        QJsonObject meta = baseMeta();
        meta.insert(QLatin1String("fragmentShader"), QStringLiteral("/etc/hostname"));
        const QString packDir = makePack(meta, QStringLiteral("absolute"));
        const auto info = ShaderRegistry::parsePackMetadata(packDir);
        QVERIFY(info.sourcePath.isEmpty());
    }

    void testAutoSlotsFillDeclarationOrderAroundExplicitSlots()
    {
        QJsonObject meta = baseMeta();
        QJsonArray params;
        params.append(param(QStringLiteral("a"), QStringLiteral("float"))); // auto
        params.append(param(QStringLiteral("b"), QStringLiteral("float"), 0)); // explicit 0
        params.append(param(QStringLiteral("c"), QStringLiteral("float"))); // auto
        params.append(param(QStringLiteral("col"), QStringLiteral("color"))); // color pool numbers independently
        meta.insert(QLatin1String("parameters"), params);
        const QString packDir = makePack(meta, QStringLiteral("slots"));
        const auto info = ShaderRegistry::parsePackMetadata(packDir);
        QCOMPARE(info.parameters.size(), 4);
        // "a" skips the reserved lane 0 (claimed by "b"), landing on 1; "c"
        // continues to 2; the color pool starts fresh at 0.
        QCOMPARE(info.parameters[0].slot, 1);
        QCOMPARE(info.parameters[1].slot, 0);
        QCOMPARE(info.parameters[2].slot, 2);
        QCOMPARE(info.parameters[3].slot, 0);

        // Pinning the slots alone is not enough: the two sides that CONSUME a
        // slot must agree with it, or a shader reads one lane while the daemon
        // uploads to another. Assert the shader-facing p_<id> preamble and the
        // upload-facing uniformName() both derive from the same pinned slots.
        // Both assertions call the very accessors the production code uses, so
        // they track the format instead of freezing a copy of it.
        const QString preamble = ShaderRegistry::paramPreamble(info);
        QVERIFY(preamble.contains(QStringLiteral("#define p_a ") + PhosphorShaders::CustomParams::glslAccessor(1)));
        QVERIFY(preamble.contains(QStringLiteral("#define p_b ") + PhosphorShaders::CustomParams::glslAccessor(0)));
        QVERIFY(preamble.contains(QStringLiteral("#define p_c ") + PhosphorShaders::CustomParams::glslAccessor(2)));
        QVERIFY(preamble.contains(QStringLiteral("#define p_col ") + PhosphorShaders::CustomColors::glslAccessor(0)));
        // Upload side: uniformName() maps the same slots to the wire uniforms.
        QCOMPARE(info.parameters[0].uniformName(), QStringLiteral("customParams1_y")); // scalar slot 1
        QCOMPARE(info.parameters[1].uniformName(), QStringLiteral("customParams1_x")); // scalar slot 0
        QCOMPARE(info.parameters[2].uniformName(), QStringLiteral("customParams1_z")); // scalar slot 2
        QCOMPARE(info.parameters[3].uniformName(), QStringLiteral("customColor1")); // color slot 0
    }

    void testDuplicateParameterIdFirstDeclarationWins()
    {
        QJsonObject meta = baseMeta();
        QJsonArray params;
        params.append(param(QStringLiteral("speed"), QStringLiteral("float"), 3));
        params.append(param(QStringLiteral("speed"), QStringLiteral("float"), 7));
        meta.insert(QLatin1String("parameters"), params);
        const QString packDir = makePack(meta, QStringLiteral("dup"));
        const auto info = ShaderRegistry::parsePackMetadata(packDir);
        QCOMPARE(info.parameters.size(), 1);
        QCOMPARE(info.parameters[0].slot, 3);
        // And the preamble emits exactly one define for it.
        const QString preamble = ShaderRegistry::paramPreamble(info);
        QCOMPARE(preamble.count(QStringLiteral("#define p_speed ")), 1);
    }

    void testSchemaGateRefusesAPackMissingRequiredFields()
    {
        QJsonObject meta; // no id / name / fragmentShader / parameters
        meta.insert(QLatin1String("description"), QStringLiteral("not a pack"));
        const QString packDir = makePack(meta, QStringLiteral("schemafail"));
        QString error;
        const auto info = ShaderRegistry::parsePackMetadata(packDir, &error);
        QVERIFY(!info.isValid());
        QVERIFY2(error.contains(QLatin1String("schema")), qPrintable(error));
    }

    void testSinglePassPackCarriesNoBufferState()
    {
        // No multipass key and no buffer.frag on disk: the parser must not
        // publish any buffer fields (the phantom implicit buffer.frag bug).
        const QString packDir = makePack(baseMeta(), QStringLiteral("singlepass"));
        const auto info = ShaderRegistry::parsePackMetadata(packDir);
        QVERIFY(!info.isMultipass);
        QVERIFY(info.bufferShaderPaths.isEmpty());
        QVERIFY(info.bufferWraps.isEmpty());
        QVERIFY(info.bufferFilters.isEmpty());
        QVERIFY(!info.bufferFeedback);
        QVERIFY(!info.useDepthBuffer);
        QVERIFY(info.halfFloatBuffers);
    }

    void testHalfFloatBuffersParseDefaultAndOptOut()
    {
        // Absent key: the safe RGBA16F default.
        const QString defDir = makePack(baseMeta(), QStringLiteral("hfbdefault"));
        QVERIFY(ShaderRegistry::parsePackMetadata(defDir).halfFloatBuffers);

        // Explicit opt-out on a MULTIPASS pack survives (the coherence reset
        // only strips buffer state from single-pass packs).
        QJsonObject meta = baseMeta();
        meta.insert(QLatin1String("multipass"), true);
        meta.insert(QLatin1String("bufferShaders"), QJsonArray{QStringLiteral("effect.frag")});
        meta.insert(QLatin1String("halfFloatBuffers"), false);
        const QString mpDir = makePack(meta, QStringLiteral("hfboptout"));
        const auto mpInfo = ShaderRegistry::parsePackMetadata(mpDir);
        QVERIFY(mpInfo.isMultipass);
        QVERIFY(!mpInfo.halfFloatBuffers);

        // On a SINGLE-PASS pack the declaration is orphan buffer state and
        // the coherence reset forces it back to true, exactly like its
        // bufferScale/bufferWrap siblings.
        QJsonObject sp = baseMeta();
        sp.insert(QLatin1String("halfFloatBuffers"), false);
        const QString spDir = makePack(sp, QStringLiteral("hfbsinglepass"));
        QVERIFY(ShaderRegistry::parsePackMetadata(spDir).halfFloatBuffers);
    }

    /// translateParamsToUniforms — first direct coverage of the zone
    /// registry's translation: scalar and color lane mapping, the wrap
    /// vocabulary guard, the both-or-neither sampler rule, and the svgSize
    /// gating. Runs against a real scanned pack so the whole
    /// parse → auto-slot → translate chain is exercised.
    void testTranslateParamsToUniforms()
    {
        QJsonObject meta = baseMeta();
        meta.insert(QLatin1String("id"), QStringLiteral("translate-pack"));
        QJsonArray params;
        params.append(param(QStringLiteral("speed"), QStringLiteral("float"))); // auto scalar 0
        params.append(param(QStringLiteral("tint"), QStringLiteral("color"))); // auto color 0
        QJsonObject img = param(QStringLiteral("tex"), QStringLiteral("image"));
        img.insert(QLatin1String("default"), QStringLiteral("tex.png"));
        img.insert(QLatin1String("wrap"), QStringLiteral("wibble")); // invalid token
        params.append(img);
        meta.insert(QLatin1String("parameters"), params);
        const QString packDir = makePack(meta, QStringLiteral("translate"));
        QFile tex(packDir + QStringLiteral("/tex.png"));
        QVERIFY(tex.open(QIODevice::WriteOnly));
        tex.write("png");
        tex.close();

        ShaderRegistry registry;
        registry.addSearchPath(m_dir.path(), PhosphorFsLoader::LiveReload::Off);
        QString shaderId;
        const auto shaders = registry.availableShaders();
        for (const auto& info : shaders) {
            if (info.packDir == QDir(packDir).absolutePath())
                shaderId = info.id;
        }
        QVERIFY2(!shaderId.isEmpty(), "the translate pack was not registered");

        const QVariantMap stored{{QStringLiteral("speed"), 2.5}, {QStringLiteral("tint"), QStringLiteral("#ff0000")}};
        const QVariantMap uniforms = registry.translateParamsToUniforms(shaderId, stored);

        // Scalar slot 0 → customParams1_x; color slot 0 → customColor1.
        QCOMPARE(uniforms.value(QStringLiteral("customParams1_x")).toDouble(), 2.5);
        QCOMPARE(uniforms.value(QStringLiteral("customColor1")).toString().toLower(), QStringLiteral("#ffff0000"));

        // Image default resolved to an absolute in-pack path, and the invalid
        // wrap token is warned about and reset to clamp rather than forwarded.
        const QString texKey = QStringLiteral("uTexture0");
        QCOMPARE(uniforms.value(texKey).toString(), QDir::cleanPath(packDir + QStringLiteral("/tex.png")));
        QCOMPARE(uniforms.value(texKey + QStringLiteral("_wrap")).toString(), QStringLiteral("clamp"));

        // svgSize passes through only alongside a bound sampler.
        const QVariantMap withSvg =
            registry.translateParamsToUniforms(shaderId, QVariantMap{{QStringLiteral("tex_svgSize"), QSize(64, 64)}});
        QVERIFY(withSvg.contains(texKey + QStringLiteral("_svgSize")));

        // An UNBOUND sampler (empty user override suppresses the default)
        // emits neither the wrap nor the svgSize key.
        const QVariantMap unbound = registry.translateParamsToUniforms(
            shaderId, QVariantMap{{QStringLiteral("tex"), QString()}, {QStringLiteral("tex_svgSize"), QSize(64, 64)}});
        QVERIFY(unbound.value(texKey).toString().isEmpty());
        QVERIFY(!unbound.contains(texKey + QStringLiteral("_wrap")));
        QVERIFY(!unbound.contains(texKey + QStringLiteral("_svgSize")));
    }

    /// Portrait-output cover crop: on a physAspect < 1 output the crop math
    /// must divide by the REAL aspect (the old clamp-at-1.0 silently rewrote
    /// it), and two adjacent virtual screens must tile seam-free — VS-A's
    /// right crop edge equals VS-B's left crop edge exactly.
    void testWallpaperCropPortraitTilesSeamFree()
    {
        const QSize wp(1000, 1000);
        const QRect phys(0, 0, 1080, 1920); // portrait
        const QRect left(0, 0, 540, 1920);
        const QRect right(540, 0, 540, 1920);

        const QRect cropLeft = ShaderRegistry::computeWallpaperCropRect(wp, phys, left);
        const QRect cropRight = ShaderRegistry::computeWallpaperCropRect(wp, phys, right);
        QVERIFY(cropLeft.isValid());
        QVERIFY(cropRight.isValid());
        QCOMPARE(cropLeft.x() + cropLeft.width(), cropRight.x());
        // Both crops sample the full wallpaper height scaled by the REAL
        // portrait aspect: cover width is wp.height * physAspect < wp.width.
        QVERIFY(cropLeft.width() < wp.width() / 2 + 1);
    }
};

QTEST_MAIN(TestShaderRegistry)
#include "test_shaderregistry.moc"
