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
// clean pack alongside. The clean pack is what catches a lint that fires on
// EVERYTHING; it cannot catch a lint that stopped running, which is what the
// negative slots are for. Two lints carry their own quiet control besides; the
// rest share the one clean pack.

#include <QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

#include <PhosphorShaders/CustomParamsKey.h>
#include <PhosphorSurface/SurfaceShaderContract.h>

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

    /// bufferShaders is a LIST, so its escape report has to say which entries.
    /// Bailing on the first one printed neither the path nor the index and
    /// suppressed the rest of the report, so an author with two bad entries
    /// fixed one and got the same anonymous line back.
    void everyEscapingBufferPathIsNamed()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(QStringLiteral("sf-buf-escape"), QJsonArray{});
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("bufferShaders"),
                   QJsonArray{QStringLiteral("../../../etc/passwd"), QStringLiteral("builtin:gaussian-h"),
                              QStringLiteral("../../../etc/shadow")});
        const PackResult r = validateSurface(tmp, QStringLiteral("sf-buf-escape"), obj, surfaceBodyReading({}));
        QVERIFY2(r.report.contains(QStringLiteral("escapes the pack directory")), qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("bufferShaders[0]: ../../../etc/passwd")), qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("bufferShaders[2]: ../../../etc/shadow")), qPrintable(r.report));
        // The builtin between them resolves and is not implicated.
        QVERIFY2(!r.report.contains(QStringLiteral("bufferShaders[1]")), qPrintable(r.report));
        QCOMPARE(r.errors, 2);
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
        QVERIFY2(r.report.contains(
                     QStringLiteral("sampler uBackdrop declared at binding 20, but the contract puts it at 15")),
                 qPrintable(r.report));
        QVERIFY(r.errors > 0);
    }

    /// The POSITIVE control for the two binding lints above, which the shared
    /// clean pack cannot provide: it declares no sampler at all, so it says
    /// nothing about a lint that fires on every DECLARED one. This pack puts a
    /// contract sampler at exactly its table slot and must draw neither
    /// diagnostic.
    ///
    /// Without it, widening either lint to reject every sampler, or dropping
    /// the equality that lets a correct slot through, passes the whole file.
    void aContractSamplerAtItsSlotPassesTheBindingLint()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(QStringLiteral("sf-binding-ok"), QJsonArray{});
        obj.insert(QStringLiteral("needsBackdrop"), true);
        const QString body = QStringLiteral("layout(binding = 15) uniform sampler2D uBackdrop;\n")
            + QStringLiteral("vec4 pSurface(vec2 uv)\n{\n    return texture(uBackdrop, uv);\n}\n");

        const PackResult r = validateSurface(tmp, QStringLiteral("sf-binding-ok"), obj, body);
        QVERIFY2(!r.report.contains(QStringLiteral("binding layout:")), qPrintable(r.report));
        QVERIFY2(!r.report.contains(QStringLiteral("but the contract puts it at")), qPrintable(r.report));
        // The error count is the guard that a fixture failure cannot satisfy
        // the two negative assertions above: a fixture report contains neither
        // string and returns -1.
        QCOMPARE(r.errors, 0);
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

    /// fromJson drops a second declaration of the same parameter id with only a
    /// qCWarning, so the lint has to walk the RAW array. Over the parsed struct
    /// this pack is indistinguishable from one that declared the id once, and it
    /// ships with the second declaration silently gone.
    void aDuplicateParameterIdIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        const QJsonObject obj =
            surfacePack(QStringLiteral("sf-dup-param"),
                        QJsonArray{surfaceParam(QStringLiteral("width"), QStringLiteral("float"), 2.0, 0.0, 8.0),
                                   surfaceParam(QStringLiteral("width"), QStringLiteral("float"), 4.0, 0.0, 8.0)});
        const PackResult r =
            validateSurface(tmp, QStringLiteral("sf-dup-param"), obj, surfaceBodyReading({QStringLiteral("width")}));
        QVERIFY2(r.report.contains(QStringLiteral("duplicate parameter id 'width'")), qPrintable(r.report));
        QVERIFY(r.errors > 0);
    }

    /// The registry gates buffer passes on the separate "multipass" key, so a
    /// pack that declares bufferShaders without it has every pass cleared at
    /// load. Before this lint such a pack validated as a clean single-pass pack
    /// and then rendered with no chain at all.
    void bufferShadersWithoutTheMultipassKeyIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(QStringLiteral("sf-no-multipass"), QJsonArray{});
        obj.insert(QStringLiteral("bufferShaders"), QJsonArray{QStringLiteral("builtin:kawase-down-0")});
        // Deliberately NO "multipass": true.

        const PackResult r = validateSurface(tmp, QStringLiteral("sf-no-multipass"), obj, surfaceBodyReading({}));
        QVERIFY2(r.report.contains(QStringLiteral("\"multipass\" is not true")), qPrintable(r.report));
        QVERIFY(r.errors > 0);
    }

    /// The four wrap/filter spellings are daemon-only: the compositor creates
    /// every buffer target GL_LINEAR / GL_CLAMP_TO_EDGE and never reads the keys.
    /// The schema and the vocabulary check both accept them, so a pack asking for
    /// "repeat" or "nearest" validated clean and then rendered one way in the
    /// settings preview and another on a real window.
    void aDaemonOnlyBufferWrapIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(QStringLiteral("sf-wrap"), QJsonArray{});
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("bufferShaders"), QJsonArray{QStringLiteral("builtin:gaussian-h")});
        obj.insert(QStringLiteral("bufferWraps"), QJsonArray{QStringLiteral("repeat")});
        obj.insert(QStringLiteral("bufferFilter"), QStringLiteral("nearest"));
        PackResult r = validateSurface(tmp, QStringLiteral("sf-wrap"), obj, surfaceBodyReading({}));
        QVERIFY2(r.report.contains(QStringLiteral("bufferWraps declares repeat")), qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("bufferFilter declares nearest")), qPrintable(r.report));

        // The token the compositor DOES honour draws no lint, so a pack that
        // states the default explicitly is not nagged for it.
        obj.insert(QStringLiteral("bufferWraps"), QJsonArray{QStringLiteral("clamp")});
        obj.insert(QStringLiteral("bufferFilter"), QStringLiteral("linear"));
        r = validateSurface(tmp, QStringLiteral("sf-wrap"), obj, surfaceBodyReading({}));
        QVERIFY2(!r.report.contains(QStringLiteral("the compositor ignores")), qPrintable(r.report));
    }

    /// The builtin Kawase passes are bound to iChannel<index> BY POSITION and the
    /// seven frags hardcode which channel they read, so the chain composes in one
    /// order only. Every token here resolves and every file compiles, so nothing
    /// but an order check can catch a pack that lists them wrongly.
    void aReorderedKawaseChainIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(QStringLiteral("sf-kawase-order"), QJsonArray{});
        obj.insert(QStringLiteral("multipass"), true);
        // The correct seven with two DOWN levels transposed.
        obj.insert(QStringLiteral("bufferShaders"),
                   QJsonArray{QStringLiteral("builtin:kawase-down-1"), QStringLiteral("builtin:kawase-down-0"),
                              QStringLiteral("builtin:kawase-down-2"), QStringLiteral("builtin:kawase-down-3"),
                              QStringLiteral("builtin:kawase-up-0"), QStringLiteral("builtin:kawase-up-1"),
                              QStringLiteral("builtin:kawase-up-2")});

        const PackResult r = validateSurface(tmp, QStringLiteral("sf-kawase-order"), obj, surfaceBodyReading({}));
        QVERIFY2(r.report.contains(QStringLiteral("positional")), qPrintable(r.report));
        QVERIFY(r.errors > 0);
    }

    /// The positive control for the slot above: the chain in its declared order
    /// must NOT trip the order lint, or the lint would fail every blur pack.
    void theCorrectKawaseChainIsNotLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(QStringLiteral("sf-kawase-ok"), QJsonArray{});
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("bufferShaders"),
                   QJsonArray{QStringLiteral("builtin:kawase-down-0"), QStringLiteral("builtin:kawase-down-1"),
                              QStringLiteral("builtin:kawase-down-2"), QStringLiteral("builtin:kawase-down-3"),
                              QStringLiteral("builtin:kawase-up-0"), QStringLiteral("builtin:kawase-up-1"),
                              QStringLiteral("builtin:kawase-up-2")});

        const PackResult r = validateSurface(tmp, QStringLiteral("sf-kawase-ok"), obj, surfaceBodyReading({}));
        QVERIFY2(!r.report.contains(QStringLiteral("positional")), qPrintable(r.report));
    }

    /// Boolean keys are read with toBool(default), which answers the DEFAULT for
    /// a non-bool, so `"halfFloatBuffers": "false"` loads as TRUE. Nothing in the
    /// tree told the author, since the JSON schema only covers bundled packs.
    void aNonBooleanPackFlagIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(QStringLiteral("sf-bool"), QJsonArray{});
        obj.insert(QStringLiteral("halfFloatBuffers"), QStringLiteral("false"));

        const PackResult r = validateSurface(tmp, QStringLiteral("sf-bool"), obj, surfaceBodyReading({}));
        QVERIFY2(r.report.contains(QStringLiteral("\"halfFloatBuffers\" must be true or false")), qPrintable(r.report));
        QVERIFY(r.errors > 0);
    }

    /// paddingRequest answers 0 for a paddingParam that resolves to no numeric
    /// parameter, so a typo does not fail: the pack just asks for no margin and
    /// clips at the frame edge, which reads as a shader bug rather than a typo.
    void aPaddingParamNamingNoParameterIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj =
            surfacePack(QStringLiteral("sf-padding"),
                        QJsonArray{surfaceParam(QStringLiteral("glowSize"), QStringLiteral("int"), 8, 0.0, 64.0)});
        obj.insert(QStringLiteral("paddingParam"), QStringLiteral("glowSizee"));

        const PackResult r =
            validateSurface(tmp, QStringLiteral("sf-padding"), obj, surfaceBodyReading({QStringLiteral("glowSize")}));
        QVERIFY2(r.report.contains(QStringLiteral("paddingParam 'glowSizee'")), qPrintable(r.report));
        QVERIFY(r.errors > 0);
    }

    /// A preview naming a file that is not there is accepted at load, so the pack
    /// ships with no thumbnail and nothing says why.
    void aMissingPreviewIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(QStringLiteral("sf-preview"), QJsonArray{});
        obj.insert(QStringLiteral("preview"), QStringLiteral("thumb.png"));

        const PackResult r = validateSurface(tmp, QStringLiteral("sf-preview"), obj, surfaceBodyReading({}));
        QVERIFY2(r.report.contains(QStringLiteral("preview missing: thumb.png")), qPrintable(r.report));
        QVERIFY(r.errors > 0);
    }

    /// toDouble answers its DEFAULT for a string, so a quoted bufferScale loads as
    /// 1.0 and the range check never sees anything wrong.
    void aNonNumericBufferScaleIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(QStringLiteral("sf-scale-str"), QJsonArray{});
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("bufferShaders"), QJsonArray{QStringLiteral("builtin:gaussian-h")});
        obj.insert(QStringLiteral("bufferScale"), QStringLiteral("0.5"));

        const PackResult r = validateSurface(tmp, QStringLiteral("sf-scale-str"), obj, surfaceBodyReading({}));
        QVERIFY2(r.report.contains(QStringLiteral("bufferScale is not a number")), qPrintable(r.report));
        QVERIFY(r.errors > 0);
    }

    /// toArray() answers an EMPTY array for any non-array value, so a list handed
    /// a scalar is dropped whole with no diagnostic.
    void aNonArrayBufferScalesIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(QStringLiteral("sf-scales-scalar"), QJsonArray{});
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("bufferShaders"), QJsonArray{QStringLiteral("builtin:gaussian-h")});
        obj.insert(QStringLiteral("bufferScales"), 0.5);

        const PackResult r = validateSurface(tmp, QStringLiteral("sf-scales-scalar"), obj, surfaceBodyReading({}));
        QVERIFY2(r.report.contains(QStringLiteral("bufferScales must be an array")), qPrintable(r.report));
        QVERIFY(r.errors > 0);
    }

    /// The one wrap/filter fault that reached the user with no diagnostic at all,
    /// because toString() answers empty for a number and the emptiness gate then
    /// reads that as "not specified".
    void aNonStringBufferWrapEntryIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(QStringLiteral("sf-wrap-num"), QJsonArray{});
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("bufferShaders"), QJsonArray{QStringLiteral("builtin:gaussian-h")});
        obj.insert(QStringLiteral("bufferWraps"), QJsonArray{3});

        const PackResult r = validateSurface(tmp, QStringLiteral("sf-wrap-num"), obj, surfaceBodyReading({}));
        QVERIFY2(r.report.contains(QStringLiteral("bufferWraps has a non-string entry")), qPrintable(r.report));
        QVERIFY(r.errors > 0);
    }

    /// The inverse of bufferShadersWithoutTheMultipassKeyIsLinted: the header
    /// still says "multipass" while every buffer lint and the whole buffer bake
    /// quietly become no-ops.
    void multipassWithoutBufferShadersIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(QStringLiteral("sf-empty-multipass"), QJsonArray{});
        obj.insert(QStringLiteral("multipass"), true);

        const PackResult r = validateSurface(tmp, QStringLiteral("sf-empty-multipass"), obj, surfaceBodyReading({}));
        QVERIFY2(r.report.contains(QStringLiteral("normalised back to single-pass")), qPrintable(r.report));
        QVERIFY(r.errors > 0);
    }

    /// Buffer keys are read only inside the multipass branch, so on a single-pass
    /// pack they are inert and their presence is an authoring mistake.
    void bufferScalesOnASinglePassPackIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(QStringLiteral("sf-stray-scales"), QJsonArray{});
        obj.insert(QStringLiteral("bufferScales"), QJsonArray{0.5});

        const PackResult r = validateSurface(tmp, QStringLiteral("sf-stray-scales"), obj, surfaceBodyReading({}));
        QVERIFY2(r.report.contains(QStringLiteral("declared on a single-pass pack")), qPrintable(r.report));
        QVERIFY(r.errors > 0);
    }

    /// A declared default is never checked against its own parameter's type, in
    /// the parser or the validator, so a quoted number on a float parameter
    /// renders 0.0 because the conversion fails silently.
    void aParameterDefaultOfTheWrongTypeIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject param = surfaceParam(QStringLiteral("width"), QStringLiteral("float"), 2.0, 0.0, 8.0);
        param.insert(QStringLiteral("default"), QStringLiteral("wide"));
        const QJsonObject obj = surfacePack(QStringLiteral("sf-bad-default"), QJsonArray{param});

        const PackResult r =
            validateSurface(tmp, QStringLiteral("sf-bad-default"), obj, surfaceBodyReading({QStringLiteral("width")}));
        QVERIFY2(r.report.contains(QStringLiteral("parameter 'width' is float but its default is not a number")),
                 qPrintable(r.report));
        QVERIFY(r.errors > 0);
    }

    /// And a default outside the range the pack itself declares, which the UI
    /// then clamps to a value the author never chose.
    void aParameterDefaultOutsideItsOwnRangeIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        const QJsonObject obj =
            surfacePack(QStringLiteral("sf-default-range"),
                        QJsonArray{surfaceParam(QStringLiteral("width"), QStringLiteral("float"), 99.0, 0.0, 8.0)});

        const PackResult r = validateSurface(tmp, QStringLiteral("sf-default-range"), obj,
                                             surfaceBodyReading({QStringLiteral("width")}));
        QVERIFY2(r.report.contains(QStringLiteral("default 99 is outside its own declared range")),
                 qPrintable(r.report));
        QVERIFY(r.errors > 0);
    }

    /// The registry drops every colour parameter past the budget with a journal
    /// warning and emits no p_<id> for it, so a pack reading one fails its bake
    /// with an undeclared identifier whose name IS declared. The animation arm
    /// has had this lint all along.
    void tooManyColourParamsIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonArray params;
        const int over = PhosphorSurfaceShaders::SurfaceShaderContract::kMaxCustomColors + 1;
        for (int i = 0; i < over; ++i) {
            params.append(surfaceParam(QStringLiteral("tint%1").arg(i), QStringLiteral("color"),
                                       QStringLiteral("#ffffffff"), 0.0, 1.0));
        }
        const QJsonObject obj = surfacePack(QStringLiteral("sf-colour-budget"), params);

        const PackResult r = validateSurface(tmp, QStringLiteral("sf-colour-budget"), obj, surfaceBodyReading({}));
        QVERIFY2(r.report.contains(QStringLiteral("too many color params")), qPrintable(r.report));
        QVERIFY(r.errors > 0);
    }

    /// A depth pack pins every pass to the single bufferScale on the daemon,
    /// because the passes share one depth attachment. That is correct, but it
    /// means a pack declaring both gets its whole pyramid flattened at load, and
    /// before this lint it shipped green.
    void bufferScalesAlongsideDepthBufferIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(QStringLiteral("sf-depth-scales"), QJsonArray{});
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("depthBuffer"), true);
        obj.insert(QStringLiteral("bufferShaders"), QJsonArray{QStringLiteral("builtin:gaussian-h")});
        obj.insert(QStringLiteral("bufferScales"), QJsonArray{0.5});

        const PackResult r = validateSurface(tmp, QStringLiteral("sf-depth-scales"), obj, surfaceBodyReading({}));
        QVERIFY2(r.report.contains(QStringLiteral("discarded at load")), qPrintable(r.report));
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

    /// bufferScales is the field this whole change introduced and NOTHING
    /// asserted any of its lints. Each of the four arms answers a different
    /// load-time behaviour, and the messages are not interchangeable because
    /// the consequences are not: a short array falls back per pass, a non-number
    /// falls back to the single bufferScale, an out-of-range value is CLAMPED,
    /// and an entry past the pass budget is DROPPED rather than clamped.
    ///
    /// Positional alignment is why the length arm matters: bufferScales[i] is
    /// the scale of pass i, so an array one short does not mean "the last pass
    /// has no scale", it means every later pass is reading a neighbour's.
    void bufferScalesLintsCoverLengthTypeRangeAndTheBudget()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        const auto twoPassPack = [](const QString& id) {
            QJsonObject obj = surfacePack(id, QJsonArray{});
            obj.insert(QStringLiteral("multipass"), true);
            obj.insert(QStringLiteral("bufferShaders"),
                       QJsonArray{QStringLiteral("builtin:kawase-down-0"), QStringLiteral("builtin:kawase-up-2")});
            return obj;
        };

        // LENGTH: one scale for two passes.
        {
            QJsonObject obj = twoPassPack(QStringLiteral("sf-scales-len"));
            obj.insert(QStringLiteral("bufferScales"), QJsonArray{0.25});
            const PackResult r = validateSurface(tmp, QStringLiteral("sf-scales-len"), obj, surfaceBodyReading({}));
            QVERIFY2(r.report.contains(QStringLiteral("bufferScales has 1 entries for 2 buffer shaders")),
                     qPrintable(r.report));
        }

        // TYPE: a string where a number belongs. The likeliest spelling of this
        // mistake, since every sibling buffer array IS a list of strings.
        {
            QJsonObject obj = twoPassPack(QStringLiteral("sf-scales-type"));
            obj.insert(QStringLiteral("bufferScales"), QJsonArray{0.25, QStringLiteral("0.125")});
            const PackResult r = validateSurface(tmp, QStringLiteral("sf-scales-type"), obj, surfaceBodyReading({}));
            QVERIFY2(r.report.contains(QStringLiteral("bufferScales entry 1 is not a number")), qPrintable(r.report));
            // Entry 0 is fine and must draw nothing, or the arm would be firing
            // on the array rather than on the entry.
            QVERIFY2(!r.report.contains(QStringLiteral("bufferScales entry 0")), qPrintable(r.report));
        }

        // RANGE, on both ends. The floor is the one this change lowered, so a
        // value under it is exactly the case a stale bound would let through.
        {
            QJsonObject obj = twoPassPack(QStringLiteral("sf-scales-range"));
            obj.insert(QStringLiteral("bufferScales"),
                       QJsonArray{PhosphorShaders::kMinBufferScale / 2.0, PhosphorShaders::kMaxBufferScale * 2.0});
            const PackResult r = validateSurface(tmp, QStringLiteral("sf-scales-range"), obj, surfaceBodyReading({}));
            QVERIFY2(r.report.contains(QStringLiteral("bufferScales entry 0 out of range")), qPrintable(r.report));
            QVERIFY2(r.report.contains(QStringLiteral("bufferScales entry 1 out of range")), qPrintable(r.report));
            QVERIFY2(r.report.contains(QStringLiteral("clamped at load")), qPrintable(r.report));
        }

        // Exactly AT each bound draws nothing. Without this the range arm would
        // pass just as well with a `<=` in place of the `<`, which would reject
        // the pyramid's own base scale.
        {
            QJsonObject obj = twoPassPack(QStringLiteral("sf-scales-edge"));
            obj.insert(QStringLiteral("bufferScales"),
                       QJsonArray{PhosphorShaders::kMinBufferScale, PhosphorShaders::kMaxBufferScale});
            const PackResult r = validateSurface(tmp, QStringLiteral("sf-scales-edge"), obj, surfaceBodyReading({}));
            QVERIFY2(!r.report.contains(QStringLiteral("out of range")), qPrintable(r.report));
        }

        // THE PASS BUDGET. One past kMaxBufferPasses, and the surplus is
        // reported as DROPPED rather than clamped, which is the distinction the
        // per-entry loop stops short of the cap to preserve.
        {
            QJsonObject obj = surfacePack(QStringLiteral("sf-scales-cap"), QJsonArray{});
            obj.insert(QStringLiteral("multipass"), true);
            QJsonArray passes;
            QJsonArray scales;
            for (int i = 0; i < PhosphorShaders::kMaxBufferPasses + 1; ++i) {
                passes.append(QStringLiteral("builtin:kawase-down-0"));
                scales.append(0.25);
            }
            obj.insert(QStringLiteral("bufferShaders"), passes);
            obj.insert(QStringLiteral("bufferScales"), scales);
            const PackResult r = validateSurface(tmp, QStringLiteral("sf-scales-cap"), obj, surfaceBodyReading({}));
            QVERIFY2(
                r.report.contains(
                    QStringLiteral("past the %1-pass budget").arg(static_cast<int>(PhosphorShaders::kMaxBufferPasses))),
                qPrintable(r.report));
            QVERIFY2(r.report.contains(QStringLiteral("dropped at load rather than clamped")), qPrintable(r.report));
        }

        // Exactly AT the budget is silent, so the cap cannot drift down by one
        // without this failing.
        {
            QJsonObject obj = surfacePack(QStringLiteral("sf-scales-atcap"), QJsonArray{});
            obj.insert(QStringLiteral("multipass"), true);
            QJsonArray passes;
            QJsonArray scales;
            for (int i = 0; i < PhosphorShaders::kMaxBufferPasses; ++i) {
                passes.append(QStringLiteral("builtin:kawase-down-0"));
                scales.append(0.25);
            }
            obj.insert(QStringLiteral("bufferShaders"), passes);
            obj.insert(QStringLiteral("bufferScales"), scales);
            const PackResult r = validateSurface(tmp, QStringLiteral("sf-scales-atcap"), obj, surfaceBodyReading({}));
            QVERIFY2(!r.report.contains(QStringLiteral("pass budget")), qPrintable(r.report));
        }
    }
};

QTEST_MAIN(TestSurfacePackValidator)
#include "test_surface_pack_validator.moc"
