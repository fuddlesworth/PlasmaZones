// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The offline pack validator's POINTER arm. Split out of
// test_pack_validators.cpp, which covers the animation and overlay models and
// had grown past the file-size ceiling once the pointer rules earned the
// coverage they were missing.
//
// The bundled-pack gate (shader_validate_pointer) only proves the shipped
// packs are clean; it cannot show that a BROKEN pack is actually caught, which
// is how a pack with a speed gate the preview could never open shipped
// invisible. These slots build deliberately-broken packs in a temp dir and
// assert the diagnostic.
//
// Every slot needs glslang and the bundled shared/ helpers linked into the
// fixture root (REQUIRE_POINTER_FIXTURE): the validator bakes every pointer
// stage for the compositor, and the entry prologue includes pointer_lib.glsl
// unconditionally, so without the link a fixture silently bakes against
// whatever is INSTALLED rather than the working tree.

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

#include <PhosphorPointer/PointerShaderContract.h>

#include "packvalidatortesthelpers.h"

using namespace PackValidatorTest;

namespace {

/// Every fixture in a slot shares one QTemporaryDir, and includePathsFor
/// makes that shared root an ANGLE-include root for all of them (it is the
/// pack's parent). So a file written at the tmp root, rather than inside a
/// pack directory, is angle-includable from every other fixture in the same
/// slot. Nothing relies on that today; a case testing include isolation would
/// need its own QTemporaryDir.
///
/// The pointer twin of `validate`. Fixtures call the REAL `pointerSpeedGate`
/// out of pointer_lib.glsl, which REQUIRE_POINTER_FIXTURE links in from the
/// working tree: the entry prologue includes the helpers unconditionally, so a
/// fixture that defined its own stub collided with the real body the moment
/// the include actually resolved. That collision is why the stub existed at
/// all, and why the pointer slots were silently baking against whatever
/// helpers happened to be installed instead of the branch's.
///
/// The scan's definition-versus-call distinction is exercised by the
/// `pt-gate-lookalike` case rather than by a stub here.
PackResult validatePointer(const QTemporaryDir& tmp, const QString& name, const QJsonObject& metadata,
                           const QString& body)
{
    const QString dir = tmp.filePath(name);
    if (!writePackFile(dir, QStringLiteral("metadata.json"), QJsonDocument(metadata).toJson())) {
        return fixtureFailure(QStringLiteral("failed to write metadata.json under ") + dir);
    }
    const QByteArray frag = body.toUtf8();
    if (!writePackFile(dir, QStringLiteral("effect.frag"), frag)) {
        return fixtureFailure(QStringLiteral("failed to write effect.frag under ") + dir);
    }

    PackResult result;
    QTextStream stream(&result.report);
    result.errors = PlasmaZones::ShaderValidate::validatePointerPack(dir, stream);
    stream.flush();
    return result;
}

/// One pointer parameter declaration.
QJsonObject pointerParam(const QString& id, const QString& type, double def, double min, double max)
{
    QJsonObject param;
    param.insert(QStringLiteral("id"), id);
    param.insert(QStringLiteral("name"), id);
    param.insert(QStringLiteral("type"), type);
    param.insert(QStringLiteral("default"), def);
    param.insert(QStringLiteral("min"), min);
    param.insert(QStringLiteral("max"), max);
    return param;
}

/// A minimal pointer pack with the given parameters.
QJsonObject pointerPack(const QString& id, const QJsonArray& params)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("name"), id);
    obj.insert(QStringLiteral("fragmentShader"), QStringLiteral("effect.frag"));
    obj.insert(QStringLiteral("trailSeconds"), 1.0);
    // Fixture bodies draw from the parameters, not from uPointerTrail, so the
    // honest declaration is false. Without it every fixture carries the
    // samplesTrail lint and the clean-pack assertion could never be clean.
    obj.insert(QStringLiteral("samplesTrail"), false);
    obj.insert(QStringLiteral("parameters"), params);
    return obj;
}

/// A pointer pack declaring one float parameter `activationSpeed` at `def`.
QJsonObject pointerPackWithGate(const QString& id, double def)
{
    return pointerPack(
        id, QJsonArray{pointerParam(QStringLiteral("activationSpeed"), QStringLiteral("float"), def, 0.0, 2000.0)});
}

/// A pointer body that reads every scalar parameter in @p ids, so the
/// declared-but-unread sweep stays quiet and the lint under test is the only
/// thing in the report about that parameter.
QString pointerBodyReadingScalars(const QStringList& ids)
{
    QString body = QStringLiteral("vec4 pPointer(vec2 uv) {\n    float acc = 0.0;\n");
    for (const QString& id : ids) {
        body += QStringLiteral("    acc += float(p_") + id + QStringLiteral(");\n");
    }
    body += QStringLiteral("    return vec4(acc);\n}\n");
    return body;
}

/// Write @p body as a buffer pass file in the pointer pack @p name. Returns
/// false when the write fails, for the caller to QVERIFY.
bool writePointerBuffer(const QTemporaryDir& tmp, const QString& name, const QString& file, const QString& body)
{
    return writePackFile(tmp.filePath(name), file, body.toUtf8());
}

} // namespace

class TestPointerPackValidator : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// A pointer pack whose speed-gate DEFAULT sits above the preview
    /// pointer's peak draws nothing on the whole preview lap, so it shows an
    /// empty stage in the browser where packs are chosen. That is how the
    /// windtrail pack shipped: it loaded, it compiled, every metadata lint
    /// passed, and it rendered nothing. No lint could see it, because every
    /// other lint here asks whether the loader had to repair the metadata,
    /// and nothing had to be repaired.
    ///
    /// Both directions are asserted. A test that only checked the bad default
    /// would still pass if the lint were widened to fire on everything, which
    /// would be worse than no lint at all.
    void speedGateDefaultAbovePreviewPeakIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_POINTER_FIXTURE(tmp);

        const QString body = QStringLiteral(
            "vec4 pPointer(vec2 uv) {\n"
            "    float g = pointerSpeedGate(uPointerVelocity.z, p_activationSpeed);\n"
            "    return vec4(g);\n"
            "}\n");
        const QLatin1String marker("previews as an empty stage");

        {
            // 400 was windtrail's shipped default. smoothstep(400, 800, 324)
            // is exactly 0, so the gate never opened at any point of the lap.
            const PackResult r = validatePointer(tmp, QStringLiteral("pt-shut"),
                                                 pointerPackWithGate(QStringLiteral("pt-shut"), 400.0), body);
            QVERIFY2(r.report.contains(marker), qPrintable(r.report));
            QVERIFY2(r.report.contains(QStringLiteral("activationSpeed")), qPrintable(r.report));
        }
        {
            // The value windtrail now ships. smoothstep(120, 240, 324) is 1.
            const PackResult r = validatePointer(tmp, QStringLiteral("pt-open"),
                                                 pointerPackWithGate(QStringLiteral("pt-open"), 120.0), body);
            QVERIFY2(!r.report.contains(marker), qPrintable(r.report));
        }
        {
            // 0 is the documented "no threshold" case: pointerSpeedGate
            // short-circuits to 1.0, which is what the packs shipping the
            // parameter at 0 rely on to keep their old behaviour.
            const PackResult r = validatePointer(tmp, QStringLiteral("pt-zero"),
                                                 pointerPackWithGate(QStringLiteral("pt-zero"), 0.0), body);
            QVERIFY2(!r.report.contains(marker), qPrintable(r.report));
        }
        {
            // The pointerActivationGate() wrapper is what a pack whose
            // threshold defaults to 0 calls; a pack that moves to it must not
            // fall out of this lint's coverage, since windtrail (default 120)
            // is exactly the shipped shape it was written for.
            QJsonObject obj = pointerPackWithGate(QStringLiteral("pt-wrapper-shut"), 400.0);
            const PackResult r =
                validatePointer(tmp, QStringLiteral("pt-wrapper-shut"), obj,
                                QStringLiteral("float pointerActivationGate(float a) {\n"
                                               "    return a > 0.0 ? pointerSpeedGate(uPointerVelocity.z, a) : 1.0;\n"
                                               "}\n"
                                               "vec4 pPointer(vec2 uv) {\n"
                                               "    return vec4(pointerActivationGate(p_activationSpeed));\n"
                                               "}\n"));
            QVERIFY2(r.report.contains(marker), qPrintable(r.report));
            QVERIFY2(r.report.contains(QStringLiteral("activationSpeed")), qPrintable(r.report));
        }
        {
            // A pack that never gates on speed must never be linted for it,
            // however high a numeric parameter of its own happens to default.
            QJsonObject obj = pointerPackWithGate(QStringLiteral("pt-ungated"), 900.0);
            const PackResult r = validatePointer(tmp, QStringLiteral("pt-ungated"), obj,
                                                 QStringLiteral("vec4 pPointer(vec2 uv) {\n"
                                                                "    return vec4(p_activationSpeed * 0.001);\n"
                                                                "}\n"));
            QVERIFY2(!r.report.contains(marker), qPrintable(r.report));
        }
        {
            // A call that survives only in a comment is not a call. Without
            // the comment strip this fixture would be linted.
            QJsonObject obj = pointerPackWithGate(QStringLiteral("pt-comment"), 900.0);
            const PackResult r =
                validatePointer(tmp, QStringLiteral("pt-comment"), obj,
                                QStringLiteral("vec4 pPointer(vec2 uv) {\n"
                                               "    // pointerSpeedGate(uPointerVelocity.z, p_activationSpeed)\n"
                                               "    return vec4(p_activationSpeed * 0.001);\n"
                                               "}\n"));
            QVERIFY2(!r.report.contains(marker), qPrintable(r.report));
        }
    }

    /// The pointer arm's metadata lints, one fixture each. Every one of these
    /// fields is silently repaired by PointerShaderEffect::fromJson (an unknown
    /// layer falls back to below, reach and trailSeconds are clamped, a
    /// reachParam naming nothing numeric is ignored), so without a fixture per
    /// lint a repair that quietly widened would take the diagnostic with it
    /// and the bundled-pack gate would stay green. Each asserts the
    /// diagnostic's text, not just an error count, so a lint firing for the
    /// wrong reason is not mistaken for the right one.
    void pointerMetadataLintsCatchEachSilentlyRepairedField()
    {
        QTemporaryDir tmp;
        REQUIRE_POINTER_FIXTURE(tmp);

        const QString body = pointerBodyReadingScalars({QStringLiteral("activationSpeed")});
        {
            QJsonObject obj = pointerPackWithGate(QStringLiteral("pt-layer"), 0.0);
            obj.insert(QStringLiteral("layer"), QStringLiteral("between"));
            const PackResult r = validatePointer(tmp, QStringLiteral("pt-layer"), obj, body);
            QVERIFY2(r.report.contains(QStringLiteral("layer must be \"below\" or \"above\": between")),
                     qPrintable(r.report));
        }
        {
            QJsonObject obj = pointerPackWithGate(QStringLiteral("pt-reach-range"), 0.0);
            obj.insert(QStringLiteral("reach"), 4096.0);
            const PackResult r = validatePointer(tmp, QStringLiteral("pt-reach-range"), obj, body);
            QVERIFY2(r.report.contains(QStringLiteral("reach out of range [0, 1024]: 4096")), qPrintable(r.report));
        }
        {
            QJsonObject obj = pointerPackWithGate(QStringLiteral("pt-reach-undeclared"), 0.0);
            obj.insert(QStringLiteral("reachParam"), QStringLiteral("radius"));
            const PackResult r = validatePointer(tmp, QStringLiteral("pt-reach-undeclared"), obj, body);
            QVERIFY2(r.report.contains(QStringLiteral("reachParam 'radius' names no declared parameter")),
                     qPrintable(r.report));
        }
        {
            QJsonObject tint;
            tint.insert(QStringLiteral("id"), QStringLiteral("tint"));
            tint.insert(QStringLiteral("name"), QStringLiteral("tint"));
            tint.insert(QStringLiteral("type"), QStringLiteral("color"));
            tint.insert(QStringLiteral("default"), QStringLiteral("#ffffffff"));
            QJsonObject obj = pointerPack(QStringLiteral("pt-reach-color"), QJsonArray{tint});
            obj.insert(QStringLiteral("reachParam"), QStringLiteral("tint"));
            const PackResult r = validatePointer(tmp, QStringLiteral("pt-reach-color"), obj,
                                                 QStringLiteral("vec4 pPointer(vec2 uv) { return p_tint; }\n"));
            QVERIFY2(r.report.contains(QStringLiteral("reachParam 'tint' has type 'color'")), qPrintable(r.report));
        }
        {
            QJsonObject obj = pointerPack(
                QStringLiteral("pt-reach-max"),
                QJsonArray{pointerParam(QStringLiteral("radius"), QStringLiteral("float"), 64.0, 8.0, 2048.0)});
            obj.insert(QStringLiteral("reachParam"), QStringLiteral("radius"));
            const PackResult r = validatePointer(tmp, QStringLiteral("pt-reach-max"), obj,
                                                 pointerBodyReadingScalars({QStringLiteral("radius")}));
            QVERIFY2(
                r.report.contains(QStringLiteral("reachParam 'radius' allows up to 2048, past the 1024 reach cap")),
                qPrintable(r.report));
        }
        {
            // The floor: a reach the user can drag down to a pixel clips the
            // pack to nothing around the path.
            QJsonObject obj = pointerPack(
                QStringLiteral("pt-reach-min"),
                QJsonArray{pointerParam(QStringLiteral("radius"), QStringLiteral("float"), 64.0, 1.0, 256.0)});
            obj.insert(QStringLiteral("reachParam"), QStringLiteral("radius"));
            const PackResult r = validatePointer(tmp, QStringLiteral("pt-reach-min"), obj,
                                                 pointerBodyReadingScalars({QStringLiteral("radius")}));
            QVERIFY2(r.report.contains(QStringLiteral("reachParam 'radius' allows a minimum of 1 logical px")),
                     qPrintable(r.report));
            QVERIFY2(r.report.contains(QStringLiteral("clips the pack to nothing")), qPrintable(r.report));

            // And a floor at the limit is not linted.
            QJsonObject ok = pointerPack(
                QStringLiteral("pt-reach-min-ok"),
                QJsonArray{pointerParam(QStringLiteral("radius"), QStringLiteral("float"), 64.0, 4.0, 256.0)});
            ok.insert(QStringLiteral("reachParam"), QStringLiteral("radius"));
            const PackResult fine = validatePointer(tmp, QStringLiteral("pt-reach-min-ok"), ok,
                                                    pointerBodyReadingScalars({QStringLiteral("radius")}));
            QVERIFY2(!fine.report.contains(QStringLiteral("allows a minimum of")), qPrintable(fine.report));
        }
        {
            QJsonObject obj = pointerPackWithGate(QStringLiteral("pt-trail"), 0.0);
            obj.insert(QStringLiteral("trailSeconds"), 0.0);
            const PackResult r = validatePointer(tmp, QStringLiteral("pt-trail"), obj, body);
            QVERIFY2(r.report.contains(QStringLiteral("trailSeconds must be positive: 0")), qPrintable(r.report));
        }
        {
            // Declared and never read: the control is dead but nothing at
            // load says so.
            QJsonObject obj = pointerPackWithGate(QStringLiteral("pt-unread"), 0.0);
            const PackResult r = validatePointer(tmp, QStringLiteral("pt-unread"), obj,
                                                 QStringLiteral("vec4 pPointer(vec2 uv) { return vec4(0.0); }\n"));
            QVERIFY2(r.report.contains(QStringLiteral(
                         "parameter 'activationSpeed' is declared but no stage reads p_activationSpeed")),
                     qPrintable(r.report));
        }
        {
            // The reachParam parameter is consumed by the host and handed back
            // as uPointerFlags.y, so a pack that reads it only through
            // pointerReach() (the shape comet, orbit and sparks have) has a
            // live control and must not be told it is dead.
            QJsonObject obj = pointerPack(
                QStringLiteral("pt-reach-via-uniform"),
                QJsonArray{pointerParam(QStringLiteral("radius"), QStringLiteral("float"), 64.0, 4.0, 256.0)});
            obj.insert(QStringLiteral("reachParam"), QStringLiteral("radius"));
            const PackResult r =
                validatePointer(tmp, QStringLiteral("pt-reach-via-uniform"), obj,
                                QStringLiteral("vec4 pPointer(vec2 uv) { return vec4(pointerReach() * 0.0); }\n"));
            QVERIFY2(!r.report.contains(QStringLiteral("parameter 'radius' is declared but no stage reads")),
                     qPrintable(r.report));
            QVERIFY2(!r.report.contains(QStringLiteral("sizes the damage rect but no stage reads")),
                     qPrintable(r.report));
        }
        {
            // The exemption is not blanket: a reachParam the shader reads
            // neither by name nor through pointerReach() is almost certainly
            // mirroring the reach by hand, the drift the helper exists to
            // prevent, and the author is told so.
            QJsonObject obj = pointerPack(
                QStringLiteral("pt-reach-unread"),
                QJsonArray{pointerParam(QStringLiteral("radius"), QStringLiteral("float"), 64.0, 4.0, 256.0)});
            obj.insert(QStringLiteral("reachParam"), QStringLiteral("radius"));
            const PackResult r = validatePointer(tmp, QStringLiteral("pt-reach-unread"), obj,
                                                 QStringLiteral("vec4 pPointer(vec2 uv) { return vec4(0.0); }\n"));
            QVERIFY2(r.report.contains(QStringLiteral(
                         "reachParam 'radius' sizes the damage rect but no stage reads pointerReach() or p_radius")),
                     qPrintable(r.report));
        }
        {
            // An ANGLE include of a pack-local file resolves in the preview
            // (whose expansion looks beside the including file for both
            // forms) and fails on the compositor (registry roots only). The
            // compositor bake expands includes the compositor's way, so the
            // pack is caught here rather than shipping a disabled layer.
            QJsonObject obj = pointerPackWithGate(QStringLiteral("pt-angle-local"), 0.0);
            QVERIFY(writePointerBuffer(tmp, QStringLiteral("pt-angle-local"), QStringLiteral("local_helper.glsl"),
                                       QStringLiteral("const float kLocal = 1.0;\n")));
            const PackResult r = validatePointer(tmp, QStringLiteral("pt-angle-local"), obj,
                                                 QStringLiteral("#include <local_helper.glsl>\n"
                                                                "vec4 pPointer(vec2 uv) {\n"
                                                                "    return vec4(kLocal * p_activationSpeed);\n"
                                                                "}\n"));
            QVERIFY2(r.errors > 0, qPrintable(r.report));
            QVERIFY2(r.report.contains(QStringLiteral("include expansion failed the way the compositor expands it")),
                     qPrintable(r.report));
            // Pinned to the pack's OWN include. The validator emits one message
            // for every expansion failure, so without naming the file this
            // passes on any unresolved include — including the prologue's
            // pointer_lib.glsl, which is exactly how this case stayed green on
            // a machine whose shared/ helpers were never linked in.
            QVERIFY2(r.report.contains(QStringLiteral("local_helper.glsl")), qPrintable(r.report));

            // The quoted form is the pack-local form and passes both bakes.
            QJsonObject quoted = pointerPackWithGate(QStringLiteral("pt-quoted-local"), 0.0);
            QVERIFY(writePointerBuffer(tmp, QStringLiteral("pt-quoted-local"), QStringLiteral("local_helper.glsl"),
                                       QStringLiteral("const float kLocal = 1.0;\n")));
            const PackResult q = validatePointer(tmp, QStringLiteral("pt-quoted-local"), quoted,
                                                 QStringLiteral("#include \"local_helper.glsl\"\n"
                                                                "vec4 pPointer(vec2 uv) {\n"
                                                                "    return vec4(kLocal * p_activationSpeed);\n"
                                                                "}\n"));
            QVERIFY2(!q.report.contains(QStringLiteral("include expansion failed")), qPrintable(q.report));
            // The whole-report assertion the pointer arm was missing. Every
            // other pointer case asserts only that some phrase is ABSENT,
            // which stays green however badly the pack failed for unrelated
            // reasons — so a fixture that stopped resolving its helpers, or a
            // new lint firing on every pack, showed up nowhere. This is the
            // canary for both.
            QVERIFY2(q.errors == 0, qPrintable(q.report));
        }
        {
            // samplesTrail decides whether this pack's trailSeconds gets a say
            // in how the shared history ring is spaced, so both directions of
            // a wrong declaration matter and both are checked against the
            // stage sources.
            //
            // Reading the trail while declaring false: the pack ends up
            // drawing from slots spaced for somebody else.
            QJsonObject reads = pointerPackWithGate(QStringLiteral("pt-trail-understated"), 0.0);
            reads.insert(QStringLiteral("samplesTrail"), false);
            const PackResult u = validatePointer(tmp, QStringLiteral("pt-trail-understated"), reads,
                                                 QStringLiteral("vec4 pPointer(vec2 uv) {\n"
                                                                "    vec4 s = pointerTrailAt(0);\n"
                                                                "    return vec4(s.xy, 0.0, p_activationSpeed);\n"
                                                                "}\n"));
            QVERIFY2(u.report.contains(QStringLiteral("samplesTrail is false but a stage reads the pointer trail")),
                     qPrintable(u.report));

            // And the other way: saying nothing (the default is true) while
            // reading none of it, which is what coarsens every trail pack
            // chained beside a click pack.
            QJsonObject silent = pointerPack(
                QStringLiteral("pt-trail-overstated"),
                QJsonArray{pointerParam(QStringLiteral("activationSpeed"), QStringLiteral("float"), 0.0, 0.0, 2000.0)});
            silent.remove(QStringLiteral("samplesTrail"));
            const PackResult o = validatePointer(tmp, QStringLiteral("pt-trail-overstated"), silent, body);
            QVERIFY2(o.report.contains(QStringLiteral("declare `samplesTrail: false`")), qPrintable(o.report));

            // A stage carrying an #include suppresses the "declare false"
            // arm. The scan reads sources as WRITTEN, so a pack whose trail
            // walk lives in a file it includes reads as touching no trail
            // here; advising that author to declare false would take a pack
            // that really does read the ring out of the spacing decision.
            // Six of the thirteen bundled packs carry an include, and every
            // other fixture in this file is include-free, so without this
            // case the suppression has no coverage at all.
            QJsonObject included = pointerPack(
                QStringLiteral("pt-trail-included"),
                QJsonArray{pointerParam(QStringLiteral("activationSpeed"), QStringLiteral("float"), 0.0, 0.0, 2000.0)});
            included.remove(QStringLiteral("samplesTrail"));
            QVERIFY(writePointerBuffer(tmp, QStringLiteral("pt-trail-included"), QStringLiteral("local_helper.glsl"),
                                       QStringLiteral("const float kLocal = 1.0;\n")));
            const PackResult inc = validatePointer(tmp, QStringLiteral("pt-trail-included"), included,
                                                   QStringLiteral("#include \"local_helper.glsl\"\n"
                                                                  "vec4 pPointer(vec2 uv) {\n"
                                                                  "    return vec4(kLocal * p_activationSpeed);\n"
                                                                  "}\n"));
            QVERIFY2(!inc.report.contains(QStringLiteral("declare `samplesTrail: false`")), qPrintable(inc.report));

            // The trail WINDOW gate. It decides the ring's spacing, so a
            // declaration that names nothing, names the wrong type, or
            // reaches past the pack's own liveness is a real defect rather
            // than a tidiness one.
            QJsonObject missing = pointerPackWithGate(QStringLiteral("pt-window-missing"), 0.0);
            missing.insert(QStringLiteral("trailWindowParam"), QStringLiteral("nosuch"));
            const PackResult wm = validatePointer(tmp, QStringLiteral("pt-window-missing"), missing, body);
            QVERIFY2(wm.report.contains(QStringLiteral("trailWindowParam 'nosuch' names no declared parameter")),
                     qPrintable(wm.report));

            QJsonObject past = pointerPack(
                QStringLiteral("pt-window-past-liveness"),
                QJsonArray{pointerParam(QStringLiteral("lifetime"), QStringLiteral("float"), 0.5, 0.0, 9.0)});
            past.insert(QStringLiteral("trailWindowParam"), QStringLiteral("lifetime"));
            const PackResult wp =
                validatePointer(tmp, QStringLiteral("pt-window-past-liveness"), past,
                                QStringLiteral("vec4 pPointer(vec2 uv) { return vec4(p_lifetime); }\n"));
            QVERIFY2(wp.report.contains(QStringLiteral("reads further back than it stays live for")),
                     qPrintable(wp.report));

            // A window declared on a pack that reads nothing is ignored, and
            // saying both ways is dead weight.
            QJsonObject both = pointerPackWithGate(QStringLiteral("pt-window-both"), 0.0);
            both.insert(QStringLiteral("trailWindowSeconds"), 0.5);
            both.insert(QStringLiteral("trailWindowParam"), QStringLiteral("activationSpeed"));
            const PackResult wb = validatePointer(tmp, QStringLiteral("pt-window-both"), both, body);
            QVERIFY2(wb.report.contains(QStringLiteral("the parameter wins")), qPrintable(wb.report));
            QVERIFY2(wb.report.contains(QStringLiteral("samplesTrail is false, so the declared trail window is "
                                                       "ignored")),
                     qPrintable(wb.report));

            // The honest declaration draws neither lint.
            QJsonObject honest = pointerPackWithGate(QStringLiteral("pt-trail-honest"), 0.0);
            const PackResult h = validatePointer(tmp, QStringLiteral("pt-trail-honest"), honest, body);
            QVERIFY2(!h.report.contains(QStringLiteral("samplesTrail")), qPrintable(h.report));
        }
        {
            // A literal reach under the floor the reachParam arm enforces
            // leaves the damage rect a sliver (empty for a resting pointer),
            // so the pack can never draw.
            QJsonObject obj = pointerPackWithGate(QStringLiteral("pt-reach-tiny"), 0.0);
            obj.insert(QStringLiteral("reach"), 0.0);
            const PackResult r = validatePointer(tmp, QStringLiteral("pt-reach-tiny"), obj, body);
            QVERIFY2(r.report.contains(QStringLiteral("reach 0 is under the 4 logical px floor")),
                     qPrintable(r.report));
            // Exactly at the floor is allowed: the comparison is strict.
            QJsonObject at = pointerPackWithGate(QStringLiteral("pt-reach-floor"), 0.0);
            at.insert(QStringLiteral("reach"), 4.0);
            const PackResult fine = validatePointer(tmp, QStringLiteral("pt-reach-floor"), at, body);
            QVERIFY2(!fine.report.contains(QStringLiteral("is under the")), qPrintable(fine.report));
        }
        {
            // The gate's parameter is found inside an expression too: a pack
            // scaling the threshold itself must not slip out of the lint.
            QJsonObject obj = pointerPackWithGate(QStringLiteral("pt-gate-expr"), 400.0);
            const PackResult r = validatePointer(
                tmp, QStringLiteral("pt-gate-expr"), obj,
                QStringLiteral("vec4 pPointer(vec2 uv) {\n"
                               "    return vec4(pointerSpeedGate(uPointerVelocity.z, p_activationSpeed * 2.0));\n"
                               "}\n"));
            QVERIFY2(r.report.contains(QStringLiteral("previews as an empty stage")), qPrintable(r.report));
            // And in a non-leading position, wrapped in parentheses.
            QJsonObject trailing = pointerPackWithGate(QStringLiteral("pt-gate-trailing"), 400.0);
            const PackResult t = validatePointer(
                tmp, QStringLiteral("pt-gate-trailing"), trailing,
                QStringLiteral("vec4 pPointer(vec2 uv) {\n"
                               "    return vec4(pointerSpeedGate(uPointerVelocity.z, 2.0 * (p_activationSpeed)));\n"
                               "}\n"));
            QVERIFY2(t.report.contains(QStringLiteral("previews as an empty stage")), qPrintable(t.report));
        }
        {
            // A shader that mirrors trailSeconds as kTrailSeconds is held to
            // the metadata, or an edit to one leaves the pack fading against
            // the wrong window.
            QJsonObject obj = pointerPackWithGate(QStringLiteral("pt-trail-mirror"), 0.0);
            obj.insert(QStringLiteral("trailSeconds"), 0.9);
            const PackResult r = validatePointer(tmp, QStringLiteral("pt-trail-mirror"), obj,
                                                 QStringLiteral("const float kTrailSeconds = 0.8;\n"
                                                                "vec4 pPointer(vec2 uv) {\n"
                                                                "    return vec4(kTrailSeconds * p_activationSpeed);\n"
                                                                "}\n"));
            QVERIFY2(r.report.contains(QStringLiteral("kTrailSeconds is 0.8 in the shader but trailSeconds is 0.9")),
                     qPrintable(r.report));
            QJsonObject ok = pointerPackWithGate(QStringLiteral("pt-trail-mirror-ok"), 0.0);
            ok.insert(QStringLiteral("trailSeconds"), 0.8);
            const PackResult fine =
                validatePointer(tmp, QStringLiteral("pt-trail-mirror-ok"), ok,
                                QStringLiteral("const float kTrailSeconds = 0.8;\n"
                                               "vec4 pPointer(vec2 uv) {\n"
                                               "    return vec4(kTrailSeconds * p_activationSpeed);\n"
                                               "}\n"));
            QVERIFY2(!fine.report.contains(QStringLiteral("kTrailSeconds is")), qPrintable(fine.report));
            // The mirror is found in a buffer stage too, and as a #define.
            QJsonObject buf = pointerPackWithGate(QStringLiteral("pt-trail-mirror-buf"), 0.0);
            buf.insert(QStringLiteral("trailSeconds"), 0.9);
            buf.insert(QStringLiteral("multipass"), true);
            buf.insert(QStringLiteral("bufferShaders"), toArray({QStringLiteral("buffer.frag")}));
            QVERIFY(writePointerBuffer(tmp, QStringLiteral("pt-trail-mirror-buf"), QStringLiteral("buffer.frag"),
                                       QStringLiteral("#version 450\n"
                                                      "#define kTrailSeconds 0.8\n"
                                                      "uniform sampler2D iChannel0;\n"
                                                      "layout(location = 0) in vec2 vTexCoord;\n"
                                                      "layout(location = 0) out vec4 fragColor;\n"
                                                      "void main() {\n"
                                                      "    fragColor = texture(iChannel0, vTexCoord) * kTrailSeconds;\n"
                                                      "}\n")));
            const PackResult b =
                validatePointer(tmp, QStringLiteral("pt-trail-mirror-buf"), buf,
                                QStringLiteral("uniform sampler2D iChannel0;\n"
                                               "vec4 pPointer(vec2 uv) {\n"
                                               "    return texture(iChannel0, uv) * p_activationSpeed;\n"
                                               "}\n"));
            QVERIFY2(b.report.contains(QStringLiteral("kTrailSeconds is 0.8 in the shader but trailSeconds is 0.9")),
                     qPrintable(b.report));
        }
        {
            // A pack helper whose name merely ends in the shared gate's name
            // is not the shared gate; the scan is identifier-bounded.
            //
            // The lookalike has to keep the gate's name in its EXACT case, or
            // the case-sensitive indexOf never finds the substring and the
            // left-boundary guard under test is never reached — an assertion
            // that then passes with the guard deleted. `my_` is the boundary
            // character that does the suppressing here.
            QJsonObject obj = pointerPackWithGate(QStringLiteral("pt-gate-lookalike"), 900.0);
            const PackResult r =
                validatePointer(tmp, QStringLiteral("pt-gate-lookalike"), obj,
                                QStringLiteral("float my_pointerSpeedGate(float s, float a) { return s * a; }\n"
                                               "vec4 pPointer(vec2 uv) {\n"
                                               "    return vec4(my_pointerSpeedGate(0.0, p_activationSpeed));\n"
                                               "}\n"));
            QVERIFY2(!r.report.contains(QStringLiteral("previews as an empty stage")), qPrintable(r.report));

            // The positive control for the same scan: an unprefixed call at
            // the same default IS linted, so the case above is demonstrably
            // the guard suppressing it rather than the lint being inert.
            QJsonObject bare = pointerPackWithGate(QStringLiteral("pt-gate-bare"), 900.0);
            const PackResult n = validatePointer(tmp, QStringLiteral("pt-gate-bare"), bare,
                                                 QStringLiteral("vec4 pPointer(vec2 uv) {\n"
                                                                "    return vec4(pointerSpeedGate(uPointerVelocity.z, "
                                                                "p_activationSpeed));\n"
                                                                "}\n"));
            QVERIFY2(n.report.contains(QStringLiteral("previews as an empty stage")), qPrintable(n.report));
        }
    }

    /// The multipass, texture and cursor lints the pointer arm was missing
    /// while the overlay and surface arms had them. A traversal in
    /// bufferShaders is rejected outright, the way every other user-editable
    /// path is; a multipass switch with nothing behind it is diagnosed; and a
    /// texture list past the contract cap is told the surplus is dropped.
    void pointerMultipassAndTextureLintsMatchTheSiblingArms()
    {
        QTemporaryDir tmp;
        REQUIRE_POINTER_FIXTURE(tmp);
        const QString body = pointerBodyReadingScalars({QStringLiteral("activationSpeed")});

        {
            QJsonObject obj = pointerPackWithGate(QStringLiteral("pt-buf-escape"), 0.0);
            obj.insert(QStringLiteral("multipass"), true);
            obj.insert(QStringLiteral("bufferShaders"), toArray({QStringLiteral("../../etc/passwd")}));
            const PackResult r = validatePointer(tmp, QStringLiteral("pt-buf-escape"), obj, body);
            QVERIFY2(r.errors > 0, qPrintable(r.report));
            QVERIFY2(r.report.contains(QStringLiteral("bufferShaders path escapes the pack directory")),
                     qPrintable(r.report));
        }
        {
            QJsonObject obj = pointerPackWithGate(QStringLiteral("pt-mp-empty"), 0.0);
            obj.insert(QStringLiteral("multipass"), true);
            const PackResult r = validatePointer(tmp, QStringLiteral("pt-mp-empty"), obj, body);
            QVERIFY2(r.report.contains(QStringLiteral("multipass is true but no bufferShaders are declared")),
                     qPrintable(r.report));
        }
        {
            QJsonObject obj = pointerPackWithGate(QStringLiteral("pt-tex-cap"), 0.0);
            QJsonArray textures;
            for (int i = 0; i < 4; ++i) {
                QJsonObject tex;
                tex.insert(QStringLiteral("path"), QStringLiteral("tile%1.png").arg(i));
                textures.append(tex);
            }
            obj.insert(QStringLiteral("textures"), textures);
            const PackResult r = validatePointer(tmp, QStringLiteral("pt-tex-cap"), obj, body);
            QVERIFY2(r.report.contains(QStringLiteral("too many textures: 4 declared, cap is 3")),
                     qPrintable(r.report));
        }
        {
            // A buffer pass reaches parameters by raw slot, never by p_<id>,
            // so a pack whose scalars are read only there must not be told
            // its controls are dead. This is the shape the bundled afterglow
            // pack has.
            QJsonObject obj = pointerPack(
                QStringLiteral("pt-mp-slot"),
                QJsonArray{pointerParam(QStringLiteral("persistence"), QStringLiteral("float"), 0.9, 0.0, 1.0)});
            obj.insert(QStringLiteral("multipass"), true);
            obj.insert(QStringLiteral("bufferShaders"), toArray({QStringLiteral("buffer.frag")}));
            obj.insert(QStringLiteral("bufferFeedback"), true);
            QVERIFY(
                writePointerBuffer(tmp, QStringLiteral("pt-mp-slot"), QStringLiteral("buffer.frag"),
                                   QStringLiteral("#version 450\n"
                                                  "uniform vec4 customParams[8];\n"
                                                  "uniform sampler2D iChannel0;\n"
                                                  "layout(location = 0) in vec2 vTexCoord;\n"
                                                  "layout(location = 0) out vec4 fragColor;\n"
                                                  "void main() {\n"
                                                  "    fragColor = texture(iChannel0, vTexCoord) * customParams[0].x;\n"
                                                  "}\n")));
            const PackResult r =
                validatePointer(tmp, QStringLiteral("pt-mp-slot"), obj,
                                QStringLiteral("uniform sampler2D iChannel0;\n"
                                               "vec4 pPointer(vec2 uv) { return texture(iChannel0, uv); }\n"));
            QVERIFY2(!r.report.contains(QStringLiteral("parameter 'persistence' is declared but no stage reads")),
                     qPrintable(r.report));
            QVERIFY2(!r.report.contains(QStringLiteral("bufferFeedback is true but no buffer pass samples")),
                     qPrintable(r.report));
        }
        {
            // The preview keeps a feedback pair for a single buffer pass only,
            // so a two-pass feedback pack persists on the compositor and
            // starts from black in the browser. The author is told.
            QJsonObject obj = pointerPack(
                QStringLiteral("pt-mp-two-feedback"),
                QJsonArray{pointerParam(QStringLiteral("persistence"), QStringLiteral("float"), 0.9, 0.0, 1.0)});
            obj.insert(QStringLiteral("multipass"), true);
            obj.insert(QStringLiteral("bufferShaders"), toArray({QStringLiteral("a.frag"), QStringLiteral("b.frag")}));
            obj.insert(QStringLiteral("bufferFeedback"), true);
            const QString bufferSrc = QStringLiteral(
                "#version 450\n"
                "uniform vec4 customParams[8];\n"
                "uniform sampler2D iChannel0;\n"
                "layout(location = 0) in vec2 vTexCoord;\n"
                "layout(location = 0) out vec4 fragColor;\n"
                "void main() {\n"
                "    fragColor = texture(iChannel0, vTexCoord) * customParams[0].x;\n"
                "}\n");
            QVERIFY(writePointerBuffer(tmp, QStringLiteral("pt-mp-two-feedback"), QStringLiteral("a.frag"), bufferSrc));
            QVERIFY(writePointerBuffer(tmp, QStringLiteral("pt-mp-two-feedback"), QStringLiteral("b.frag"), bufferSrc));
            const PackResult r =
                validatePointer(tmp, QStringLiteral("pt-mp-two-feedback"), obj,
                                QStringLiteral("uniform sampler2D iChannel1;\n"
                                               "vec4 pPointer(vec2 uv) { return texture(iChannel1, uv); }\n"));
            QVERIFY2(r.report.contains(
                         QStringLiteral("bufferFeedback with 2 buffer passes persists on the compositor only")),
                     qPrintable(r.report));
        }
        {
            // A sprite sampled without needsCursor reads texture unit 0 on
            // the compositor, which is the contract's stated reason for the
            // gate living in the validator.
            QJsonObject obj = pointerPackWithGate(QStringLiteral("pt-cursor"), 0.0);
            const PackResult r = validatePointer(
                tmp, QStringLiteral("pt-cursor"), obj,
                QStringLiteral("uniform sampler2D uCursorSprite;\n"
                               "vec4 pPointer(vec2 uv) { return texture(uCursorSprite, uv) * p_activationSpeed; }\n"));
            QVERIFY2(
                r.report.contains(QStringLiteral("samples uCursorSprite but the pack does not declare `needsCursor`")),
                qPrintable(r.report));

            // The other direction had no fixture at all, so deleting its
            // branch left the suite green.
            QJsonObject unused = pointerPackWithGate(QStringLiteral("pt-cursor-unused"), 0.0);
            unused.insert(QStringLiteral("needsCursor"), true);
            const PackResult u = validatePointer(tmp, QStringLiteral("pt-cursor-unused"), unused, body);
            QVERIFY2(u.report.contains(QStringLiteral("needsCursor is declared but no stage samples uCursorSprite")),
                     qPrintable(u.report));
        }
        {
            // The multipass and feedback channel rules: a pack that draws
            // buffers nothing reads, and a feedback pack whose buffer never
            // reads its own channel. Neither had a fixture, so both branches
            // could be deleted with the suite staying green.
            QJsonObject obj = pointerPack(
                QStringLiteral("pt-mp-nochannel"),
                QJsonArray{pointerParam(QStringLiteral("persistence"), QStringLiteral("float"), 0.9, 0.0, 1.0)});
            obj.insert(QStringLiteral("multipass"), true);
            obj.insert(QStringLiteral("bufferShaders"), toArray({QStringLiteral("buffer.frag")}));
            obj.insert(QStringLiteral("bufferFeedback"), true);
            QVERIFY(writePointerBuffer(tmp, QStringLiteral("pt-mp-nochannel"), QStringLiteral("buffer.frag"),
                                       QStringLiteral("#version 450\n"
                                                      "uniform vec4 customParams[8];\n"
                                                      "layout(location = 0) in vec2 vTexCoord;\n"
                                                      "layout(location = 0) out vec4 fragColor;\n"
                                                      "void main() {\n"
                                                      "    fragColor = vec4(customParams[0].x);\n"
                                                      "}\n")));
            const PackResult r =
                validatePointer(tmp, QStringLiteral("pt-mp-nochannel"), obj,
                                QStringLiteral("vec4 pPointer(vec2 uv) { return vec4(p_persistence); }\n"));
            QVERIFY2(r.report.contains(QStringLiteral("multipass is true but the main fragment never samples an "
                                                      "iChannel")),
                     qPrintable(r.report));
            QVERIFY2(r.report.contains(QStringLiteral("bufferFeedback is true but no buffer pass samples an "
                                                      "iChannel")),
                     qPrintable(r.report));
        }
        {
            // bufferScale's two arms, and the pair of else-branch lints for a
            // pack that set the buffer keys without the multipass switch.
            QJsonObject text = pointerPackWithGate(QStringLiteral("pt-scale-text"), 0.0);
            text.insert(QStringLiteral("multipass"), true);
            text.insert(QStringLiteral("bufferShaders"), toArray({QStringLiteral("buffer.frag")}));
            text.insert(QStringLiteral("bufferScale"), QStringLiteral("0.5"));
            QVERIFY(writePointerBuffer(tmp, QStringLiteral("pt-scale-text"), QStringLiteral("buffer.frag"),
                                       QStringLiteral("#version 450\n"
                                                      "uniform sampler2D iChannel0;\n"
                                                      "layout(location = 0) in vec2 vTexCoord;\n"
                                                      "layout(location = 0) out vec4 fragColor;\n"
                                                      "void main() { fragColor = texture(iChannel0, vTexCoord); }\n")));
            const PackResult t = validatePointer(
                tmp, QStringLiteral("pt-scale-text"), text,
                QStringLiteral("uniform sampler2D iChannel0;\n"
                               "vec4 pPointer(vec2 uv) { return texture(iChannel0, uv) * p_activationSpeed; }\n"));
            QVERIFY2(t.report.contains(QStringLiteral("bufferScale is not a number")), qPrintable(t.report));

            QJsonObject wide = pointerPackWithGate(QStringLiteral("pt-scale-range"), 0.0);
            wide.insert(QStringLiteral("multipass"), true);
            wide.insert(QStringLiteral("bufferShaders"), toArray({QStringLiteral("buffer.frag")}));
            wide.insert(QStringLiteral("bufferScale"), 8.0);
            QVERIFY(writePointerBuffer(tmp, QStringLiteral("pt-scale-range"), QStringLiteral("buffer.frag"),
                                       QStringLiteral("#version 450\n"
                                                      "uniform sampler2D iChannel0;\n"
                                                      "layout(location = 0) in vec2 vTexCoord;\n"
                                                      "layout(location = 0) out vec4 fragColor;\n"
                                                      "void main() { fragColor = texture(iChannel0, vTexCoord); }\n")));
            const PackResult w = validatePointer(
                tmp, QStringLiteral("pt-scale-range"), wide,
                QStringLiteral("uniform sampler2D iChannel0;\n"
                               "vec4 pPointer(vec2 uv) { return texture(iChannel0, uv) * p_activationSpeed; }\n"));
            QVERIFY2(w.report.contains(QStringLiteral("bufferScale out of range")), qPrintable(w.report));

            QJsonObject off = pointerPackWithGate(QStringLiteral("pt-buffers-no-switch"), 0.0);
            off.insert(QStringLiteral("bufferShaders"), toArray({QStringLiteral("buffer.frag")}));
            off.insert(QStringLiteral("bufferFeedback"), true);
            const PackResult o = validatePointer(tmp, QStringLiteral("pt-buffers-no-switch"), off, body);
            QVERIFY2(o.report.contains(QStringLiteral("bufferShaders declared without `multipass: true`")),
                     qPrintable(o.report));
            QVERIFY2(o.report.contains(QStringLiteral("bufferFeedback declared without `multipass: true`")),
                     qPrintable(o.report));
        }
        {
            // The parameter sweep: none of its arms was reachable from any
            // pointer fixture, where the animation arm has covered its own
            // budget check since it was written.
            QJsonObject dup =
                pointerPack(QStringLiteral("pt-param-dup"),
                            QJsonArray{pointerParam(QStringLiteral("speed"), QStringLiteral("float"), 1.0, 0.0, 2.0),
                                       pointerParam(QStringLiteral("speed"), QStringLiteral("float"), 1.0, 0.0, 2.0)});
            const PackResult d = validatePointer(tmp, QStringLiteral("pt-param-dup"), dup,
                                                 QStringLiteral("vec4 pPointer(vec2 uv) { return vec4(p_speed); }\n"));
            QVERIFY2(d.report.contains(QStringLiteral("duplicate parameter id")), qPrintable(d.report));

            QJsonObject bad =
                pointerPack(QStringLiteral("pt-param-type"),
                            QJsonArray{pointerParam(QStringLiteral("speed"), QStringLiteral("vec9"), 1.0, 0.0, 2.0)});
            const PackResult b2 = validatePointer(tmp, QStringLiteral("pt-param-type"), bad,
                                                  QStringLiteral("vec4 pPointer(vec2 uv) { return vec4(p_speed); }\n"));
            QVERIFY2(b2.report.contains(QStringLiteral("unknown param type 'vec9'")), qPrintable(b2.report));
        }
        {
            // The shared pointer.vert is the preview's stage: a pack naming a
            // copy of it as its own gets a vertex stage that is dead on the
            // compositor, and the message has to say why rather than leave a
            // bare undeclared-identifier error.
            QJsonObject obj = pointerPackWithGate(QStringLiteral("pt-vert-qt"), 0.0);
            obj.insert(QStringLiteral("vertexShader"), QStringLiteral("pointer.vert"));
            QVERIFY(writePointerBuffer(
                tmp, QStringLiteral("pt-vert-qt"), QStringLiteral("pointer.vert"),
                QStringLiteral("#version 450\n"
                               "layout(std140, binding = 0) uniform U { mat4 qt_Matrix; };\n"
                               "layout(location = 0) in vec2 position;\n"
                               "void main() { gl_Position = qt_Matrix * vec4(position, 0.0, 1.0); }\n")));
            const PackResult r = validatePointer(tmp, QStringLiteral("pt-vert-qt"), obj, body);
            QVERIFY2(r.report.contains(QStringLiteral("reads qt_Matrix / qt_Opacity, which exist only in the preview")),
                     qPrintable(r.report));
        }
    }

    /// The preset lint runs on THIS arm too.
    ///
    /// `reportPresetProblems` is wired into all four validator arms, and every test
    /// that exercised it drove the animation or overlay one, so deleting the call from
    /// the pointer arm left the whole suite green. Every executable here compiles all
    /// four arms, so that deletion also failed no link.
    void thePointerArmLintsPresetsToo()
    {
        QTemporaryDir tmp;
        REQUIRE_POINTER_FIXTURE(tmp);

        QJsonObject obj =
            pointerPack(QStringLiteral("pt-presets"),
                        QJsonArray{pointerParam(QStringLiteral("speed"), QStringLiteral("float"), 1.0, 0.0, 2.0)});
        QJsonObject presets;
        presets.insert(QStringLiteral("Undeclared"), QJsonObject{{QStringLiteral("noSuchThing"), 1.0}});
        presets.insert(QStringLiteral("TooFast"), QJsonObject{{QStringLiteral("speed"), 99.0}});
        presets.insert(QStringLiteral("Tidy"), QJsonObject{{QStringLiteral("speed"), 1.5}});
        obj.insert(QStringLiteral("presets"), presets);

        const PackResult r = validatePointer(tmp, QStringLiteral("pt-presets"), obj,
                                             pointerBodyReadingScalars({QStringLiteral("speed")}));
        QVERIFY2(r.report.contains(QStringLiteral("presets        ERROR")), qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("which the pack does not declare")), qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("above its declared maximum")), qPrintable(r.report));
        // The well-formed preset beside them is silent, so neither check is firing on
        // every value it sees.
        QVERIFY2(!r.report.contains(QStringLiteral("preset 'Tidy'")), qPrintable(r.report));
        QVERIFY(r.errors >= 2);
    }
};

QTEST_MAIN(TestPointerPackValidator)
#include "test_pointer_pack_validator.moc"
