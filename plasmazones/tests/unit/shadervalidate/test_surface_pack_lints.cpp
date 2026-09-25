// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The surface validator's remaining metadata lints, one negative slot each.
//
// WHY A SECOND FILE. test_surface_pack_validator.cpp covers the arm's binding
// lints, its preset arm, its buffer-scale arm and its stage bakes, and adding
// these to it would push it past the file-size ceiling. The cut is by COVERAGE
// GAP rather than by subject, so this file is the answer to one question: which
// lints could be deleted with the suite still green? Each slot below existed only
// as a line of validator code until it was written.
//
// The fixture writers are shared (packvalidatortesthelpers.h) rather than copied,
// because two copies of a fixture writer is how two test files start disagreeing
// about what a valid pack looks like.
//
// EVERY SLOT IS A NEGATIVE, and they lean on the clean-pack positive control in
// the sibling file: a lint that fired on EVERYTHING would satisfy all of these
// and fail that one. Where a lint has a near-miss worth pinning (a value just
// inside the bound, the correct spelling of a token) the slot carries its own
// quiet control too, because the shared clean pack cannot express those.

#include <QtTest>

#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

#include <PhosphorSurface/SurfaceShaderContract.h>

#include "packvalidatortesthelpers.h"

using namespace PackValidatorTest;

class TestSurfacePackLints : public QObject
{
    Q_OBJECT

private:
    /// Write a 1x1 transparent PNG into the pack, for the slots that need a
    /// texture file to EXIST so the missing-texture lint is not what fires.
    static bool writeStubImage(const QString& dir, const QString& file)
    {
        QImage px(1, 1, QImage::Format_RGBA8888);
        px.fill(Qt::transparent);
        return px.save(QDir(dir).filePath(file));
    }

private Q_SLOTS:
    /// An id that is not a GLSL identifier gets no `p_` define, so the pack
    /// compiles and the parameter silently does nothing. Distinct from the
    /// duplicate-id lint the sibling file covers.
    void anInvalidParameterIdIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        const QJsonObject obj =
            surfacePack(QStringLiteral("sf-badid"),
                        QJsonArray{surfaceParam(QStringLiteral("has-a-dash"), QStringLiteral("float"), 1.0, 0.0, 2.0)});
        const PackResult r = validateSurface(tmp, QStringLiteral("sf-badid"), obj, surfaceBodyReading({}));
        QVERIFY2(r.report.contains(QStringLiteral("invalid parameter id")), qPrintable(r.report));
    }

    /// Past the contract's user-texture slot count the surplus is dropped at
    /// load, so the pack renders with fewer textures than it declares. The
    /// count comes from the contract rather than a literal, so the slot cannot
    /// drift from the cap it is testing.
    void tooManyTexturesIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        const int cap = PhosphorSurfaceShaders::SurfaceShaderContract::kMaxUserTextureSlots;
        QJsonObject obj = surfacePack(QStringLiteral("sf-manytex"), QJsonArray{});
        QJsonArray textures;
        for (int i = 0; i < cap + 1; ++i) {
            QJsonObject tex;
            tex.insert(QStringLiteral("path"), QStringLiteral("t%1.png").arg(i));
            textures.append(tex);
        }
        obj.insert(QStringLiteral("textures"), textures);
        const QString dir = tmp.filePath(QStringLiteral("sf-manytex"));
        QVERIFY(writePackFile(dir, QStringLiteral("metadata.json"), QJsonDocument(obj).toJson()));
        for (int i = 0; i < cap + 1; ++i) {
            QVERIFY(writeStubImage(dir, QStringLiteral("t%1.png").arg(i)));
        }
        QVERIFY(writePackFile(dir, QStringLiteral("effect.frag"), surfaceBodyReading({}).toUtf8()));

        QString report;
        QTextStream stream(&report);
        PlasmaZones::ShaderValidate::validateSurfacePack(dir, stream);
        stream.flush();
        QVERIFY2(report.contains(QStringLiteral("too many textures")), qPrintable(report));

        // Exactly AT the cap is silent, so the lint cannot drift down by one.
        QJsonArray atCap;
        for (int i = 0; i < cap; ++i) {
            QJsonObject tex;
            tex.insert(QStringLiteral("path"), QStringLiteral("t%1.png").arg(i));
            atCap.append(tex);
        }
        obj.insert(QStringLiteral("textures"), atCap);
        QVERIFY(writePackFile(dir, QStringLiteral("metadata.json"), QJsonDocument(obj).toJson()));
        QString okReport;
        QTextStream okStream(&okReport);
        PlasmaZones::ShaderValidate::validateSurfacePack(dir, okStream);
        okStream.flush();
        QVERIFY2(!okReport.contains(QStringLiteral("too many textures")), qPrintable(okReport));
    }

    /// The three ways a texture entry fails to name a usable file. Grouped
    /// because they share one fixture shape and differ only in the path.
    void theTextureEntryPathLintsEachFire()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        const auto runWithTexturePath = [&tmp](const QString& name, const QString& path) {
            QJsonObject tex;
            tex.insert(QStringLiteral("path"), path);
            QJsonObject obj = surfacePack(name, QJsonArray{});
            obj.insert(QStringLiteral("textures"), QJsonArray{tex});
            return validateSurface(tmp, name, obj, surfaceBodyReading({}));
        };

        // Empty path: dropped at load, which also shifts every later texture
        // down one sampler slot — the consequence the message names.
        const PackResult empty = runWithTexturePath(QStringLiteral("sf-tex-empty"), QString());
        QVERIFY2(empty.report.contains(QStringLiteral("texture entry with empty `path`")), qPrintable(empty.report));

        // Escaping the pack directory: rejected outright, sampler reads
        // transparent.
        const PackResult escape =
            runWithTexturePath(QStringLiteral("sf-tex-escape"), QStringLiteral("../../../etc/passwd"));
        QVERIFY2(escape.report.contains(QStringLiteral("texture path escapes the pack directory")),
                 qPrintable(escape.report));

        // Confined but absent: a typo ships green and fails at first paint.
        const PackResult missing = runWithTexturePath(QStringLiteral("sf-tex-missing"), QStringLiteral("nope.png"));
        QVERIFY2(missing.report.contains(QStringLiteral("texture missing")), qPrintable(missing.report));
    }

    /// The buffer-entry lints that are not about a path escaping, which the
    /// sibling file covers. An empty entry and an unknown builtin token both
    /// fail the pack closed to single-pass, and a pack-local file that is simply
    /// absent does the same.
    void theBufferEntryLintsEachFire()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        const auto runWithBuffers = [&tmp](const QString& name, const QJsonArray& buffers) {
            QJsonObject obj = surfacePack(name, QJsonArray{});
            obj.insert(QStringLiteral("multipass"), true);
            obj.insert(QStringLiteral("bufferShaders"), buffers);
            return validateSurface(tmp, name, obj, surfaceBodyReading({}));
        };

        const PackResult empty = runWithBuffers(QStringLiteral("sf-buf-empty"), QJsonArray{QString()});
        QVERIFY2(empty.report.contains(QStringLiteral("empty bufferShaders entry")), qPrintable(empty.report));

        const PackResult unknown =
            runWithBuffers(QStringLiteral("sf-buf-unknown"), QJsonArray{QStringLiteral("builtin:not-a-pass")});
        QVERIFY2(unknown.report.contains(QStringLiteral("unknown or unlocatable builtin buffer shader")),
                 qPrintable(unknown.report));

        const PackResult missing =
            runWithBuffers(QStringLiteral("sf-buf-missing"), QJsonArray{QStringLiteral("pass0.frag")});
        QVERIFY2(missing.report.contains(QStringLiteral("multipass buffer shader missing")),
                 qPrintable(missing.report));
    }

    /// bufferWraps and bufferFilters are POSITIONALLY aligned to bufferShaders,
    /// so a list of the wrong length does not mean "the rest are unset", it means
    /// every later pass reads a neighbour's value. Both keys, because the lint is
    /// generated per key and one could be wired and the other not.
    void aMisalignedBufferArrayLengthIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        for (const QString& key : {QStringLiteral("bufferWraps"), QStringLiteral("bufferFilters")}) {
            QJsonObject obj = surfacePack(QStringLiteral("sf-align"), QJsonArray{});
            obj.insert(QStringLiteral("multipass"), true);
            obj.insert(QStringLiteral("bufferShaders"),
                       QJsonArray{QStringLiteral("builtin:gaussian-h"), QStringLiteral("builtin:gaussian-v")});
            obj.insert(key, QJsonArray{QStringLiteral("clamp")}); // one entry for two passes
            const PackResult r = validateSurface(tmp, QStringLiteral("sf-align"), obj, surfaceBodyReading({}));
            QVERIFY2(r.report.contains(QStringLiteral("%1 has 1 entries for 2 buffer shaders").arg(key)),
                     qPrintable(r.report));
        }
    }

    /// An unrecognised wrap or filter token is cleared to empty at load with only
    /// a journal warning, so the pack renders at the default and the author's
    /// typo is invisible. Distinct from the daemon-only-token lint the sibling
    /// file covers, which is about a token that IS recognised.
    void anUnrecognisedBufferTokenIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(QStringLiteral("sf-vocab"), QJsonArray{});
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("bufferShaders"), QJsonArray{QStringLiteral("builtin:gaussian-h")});
        obj.insert(QStringLiteral("bufferWrap"), QStringLiteral("wrap-around"));
        obj.insert(QStringLiteral("bufferFilter"), QStringLiteral("trilinear"));
        const PackResult r = validateSurface(tmp, QStringLiteral("sf-vocab"), obj, surfaceBodyReading({}));
        QVERIFY2(r.report.contains(QStringLiteral("wrap-around")), qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("trilinear")), qPrintable(r.report));
    }

    /// The SINGLE bufferScale, out of range at both ends. The per-pass list has
    /// its own slot in the sibling file; this is the pack-wide scalar, which is
    /// what every pass falls back to.
    void aSingleBufferScaleOutOfRangeIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        const auto runWithScale = [&tmp](const QString& name, double scale) {
            QJsonObject obj = surfacePack(name, QJsonArray{});
            obj.insert(QStringLiteral("multipass"), true);
            obj.insert(QStringLiteral("bufferShaders"), QJsonArray{QStringLiteral("builtin:gaussian-h")});
            obj.insert(QStringLiteral("bufferScale"), scale);
            return validateSurface(tmp, name, obj, surfaceBodyReading({}));
        };

        // KEYED ON THE RANGE LINT'S OWN WORDS, not on the bare key name. A bare
        // `contains("bufferScale")` is satisfied by the NOT-A-NUMBER lint too, so
        // it would pass on a pack whose range check never ran.
        const PackResult low = runWithScale(QStringLiteral("sf-scale-low"), 0.0001);
        QVERIFY2(low.report.contains(QStringLiteral("bufferScale out of range")), qPrintable(low.report));
        const PackResult high = runWithScale(QStringLiteral("sf-scale-high"), 4.0);
        QVERIFY2(high.report.contains(QStringLiteral("bufferScale out of range")), qPrintable(high.report));

        // A legal scale draws nothing, so neither arm is firing on the key's mere
        // presence.
        //
        // THIS CONTROL WAS VACUOUS. It asserted the absence of "bufferScale value",
        // a string this tree emits NOWHERE, so it could not fail and proved none of
        // what the sentence above claims. It is the exact trap this file's header
        // warns about, which is worth leaving on the record rather than quietly
        // correcting.
        const PackResult ok = runWithScale(QStringLiteral("sf-scale-ok"), 0.5);
        QVERIFY2(!ok.report.contains(QStringLiteral("bufferScale out of range")), qPrintable(ok.report));
    }

    /// A declared stage that does not exist. The fragment is the pack's required
    /// stage and the vertex is optional, so they fail differently and both are
    /// worth pinning.
    void aDeclaredButAbsentStageIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        // FRAGMENT declared as a name nothing writes. validateSurface always
        // writes effect.frag, so the metadata names something else.
        {
            QJsonObject obj = surfacePack(QStringLiteral("sf-nofrag"), QJsonArray{});
            obj.insert(QStringLiteral("fragmentShader"), QStringLiteral("absent.frag"));
            const PackResult r = validateSurface(tmp, QStringLiteral("sf-nofrag"), obj, surfaceBodyReading({}));
            QVERIFY2(r.errors > 0, qPrintable(r.report));
        }
        // VERTEX declared and absent: the pack is otherwise fine, so this is
        // the lint on its own rather than a cascade.
        {
            QJsonObject obj = surfacePack(QStringLiteral("sf-novert"), QJsonArray{});
            obj.insert(QStringLiteral("vertexShader"), QStringLiteral("absent.vert"));
            const PackResult r = validateSurface(tmp, QStringLiteral("sf-novert"), obj, surfaceBodyReading({}));
            QVERIFY2(r.report.contains(QStringLiteral("vertex shader missing")), qPrintable(r.report));
        }
    }

    /// A vertexShader path escaping the pack directory takes the EARLY-EXIT
    /// branch, which returns before any other lint runs. The sibling file covers
    /// the fragment and bufferShaders early exits; this is the third.
    void aVertexPathEscapingThePackIsRefused()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(QStringLiteral("sf-vert-escape"), QJsonArray{});
        obj.insert(QStringLiteral("vertexShader"), QStringLiteral("../../../etc/passwd"));
        const PackResult r = validateSurface(tmp, QStringLiteral("sf-vert-escape"), obj, surfaceBodyReading({}));
        QVERIFY2(r.report.contains(QStringLiteral("vertexShader path escapes the pack directory")),
                 qPrintable(r.report));
        QCOMPARE(r.errors, 1);
    }

    /// A pack missing a REQUIRED field, and metadata that is not readable or not
    /// JSON at all. These are the validator's three earliest exits and none of
    /// them had a slot, so the whole entry path could have been deleted.
    void theMetadataEntryFailuresEachFire()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        // No fragmentShader key at all.
        {
            QJsonObject obj;
            obj.insert(QStringLiteral("id"), QStringLiteral("sf-noid"));
            obj.insert(QStringLiteral("name"), QStringLiteral("sf-noid"));
            const PackResult r = validateSurface(tmp, QStringLiteral("sf-noid"), obj, surfaceBodyReading({}));
            QVERIFY2(r.report.contains(QStringLiteral("missing required field")), qPrintable(r.report));
        }
        // Not JSON.
        {
            const QString dir = tmp.filePath(QStringLiteral("sf-badjson"));
            QVERIFY(writePackFile(dir, QStringLiteral("metadata.json"), "{ this is not json"));
            QVERIFY(writePackFile(dir, QStringLiteral("effect.frag"), surfaceBodyReading({}).toUtf8()));
            QString report;
            QTextStream stream(&report);
            const int errors = PlasmaZones::ShaderValidate::validateSurfacePack(dir, stream);
            stream.flush();
            QVERIFY2(report.contains(QStringLiteral("invalid JSON")), qPrintable(report));
            QCOMPARE(errors, 1);
        }
        // No metadata.json at all.
        {
            const QString dir = tmp.filePath(QStringLiteral("sf-nometa"));
            QVERIFY(QDir().mkpath(dir));
            QString report;
            QTextStream stream(&report);
            const int errors = PlasmaZones::ShaderValidate::validateSurfacePack(dir, stream);
            stream.flush();
            QVERIFY2(report.contains(QStringLiteral("cannot read metadata.json")), qPrintable(report));
            QCOMPARE(errors, 1);
        }
    }

    /// An include the tree cannot resolve fails EXPANSION rather than compilation,
    /// which is a different report line and a different exit. Without this the
    /// expansion-failure branch is unreachable from the suite.
    void anUnresolvableIncludeReportsExpansionFailure()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        const QJsonObject obj = surfacePack(QStringLiteral("sf-badinc"), QJsonArray{});
        const PackResult r =
            validateSurface(tmp, QStringLiteral("sf-badinc"), obj,
                            QStringLiteral("#include <no_such_header.glsl>\n") + surfaceBodyReading({}));
        QVERIFY2(r.report.contains(QStringLiteral("include expansion failed")), qPrintable(r.report));
    }

    /// A buffer pass that does not compile. The fragment bakes are covered in the
    /// sibling file; a BUFFER pass is a separate loop with its own report label,
    /// and nothing exercised its failure path.
    void aBrokenBufferPassIsReported()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(QStringLiteral("sf-badbuf"), QJsonArray{});
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("bufferShaders"), QJsonArray{QStringLiteral("pass0.frag")});
        const QString dir = tmp.filePath(QStringLiteral("sf-badbuf"));
        QVERIFY(writePackFile(dir, QStringLiteral("metadata.json"), QJsonDocument(obj).toJson()));
        QVERIFY(writePackFile(dir, QStringLiteral("effect.frag"), surfaceBodyReading({}).toUtf8()));
        // A buffer pass ships its own main() and gets no generated preamble, so
        // it carries its own #version. The undeclared identifier is the break.
        QVERIFY(writePackFile(dir, QStringLiteral("pass0.frag"),
                              "#version 450\n"
                              "layout(location = 0) out vec4 fragColor;\n"
                              "void main() { fragColor = vec4(notDeclaredAnywhere); }\n"));

        QString report;
        QTextStream stream(&report);
        const int errors = PlasmaZones::ShaderValidate::validateSurfacePack(dir, stream);
        stream.flush();
        QVERIFY2(reportLineHas(report, QStringLiteral("pass0.frag"), QStringLiteral("ERROR")), qPrintable(report));
        QVERIFY2(errors > 0, qPrintable(report));
    }
};

QTEST_MAIN(TestSurfacePackLints)
#include "test_surface_pack_lints.moc"
