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
    if (!QDir(target).exists()) {
        return false;
    }
    const QString link = tmp.filePath(QStringLiteral("shared"));
    if (QFileInfo::exists(link)) {
        return true;
    }
    return QFile::link(target, link);
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
