// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The offline pack validator's SURFACE arm, which had no test harness at all.
//
// That absence was structural rather than accidental. The validator has four
// production arms and, before this file, three test executables: animation and
// overlay share test_pack_validators.cpp, pointer has its own, and surface had
// none. Each executable compiles ALL FOUR arms, so a lint removed from the
// surface arm alone broke no test and failed no link either — nothing in the
// topology made a missing family visible. The split that produced it was made
// when the shared file passed the file-size ceiling, so the cut followed line
// count rather than the family boundary.
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
PackResult validateSurface(const QTemporaryDir& tmp, const QString& name, const QJsonObject& metadata,
                           const QString& body)
{
    const QString dir = tmp.filePath(name);
    if (!writePackFile(dir, QStringLiteral("metadata.json"), QJsonDocument(metadata).toJson())) {
        return fixtureFailure(QStringLiteral("failed to write metadata.json under ") + dir);
    }
    if (!writePackFile(dir, QStringLiteral("effect.frag"), body.toUtf8())) {
        return fixtureFailure(QStringLiteral("failed to write effect.frag under ") + dir);
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
    /// building this harness: three of the four arms went unexercised, so the
    /// lint could have been deleted from this one without a single test noticing.
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

    /// The preset lint runs on THIS arm too.
    ///
    /// `reportPresetProblems` is wired into all four validator arms and every test
    /// that exercised it drove the animation or overlay one, so deleting the call from
    /// the surface arm left the whole suite green — the same family-blindness this
    /// file exists to end.
    void theSurfaceArmLintsPresetsToo()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj =
            surfacePack(QStringLiteral("sf-presets"),
                        QJsonArray{surfaceParam(QStringLiteral("speed"), QStringLiteral("float"), 1.0, 0.0, 2.0)});
        QJsonObject presets;
        presets.insert(QStringLiteral("Undeclared"), QJsonObject{{QStringLiteral("noSuchThing"), 1.0}});
        presets.insert(QStringLiteral("TooFast"), QJsonObject{{QStringLiteral("speed"), 99.0}});
        presets.insert(QStringLiteral("Tidy"), QJsonObject{{QStringLiteral("speed"), 1.5}});
        obj.insert(QStringLiteral("presets"), presets);

        const PackResult r =
            validateSurface(tmp, QStringLiteral("sf-presets"), obj, surfaceBodyReading({QStringLiteral("speed")}));
        QVERIFY2(r.report.contains(QStringLiteral("presets        ERROR")), qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("which the pack does not declare")), qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("above its declared maximum")), qPrintable(r.report));
        // The well-formed preset beside them is silent, so neither check is firing on
        // every value it sees.
        QVERIFY2(!r.report.contains(QStringLiteral("preset 'Tidy'")), qPrintable(r.report));
        QVERIFY(r.errors >= 2);
    }
};

QTEST_MAIN(TestSurfacePackValidator)
#include "test_surface_pack_validator.moc"
