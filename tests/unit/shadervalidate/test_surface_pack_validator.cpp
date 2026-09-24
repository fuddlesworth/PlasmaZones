// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The offline pack validator's SURFACE arm, which until this file had no test
// harness at all.
//
// That absence was structural rather than accidental. The validator has four
// production arms, and the executables beside this one reached only three of
// them: test_pack_validators covers animation and overlay, test_pointer_pack_-
// validator covers pointer, test_animation_pack_bakes and test_pack_model_-
// detection cover the animation stage bakes and the shared/ marker lookup, and
// surface had nothing at all. Each executable compiles ALL FOUR arms, so a lint
// removed from the surface arm alone broke no test and failed no link either.
// Nothing in the topology made a missing family visible. The split that produced
// it was made when the shared file passed the file-size ceiling, so the cut
// followed line count rather than the family boundary.
//
// The bundled-pack gate (shader_validate_surface) only proves the shipped packs
// are clean; it cannot show that a BROKEN pack is caught. These slots build
// deliberately-broken packs in a temp dir and assert the diagnostic, with a
// clean pack alongside so each negative has a positive that would fail if the
// lint stopped running altogether.

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

#include "packvalidatortesthelpers.h"

using namespace PackValidatorTest;

namespace {

/// The surface twin of `validate`. Writes the pack plus a `pSurface` entry body,
/// which the validator assembles into a full TU exactly as the daemon and the
/// compositor do.
/// @p vertBody, when given, is written under the name @p metadata declares in
/// `vertexShader`, so the fixture cannot drift from what the pack claims to ship.
PackResult validateSurface(const QTemporaryDir& tmp, const QString& name, const QJsonObject& metadata,
                           const QString& body, const QString& vertBody = QString())
{
    const QString dir = tmp.filePath(name);
    if (!writePackFile(dir, QStringLiteral("metadata.json"), QJsonDocument(metadata).toJson())) {
        return fixtureFailure(QStringLiteral("failed to write metadata.json under ") + dir);
    }
    if (!writePackFile(dir, QStringLiteral("effect.frag"), body.toUtf8())) {
        return fixtureFailure(QStringLiteral("failed to write effect.frag under ") + dir);
    }
    if (!vertBody.isEmpty()) {
        const QString vertName = metadata.value(QLatin1String("vertexShader")).toString();
        if (vertName.isEmpty()) {
            return fixtureFailure(QStringLiteral("vertex body given but metadata declares no vertexShader"));
        }
        if (!writePackFile(dir, vertName, vertBody.toUtf8())) {
            return fixtureFailure(QStringLiteral("failed to write ") + vertName + QStringLiteral(" under ") + dir);
        }
    }

    PackResult result;
    QTextStream stream(&result.report);
    result.errors = PlasmaZones::ShaderValidate::validateSurfacePack(dir, stream);
    stream.flush();
    return result;
}

/// One surface parameter declaration.
QJsonObject surfaceParam(const QString& id, const QString& type, const QJsonValue& def, double min, double max)
{
    QJsonObject param;
    param.insert(QStringLiteral("id"), id);
    param.insert(QStringLiteral("name"), id);
    param.insert(QStringLiteral("type"), type);
    param.insert(QStringLiteral("default"), def);
    if (type != QLatin1String("color") && type != QLatin1String("bool")) {
        param.insert(QStringLiteral("min"), min);
        param.insert(QStringLiteral("max"), max);
    }
    return param;
}

QJsonObject surfacePack(const QString& id, const QJsonArray& params)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("name"), id);
    obj.insert(QStringLiteral("fragmentShader"), QStringLiteral("effect.frag"));
    obj.insert(QStringLiteral("parameters"), params);
    return obj;
}

/// A `pSurface` body that READS every id in @p ids, so the declared-but-unread
/// sweep stays quiet and the lint under test is the only thing in the report.
QString surfaceBodyReading(const QStringList& ids)
{
    QString body = QStringLiteral("vec4 pSurface(vec2 uv)\n{\n    float acc = 0.0;\n");
    for (const QString& id : ids) {
        body += QStringLiteral("    acc += float(p_%1);\n").arg(id);
    }
    body += QStringLiteral("    return vec4(acc, 0.0, 0.0, 1.0);\n}\n");
    return body;
}

/// A vertex stage a pack ships ITSELF, with @p assign spliced in as the
/// gl_Position write. Deliberately free of `qt_Matrix`: that uniform is declared
/// only in the daemon UBO branch of surface_uniforms.glsl, so a stage using it
/// cannot bake for the compositor. The shared `surface.vert` fallback does use
/// it, which is why the validator bakes the fallback on the Qt-RHI path alone.
///
/// Carries its own `#version`, unlike the fragment bodies above: the validator
/// splices a generated preamble ahead of a fragment, so a `#version` there lands
/// mid-file, while a vertex stage is passed through and must declare its own.
QString packVertexBody(const QString& assign)
{
    return QStringLiteral(
               "#version 450\n"
               "#include <surface_uniforms.glsl>\n"
               "layout(location = 0) in vec2 position;\n"
               "layout(location = 1) in vec2 texCoord;\n"
               "layout(location = 0) out vec2 vTexCoord;\n"
               "void main()\n"
               "{\n"
               "    vTexCoord = texCoord;\n")
        + assign + QStringLiteral("}\n");
}

/// True when some ONE line of @p report names both @p stage and @p marker.
/// A bare `report.contains(...)` cannot express this: every slot's report also
/// carries the fragment's own compile lines, so asserting on "OK (compositor)"
/// across the whole report passes whether or not the stage under test was baked
/// at all.
bool reportLineHas(const QString& report, const QString& stage, const QString& marker)
{
    const QStringList lines = report.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        if (line.contains(stage) && line.contains(marker)) {
            return true;
        }
    }
    return false;
}

} // namespace

class TestSurfacePackValidator : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// The positive control every negative below leans on. Without it a lint that
    /// fired on EVERYTHING would satisfy all of them.
    void aCleanSurfacePackReportsNoErrors()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        const QJsonObject obj =
            surfacePack(QStringLiteral("sf-clean"),
                        QJsonArray{surfaceParam(QStringLiteral("width"), QStringLiteral("float"), 2.0, 0.0, 8.0)});
        const PackResult r =
            validateSurface(tmp, QStringLiteral("sf-clean"), obj, surfaceBodyReading({QStringLiteral("width")}));
        QVERIFY2(r.report.contains(QStringLiteral("metadata       OK")), qPrintable(r.report));
        QCOMPARE(r.errors, 0);
    }

    /// The surface arm runs the shared preset lint, which was the point of
    /// building this harness: the surface arm had no slots of its own, so the lint
    /// could have been deleted from it without a single test noticing.
    void anImagePresetValueIsReportedRatherThanSilentlyDropped()
    {
        // This arm parses a pack's presets BEFORE its directory is stamped, so
        // parsePackPresets' fail-closed guard refuses every image-typed preset value and
        // the value is gone before any other lint sees the map. That mirrors the runtime,
        // so the fix is a REPORT rather than a change: the declaration is in the metadata
        // and that is what this lints.
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(
            QStringLiteral("sf-image-preset"),
            QJsonArray{surfaceParam(QStringLiteral("tex"), QStringLiteral("image"), QStringLiteral(""), 0.0, 0.0)});
        obj.insert(QStringLiteral("presets"),
                   QJsonObject{{QStringLiteral("Textured"),
                                QJsonObject{{QStringLiteral("tex"), QStringLiteral("noise.png")}}}});
        const PackResult r = validateSurface(tmp, QStringLiteral("sf-image-preset"), obj, surfaceBodyReading({}));
        QVERIFY2(r.report.contains(QStringLiteral("is dropped at load")), qPrintable(r.report));

        // A pack declaring the image param but setting no preset value for it draws
        // nothing from this lint: an unused declaration is the type lint's business.
        obj.remove(QStringLiteral("presets"));
        const PackResult quiet = validateSurface(tmp, QStringLiteral("sf-image-preset"), obj, surfaceBodyReading({}));
        QVERIFY2(!quiet.report.contains(QStringLiteral("is dropped at load")), qPrintable(quiet.report));
    }

    void thePresetLintRunsOnTheSurfaceArm()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj =
            surfacePack(QStringLiteral("sf-preset"),
                        QJsonArray{surfaceParam(QStringLiteral("width"), QStringLiteral("float"), 2.0, 0.0, 8.0)});
        QJsonObject presets;
        presets.insert(QStringLiteral("Undeclared"), QJsonObject{{QStringLiteral("noSuchThing"), 1.0}});
        presets.insert(QStringLiteral("TooWide"), QJsonObject{{QStringLiteral("width"), 99.0}});
        presets.insert(QStringLiteral("../escape"), QJsonObject{{QStringLiteral("width"), 2.0}});
        obj.insert(QStringLiteral("presets"), presets);

        const PackResult r =
            validateSurface(tmp, QStringLiteral("sf-preset"), obj, surfaceBodyReading({QStringLiteral("width")}));
        QVERIFY2(r.report.contains(QStringLiteral("which the pack does not declare")), qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("above its declared maximum")), qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("has an unusable id")), qPrintable(r.report));
        QVERIFY(r.errors >= 3);
    }

    /// A preset value inside its declared range draws nothing, so the range check
    /// is not merely complaining about every value it sees.
    void aWellFormedSurfacePresetIsSilent()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj =
            surfacePack(QStringLiteral("sf-preset-ok"),
                        QJsonArray{surfaceParam(QStringLiteral("width"), QStringLiteral("float"), 2.0, 0.0, 8.0)});
        obj.insert(QStringLiteral("presets"),
                   QJsonObject{{QStringLiteral("Thin"), QJsonObject{{QStringLiteral("width"), 1.0}}}});

        const PackResult r =
            validateSurface(tmp, QStringLiteral("sf-preset-ok"), obj, surfaceBodyReading({QStringLiteral("width")}));
        // "preset '" is the diagnostic prefix; a bare "preset" also matches the
        // pack name in the report header.
        QVERIFY2(!r.report.contains(QStringLiteral("preset '")), qPrintable(r.report));
        QCOMPARE(r.errors, 0);
    }

    /// The surface parameter vocabulary is float/int/bool/color — no `image`,
    /// unlike the overlay family. An unknown type is dropped at load with no
    /// `p_` define, so the shader fails to compile for a reason that does not
    /// name the metadata.
    void anUnknownSurfaceParamTypeIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        const QJsonObject obj =
            surfacePack(QStringLiteral("sf-type"),
                        QJsonArray{surfaceParam(QStringLiteral("tex"), QStringLiteral("image"), 0.0, 0.0, 1.0)});
        const PackResult r = validateSurface(tmp, QStringLiteral("sf-type"), obj, surfaceBodyReading({}));
        QVERIFY2(r.report.contains(QStringLiteral("unknown param type")), qPrintable(r.report));
        QVERIFY(r.errors > 0);
    }

    /// A declared path that escapes the pack must be REFUSED rather than opened
    /// and fed to glslang.
    void aFragmentPathEscapingThePackIsRefused()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(QStringLiteral("sf-escape"), QJsonArray{});
        obj.insert(QStringLiteral("fragmentShader"), QStringLiteral("../../../etc/passwd"));
        const PackResult r = validateSurface(tmp, QStringLiteral("sf-escape"), obj, surfaceBodyReading({}));
        QVERIFY2(r.report.contains(QStringLiteral("escapes the pack directory")), qPrintable(r.report));
        QVERIFY(r.errors > 0);
    }

    /// A texture `wrap` outside {clamp,repeat,mirror} is silently cleared to
    /// clamp at load, so the author never learns their token was wrong. Linted
    /// off the RAW metadata for exactly that reason.
    void anInvalidTextureWrapIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        const QString dir = tmp.filePath(QStringLiteral("sf-wrap"));
        QVERIFY(writePackFile(dir, QStringLiteral("tex.png"), QByteArray("not-really-a-png")));

        QJsonObject tex;
        tex.insert(QStringLiteral("path"), QStringLiteral("tex.png"));
        tex.insert(QStringLiteral("wrap"), QStringLiteral("wobble"));
        QJsonObject obj = surfacePack(QStringLiteral("sf-wrap"), QJsonArray{});
        obj.insert(QStringLiteral("textures"), QJsonArray{tex});

        const PackResult r = validateSurface(tmp, QStringLiteral("sf-wrap"), obj, surfaceBodyReading({}));
        QVERIFY2(r.report.contains(QStringLiteral("wrap not in {clamp,repeat,mirror}")), qPrintable(r.report));
        QVERIFY(r.errors > 0);
    }

    /// Every baked stage is reflected against the shared descriptor-binding
    /// table. A pack that declares its own sampler inside the reserved range
    /// (here on the iChannel1 slot) compiles fine on its own, and would then
    /// fail the daemon's pipeline with nothing naming the cause, so the
    /// validator names the sampler and the slot instead.
    ///
    /// The slot below covers the other half: a pack sampler is refused wherever
    /// it sits, so this one must keep asserting on a RESERVED binding to stay a
    /// test of the reserved-range case rather than a duplicate of that one.
    void aSamplerOnAReservedBindingIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        const QJsonObject obj = surfacePack(QStringLiteral("sf-binding"), QJsonArray{});
        const QString body = QStringLiteral("layout(binding = 3) uniform sampler2D uMine;\n")
            + QStringLiteral("vec4 pSurface(vec2 uv)\n{\n    return texture(uMine, uv);\n}\n");

        const PackResult r = validateSurface(tmp, QStringLiteral("sf-binding"), obj, body);
        QVERIFY2(r.report.contains(QStringLiteral("binding layout: sampler uMine declared at binding 3")),
                 qPrintable(r.report));
        QVERIFY(r.errors > 0);
    }

    /// The contract samplers must sit at their table slot: a pack that re-declares
    /// a contract name elsewhere is a header drift, which is exactly what the
    /// lint exists to catch.
    void aContractSamplerOffItsSlotIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        const QJsonObject obj = surfacePack(QStringLiteral("sf-binding-drift"), QJsonArray{});
        // uBackdrop belongs at the wallpaper slot (15); declare it at a consumer
        // slot so the compile itself stays clean and only the table disagrees.
        const QString body = QStringLiteral("layout(binding = 20) uniform sampler2D uBackdrop;\n")
            + QStringLiteral("vec4 pSurface(vec2 uv)\n{\n    return texture(uBackdrop, uv);\n}\n");

        const PackResult r = validateSurface(tmp, QStringLiteral("sf-binding-drift"), obj, body);
        QVERIFY2(
            r.report.contains(QStringLiteral("sampler uBackdrop declared at binding 20, the contract puts it at 15")),
            qPrintable(r.report));
        QVERIFY(r.errors > 0);
    }

    /// A sampler a pack declares for ITSELF is refused wherever it sits, and the
    /// consumer range is the case that used to pass. That range is reachable
    /// only through ShaderNodeRhi::setExtraBinding, whose callers in the tree
    /// bind the overlay zone-labels texture and nothing else, and the compositor
    /// binds by name with no entry for a pack's own sampler. Accepting it told
    /// the author the pack was clean and left the sampler reading nothing.
    void aPackSamplerInTheConsumerRangeIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        const QJsonObject obj = surfacePack(QStringLiteral("sf-binding-consumer"), QJsonArray{});
        // 20 sits squarely inside the consumer range, so this fixture collides
        // with no contract slot and fails ONLY on the rule under test.
        const QString body = QStringLiteral("layout(binding = 20) uniform sampler2D uMine;\n")
            + QStringLiteral("vec4 pSurface(vec2 uv)\n{\n    return texture(uMine, uv);\n}\n");

        const PackResult r = validateSurface(tmp, QStringLiteral("sf-binding-consumer"), obj, body);
        QVERIFY2(r.report.contains(QStringLiteral("sampler uMine declared at binding 20, which nothing will bind")),
                 qPrintable(r.report));
        QVERIFY(r.errors > 0);
    }

    /// A pack that ships its OWN vertex stage gets it baked on BOTH hosts. No
    /// bundled pack declares one, so shader_validate_surface never reaches this
    /// arm and a regression in it would surface to a third-party author before
    /// it ever surfaced in CI.
    void aPackDeclaredVertexBakesOnBothBranches()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(QStringLiteral("sf-vert"), QJsonArray{});
        obj.insert(QStringLiteral("vertexShader"), QStringLiteral("probe.vert"));

        const PackResult r =
            validateSurface(tmp, QStringLiteral("sf-vert"), obj, surfaceBodyReading({}),
                            packVertexBody(QStringLiteral("    gl_Position = vec4(position, 0.0, 1.0);\n")));
        QVERIFY2(reportLineHas(r.report, QStringLiteral("probe.vert"), QStringLiteral("OK (compositor)")),
                 qPrintable(r.report));
        QCOMPARE(r.errors, 0);
    }

    /// And that compositor half is a real compile, not a line in a report: a
    /// break behind PLASMAZONES_KWIN passes the Qt-RHI bake and must still fail
    /// the pack. Without this, a bake that silently no-opped would look exactly
    /// like the slot above passing.
    void aCompositorOnlyBreakInAPackVertexIsCaught()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(QStringLiteral("sf-vert-kwin"), QJsonArray{});
        obj.insert(QStringLiteral("vertexShader"), QStringLiteral("probe.vert"));

        const QString assign = QStringLiteral(
            "#ifdef PLASMAZONES_KWIN\n"
            "    gl_Position = pzNoSuchFunction(position);\n"
            "#else\n"
            "    gl_Position = vec4(position, 0.0, 1.0);\n"
            "#endif\n");
        const PackResult r =
            validateSurface(tmp, QStringLiteral("sf-vert-kwin"), obj, surfaceBodyReading({}), packVertexBody(assign));
        QVERIFY2(reportLineHas(r.report, QStringLiteral("probe.vert"), QStringLiteral("OK")), qPrintable(r.report));
        QVERIFY2(reportLineHas(r.report, QStringLiteral("probe.vert"), QStringLiteral("ERROR (compositor)")),
                 qPrintable(r.report));
        QVERIFY(r.errors > 0);
    }

    /// The shared `surface.vert` fallback is deliberately NOT baked for the
    /// compositor, and that exclusion needs pinning because it reads like an
    /// oversight sitting next to the fragment and buffer passes. The fallback
    /// multiplies by `qt_Matrix`, which surface_uniforms.glsl declares only in
    /// the daemon UBO branch, so the obvious "completeness" fix would fail every
    /// pack that does not ship a vertex stage of its own, on an undeclared
    /// identifier in a file the author never wrote.
    void theSharedVertexFallbackIsNotBakedForTheCompositor()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        const QJsonObject obj = surfacePack(QStringLiteral("sf-vert-fallback"), QJsonArray{});
        const PackResult r = validateSurface(tmp, QStringLiteral("sf-vert-fallback"), obj, surfaceBodyReading({}));
        QVERIFY2(reportLineHas(r.report, QStringLiteral("surface.vert"), QStringLiteral("OK")), qPrintable(r.report));
        QVERIFY2(!reportLineHas(r.report, QStringLiteral("surface.vert"), QStringLiteral("(compositor)")),
                 qPrintable(r.report));
        QCOMPARE(r.errors, 0);
    }
};

QTEST_MAIN(TestSurfacePackValidator)
#include "test_surface_pack_validator.moc"
