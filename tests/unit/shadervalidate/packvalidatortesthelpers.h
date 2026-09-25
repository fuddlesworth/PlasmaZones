// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fixture helpers shared by the offline pack validator's five test executables:
// test_pack_validators, test_pointer_pack_validator, test_surface_pack_validator,
// test_animation_pack_bakes and test_pack_model_detection.
// Header-only and `inline` so each executable carries one definition and no
// test-only library has to exist for five small binaries.

#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>

#include <QtTest>

#include "shadervalidate/packvalidatorcommon.h"
#include "shadervalidate/packvalidators.h"

namespace PackValidatorTest {

struct PackResult
{
    int errors = 0;
    QString report;
};

/// A result that can only come from the FIXTURE failing to write, never from
/// the validator: a negative count and a report that names the fixture. Every
/// pack-writing helper returns this rather than a default `PackResult`, whose
/// zero-error empty report would satisfy any "does not contain" assertion.
inline PackResult fixtureFailure(const QString& what)
{
    PackResult failed;
    failed.errors = -1;
    failed.report = QStringLiteral("FIXTURE: ") + what;
    return failed;
}

/// Link @p target in as the fixture root's `shared/`, the directory
/// includePathsFor searches first for a pack at `<tmp>/<name>`.
///
/// An existing link is accepted only when it already points AT @p target. The
/// animation and pointer families both want this one path, so a translation
/// unit that used both would otherwise get `true` back from a link aimed at
/// the other family's helpers, and its includes would fall through to the
/// INSTALLED tree — silently reintroducing the very drift this exists to
/// close. Returning false there makes the caller's QSKIP fire loudly instead.
inline bool linkSharedInto(const QTemporaryDir& tmp, const QString& target)
{
    if (!QDir(target).exists()) {
        return false;
    }
    const QString link = tmp.filePath(QStringLiteral("shared"));
    const QFileInfo info(link);
    if (info.exists() || info.isSymLink()) {
        return QFileInfo(info.symLinkTarget()).canonicalFilePath() == QFileInfo(target).canonicalFilePath();
    }
    return QFile::link(target, link);
}

/// The validator derives its include path from the pack's PARENT directory
/// (`<packs-root>/shared`), matching the animation runtime. A temp packs-root
/// has no such directory, so every fragment stage would fail include
/// expansion for reasons that have nothing to do with the pack under test.
/// Link the real bundled one in once per root.
///
/// Returns false when the source tree is not available, which is the caller's
/// cue to skip rather than fail.
inline bool linkSharedIncludes(const QTemporaryDir& tmp)
{
    const QString target = QStringLiteral(P_SOURCE_DIR "/data/animations/shared");
    return linkSharedInto(tmp, target);
}

/// The pointer twin of `linkSharedIncludes`. The pointer entry prologue always
/// emits `#include <pointer_lib.glsl>`, and `includePathsFor` searches the
/// pack's PARENT directory and its `shared/` sibling before falling back to the
/// INSTALLED tree under the XDG data dirs. A temp packs-root has neither, so
/// without this every pointer fixture silently resolved the helpers from
/// whatever version happened to be installed on the machine — which meant the
/// suite baked against a different `shared/` than the one in the working tree,
/// and passed while the branch's own helper changes went unexercised.
///
/// Returns false when the source tree is not available, which is the caller's
/// cue to skip rather than fail.
inline bool linkPointerSharedIncludes(const QTemporaryDir& tmp)
{
    const QString target = QStringLiteral(P_SOURCE_DIR "/data/pointer/shared");
    return linkSharedInto(tmp, target);
}

/// The surface twin. Same reason as the pointer one: the surface validator bakes
/// every pack's fragment through glslang and resolves includes from the pack's
/// parent and its `shared/` sibling before the installed tree, so without this a
/// fixture bakes against whatever is INSTALLED rather than the working tree.
///
/// Returns false when the source tree is not available, which is the caller's
/// cue to skip rather than fail.
inline bool linkSurfaceSharedIncludes(const QTemporaryDir& tmp)
{
    const QString target = QStringLiteral(P_SOURCE_DIR "/data/surface/shared");
    return linkSharedInto(tmp, target);
}

/// The overlay twin, for a fixture whose stage `#include`s one of the overlay
/// shared headers rather than standing alone.
///
/// Most overlay slots here test METADATA lints and never need this: the fixture
/// writes a bare `pZone` body, the scaffold supplies what it needs, and nothing is
/// included. It exists for the slots that have to compile a shared header the
/// bundled packs do not reach, which is the only way those headers get baked at
/// all.
inline bool linkOverlaySharedIncludes(const QTemporaryDir& tmp)
{
    const QString target = QStringLiteral(P_SOURCE_DIR "/data/overlays/shared");
    return linkSharedInto(tmp, target);
}

/// Write @p body to @p file inside the pack directory @p dir. Returns false
/// when the write fails or is short, for the caller to QVERIFY: a QVERIFY
/// inside a lambda only returns from the lambda, so a failed fixture write
/// used to let a test run on and fail somewhere misleading.
inline bool writePackFile(const QString& dir, const QString& file, const QByteArray& body)
{
    QDir().mkpath(dir);
    QFile f(dir + QLatin1Char('/') + file);
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    const bool complete = f.write(body) == body.size();
    f.close();
    return complete && f.error() == QFile::NoError;
}

/// Write a pack directory from @p metadata (plus a trivial fragment shader
/// unless the caller declared its own) and run the animation validator over
/// it, capturing the report.
inline PackResult validate(const QTemporaryDir& tmp, const QString& name, const QJsonObject& metadata,
                           bool writeFragment = true)
{
    const QString dir = tmp.filePath(name);
    if (!writePackFile(dir, QStringLiteral("metadata.json"), QJsonDocument(metadata).toJson())) {
        return fixtureFailure(QStringLiteral("failed to write metadata.json under ") + dir);
    }
    if (writeFragment
        && !writePackFile(dir, QStringLiteral("effect.frag"),
                          "vec4 pTransition(vec2 uv, float t) { return vec4(0.0); }\n")) {
        return fixtureFailure(QStringLiteral("failed to write effect.frag under ") + dir);
    }

    PackResult result;
    QTextStream stream(&result.report);
    result.errors = PlasmaZones::ShaderValidate::validateAnimationPack(dir, stream);
    stream.flush();
    return result;
}

/// A minimal animation pack: id, name, and `effect.frag` as the fragment.
inline QJsonObject basePack(const QString& id)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("name"), QStringLiteral("Test Pack"));
    obj.insert(QStringLiteral("fragmentShader"), QStringLiteral("effect.frag"));
    return obj;
}

/// One animation parameter declaration.
inline QJsonObject animationParam(const QString& id, const QString& type, const QJsonValue& def)
{
    QJsonObject param;
    param.insert(QStringLiteral("id"), id);
    param.insert(QStringLiteral("name"), id);
    param.insert(QStringLiteral("type"), type);
    param.insert(QStringLiteral("default"), def);
    return param;
}

inline QJsonArray toArray(const QStringList& values)
{
    QJsonArray arr;
    for (const QString& v : values) {
        arr.append(v);
    }
    return arr;
}

// ── surface fixture writers ─────────────────────────────────────────────
// Shared by the surface validator's two test executables. They were file-local
// to the first one until the second needed the same writers: a per-lint negative
// slot is only cheap when the fixture is, and two copies of a fixture writer is
// how two test files start disagreeing about what a valid pack looks like.

/// The surface twin of `validate`. Writes the pack plus a `pSurface` entry body,
/// which the validator assembles into a full TU exactly as the daemon and the
/// compositor do.
/// @p vertBody, when given, is written under the name @p metadata declares in
/// `vertexShader`, so the fixture cannot drift from what the pack claims to ship.
inline PackResult validateSurface(const QTemporaryDir& tmp, const QString& name, const QJsonObject& metadata,
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
inline QJsonObject surfaceParam(const QString& id, const QString& type, const QJsonValue& def, double min, double max)
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

inline QJsonObject surfacePack(const QString& id, const QJsonArray& params)
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
inline QString surfaceBodyReading(const QStringList& ids)
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
inline QString packVertexBody(const QString& assign)
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
inline bool reportLineHas(const QString& report, const QString& stage, const QString& marker)
{
    const QStringList lines = report.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        if (line.contains(stage) && line.contains(marker)) {
            return true;
        }
    }
    return false;
}

} // namespace PackValidatorTest

/// The preconditions every slot that runs the ANIMATION validator shares. The
/// validator bakes every animation pack's fragment for the compositor through
/// glslang, unconditionally, so a slot that only wanted a metadata lint still
/// fails (or, worse, passes on the tool-missing error alone) on a machine
/// without it; and it resolves includes through the sibling `shared/`, which a
/// temp root only has once the bundled one is linked in. A macro because
/// QSKIP returns from the CALLING slot, which a helper function cannot do.
#define REQUIRE_ANIMATION_FIXTURE(tmp)                                                                                 \
    QVERIFY((tmp).isValid());                                                                                          \
    if (PlasmaZones::ShaderValidate::glslangValidatorPath().isEmpty()) {                                               \
        QSKIP("glslangValidator not on PATH");                                                                         \
    }                                                                                                                  \
    if (!PackValidatorTest::linkSharedIncludes(tmp)) {                                                                 \
        QSKIP("data/animations/shared not found — running outside source tree");                                       \
    }

/// The pointer twin of `REQUIRE_ANIMATION_FIXTURE`, and it exists for a sharper
/// reason than symmetry. The pointer validator bakes every stage through
/// glslang twice, and the entry prologue's `#include <pointer_lib.glsl>`
/// resolves from the INSTALLED tree when the temp root has no `shared/`. A slot
/// without this therefore tested whatever helpers were installed rather than
/// the ones in the working tree, and a machine with no install failed on the
/// include instead of on the lint under test.
#define REQUIRE_POINTER_FIXTURE(tmp)                                                                                   \
    QVERIFY((tmp).isValid());                                                                                          \
    if (PlasmaZones::ShaderValidate::glslangValidatorPath().isEmpty()) {                                               \
        QSKIP("glslangValidator not on PATH");                                                                         \
    }                                                                                                                  \
    if (!PackValidatorTest::linkPointerSharedIncludes(tmp)) {                                                          \
        QSKIP("data/pointer/shared not found — running outside source tree");                                          \
    }

/// The surface twin, and the reason it did not exist until now is the finding it
/// closes: the SURFACE arm of the validator had no test harness at all. Four
/// production arms (animation, pointer, surface, overlay) and, before this macro,
/// four of the five executables listed at the top of this file, between them
/// reaching only three of those arms. Each executable compiles all four, so a
/// lint deleted from the surface arm alone
/// broke no test and failed no link. The topology was an artifact of the file-size
/// ceiling rather than of the family boundary, which is why the gap went unnoticed.
///
/// Requires glslangValidator, exactly as the pointer twin above does, and for the
/// same reason. The surface arm bakes BOTH branches every surface pack ships on:
/// the daemon branch through `ShaderCompiler::compile` (QShaderBaker, Qt's vendored
/// glslang), and the compositor branch by shelling out to the binary, because
/// QShaderBaker wants Vulkan-dialect GLSL and rejects the default-block uniforms
/// that branch declares.
///
/// Skipping rather than failing is deliberate and is the safer degrade here. The
/// slots asserting `errors == 0` would break outright without the tool, and the
/// ones asserting a threshold would otherwise PASS on the tool-missing error
/// alone, which is worse than failing because it reads as coverage while testing
/// nothing.
#define REQUIRE_SURFACE_FIXTURE(tmp)                                                                                   \
    QVERIFY((tmp).isValid());                                                                                          \
    if (PlasmaZones::ShaderValidate::glslangValidatorPath().isEmpty()) {                                               \
        QSKIP("glslangValidator not on PATH");                                                                         \
    }                                                                                                                  \
    if (!PackValidatorTest::linkSurfaceSharedIncludes(tmp)) {                                                          \
        QSKIP("data/surface/shared not found — running outside source tree");                                          \
    }
