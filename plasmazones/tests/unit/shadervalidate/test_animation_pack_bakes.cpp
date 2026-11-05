// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The offline pack validator's ANIMATION stage bakes, on both hosts, and the
// compositor arm's GLES compile and link checks for every family. The
// bundled-pack gate (shader_validate_animations) only proves the shipped packs
// are clean: it cannot show that a BROKEN pack is caught, and it never reaches
// the multipass path at all (no bundled animation pack is multipass). Most
// slots build a deliberately broken pack in a temp dir and assert the
// diagnostic, most with a clean twin, so a bake quietly turned back into a
// skip is caught. The built-in vertex stages are compiled directly, with no
// pack, and the surface link slot lives here rather than in the surface test
// files.
//
// Every slot shells out to glslang for the compositor arm and QSKIPs without
// it, unlike the CI gate, which fails: the gate is what must never silently
// lose coverage, while this target is also a developer's inner-loop test.

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>

#include "packvalidatortesthelpers.h"

#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorPointer/PointerShaderRegistry.h>
#include <PhosphorShaders/ShaderParamPreamble.h>
#include <PhosphorSurface/SurfaceShaderRegistry.h>

using namespace PackValidatorTest;

namespace {

/// The report line for @p stage on the compositor arm, spacing-independent:
/// the report pads short labels to a column and separates long ones with one
/// space, so a literal with a hand-counted gap silently stops matching the
/// moment the label length or the pad width moves.
QRegularExpression compositorLine(const QString& stage, const QString& outcome)
{
    return QRegularExpression(QRegularExpression::escape(stage) + QStringLiteral("\\s+") + outcome
                              + QStringLiteral(" \\(compositor\\)"));
}

/// The report line for @p stage on the compositor's GLES arm.
QRegularExpression glesLine(const QString& stage, const QString& outcome)
{
    return QRegularExpression(QRegularExpression::escape(stage) + QStringLiteral("\\s+") + outcome
                              + QStringLiteral(" \\(compositor GLES\\)"));
}

/// The report line for @p stage's link against its vertex stage.
QRegularExpression linkLine(const QString& stage, const QString& outcome)
{
    return QRegularExpression(QRegularExpression::escape(stage) + QStringLiteral("\\s+") + outcome
                              + QStringLiteral(" \\(compositor link\\)"));
}

/// An animation vertex stage valid on both hosts that writes vTexCoord, which
/// the scaffolded fragment reads, plus @p extra declarations and @p body.
QByteArray animationVertex(const QByteArray& extra, const QByteArray& body = QByteArray())
{
    return QByteArrayLiteral(
               "#version 450\n"
               "#include <animation_uniforms.glsl>\n"
               "layout(location = 0) in vec2 position;\n"
               "layout(location = 1) in vec2 texCoord;\n"
               "layout(location = 0) out vec2 vTexCoord;\n"
               "#ifdef PLASMAZONES_KWIN\n"
               "uniform mat4 modelViewProjectionMatrix;\n"
               "#else\n"
               "#define modelViewProjectionMatrix qt_Matrix\n"
               "#endif\n")
        + extra
        + QByteArrayLiteral(
               "void main() {\n"
               "    vTexCoord = texCoord;\n")
        + body + QByteArrayLiteral("    gl_Position = modelViewProjectionMatrix * vec4(position, 0.0, 1.0);\n}\n");
}

/// Run the animation validator over a pack with @p frag and the declared
/// @p vert, for the link slots.
PackResult validateWithVertex(const QTemporaryDir& tmp, const QString& name, const QByteArray& frag,
                              const QByteArray& vert)
{
    QJsonObject obj = basePack(name);
    obj.insert(QStringLiteral("appliesTo"), toArray({QStringLiteral("appearance")}));
    obj.insert(QStringLiteral("vertexShader"), QStringLiteral("effect.vert"));
    const QString dir = tmp.filePath(name);
    if (!writePackFile(dir, QStringLiteral("effect.frag"), frag)
        || !writePackFile(dir, QStringLiteral("effect.vert"), vert)) {
        return fixtureFailure(QStringLiteral("failed to write the stages under ") + dir);
    }
    return validate(tmp, name, obj, /*writeFragment=*/false);
}

/// The report line for @p stage on the Qt-RHI preview arm.
QRegularExpression previewLine(const QString& stage, const QString& outcome)
{
    return QRegularExpression(QRegularExpression::escape(stage) + QStringLiteral(" \\(Qt-RHI preview\\)\\s+")
                              + outcome);
}

} // namespace

class TestAnimationPackBakes : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void multipassBufferShadersAreCompiled_data()
    {
        QTest::addColumn<QString>("eventClass");
        QTest::newRow("appearance") << QStringLiteral("appearance");
        QTest::newRow("geometry") << QStringLiteral("geometry");
    }

    /// A multipass pack's BUFFER shaders are compiled, not just existence
    /// checked. No bundled animation pack is multipass today, so the
    /// bundled-pack gate cannot exercise this path at all — without this test
    /// the bake would be dead code that silently stops working. Qt previews
    /// run the buffer passes for every event class, compositor-only ones
    /// included, which is what the geometry row pins.
    void multipassBufferShadersAreCompiled()
    {
        QFETCH(QString, eventClass);
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        QJsonObject obj = basePack(QStringLiteral("mp-good"));
        obj.insert(QStringLiteral("appliesTo"), toArray({eventClass}));
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("bufferShaders"), toArray({QStringLiteral("buffer0.frag")}));

        // A buffer pass ships its own main() and no entry scaffold.
        QVERIFY(writePackFile(tmp.filePath(QStringLiteral("mp-good")), QStringLiteral("buffer0.frag"),
                              "#version 440\n"
                              "layout(location = 0) out vec4 fragColor;\n"
                              "void main() { fragColor = vec4(1.0); }\n"));
        const PackResult good = validate(tmp, QStringLiteral("mp-good"), obj);
        QVERIFY2(!good.report.contains(QRegularExpression(QStringLiteral("buffer0\\.frag\\s+ERROR"))),
                 qPrintable(QStringLiteral("a valid buffer pass must bake clean:\n") + good.report));
        // Belt: the error count is spacing-proof and must be zero too.
        QCOMPARE(good.errors, 0);
        // The report says where a multipass pack's buffers do NOT run.
        QVERIFY2(good.report.contains(QStringLiteral("the compositor runs the final stage alone")),
                 qPrintable(good.report));

        // The same pack with a syntax error in the buffer must be caught
        // HERE, not at the live daemon.
        QJsonObject bad = basePack(QStringLiteral("mp-bad"));
        bad.insert(QStringLiteral("appliesTo"), toArray({eventClass}));
        bad.insert(QStringLiteral("multipass"), true);
        bad.insert(QStringLiteral("bufferShaders"), toArray({QStringLiteral("buffer0.frag")}));
        QVERIFY(writePackFile(tmp.filePath(QStringLiteral("mp-bad")), QStringLiteral("buffer0.frag"),
                              "#version 440\n"
                              "layout(location = 0) out vec4 fragColor;\n"
                              "void main() { fragColor = notADeclaredThing; }\n"));
        const PackResult r = validate(tmp, QStringLiteral("mp-bad"), bad);
        QVERIFY2(r.errors > 0, qPrintable(QStringLiteral("a broken buffer pass must fail the gate:\n") + r.report));
        QVERIFY2(r.report.contains(QRegularExpression(QStringLiteral("buffer0\\.frag\\s+ERROR"))),
                 qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("notADeclaredThing")), qPrintable(r.report));
    }

    /// A buffer pass gets NO p_<id> preamble, because
    /// ShaderNodeRhi::bakeBufferShaders does not splice one — it loads,
    /// expands includes and compiles. The gate must reproduce that exactly:
    /// splicing a preamble the runtime withholds would pass sources that fail
    /// live, and withholding one the runtime splices would fail sources that
    /// work. Pin the direction so a future "helpful" splice is caught. The
    /// diagnostic must name the buffer and the parameter, so the error cannot
    /// be the tool-missing one, and it must carry no did-you-mean hint: the
    /// stage cannot see any p_<id>, so suggesting one would be wrong.
    void multipassBufferShadersGetNoParamPreamble()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        QJsonObject obj = basePack(QStringLiteral("mp-param"));
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("bufferShaders"), toArray({QStringLiteral("buffer0.frag")}));
        obj.insert(QStringLiteral("parameters"),
                   QJsonArray{animationParam(QStringLiteral("strength"), QStringLiteral("float"), 0.5)});

        QVERIFY(writePackFile(tmp.filePath(QStringLiteral("mp-param")), QStringLiteral("buffer0.frag"),
                              "#version 440\n"
                              "layout(location = 0) out vec4 fragColor;\n"
                              "void main() { fragColor = vec4(p_strength); }\n"));

        const PackResult r = validate(tmp, QStringLiteral("mp-param"), obj);
        QVERIFY2(r.report.contains(QRegularExpression(QStringLiteral("buffer0\\.frag\\s+ERROR"))),
                 qPrintable(QStringLiteral("p_<id> in a buffer pass must NOT resolve — the runtime splices no "
                                           "preamble there, so the gate must not either:\n")
                            + r.report));
        QVERIFY2(r.report.contains(QStringLiteral("p_strength")), qPrintable(r.report));
        QVERIFY2(!r.report.contains(QStringLiteral("did you mean")), qPrintable(r.report));
    }

    /// A geometry-class pack's stages are compiled on the compositor arm, not
    /// skipped. This path had no coverage of any kind: the validator printed
    /// "SKIP (compositor-only pack; kwin-path GLSL)" for every desktop-* and
    /// geometry pack, and the only thing that ever compiled them, the GPU bake
    /// test, QSKIPs without a desktop GL 4.5 context, which is exactly the
    /// headless CI case. So a broken pack of that class passed every gate and
    /// failed at the live compositor.
    ///
    /// Both directions are asserted. A test that only checked the clean pack
    /// would still pass if the bake were quietly turned back into a skip, which
    /// is the regression worth catching.
    void geometryClassStagesAreCompiledOnBothHosts()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        // "geometry" alone, the same shape the bundled morph packs declare.
        QJsonObject clean = basePack(QStringLiteral("kwin-clean"));
        clean.insert(QStringLiteral("appliesTo"), toArray({QStringLiteral("geometry")}));
        const PackResult ok = validate(tmp, QStringLiteral("kwin-clean"), clean);
        QCOMPARE(ok.errors, 0);
        // Both stages were actually compiled rather than waved through.
        QVERIFY2(ok.report.contains(compositorLine(QStringLiteral("effect.frag"), QStringLiteral("OK"))),
                 qPrintable(ok.report));
        QVERIFY2(ok.report.contains(previewLine(QStringLiteral("effect.frag"), QStringLiteral("OK"))),
                 qPrintable(ok.report));
        QVERIFY2(!ok.report.contains(QStringLiteral("SKIP")), qPrintable(ok.report));
        // A frag-only per-window pack is linked against the built-in vertex
        // stage, and compiled as KWin 6.7 builds it on a GLES context too.
        QVERIFY2(ok.report.contains(linkLine(QStringLiteral("effect.frag"), QStringLiteral("OK"))),
                 qPrintable(ok.report));
        QVERIFY2(reportLineHas(ok.report, QStringLiteral("effect.frag"), QStringLiteral("OK (compositor GLES 6.7)")),
                 qPrintable(ok.report));

        // The same pack with a GLSL error in the body must fail on BOTH arms:
        // an undeclared identifier is rejected by any dialect, so this fails
        // for the one reason under test (the stages got compiled) and not
        // because of a dialect mismatch.
        QJsonObject broken = basePack(QStringLiteral("kwin-broken"));
        broken.insert(QStringLiteral("appliesTo"), toArray({QStringLiteral("geometry")}));
        QVERIFY(writePackFile(tmp.filePath(QStringLiteral("kwin-broken")), QStringLiteral("effect.frag"),
                              "vec4 pTransition(vec2 uv, float t) { return vec4(notADeclaredThing); }\n"));
        const PackResult bad = validate(tmp, QStringLiteral("kwin-broken"), broken, /*writeFragment=*/false);
        QVERIFY2(bad.errors > 0, qPrintable(bad.report));
        QVERIFY2(bad.report.contains(compositorLine(QStringLiteral("effect.frag"), QStringLiteral("ERROR"))),
                 qPrintable(bad.report));
        QVERIFY2(bad.report.contains(previewLine(QStringLiteral("effect.frag"), QStringLiteral("ERROR"))),
                 qPrintable(bad.report));
        QVERIFY2(bad.report.contains(QStringLiteral("notADeclaredThing")), qPrintable(bad.report));
    }

    /// A geometry-class pack's VERTEX stage is compiled on both hosts too. It
    /// is the stage the geometry packs do their per-vertex work in. The p_<id>
    /// preamble is spliced into it, matching the compositor and the daemon
    /// vertex bake: a vertex-driven pack reading its params must compile, not
    /// fail on an undeclared identifier.
    void geometryClassVertexStageIsCompiledWithParams()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        QJsonObject obj = basePack(QStringLiteral("kwin-vert"));
        obj.insert(QStringLiteral("appliesTo"), toArray({QStringLiteral("geometry")}));
        obj.insert(QStringLiteral("vertexShader"), QStringLiteral("effect.vert"));
        obj.insert(QStringLiteral("parameters"),
                   QJsonArray{animationParam(QStringLiteral("amount"), QStringLiteral("float"), 1.0)});

        // Writes vTexCoord, which the scaffolded fragment reads: the compositor
        // links the two by name, and a stage that left it out would fail to
        // link where it ships (the link check reports exactly that).
        QVERIFY(writePackFile(tmp.filePath(QStringLiteral("kwin-vert")), QStringLiteral("effect.vert"),
                              "#version 450\n"
                              "#include <animation_uniforms.glsl>\n"
                              "layout(location = 0) in vec2 position;\n"
                              "layout(location = 1) in vec2 texCoord;\n"
                              "layout(location = 0) out vec2 vTexCoord;\n"
                              "#ifdef PLASMAZONES_KWIN\n"
                              "uniform mat4 modelViewProjectionMatrix;\n"
                              "#else\n"
                              "#define modelViewProjectionMatrix qt_Matrix\n"
                              "#endif\n"
                              "void main() {\n"
                              "    vTexCoord = texCoord;\n"
                              "    gl_Position = modelViewProjectionMatrix * vec4(position * p_amount, 0.0, 1.0);\n"
                              "}\n"));

        const PackResult r = validate(tmp, QStringLiteral("kwin-vert"), obj);
        QCOMPARE(r.errors, 0);
        // The vertex stage's OWN lines on each arm, not the fragment's: a bake
        // quietly turned back into a skip for one arm leaves that line absent.
        QVERIFY2(r.report.contains(compositorLine(QStringLiteral("effect.vert"), QStringLiteral("OK"))),
                 qPrintable(r.report));
        QVERIFY2(r.report.contains(previewLine(QStringLiteral("effect.vert"), QStringLiteral("OK"))),
                 qPrintable(r.report));
        QVERIFY2(!r.report.contains(QStringLiteral("SKIP")), qPrintable(r.report));
    }

    void animationStagesRequireBothUniformAbis_data()
    {
        QTest::addColumn<QString>("eventClass");
        QTest::addColumn<bool>("vertex");
        QTest::addColumn<bool>("breakPreview");
        for (const QString& event : {QStringLiteral("geometry"), QStringLiteral("appearance")}) {
            for (bool vertex : {false, true}) {
                for (bool preview : {false, true}) {
                    const QString row = event + (vertex ? QStringLiteral("-vert") : QStringLiteral("-frag"))
                        + (preview ? QStringLiteral("-preview") : QStringLiteral("-compositor"));
                    QTest::newRow(qPrintable(row)) << event << vertex << preview;
                }
            }
        }
    }

    /// A stage that compiles on one host must still fail when the other ABI
    /// is broken, and the failure must be isolated to that host: the OTHER
    /// arm reports OK on the same pack. The loose geometry uniforms reproduce
    /// Pressed Paper's original failure: `iFromRect` / `iToRect` are absent
    /// from the shared header's kwin branch and are UBO members on the
    /// preview branch, so an unguarded `uniform vec4 iFromRect;` is valid
    /// classic GL and a redeclaration in the Qt UBO.
    void animationStagesRequireBothUniformAbis()
    {
        QFETCH(QString, eventClass);
        QFETCH(bool, vertex);
        QFETCH(bool, breakPreview);
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);
        const QString name = QStringLiteral("dual-abi");
        QJsonObject obj = basePack(name);
        obj.insert(QStringLiteral("appliesTo"), toArray({eventClass}));
        if (vertex) {
            obj.insert(QStringLiteral("vertexShader"), QStringLiteral("effect.vert"));
        }
        const QString dir = tmp.filePath(name);
        const QString stage = vertex ? QStringLiteral("effect.vert") : QStringLiteral("effect.frag");
        const auto writeStage = [&](bool broken) {
            QByteArray source;
            if (vertex) {
                // vTexCoord is the varying the scaffolded fragment reads,
                // which the link checks the stage writes.
                source +=
                    "#version 450\n#include <animation_uniforms.glsl>\n"
                    "layout(location = 0) out vec2 vTexCoord;\n";
            }
            if (!broken || !breakPreview) {
                source += "#ifdef PLASMAZONES_KWIN\n";
            }
            source += "uniform vec4 iFromRect;\nuniform vec4 iToRect;\n";
            if (broken && !breakPreview) {
                source += "#define iFromRect missingCompositorRect\n";
            }
            if (!broken || !breakPreview) {
                source += "#endif\n";
            }
            source += vertex ? "void main() { vTexCoord = iFromRect.xy; gl_Position = iFromRect + iToRect; }\n"
                             : "vec4 pTransition(vec2 uv, float t) { return iFromRect + iToRect; }\n";
            return writePackFile(dir, stage, source);
        };

        QVERIFY(writeStage(true));
        const PackResult bad = validate(tmp, name, obj, /*writeFragment=*/vertex);
        QVERIFY2(bad.errors > 0, qPrintable(bad.report));
        const QRegularExpression failed =
            breakPreview ? previewLine(stage, QStringLiteral("ERROR")) : compositorLine(stage, QStringLiteral("ERROR"));
        const QRegularExpression survived =
            breakPreview ? compositorLine(stage, QStringLiteral("OK")) : previewLine(stage, QStringLiteral("OK"));
        QVERIFY2(bad.report.contains(failed), qPrintable(bad.report));
        // Isolation: the fixture breaks ONE host, so the other must still
        // pass on it, or the row proves only that something failed.
        QVERIFY2(bad.report.contains(survived), qPrintable(bad.report));
        QVERIFY2(
            bad.report.contains(breakPreview ? QStringLiteral("iFromRect") : QStringLiteral("missingCompositorRect")),
            qPrintable(bad.report));

        QVERIFY(writeStage(false));
        const PackResult good = validate(tmp, name, obj, /*writeFragment=*/vertex);
        QVERIFY2(good.errors == 0, qPrintable(good.report));
        QVERIFY2(good.report.contains(previewLine(stage, QStringLiteral("OK"))), qPrintable(good.report));
        QVERIFY2(good.report.contains(compositorLine(stage, QStringLiteral("OK"))), qPrintable(good.report));
    }

    /// The compositor resolves `<...>` includes against the shared roots
    /// only, while the preview's expander also searches the shader's own
    /// directory. A pack-local header included with angle brackets therefore
    /// works in the preview and fails in the compositor, and the compositor
    /// arm must reproduce that rather than wave it through. The same header
    /// included with quotes resolves on both.
    void packLocalAngleBracketIncludeFailsTheCompositorArmOnly()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        const QString dir = tmp.filePath(QStringLiteral("local-include"));
        QVERIFY(writePackFile(dir, QStringLiteral("mylocal.glsl"), "vec4 localColor() { return vec4(0.5); }\n"));
        QJsonObject obj = basePack(QStringLiteral("local-include"));

        QVERIFY(writePackFile(dir, QStringLiteral("effect.frag"),
                              "#include <mylocal.glsl>\n"
                              "vec4 pTransition(vec2 uv, float t) { return localColor(); }\n"));
        const PackResult angled = validate(tmp, QStringLiteral("local-include"), obj, /*writeFragment=*/false);
        QVERIFY2(angled.errors > 0, qPrintable(angled.report));
        // An include failure is the same in both dialects, so the arm reports
        // it once for the pair.
        QVERIFY2(angled.report.contains(
                     QRegularExpression(QStringLiteral("effect\\.frag\\s+ERROR \\(compositor, both dialects\\)"))),
                 qPrintable(angled.report));
        QVERIFY2(angled.report.contains(previewLine(QStringLiteral("effect.frag"), QStringLiteral("OK"))),
                 qPrintable(angled.report));

        QVERIFY(writePackFile(dir, QStringLiteral("effect.frag"),
                              "#include \"mylocal.glsl\"\n"
                              "vec4 pTransition(vec2 uv, float t) { return localColor(); }\n"));
        const PackResult quoted = validate(tmp, QStringLiteral("local-include"), obj, /*writeFragment=*/false);
        QCOMPARE(quoted.errors, 0);
    }

    /// The per-window compositor path declares `pzFinalizeColor` and the
    /// `PZ_FINALIZE_COLOR` macro above the pack; the bake splices an identity
    /// stand-in with the same names, so a pack that defines the function
    /// itself collides here the way it collides live. The desktop and strip
    /// passes splice no such block, so the same source is clean for them.
    void finalizeColorSymbolCollisionIsCaughtOnThePerWindowPath()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        const QByteArray body =
            "vec4 pzFinalizeColor(vec4 c) { return c * 0.5; }\n"
            "vec4 pTransition(vec2 uv, float t) { return pzFinalizeColor(vec4(t)); }\n";

        QJsonObject window = basePack(QStringLiteral("finalize-window"));
        window.insert(QStringLiteral("appliesTo"), toArray({QStringLiteral("appearance")}));
        QVERIFY(writePackFile(tmp.filePath(QStringLiteral("finalize-window")), QStringLiteral("effect.frag"), body));
        const PackResult collides = validate(tmp, QStringLiteral("finalize-window"), window, /*writeFragment=*/false);
        QVERIFY2(collides.errors > 0, qPrintable(collides.report));
        QVERIFY2(collides.report.contains(compositorLine(QStringLiteral("effect.frag"), QStringLiteral("ERROR"))),
                 qPrintable(collides.report));
        QVERIFY2(collides.report.contains(QStringLiteral("pzFinalizeColor")), qPrintable(collides.report));

        QJsonObject desktop = basePack(QStringLiteral("finalize-desktop"));
        desktop.insert(QStringLiteral("appliesTo"), toArray({QStringLiteral("desktop")}));
        QVERIFY(writePackFile(tmp.filePath(QStringLiteral("finalize-desktop")), QStringLiteral("effect.frag"), body));
        const PackResult screen = validate(tmp, QStringLiteral("finalize-desktop"), desktop, /*writeFragment=*/false);
        QVERIFY2(!screen.report.contains(compositorLine(QStringLiteral("effect.frag"), QStringLiteral("ERROR"))),
                 qPrintable(screen.report));
        // The screen-level frag links against the screen vertex stage, and
        // gets no 6.8-only define, so no 6.7 GLES arm either.
        QVERIFY2(screen.report.contains(linkLine(QStringLiteral("effect.frag"), QStringLiteral("OK"))),
                 qPrintable(screen.report));
        QVERIFY2(!screen.report.contains(QStringLiteral("(compositor GLES 6.7)")), qPrintable(screen.report));
    }

    /// An empty stage file is reported as empty, under the stage's label,
    /// once. It used to be reported as a failed include expansion with an
    /// empty reason, once per arm.
    void anEmptyStageFileIsReportedAsEmpty()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        QJsonObject obj = basePack(QStringLiteral("empty-frag"));
        QVERIFY(writePackFile(tmp.filePath(QStringLiteral("empty-frag")), QStringLiteral("effect.frag"), ""));
        const PackResult r = validate(tmp, QStringLiteral("empty-frag"), obj, /*writeFragment=*/false);
        QCOMPARE(r.errors, 1);
        QVERIFY2(r.report.contains(QStringLiteral("shader file is empty")), qPrintable(r.report));
        QVERIFY2(!r.report.contains(QStringLiteral("include expansion failed")), qPrintable(r.report));
    }

    // ── the compositor's GLES arm and link ──────────────────────────────────
    // The slots below cover the checks the GLES arms and the link add. Each
    // negative one fails one such check and passes every other, so deleting
    // that check (or turning the arm back into a single desktop compile)
    // leaves its slot red.

    /// A varying location the strip does not recognise is legal on a 6.7
    /// desktop context (GL_ARB_separate_shader_objects is enabled there) and
    /// rejected by GLSL ES 300, which is #1142 exactly. Only the GLES arm can
    /// see it.
    void aVaryingLocationTheStripMissesFailsOnlyTheGlesArm()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);
        const QByteArray frag = "vec4 pTransition(vec2 uv, float t) { return vec4(uv, t, 1.0); }\n";
        const PackResult r = validateWithVertex(
            tmp, QStringLiteral("loc-missed"), frag,
            animationVertex("layout(location = 1) /* not stripped */ out vec2 vExtra;\n", "    vExtra = position;\n"));
        QVERIFY2(r.report.contains(compositorLine(QStringLiteral("effect.vert"), QStringLiteral("OK"))),
                 qPrintable(r.report));
        QVERIFY2(r.report.contains(glesLine(QStringLiteral("effect.vert"), QStringLiteral("ERROR"))),
                 qPrintable(r.report));
        QCOMPARE(r.errors, 1);
    }

    /// The compositor strips the locations and links varyings by NAME, which
    /// glslang's own link does not check. A fragment reading a varying under
    /// a name the vertex stage does not write fails the link, while the
    /// preview (SPIR-V, matched by location) stays clean.
    void aVaryingReadUnderAnotherNameFailsTheLink()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);
        const QByteArray frag =
            "layout(location = 1) in vec2 vWanted;\n"
            "vec4 pTransition(vec2 uv, float t) { return vec4(vWanted, t, 1.0); }\n";
        const PackResult bad = validateWithVertex(
            tmp, QStringLiteral("name-bad"), frag,
            animationVertex("layout(location = 1) out vec2 vWritten;\n", "    vWritten = position;\n"));
        QVERIFY2(bad.report.contains(linkLine(QStringLiteral("effect.frag"), QStringLiteral("ERROR"))),
                 qPrintable(bad.report));
        QVERIFY2(bad.report.contains(QStringLiteral("reads the varying vWanted")), qPrintable(bad.report));
        QVERIFY2(bad.report.contains(previewLine(QStringLiteral("effect.frag"), QStringLiteral("OK"))),
                 qPrintable(bad.report));
        QCOMPARE(bad.errors, 1);

        // The same pair with the names agreeing links, so the check fires on
        // the mismatch and not on every pack with a varying of its own.
        const PackResult good = validateWithVertex(
            tmp, QStringLiteral("name-good"), frag,
            animationVertex("layout(location = 1) out vec2 vWanted;\n", "    vWanted = position;\n"));
        QVERIFY2(good.report.contains(linkLine(QStringLiteral("effect.frag"), QStringLiteral("OK"))),
                 qPrintable(good.report));
        QCOMPARE(good.errors, 0);
    }

    /// GLSL ES defaults int to highp in the vertex stage and mediump in the
    /// fragment, and a uniform both stages declare must agree on precision to
    /// link, which compiling each stage alone cannot show.
    void aUniformPrecisionMismatchFailsTheLink()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);
        const QByteArray frag =
            "#ifdef PLASMAZONES_KWIN\nuniform highp int uLinked;\n#else\n#define uLinked 0\n#endif\n"
            "vec4 pTransition(vec2 uv, float t) { return vec4(uv, float(uLinked), 1.0); }\n";
        const PackResult r = validateWithVertex(
            tmp, QStringLiteral("precision"), frag,
            animationVertex("#ifdef PLASMAZONES_KWIN\nuniform mediump int uLinked;\n#else\n#define uLinked 0\n#endif\n",
                            "    vTexCoord *= float(uLinked);\n"));
        QVERIFY2(r.report.contains(linkLine(QStringLiteral("effect.frag"), QStringLiteral("ERROR"))),
                 qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("Precision qualifiers must match")), qPrintable(r.report));
        QCOMPARE(r.errors, 1);
    }

    /// An `#extension` after KWin's precision declarations is rejected by Mesa
    /// and NVIDIA and accepted by glslang, so the arm checks it by hand. The
    /// define block's own extensions sit behind `#ifndef GL_ES` and pass.
    void aMidShaderExtensionFailsTheLink()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);
        const QByteArray frag =
            "#if defined(PLASMAZONES_KWIN) && defined(GL_ES)\n#extension GL_OES_standard_derivatives : enable\n#endif\n"
            "vec4 pTransition(vec2 uv, float t) { return vec4(uv, t, 1.0); }\n";
        const PackResult r = validateWithVertex(tmp, QStringLiteral("mid-ext"), frag, animationVertex(""));
        QVERIFY2(r.report.contains(glesLine(QStringLiteral("effect.frag"), QStringLiteral("OK"))),
                 qPrintable(r.report));
        QVERIFY2(r.report.contains(linkLine(QStringLiteral("effect.frag"), QStringLiteral("ERROR"))),
                 qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("enables GL_OES_standard_derivatives after KWin's precision")),
                 qPrintable(r.report));
        QCOMPARE(r.errors, 1);
    }

    /// The compositor's own built-in vertex stages, which every frag-only pack
    /// links against and no pack declares, compile in both of KWin's dialects,
    /// with and without the top-down define the stages that key off it get on
    /// 6.8 (the arm without it is the one 6.7 builds on a GLES context). The decoration present stage is the one #1142
    /// broke.
    void theBuiltinCompositorVertexStagesCompileInBothDialects()
    {
        if (PlasmaZones::ShaderValidate::glslangValidatorPath().isEmpty()) {
            QSKIP("glslangValidator not on PATH");
        }
        using PlasmaZones::ShaderValidate::builtinCompositorStage;
        const QString topDown = PhosphorShaders::kwinTopDownTexcoordsDefine();
        const auto vertex = PhosphorShaders::ShaderStage::Vertex;
        const QList<PlasmaZones::ShaderValidate::CompositorStage> stages = {
            builtinCompositorStage(QStringLiteral("animation"), vertex,
                                   PhosphorAnimationShaders::AnimationShaderRegistry::compositorVertexSource(),
                                   topDown),
            builtinCompositorStage(QStringLiteral("screen"), vertex,
                                   PhosphorAnimationShaders::AnimationShaderRegistry::compositorScreenVertexSource()),
            builtinCompositorStage(QStringLiteral("surface"), vertex,
                                   PhosphorSurfaceShaders::SurfaceShaderRegistry::compositorVertexSource()),
            builtinCompositorStage(QStringLiteral("surface-buffer"), vertex,
                                   PhosphorSurfaceShaders::SurfaceShaderRegistry::compositorBufferVertexSource()),
            builtinCompositorStage(QStringLiteral("present"), vertex,
                                   PhosphorSurfaceShaders::SurfaceShaderRegistry::compositorPresentVertexSource(),
                                   topDown),
            builtinCompositorStage(QStringLiteral("pointer"), vertex,
                                   PhosphorPointerShaders::PointerShaderRegistry::compositorVertexSource()),
        };
        for (const auto& stage : stages) {
            QString report;
            QTextStream out(&report);
            const int errors = PlasmaZones::ShaderValidate::reportCompositorCompile(out, stage);
            out.flush();
            QVERIFY2(errors == 0, qPrintable(report));
            QVERIFY2(report.contains(compositorLine(stage.label, QStringLiteral("OK"))), qPrintable(report));
            QVERIFY2(report.contains(glesLine(stage.label, QStringLiteral("OK"))), qPrintable(report));
            // A stage with a 6.8-only define is also compiled without it, as
            // KWin 6.7 builds it on a GLES context.
            if (!stage.glesExtraDefines.isEmpty()) {
                QVERIFY2(reportLineHas(report, stage.label, QStringLiteral("OK (compositor GLES 6.7)")),
                         qPrintable(report));
            }
        }
    }

    /// A compile error reports the author's own line in BOTH dialects and on
    /// the preview. Desktop GLSL 140 numbers the line after `#line N` as N+1
    /// where 330+ and ES 300 number it N, so a plain re-anchor put every 6.7
    /// diagnostic one line low, and the family prologue an entry-only pack is
    /// wrapped in pushed every host's line past the fault by its own length.
    /// The include on the author's line 1 checks the resolver's re-anchor
    /// after it counts from the author's first line too.
    void aCompileErrorNamesTheSameAuthorLineInBothDialects()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);
        QJsonObject obj = basePack(QStringLiteral("line-numbers"));
        obj.insert(QStringLiteral("appliesTo"), toArray({QStringLiteral("appearance")}));
        QVERIFY(writePackFile(tmp.filePath(QStringLiteral("line-numbers")), QStringLiteral("effect.frag"),
                              "#include <animation_uniforms.glsl>\n"
                              "// a comment on line 2\n"
                              "// a comment on line 3\n"
                              "vec4 pTransition(vec2 uv, float t) { return vec4(notDeclaredOnLineFour); }\n"));
        const PackResult r = validate(tmp, QStringLiteral("line-numbers"), obj, /*writeFragment=*/false);
        QVERIFY2(r.report.contains(compositorLine(QStringLiteral("effect.frag"), QStringLiteral("ERROR"))),
                 qPrintable(r.report));
        QVERIFY2(r.report.contains(glesLine(QStringLiteral("effect.frag"), QStringLiteral("ERROR"))),
                 qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("(the same diagnostics as the compositor arm above)")),
                 qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("ERROR: effect.frag:4: 'notDeclaredOnLineFour'")),
                 qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("ERROR: effect.frag (Qt-RHI preview):4: 'notDeclaredOnLineFour'")),
                 qPrintable(r.report));
    }

    /// A surface pack with its own vertex stage includes the family header in
    /// both stages, so both declare its int uniforms. Without the header's
    /// GLES `precision highp int` the two stages default to different int
    /// precisions and fail to link on a GLES context.
    void aSurfaceVertexStageSharingTheHeaderLinks()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);
        QJsonObject obj = surfacePack(QStringLiteral("sf-shared-int"), QJsonArray{});
        obj.insert(QStringLiteral("vertexShader"), QStringLiteral("probe.vert"));
        const PackResult r = validateSurface(
            tmp, QStringLiteral("sf-shared-int"), obj,
            QStringLiteral("vec4 pSurface(vec2 uv)\n{\n    return vec4(uv, float(iAudioSpectrumSize), 1.0);\n}\n"),
            packVertexBody(
                QStringLiteral("    gl_Position = vec4(position * float(iAudioSpectrumSize + 1), 0.0, 1.0);\n")));
        QVERIFY2(r.report.contains(linkLine(QStringLiteral("effect.frag"), QStringLiteral("OK"))),
                 qPrintable(r.report));
        QCOMPARE(r.errors, 0);
    }

    /// Mesa rejects an `#extension` after a declaration on a desktop context
    /// too, where NVIDIA lets it through and glslang accepts it anywhere. On a
    /// per-window fragment the compositor's colour block is a declaration
    /// that precedes every line of the pack, so a guard against GLES alone
    /// does not save it.
    void aMidShaderExtensionFailsTheDesktopLinkCheckToo()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);
        const QByteArray frag =
            "#if defined(PLASMAZONES_KWIN) && !defined(GL_ES)\n"
            "#extension GL_ARB_shading_language_420pack : enable\n#endif\n"
            "vec4 pTransition(vec2 uv, float t) { return vec4(uv, t, 1.0); }\n";
        const PackResult r = validateWithVertex(tmp, QStringLiteral("mid-ext-desktop"), frag, animationVertex(""));
        QVERIFY2(r.report.contains(compositorLine(QStringLiteral("effect.frag"), QStringLiteral("OK"))),
                 qPrintable(r.report));
        QVERIFY2(r.report.contains(linkLine(QStringLiteral("effect.frag"), QStringLiteral("ERROR"))),
                 qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("enables GL_ARB_shading_language_420pack after a declaration, "
                                                  "which Mesa rejects")),
                 qPrintable(r.report));
        // The scaffold and the colour block come first here, so the advice is
        // that the stage cannot have the extension, not where to move it.
        QVERIFY2(r.report.contains(QStringLiteral("cannot enable an extension on Mesa at all")), qPrintable(r.report));
        QCOMPARE(r.errors, 1);

        // A declared vertex stage gets nothing above its own code, so there
        // the extension can move. A `#pragma` counts as a statement on Mesa.
        const QByteArray vert =
            "#version 450\n"
            "#if defined(PLASMAZONES_KWIN) && !defined(GL_ES)\n"
            "#pragma optimize(on)\n"
            "#extension GL_ARB_shading_language_420pack : enable\n"
            "#endif\n"
            "#include <animation_uniforms.glsl>\n"
            "layout(location = 0) in vec2 position;\n"
            "layout(location = 1) in vec2 texCoord;\n"
            "layout(location = 0) out vec2 vTexCoord;\n"
            "void main() { vTexCoord = texCoord; gl_Position = vec4(position, 0.0, 1.0); }\n";
        const PackResult movable =
            validateWithVertex(tmp, QStringLiteral("mid-ext-vert"),
                               "vec4 pTransition(vec2 uv, float t) { return vec4(uv, t, 1.0); }\n", vert);
        QVERIFY2(movable.report.contains(QStringLiteral("effect.vert enables GL_ARB_shading_language_420pack after a "
                                                        "declaration")),
                 qPrintable(movable.report));
        QVERIFY2(movable.report.contains(QStringLiteral("Move it directly under #version")),
                 qPrintable(movable.report));
        QCOMPARE(movable.errors, 1);
    }

    /// GLSL fails a link only on an unmatched input the fragment USES, and
    /// Mesa and NVIDIA both link an unused one. Nor may declaring several
    /// varyings on one line hide them from the name check: there the check
    /// once saw neither the vertex outputs nor the fragment inputs.
    void theVaryingNameCheckFollowsUseAndEveryDeclarator()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);
        const QByteArray unusedFrag =
            "layout(location = 1) in vec2 vNeverWritten;\n"
            "vec4 pTransition(vec2 uv, float t) { return vec4(uv, t, 1.0); }\n";
        const PackResult unused = validateWithVertex(tmp, QStringLiteral("unused-in"), unusedFrag, animationVertex(""));
        QVERIFY2(unused.report.contains(linkLine(QStringLiteral("effect.frag"), QStringLiteral("OK"))),
                 qPrintable(unused.report));
        QCOMPARE(unused.errors, 0);

        // Several declarators on a line are compositor-only here: SPIR-V wants
        // a location per varying, so the preview branch declares them apart.
        const QByteArray multiVert = animationVertex(
            "#ifdef PLASMAZONES_KWIN\nout vec2 vFirst, vSecond;\n#else\n"
            "layout(location = 1) out vec2 vFirst;\nlayout(location = 2) out vec2 vSecond;\n#endif\n",
            "    vFirst = position;\n    vSecond = position;\n");
        const QByteArray readsSecond =
            "#ifdef PLASMAZONES_KWIN\nin vec2 vSecond, vOther;\n#else\n"
            "layout(location = 2) in vec2 vSecond;\nlayout(location = 3) in vec2 vOther;\n#endif\n"
            "vec4 pTransition(vec2 uv, float t) { return vec4(vSecond, t, 1.0); }\n";
        const PackResult multi = validateWithVertex(tmp, QStringLiteral("multi-decl"), readsSecond, multiVert);
        QVERIFY2(multi.report.contains(linkLine(QStringLiteral("effect.frag"), QStringLiteral("OK"))),
                 qPrintable(multi.report));
        QCOMPARE(multi.errors, 0);

        const QByteArray readsOther =
            "#ifdef PLASMAZONES_KWIN\nin vec2 vSecond, vOther;\n#else\n"
            "layout(location = 2) in vec2 vSecond;\nlayout(location = 3) in vec2 vOther;\n#endif\n"
            "vec4 pTransition(vec2 uv, float t) { return vec4(vOther, t, 1.0); }\n";
        const PackResult missed = validateWithVertex(tmp, QStringLiteral("multi-missed"), readsOther, multiVert);
        QVERIFY2(missed.report.contains(linkLine(QStringLiteral("effect.frag"), QStringLiteral("ERROR"))),
                 qPrintable(missed.report));
        QVERIFY2(missed.report.contains(QStringLiteral("reads the varying vOther")), qPrintable(missed.report));
        QCOMPARE(missed.errors, 1);

        // A parameter of the same name is a declarator, not a use of the
        // input, and two declarations on one line are both read.
        const QByteArray shadowFrag =
            "layout(location = 1) in vec2 vShadowed;\n"
            "#ifdef PLASMAZONES_KWIN\nin vec2 vOne; in vec2 vTwo;\n#else\n"
            "layout(location = 2) in vec2 vOne;\nlayout(location = 3) in vec2 vTwo;\n#endif\n"
            "float unusedHelper(vec2 vShadowed) { return 1.0; }\n"
            "vec4 pTransition(vec2 uv, float t) { return vec4(vTwo, t, 1.0); }\n";
        const PackResult shadow = validateWithVertex(tmp, QStringLiteral("shadow"), shadowFrag, animationVertex(""));
        QVERIFY2(shadow.report.contains(linkLine(QStringLiteral("effect.frag"), QStringLiteral("ERROR"))),
                 qPrintable(shadow.report));
        QVERIFY2(shadow.report.contains(QStringLiteral("reads the varying vTwo")), qPrintable(shadow.report));
        QVERIFY2(!shadow.report.contains(QStringLiteral("reads the varying vShadowed")), qPrintable(shadow.report));
        QVERIFY2(!shadow.report.contains(QStringLiteral("reads the varying vOne")), qPrintable(shadow.report));
    }

    /// Use, type and dialect follow GLSL rather than the text: a member of the
    /// same name is not the input, a `return` or `else` operand is a use, an
    /// array size on the type equals one on the name, and a varying declared
    /// on a desktop context alone is checked there.
    void theVaryingCheckFollowsGlslRatherThanText()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);
        const QByteArray member =
            "layout(location = 1) in vec2 vGhost;\n"
            "struct Holder { vec2 vGhost; };\n"
            "vec4 pTransition(vec2 uv, float t) { Holder h; h.vGhost = uv; return vec4(h.vGhost, t, 1.0); }\n";
        const PackResult m = validateWithVertex(tmp, QStringLiteral("member"), member, animationVertex(""));
        QVERIFY2(m.report.contains(linkLine(QStringLiteral("effect.frag"), QStringLiteral("OK"))),
                 qPrintable(m.report));

        const QByteArray keywords =
            "layout(location = 1) in vec2 vReturned;\n"
            "layout(location = 2) in vec2 vElse;\n"
            "vec2 pick() { return vReturned; }\n"
            "vec4 pTransition(vec2 uv, float t) {\n"
            "    float f = 0.0;\n"
            "    if (t > 1.0) f = 1.0; else vElse.x > 0.0 ? (f = 2.0) : (f = 3.0);\n"
            "    return vec4(pick(), t, f);\n"
            "}\n";
        const PackResult k = validateWithVertex(tmp, QStringLiteral("keywords"), keywords, animationVertex(""));
        QVERIFY2(k.report.contains(QStringLiteral("reads the varying vReturned")), qPrintable(k.report));
        QVERIFY2(k.report.contains(QStringLiteral("reads the varying vElse")), qPrintable(k.report));

        const QByteArray arrayVert = animationVertex(
            "#ifdef PLASMAZONES_KWIN\nout vec2 vArr[2];\n#else\nlayout(location = 1) out vec2 vArr[2];\n#endif\n",
            "    vArr[0] = position;\n    vArr[1] = texCoord;\n");
        const QByteArray arrayFrag =
            "#ifdef PLASMAZONES_KWIN\nin vec2[2] vArr;\n#else\nlayout(location = 1) in vec2 vArr[2];\n#endif\n"
            "vec4 pTransition(vec2 uv, float t) { return vec4(vArr[0] + vArr[1], t, 1.0); }\n";
        const PackResult arr = validateWithVertex(tmp, QStringLiteral("array-type"), arrayFrag, arrayVert);
        QVERIFY2(arr.report.contains(linkLine(QStringLiteral("effect.frag"), QStringLiteral("OK"))),
                 qPrintable(arr.report));
        QCOMPARE(arr.errors, 0);

        const QByteArray desktopOnly =
            "#if defined(PLASMAZONES_KWIN) && !defined(GL_ES)\n"
            "in vec2 vDesktopOnly;\n#define PZ_PROBE_UV vDesktopOnly\n"
            "#else\n#define PZ_PROBE_UV uv\n#endif\n"
            "vec4 pTransition(vec2 uv, float t) { return vec4(PZ_PROBE_UV, t, 1.0); }\n";
        const PackResult d = validateWithVertex(tmp, QStringLiteral("desktop-only"), desktopOnly, animationVertex(""));
        QVERIFY2(d.report.contains(linkLine(QStringLiteral("effect.frag"), QStringLiteral("ERROR"))),
                 qPrintable(d.report));
        QVERIFY2(d.report.contains(QStringLiteral("reads the varying vDesktopOnly")), qPrintable(d.report));
    }

    /// A per-window fragment with its own main() still gets the compositor's
    /// colour block above its first line, so an `#extension` there cannot be
    /// moved high enough, and the advice says so.
    void aLateExtensionUnderTheColourBlockCannotBeMoved()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);
        QJsonObject obj = basePack(QStringLiteral("colour-first"));
        obj.insert(QStringLiteral("appliesTo"), toArray({QStringLiteral("appearance")}));
        QVERIFY(writePackFile(tmp.filePath(QStringLiteral("colour-first")), QStringLiteral("effect.frag"),
                              "#version 450\n"
                              "#if defined(PLASMAZONES_KWIN) && !defined(GL_ES)\n"
                              "#extension GL_ARB_shading_language_420pack : enable\n"
                              "#endif\n"
                              "#include <animation_uniforms.glsl>\n"
                              "layout(location = 0) in vec2 vTexCoord;\n"
                              "layout(location = 0) out vec4 fragColor;\n"
                              "void main() { fragColor = PZ_FINALIZE_COLOR(vec4(vTexCoord, 0.0, 1.0)); }\n"));
        const PackResult r = validate(tmp, QStringLiteral("colour-first"), obj, /*writeFragment=*/false);
        QVERIFY2(r.report.contains(linkLine(QStringLiteral("effect.frag"), QStringLiteral("ERROR"))),
                 qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("cannot enable an extension on Mesa at all")), qPrintable(r.report));
    }

    /// A screen-level pass always runs with the compositor's screen vertex
    /// stage, and `vertexShader` is ignored there, so the advice for a varying
    /// it does not write must not be to declare one.
    void aFixedBuiltinVertexStageGetsAdviceThatFits()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);
        QJsonObject obj = basePack(QStringLiteral("screen-varying"));
        obj.insert(QStringLiteral("appliesTo"), toArray({QStringLiteral("desktop")}));
        QVERIFY(writePackFile(tmp.filePath(QStringLiteral("screen-varying")), QStringLiteral("effect.frag"),
                              "layout(location = 1) in vec2 vScreenExtra;\n"
                              "vec4 pTransition(vec2 uv, float t) { return vec4(vScreenExtra, t, 1.0); }\n"));
        const PackResult r = validate(tmp, QStringLiteral("screen-varying"), obj, /*writeFragment=*/false);
        QVERIFY2(r.report.contains(linkLine(QStringLiteral("effect.frag"), QStringLiteral("ERROR"))),
                 qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("This pass always runs with that stage")), qPrintable(r.report));
        QVERIFY2(!r.report.contains(QStringLiteral("declare a vertexShader")), qPrintable(r.report));
    }

    /// KWin 6.7 on a GLES context compiles a per-window stage as GLSL ES 300
    /// WITHOUT the top-down texcoord define 6.8 adds, so the branch a header
    /// or pack keeps for 6.7 is compiled in that dialect too. Implicit
    /// int-to-float conversion is legal in desktop GLSL 140 and an error in
    /// GLSL ES, which isolates the one arm.
    void theBottomUpBranchIsCompiledAsGles()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);
        QJsonObject obj = basePack(QStringLiteral("gles-67"));
        obj.insert(QStringLiteral("appliesTo"), toArray({QStringLiteral("appearance")}));
        QVERIFY(writePackFile(tmp.filePath(QStringLiteral("gles-67")), QStringLiteral("effect.frag"),
                              "#if defined(PLASMAZONES_KWIN) && !defined(PZ_KWIN_OFFSCREEN_TEXCOORDS_TOP_DOWN)\n"
                              "const float kBottomUpOnly = 1;\n"
                              "#else\n"
                              "const float kBottomUpOnly = 1.0;\n"
                              "#endif\n"
                              "vec4 pTransition(vec2 uv, float t) { return vec4(uv, t, kBottomUpOnly); }\n"));
        const PackResult r = validate(tmp, QStringLiteral("gles-67"), obj, /*writeFragment=*/false);
        QVERIFY2(r.report.contains(compositorLine(QStringLiteral("effect.frag"), QStringLiteral("OK"))),
                 qPrintable(r.report));
        QVERIFY2(r.report.contains(glesLine(QStringLiteral("effect.frag"), QStringLiteral("OK"))),
                 qPrintable(r.report));
        QVERIFY2(reportLineHas(r.report, QStringLiteral("effect.frag"), QStringLiteral("ERROR (compositor GLES 6.7)")),
                 qPrintable(r.report));
        QCOMPARE(r.errors, 1);
    }
};

QTEST_MAIN(TestAnimationPackBakes)
#include "test_animation_pack_bakes.moc"
