// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The offline pack validator's metadata lints, across three of the four
// authoring models: animation, overlay and pointer. The bundled-pack CI gates
// (shader_validate_*) only prove the shipped packs are clean — they cannot
// show that a BROKEN pack is actually caught, which is how an appliesTo token
// that the parser accepted and the lint rejected shipped undetected, and how a
// pointer pack with a speed gate the preview could never open shipped
// invisible. These tests build deliberately-broken packs in a temp dir and
// assert the diagnostic.
//
// The stage bakes live in test_animation_pack_bakes.cpp and the authoring-model
// detection in test_pack_model_detection.cpp. Every animation slot here still
// bakes the pack's fragment on both hosts, since the validator does that for
// every animation pack, so every one of them needs glslang and the bundled
// shared/ helpers linked in (REQUIRE_ANIMATION_FIXTURE).

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

#include <PhosphorAnimation/AnimationShaderContract.h>
#include <PhosphorAnimation/ProfilePaths.h>

#include "packvalidatortesthelpers.h"

using namespace PackValidatorTest;

namespace {

/// The overlay twin of `validate`. Writes the pack plus a trivial zone
/// fragment and every buffer pass it declares, so the metadata lints under
/// test are the only thing that can fail. The buffer stages are written from
/// the DECLARED names, empty ones skipped, which is what lets the
/// empty-entry case exercise the lint rather than a missing file.
PackResult validateOverlay(const QTemporaryDir& tmp, const QString& name, const QJsonObject& metadata)
{
    const QString dir = tmp.filePath(name);
    if (!writePackFile(dir, QStringLiteral("metadata.json"), QJsonDocument(metadata).toJson())) {
        return fixtureFailure(QStringLiteral("failed to write metadata.json under ") + dir);
    }

    // Returns bool like the multipass writeBuffer sibling: a silently
    // dropped stage write would surface later as a misleading
    // "buffer shader missing" validator diagnostic instead of a fixture
    // failure.
    const auto writeStage = [&dir](const QString& file) {
        return writePackFile(dir, file, "vec4 pZone(vec2 uv) { return vec4(0.0); }\n");
    };
    bool stagesOk = writeStage(QStringLiteral("zone.frag"));
    for (const QJsonValue& v : metadata.value(QLatin1String("bufferShaders")).toArray()) {
        if (!v.toString().isEmpty()) {
            stagesOk = writeStage(v.toString()) && stagesOk;
        }
    }
    if (!stagesOk) {
        return fixtureFailure(QStringLiteral("failed to write a stage file under ") + dir);
    }

    PackResult result;
    QTextStream stream(&result.report);
    result.errors = PlasmaZones::ShaderValidate::validatePack(dir, stream);
    stream.flush();
    return result;
}

/// The pointer twin of `validate`. The fragment defines its own
/// pointerSpeedGate rather than including pointer_lib.glsl, because a fixture
/// in a QTemporaryDir has no `shared/` sibling for the include to resolve
/// against and a failed include would bury the lint under a compile error.
/// The stub's own signature is not a call, so the scan under test ignores it,
/// which is itself worth having exercised.
PackResult validatePointer(const QTemporaryDir& tmp, const QString& name, const QJsonObject& metadata,
                           const QString& body)
{
    const QString dir = tmp.filePath(name);
    if (!writePackFile(dir, QStringLiteral("metadata.json"), QJsonDocument(metadata).toJson())) {
        return fixtureFailure(QStringLiteral("failed to write metadata.json under ") + dir);
    }
    const QByteArray frag = QByteArray(
                                "float pointerSpeedGate(float speed, float activationSpeed) {\n"
                                "    return smoothstep(activationSpeed, activationSpeed * 2.0, speed);\n"
                                "}\n")
        + body.toUtf8();
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

/// `multipass` is set because the buffer lints gate on it, matching the
/// runtime: parseShaderMetadata takes isMultipass from this key alone, so a
/// pack that lists bufferShaders without it is inert and its buffer list is
/// never resolved. A fixture that omitted it would be asserting on a
/// configuration nothing acts on.
QJsonObject overlayPack(const QString& id)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("name"), QStringLiteral("Test Overlay"));
    obj.insert(QStringLiteral("fragmentShader"), QStringLiteral("zone.frag"));
    obj.insert(QStringLiteral("multipass"), true);
    return obj;
}

/// A bundled shared helper's CODE, with its `//` comments removed. The strip
/// helper's prose names iStripAxis a dozen times, so an assertion about what
/// the helper computes has to read past it or a commented-out body would
/// satisfy it.
///
/// Returns an empty string when the source tree is not available, the same
/// skip cue as linkSharedIncludes.
///
/// The strip is textual, not a lexer: a `//` inside a string or a block
/// comment would confuse it. GLSL has no string literals and the shared
/// helpers use line comments throughout, so the fixtures this reads never hit
/// that case. Left simple on purpose rather than hardened.
QString sharedHelperCode(const QString& fileName)
{
    QFile file(QStringLiteral(P_SOURCE_DIR "/data/animations/shared/") + fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    QString code;
    const QString text = QString::fromUtf8(file.readAll());
    for (const QString& line : text.split(QLatin1Char('\n'))) {
        code += line.section(QLatin1String("//"), 0, 0) + QLatin1Char('\n');
    }
    return code;
}

/// The body of the GLSL function named @p name in @p code, between its opening
/// brace and the matching close. Empty when the function is absent.
///
/// Matches the FIRST occurrence of the name, so a call site appearing before
/// the definition, or a longer name ending in @p name, would pick the wrong
/// body. The helpers this reads define each function before any use and share
/// no such name suffix. Left simple on purpose rather than hardened.
QString glslFunctionBody(const QString& code, const QString& name)
{
    const int signature = code.indexOf(name + QLatin1Char('('));
    if (signature < 0) {
        return QString();
    }
    const int open = code.indexOf(QLatin1Char('{'), signature);
    if (open < 0) {
        return QString();
    }
    int depth = 0;
    for (int i = open; i < code.size(); ++i) {
        if (code.at(i) == QLatin1Char('{')) {
            ++depth;
        } else if (code.at(i) == QLatin1Char('}')) {
            if (--depth == 0) {
                return code.mid(open + 1, i - open - 1);
            }
        }
    }
    return QString();
}

} // namespace

class TestPackValidators : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// Every token the parser accepts must pass the lint. These two lists
    /// drifted apart once already: the parser learned "strip" while the lint
    /// still named four tokens, so a correct pack drew a spurious diagnostic.
    /// Both now read ProfilePaths::allEventClassTokens(), and this asserts it
    /// end to end rather than trusting that they still do.
    void everyAcceptedTokenPassesTheLint()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        const QStringList tokens = PhosphorAnimation::ProfilePaths::allEventClassTokens();
        QVERIFY(!tokens.isEmpty());

        QStringList rejected;
        for (const QString& token : tokens) {
            QJsonObject obj = basePack(QStringLiteral("tok-") + token);
            obj.insert(QStringLiteral("appliesTo"), toArray({token}));
            const PackResult r = validate(tmp, QStringLiteral("tok-") + token, obj);
            if (r.report.contains(QStringLiteral("unknown appliesTo token"))) {
                rejected << token;
            }
        }
        QVERIFY2(rejected.isEmpty(),
                 qPrintable(QStringLiteral("the lint rejects token(s) the parser accepts: ")
                            + rejected.join(QLatin1String(", "))));
    }

    /// The strip helpers must resolve through the bound axis uniform rather
    /// than through a hardcoded x.
    ///
    /// Compilation alone cannot catch this: a stripAxisOffset reverted to
    /// `vec2(amount, 0.0)` compiles, links, bakes and ships, and each of the
    /// three packs that displace through it (jelly, chromatic, motion-blur;
    /// carousel builds its displacement from the axis directly) smears
    /// sideways on a vertical strip with no diagnostic anywhere. The helper
    /// is the single point they all go through, which is what makes a source
    /// assertion worth making here instead of per pack.
    void stripHelpersDisplaceAlongTheBoundAxis()
    {
        const QString code = sharedHelperCode(QStringLiteral("strip_transition.glsl"));
        if (code.isEmpty())
            QSKIP("data/animations/shared not found — running outside source tree");

        const QString offset = glslFunctionBody(code, QStringLiteral("stripAxisOffset"));
        QVERIFY2(!offset.isEmpty(), "stripAxisOffset is missing from shared/strip_transition.glsl");
        QVERIFY2(
            offset.contains(QLatin1String("iStripAxis")),
            qPrintable(QStringLiteral("stripAxisOffset does not multiply by the bound axis: ") + offset.simplified()));

        // The perpendicular is the axis SWAP, not a rotation: vec2(-y, x)
        // would hand a vertical strip (-1, 0) and mirror every across
        // coordinate against the horizontal case, which is a sign error no
        // horizontal-only test can see.
        const QString perp = glslFunctionBody(code, QStringLiteral("stripAxisPerp"));
        QVERIFY2(!perp.isEmpty(), "stripAxisPerp is missing from shared/strip_transition.glsl");
        // Accept both spellings of the same swap: the component form
        // vec2(iStripAxis.y, iStripAxis.x) and the equivalent .yx swizzle. A
        // reformat between them is semantically identical, and failing on it
        // would be a false alarm rather than a caught regression. What must
        // NOT appear either way is a negation, which is the rotation.
        const bool componentSwap =
            perp.contains(QLatin1String("iStripAxis.y")) && perp.contains(QLatin1String("iStripAxis.x"));
        const bool swizzleSwap = perp.contains(QLatin1String("iStripAxis.yx"));
        QVERIFY2(!perp.contains(QLatin1String("-iStripAxis")),
                 qPrintable(QStringLiteral("stripAxisPerp negates a component, which is the rotation the header "
                                           "warns against rather than the swap: ")
                            + perp.simplified()));
        QVERIFY2(componentSwap || swizzleSwap,
                 qPrintable(QStringLiteral("stripAxisPerp is not the plain component swap: ") + perp.simplified()));

        // stripEdgeFade is the third axis-coupled helper: its travel
        // coordinate must come from dot(uv, iStripAxis), not from a bare
        // uv.x — a revert fades the wrong edges on a vertical strip in every
        // pack that budgets its displacement against the fade (the same
        // silent, compositor-only failure mode as the offset above).
        const QString fade = glslFunctionBody(code, QStringLiteral("stripEdgeFade"));
        QVERIFY2(!fade.isEmpty(), "stripEdgeFade is missing from shared/strip_transition.glsl");
        QVERIFY2(fade.contains(QLatin1String("iStripAxis")),
                 qPrintable(QStringLiteral("stripEdgeFade does not derive its travel coordinate from the bound axis: ")
                            + fade.simplified()));
    }

    /// The complement: an unknown token IS linted, and the message names the
    /// live vocabulary rather than a stale hand-written list.
    void unknownTokenIsLintedAndMessageNamesTheVocabulary()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        QJsonObject obj = basePack(QStringLiteral("bad-token"));
        obj.insert(QStringLiteral("appliesTo"), toArray({QStringLiteral("teleport")}));
        const PackResult r = validate(tmp, QStringLiteral("bad-token"), obj);

        QVERIFY(r.errors > 0);
        QVERIFY(r.report.contains(QStringLiteral("unknown appliesTo token 'teleport'")));
        for (const QString& token : PhosphorAnimation::ProfilePaths::allEventClassTokens()) {
            QVERIFY2(r.report.contains(token),
                     qPrintable(QStringLiteral("diagnostic omits the valid token '") + token + QLatin1Char('\'')));
        }
    }

    /// An explicit `"appliesTo": []` is a legal spelling of the universal
    /// default and must stay lint-free, while the two degenerate spellings
    /// that LOOK like a constraint and silently become universal are linted.
    /// The difference is author intent, not effect: `[""]` and `["teleport"]`
    /// are typos, `[]` is a statement. This pins the asymmetry so nobody
    /// "evens it up" and starts failing every universal pack that spells its
    /// default out.
    void explicitEmptyAppliesToIsNotLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        QJsonObject empty = basePack(QStringLiteral("empty-applies"));
        empty.insert(QStringLiteral("appliesTo"), QJsonArray());
        const PackResult r = validate(tmp, QStringLiteral("empty-applies"), empty);
        QVERIFY2(!r.report.contains(QStringLiteral("appliesTo")),
                 qPrintable(QStringLiteral("an explicit empty appliesTo must draw no diagnostic:\n") + r.report));
        QCOMPARE(r.errors, 0);

        // The degenerate cousins DO lint, which is what makes the silence
        // above a decision rather than a gap.
        QJsonObject blank = basePack(QStringLiteral("blank-token"));
        blank.insert(QStringLiteral("appliesTo"), toArray({QString()}));
        QVERIFY(validate(tmp, QStringLiteral("blank-token"), blank)
                    .report.contains(QStringLiteral("empty appliesTo token")));
    }

    /// A bare string instead of an array is ignored wholesale at load, which
    /// silently makes the pack universal.
    void nonArrayAppliesToIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        QJsonObject obj = basePack(QStringLiteral("bad-shape"));
        obj.insert(QStringLiteral("appliesTo"), QStringLiteral("strip"));
        const PackResult r = validate(tmp, QStringLiteral("bad-shape"), obj);

        QVERIFY(r.errors > 0);
        QVERIFY(r.report.contains(QStringLiteral("appliesTo must be an array")));
    }

    /// The screen-level passes draw their own full-screen quad, so a vertex
    /// stage or a geometry grid declared by a desktop or strip pack is loaded
    /// and then never used.
    void screenLevelPacksAreToldTheirVertexStageIsIgnored()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        QJsonObject obj = basePack(QStringLiteral("strip-vert"));
        obj.insert(QStringLiteral("appliesTo"), toArray({QStringLiteral("strip")}));
        obj.insert(QStringLiteral("vertexShader"), QStringLiteral("effect.vert"));
        obj.insert(QStringLiteral("geometryGrid"), 8);

        QVERIFY(writePackFile(tmp.filePath(QStringLiteral("strip-vert")), QStringLiteral("effect.vert"),
                              "#version 450\nvoid main() {}\n"));

        const PackResult r = validate(tmp, QStringLiteral("strip-vert"), obj);
        QVERIFY(r.report.contains(QStringLiteral("vertexShader is ignored for desktop/strip packs")));
        QVERIFY(r.report.contains(QStringLiteral("geometryGrid is ignored for desktop/strip packs")));

        // A single-surface pack keeps both without complaint.
        QJsonObject surface = basePack(QStringLiteral("surface-vert"));
        surface.insert(QStringLiteral("appliesTo"), toArray({QStringLiteral("appearance")}));
        surface.insert(QStringLiteral("geometryGrid"), 8);
        const PackResult ok = validate(tmp, QStringLiteral("surface-vert"), surface);
        QVERIFY(!ok.report.contains(QStringLiteral("geometryGrid is ignored")));
    }

    /// The screen-level passes bind only their own scene captures, so a
    /// declared `textures` array is dead on them, and on the preview branch
    /// it would alias the capture slots. A strip pack is told; a single
    /// surface pack, whose textures are real, is not.
    void screenLevelPacksAreToldTheirTexturesAreIgnored()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        // Returns bool for the caller to QVERIFY: a QVERIFY inside the lambda
        // only returns from the lambda, so a failed PNG write would let the
        // test run on and fail on a misleading "texture missing" lint.
        const auto declareTexture = [&tmp](QJsonObject& obj, const QString& id) {
            const QString dir = tmp.filePath(id);
            QDir().mkpath(dir);
            QImage px(1, 1, QImage::Format_RGBA8888);
            px.fill(Qt::white);
            if (!px.save(dir + QStringLiteral("/tile.png"))) {
                return false;
            }
            QJsonObject tex;
            tex.insert(QStringLiteral("path"), QStringLiteral("tile.png"));
            obj.insert(QStringLiteral("textures"), QJsonArray{tex});
            return true;
        };

        QJsonObject strip = basePack(QStringLiteral("strip-tex"));
        strip.insert(QStringLiteral("appliesTo"), toArray({QStringLiteral("strip")}));
        QVERIFY(declareTexture(strip, QStringLiteral("strip-tex")));
        const PackResult r = validate(tmp, QStringLiteral("strip-tex"), strip);
        QVERIFY2(r.report.contains(QStringLiteral("textures are ignored for desktop/strip packs")),
                 qPrintable(r.report));

        QJsonObject surface = basePack(QStringLiteral("surface-tex"));
        surface.insert(QStringLiteral("appliesTo"), toArray({QStringLiteral("appearance")}));
        QVERIFY(declareTexture(surface, QStringLiteral("surface-tex")));
        const PackResult ok = validate(tmp, QStringLiteral("surface-tex"), surface);
        QVERIFY2(!ok.report.contains(QStringLiteral("textures are ignored")), qPrintable(ok.report));
    }

    /// geometryGrid clamps to 0 at load, so a negative value disables the
    /// grid indistinguishably from never declaring it.
    void negativeGeometryGridIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        QJsonObject obj = basePack(QStringLiteral("neg-grid"));
        obj.insert(QStringLiteral("geometryGrid"), -4);
        const PackResult r = validate(tmp, QStringLiteral("neg-grid"), obj);

        QVERIFY(r.errors > 0);
        QVERIFY(r.report.contains(QStringLiteral("geometryGrid is negative")));
    }
    /// The other shapes geometryGrid can take and still disable the grid,
    /// each named for what it is: a value that is not a number at all reads
    /// as 0, and a fractional one reads as 0, while a huge whole number is
    /// clamped to the cap (the loud side, linted nowhere here).
    void geometryGridShapeLintsNameTheShape()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        QJsonObject text = basePack(QStringLiteral("grid-text"));
        text.insert(QStringLiteral("geometryGrid"), QStringLiteral("8"));
        const PackResult t = validate(tmp, QStringLiteral("grid-text"), text);
        QVERIFY2(t.report.contains(QStringLiteral("geometryGrid is not a number")), qPrintable(t.report));

        QJsonObject fraction = basePack(QStringLiteral("grid-fraction"));
        fraction.insert(QStringLiteral("geometryGrid"), 8.5);
        const PackResult f = validate(tmp, QStringLiteral("grid-fraction"), fraction);
        QVERIFY2(f.report.contains(QStringLiteral("geometryGrid is not a whole number")), qPrintable(f.report));

        QJsonObject huge = basePack(QStringLiteral("grid-huge"));
        huge.insert(QStringLiteral("geometryGrid"), 1e10);
        const PackResult h = validate(tmp, QStringLiteral("grid-huge"), huge);
        QVERIFY2(!h.report.contains(QStringLiteral("geometryGrid is not")), qPrintable(h.report));
    }

    /// The slot budget: the registry drops every scalar past the flat slot
    /// count and every colour past the colour count at load, and the preamble
    /// emits no p_<id> for them. A pack at the budget is clean; one past it
    /// is told which pool overflowed. The texture and buffer caps have had
    /// this lint all along; the parameter pools did not.
    void parameterBudgetOverflowIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        constexpr int kScalars = PhosphorAnimationShaders::AnimationShaderContract::kMaxParameterSlots;
        constexpr int kColors = PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors;
        const auto scalars = [](int n) {
            QJsonArray arr;
            for (int i = 0; i < n; ++i) {
                arr.append(animationParam(QStringLiteral("s%1").arg(i), QStringLiteral("float"), 0.0));
            }
            return arr;
        };
        const auto colors = [](int n) {
            QJsonArray arr;
            for (int i = 0; i < n; ++i) {
                arr.append(
                    animationParam(QStringLiteral("c%1").arg(i), QStringLiteral("color"), QStringLiteral("#ffffff")));
            }
            return arr;
        };

        QJsonObject atBudget = basePack(QStringLiteral("params-full"));
        atBudget.insert(QStringLiteral("parameters"), scalars(kScalars));
        const PackResult full = validate(tmp, QStringLiteral("params-full"), atBudget);
        QVERIFY2(!full.report.contains(QStringLiteral("too many")), qPrintable(full.report));

        QJsonObject over = basePack(QStringLiteral("params-over"));
        over.insert(QStringLiteral("parameters"), scalars(kScalars + 1));
        const PackResult r = validate(tmp, QStringLiteral("params-over"), over);
        QVERIFY2(
            r.report.contains(
                QStringLiteral("too many scalar params: %1 declared, budget is %2").arg(kScalars + 1).arg(kScalars)),
            qPrintable(r.report));

        QJsonObject overColors = basePack(QStringLiteral("colors-over"));
        overColors.insert(QStringLiteral("parameters"), colors(kColors + 1));
        const PackResult c = validate(tmp, QStringLiteral("colors-over"), overColors);
        QVERIFY2(c.report.contains(
                     QStringLiteral("too many color params: %1 declared, budget is %2").arg(kColors + 1).arg(kColors)),
                 qPrintable(c.report));
    }

    /// A strip pack that ships its own main() is abandoned by the strip pass
    /// at load, and one that never samples the strip through getStripColor()
    /// is abandoned at link. Both bakes pass such packs, so both are lints.
    /// A strip pack that does the ordinary thing draws neither.
    void stripPackEntryAndSamplingContractsAreLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        QJsonObject ownMain = basePack(QStringLiteral("strip-main"));
        ownMain.insert(QStringLiteral("appliesTo"), toArray({QStringLiteral("strip")}));
        QVERIFY(writePackFile(tmp.filePath(QStringLiteral("strip-main")), QStringLiteral("effect.frag"),
                              "#version 450\n"
                              "#include <animation_uniforms.glsl>\n"
                              "#include <strip_transition.glsl>\n"
                              "layout(location = 0) in vec2 vTexCoord;\n"
                              "layout(location = 0) out vec4 fragColor;\n"
                              "void main() { fragColor = getStripColor(vTexCoord); }\n"));
        const PackResult m = validate(tmp, QStringLiteral("strip-main"), ownMain, /*writeFragment=*/false);
        QVERIFY2(m.report.contains(QStringLiteral("strip packs must not define main()")), qPrintable(m.report));

        QJsonObject noSample = basePack(QStringLiteral("strip-nosample"));
        noSample.insert(QStringLiteral("appliesTo"), toArray({QStringLiteral("strip")}));
        const PackResult n = validate(tmp, QStringLiteral("strip-nosample"), noSample);
        QVERIFY2(n.report.contains(QStringLiteral("strip packs must sample the strip through getStripColor()")),
                 qPrintable(n.report));

        QJsonObject fine = basePack(QStringLiteral("strip-fine"));
        fine.insert(QStringLiteral("appliesTo"), toArray({QStringLiteral("strip")}));
        QVERIFY(writePackFile(tmp.filePath(QStringLiteral("strip-fine")), QStringLiteral("effect.frag"),
                              "#include <strip_transition.glsl>\n"
                              "vec4 pTransition(vec2 uv, float t) { return getStripColor(uv); }\n"));
        const PackResult ok = validate(tmp, QStringLiteral("strip-fine"), fine, /*writeFragment=*/false);
        QVERIFY2(!ok.report.contains(QStringLiteral("strip packs must")), qPrintable(ok.report));
    }

    /// A window-class pack that ships its own main() must route its fragColor
    /// write through PZ_FINALIZE_COLOR, or it renders without the
    /// compositor's HDR colour management; and it must carry a #version
    /// line, since only the scaffold supplies one. The desktop pass keeps the
    /// identity macro, so a desktop main() pack is exempt from the first.
    void mainPacksAreToldAboutFinalizeColorAndVersion()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        QJsonObject bare = basePack(QStringLiteral("main-bare"));
        QVERIFY(writePackFile(tmp.filePath(QStringLiteral("main-bare")), QStringLiteral("effect.frag"),
                              "#include <animation_uniforms.glsl>\n"
                              "layout(location = 0) in vec2 vTexCoord;\n"
                              "layout(location = 0) out vec4 fragColor;\n"
                              "void main() { fragColor = surfaceColor(vTexCoord); }\n"));
        const PackResult b = validate(tmp, QStringLiteral("main-bare"), bare, /*writeFragment=*/false);
        QVERIFY2(b.report.contains(QStringLiteral("never routes fragColor through PZ_FINALIZE_COLOR")),
                 qPrintable(b.report));
        QVERIFY2(b.report.contains(QStringLiteral("defines main() but no #version directive")), qPrintable(b.report));

        QJsonObject routed = basePack(QStringLiteral("main-routed"));
        QVERIFY(writePackFile(tmp.filePath(QStringLiteral("main-routed")), QStringLiteral("effect.frag"),
                              "#version 450\n"
                              "#include <animation_uniforms.glsl>\n"
                              "layout(location = 0) in vec2 vTexCoord;\n"
                              "layout(location = 0) out vec4 fragColor;\n"
                              "void main() { fragColor = PZ_FINALIZE_COLOR(surfaceColor(vTexCoord)); }\n"));
        const PackResult r = validate(tmp, QStringLiteral("main-routed"), routed, /*writeFragment=*/false);
        // Clean on both hosts, and no lint: a main() pack that does the right
        // thing must not be nagged about it.
        QCOMPARE(r.errors, 0);
        QVERIFY2(!r.report.contains(QStringLiteral("PZ_FINALIZE_COLOR")), qPrintable(r.report));

        QJsonObject desktop = basePack(QStringLiteral("main-desktop"));
        desktop.insert(QStringLiteral("appliesTo"), toArray({QStringLiteral("desktop")}));
        QVERIFY(writePackFile(tmp.filePath(QStringLiteral("main-desktop")), QStringLiteral("effect.frag"),
                              "#version 450\n"
                              "#include <animation_uniforms.glsl>\n"
                              "#include <desktop_transition.glsl>\n"
                              "layout(location = 0) in vec2 vTexCoord;\n"
                              "layout(location = 0) out vec4 fragColor;\n"
                              "void main() { fragColor = getToColor(vTexCoord); }\n"));
        const PackResult d = validate(tmp, QStringLiteral("main-desktop"), desktop, /*writeFragment=*/false);
        QVERIFY2(!d.report.contains(QStringLiteral("PZ_FINALIZE_COLOR")), qPrintable(d.report));
    }

    /// The buffer lints the OVERLAY arm was missing while both siblings had
    /// them. This arm needed them most: the overlay runtime coerces an unknown
    /// wrap or filter token, and pads or trims a misaligned array, with no
    /// warning at ANY layer, where the animation and surface runtimes at least
    /// log. Every case here is silent without the lint.
    void overlayBufferLintsCoverTheSilentlyCoercedFields()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        // An empty entry is the worst of them: resolveWithinPack answers empty
        // before the traversal guard, and the caller reads that as fail-closed,
        // clearing the whole list and turning multipass off for the pack.
        {
            QJsonObject obj = overlayPack(QStringLiteral("ov-empty"));
            obj.insert(QStringLiteral("bufferShaders"), QJsonArray{QStringLiteral(""), QStringLiteral("pass0.frag")});
            const PackResult r = validateOverlay(tmp, QStringLiteral("ov-empty"), obj);
            QVERIFY2(r.report.contains(QStringLiteral("empty bufferShaders entry")), qPrintable(r.report));
            QVERIFY2(r.report.contains(QStringLiteral("disables multipass")), qPrintable(r.report));
            // And it must NOT report the implicit buffer.frag, which the
            // runtime never looks at once the array is declared. That wrong
            // diagnostic is what the old filtered-list fallback produced.
            QVERIFY2(!r.report.contains(QStringLiteral("multipass buffer shader missing: buffer.frag")),
                     qPrintable(r.report));
        }
        {
            QJsonObject obj = overlayPack(QStringLiteral("ov-vocab"));
            obj.insert(QStringLiteral("bufferShaders"), QJsonArray{QStringLiteral("pass0.frag")});
            obj.insert(QStringLiteral("bufferWraps"), QJsonArray{QStringLiteral("wrapp")});
            obj.insert(QStringLiteral("bufferFilters"), QJsonArray{QStringLiteral("bilinear")});
            const PackResult r = validateOverlay(tmp, QStringLiteral("ov-vocab"), obj);
            QVERIFY2(r.report.contains(QStringLiteral("bufferWraps value 'wrapp'")), qPrintable(r.report));
            QVERIFY2(r.report.contains(QStringLiteral("bufferFilters value 'bilinear'")), qPrintable(r.report));
        }
        {
            // Short, not surplus: the likelier authoring slip, and the one the
            // surface arm's old surplus-only lint drew nothing for.
            QJsonObject obj = overlayPack(QStringLiteral("ov-short"));
            obj.insert(QStringLiteral("bufferShaders"),
                       QJsonArray{QStringLiteral("pass0.frag"), QStringLiteral("pass1.frag")});
            obj.insert(QStringLiteral("bufferWraps"), QJsonArray{QStringLiteral("clamp")});
            const PackResult r = validateOverlay(tmp, QStringLiteral("ov-short"), obj);
            QVERIFY2(r.report.contains(QStringLiteral("bufferWraps has 1 entries for 2 buffer shaders")),
                     qPrintable(r.report));
        }
        {
            QJsonObject obj = overlayPack(QStringLiteral("ov-single"));
            obj.insert(QStringLiteral("bufferShaders"), QJsonArray{QStringLiteral("pass0.frag")});
            obj.insert(QStringLiteral("bufferWrap"), QStringLiteral("tile"));
            const PackResult r = validateOverlay(tmp, QStringLiteral("ov-single"), obj);
            QVERIFY2(r.report.contains(QStringLiteral("bufferWrap value 'tile'")), qPrintable(r.report));
        }
    }

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
        QVERIFY(tmp.isValid());

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
        QVERIFY(tmp.isValid());

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
        }
        {
            // A pack helper whose name merely ends in the shared gate's name
            // is not the shared gate; the scan is identifier-bounded.
            QJsonObject obj = pointerPackWithGate(QStringLiteral("pt-gate-lookalike"), 900.0);
            const PackResult r =
                validatePointer(tmp, QStringLiteral("pt-gate-lookalike"), obj,
                                QStringLiteral("float myPointerSpeedGate(float s, float a) { return s * a; }\n"
                                               "vec4 pPointer(vec2 uv) {\n"
                                               "    return vec4(myPointerSpeedGate(0.0, p_activationSpeed));\n"
                                               "}\n"));
            QVERIFY2(!r.report.contains(QStringLiteral("previews as an empty stage")), qPrintable(r.report));
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
        QVERIFY(tmp.isValid());
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
};

QTEST_MAIN(TestPackValidators)
#include "test_pack_validators.moc"
