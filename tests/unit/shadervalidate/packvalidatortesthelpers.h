// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fixture helpers shared by the offline pack validator's test executables
// (test_pack_validators, test_animation_pack_bakes, test_pack_model_detection).
// Header-only and `inline` so each executable carries one definition and no
// test-only library has to exist for three small binaries.

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
