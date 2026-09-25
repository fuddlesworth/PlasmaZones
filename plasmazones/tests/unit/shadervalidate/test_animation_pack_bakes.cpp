// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The offline pack validator's ANIMATION stage bakes, on both hosts. The
// bundled-pack gate (shader_validate_animations) only proves the shipped packs
// are clean: it cannot show that a BROKEN pack is caught, and it never reaches
// the multipass path at all (no bundled animation pack is multipass). Every
// slot here builds a deliberately broken pack in a temp dir and asserts the
// diagnostic, and its clean twin, so a bake quietly turned back into a skip
// is caught in both directions.
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

        QVERIFY(writePackFile(tmp.filePath(QStringLiteral("kwin-vert")), QStringLiteral("effect.vert"),
                              "#version 450\n"
                              "#include <animation_uniforms.glsl>\n"
                              "layout(location = 0) in vec2 position;\n"
                              "#ifdef PLASMAZONES_KWIN\n"
                              "uniform mat4 modelViewProjectionMatrix;\n"
                              "#else\n"
                              "#define modelViewProjectionMatrix qt_Matrix\n"
                              "#endif\n"
                              "void main() {\n"
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
                source += "#version 450\n#include <animation_uniforms.glsl>\n";
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
            source += vertex ? "void main() { gl_Position = iFromRect + iToRect; }\n"
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
        QVERIFY2(angled.report.contains(compositorLine(QStringLiteral("effect.frag"), QStringLiteral("ERROR"))),
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
};

QTEST_MAIN(TestAnimationPackBakes)
#include "test_animation_pack_bakes.moc"
