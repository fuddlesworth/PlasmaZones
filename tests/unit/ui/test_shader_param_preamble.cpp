// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pins buildParamPreamble() — the generated `#define p_<id> <accessor>` block
// that lets shader authors read parameters by name instead of hand-decoding a
// customParams[N].xyzw lane. Auto-slotting must match the lane the runtime
// uploads to, so these tests pin the declaration-order numbering, the
// independent scalar/color/image pools, explicit-slot honouring, and the
// skip-don't-break behaviour for bad input.

#include <PhosphorShaders/ShaderParamPreamble.h>
#include <PhosphorShaders/ShaderRegistry.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using PhosphorShaders::PreambleParam;
using PhosphorShaders::ShaderRegistry;

class TestShaderParamPreamble : public QObject
{
    Q_OBJECT

    static PreambleParam scalar(const QString& id, int slot = -1)
    {
        return {id, PreambleParam::Pool::Scalar, slot};
    }
    static PreambleParam color(const QString& id, int slot = -1)
    {
        return {id, PreambleParam::Pool::Color, slot};
    }
    static PreambleParam image(const QString& id, int slot = -1)
    {
        return {id, PreambleParam::Pool::Image, slot};
    }

private Q_SLOTS:

    void testEmptyIsEmpty()
    {
        QCOMPARE(PhosphorShaders::buildParamPreamble({}), QString());
    }

    // Scalars auto-number across vec4 lanes in declaration order:
    // slot 0→[0].x, 1→[0].y, 2→[0].z, 3→[0].w, 4→[1].x.
    void testScalarAutoNumbering()
    {
        const QString out = PhosphorShaders::buildParamPreamble(
            {scalar(QStringLiteral("speed")), scalar(QStringLiteral("flow")), scalar(QStringLiteral("scale")),
             scalar(QStringLiteral("detail")), scalar(QStringLiteral("shift"))});
        QVERIFY2(out.contains(QStringLiteral("#define p_speed customParams[0].x")), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("#define p_flow customParams[0].y")), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("#define p_scale customParams[0].z")), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("#define p_detail customParams[0].w")), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("#define p_shift customParams[1].x")), qPrintable(out));
    }

    // Color and scalar pools advance independently — a color does not consume a
    // scalar sub-slot (mirrors the runtime's two-allocator contract).
    void testPoolsAreIndependent()
    {
        const QString out = PhosphorShaders::buildParamPreamble(
            {color(QStringLiteral("tint")), scalar(QStringLiteral("amount")), color(QStringLiteral("glow"))});
        QVERIFY2(out.contains(QStringLiteral("#define p_tint customColors[0]")), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("#define p_amount customParams[0].x")), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("#define p_glow customColors[1]")), qPrintable(out));
    }

    // Image params map to uTexture<slot>.
    void testImagePool()
    {
        const QString out =
            PhosphorShaders::buildParamPreamble({image(QStringLiteral("logo")), image(QStringLiteral("mask"))});
        QVERIFY2(out.contains(QStringLiteral("#define p_logo uTexture0")), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("#define p_mask uTexture1")), qPrintable(out));
    }

    // An explicit slot is honoured verbatim (the zone path carries them today).
    void testExplicitSlotHonoured()
    {
        const QString out = PhosphorShaders::buildParamPreamble({scalar(QStringLiteral("opacity"), 8)});
        QVERIFY2(out.contains(QStringLiteral("#define p_opacity customParams[2].x")), qPrintable(out));
    }

    // Mixed explicit + auto in one pool: the explicit slot is RESERVED first, so
    // the auto params skip it — matching parseShaderMetadata's two-pass auto-slot,
    // so a mixed pack's p_<id> defines and its upload lanes can't drift apart.
    void testMixedExplicitAutoSlotsReserve()
    {
        const QString out = PhosphorShaders::buildParamPreamble(
            {scalar(QStringLiteral("pinned"), 0), scalar(QStringLiteral("a")), scalar(QStringLiteral("b"))});
        QVERIFY2(out.contains(QStringLiteral("#define p_pinned customParams[0].x")), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("#define p_a customParams[0].y")), qPrintable(out)); // slot 1, skips 0
        QVERIFY2(out.contains(QStringLiteral("#define p_b customParams[0].z")), qPrintable(out)); // slot 2
    }

    // A skipped invalid-id param consumes NO lane — a following valid param keeps
    // the next sequential slot (matches parseShaderMetadata's invalid-id skip, so
    // the two auto-numberings stay identical).
    void testInvalidIdConsumesNoLane()
    {
        const QString out = PhosphorShaders::buildParamPreamble(
            {scalar(QStringLiteral("first")), scalar(QStringLiteral("has space")), scalar(QStringLiteral("second"))});
        QVERIFY2(out.contains(QStringLiteral("#define p_first customParams[0].x")), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("#define p_second customParams[0].y")), qPrintable(out)); // slot 1, not 2
    }

    // A bad identifier or out-of-range slot is skipped with a comment, never a
    // broken #define — the block must always compile.
    void testInvalidInputsAreSkippedNotBroken()
    {
        const QString out =
            PhosphorShaders::buildParamPreamble({scalar(QStringLiteral("has space")), scalar(QStringLiteral("good")),
                                                 scalar(QStringLiteral("waytoobig"), 99)});
        QVERIFY2(!out.contains(QStringLiteral("#define p_has space")), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("#define p_good customParams[0].x")), qPrintable(out));
        QVERIFY2(!out.contains(QStringLiteral("#define p_waytoobig")), qPrintable(out));
        // The skipped entries leave a comment trail.
        QVERIFY2(out.contains(QStringLiteral("// p: skipped")), qPrintable(out));
    }

    // A leading digit in the id is fine — the p_ prefix guarantees a valid
    // leading identifier character.
    void testLeadingDigitIdIsOkUnderPrefix()
    {
        const QString out = PhosphorShaders::buildParamPreamble({scalar(QStringLiteral("3dDepth"))});
        QVERIFY2(out.contains(QStringLiteral("#define p_3dDepth customParams[0].x")), qPrintable(out));
    }

    // The block is newline-terminated and self-delimited (no #version/#line).
    void testBlockShape()
    {
        const QString out = PhosphorShaders::buildParamPreamble({scalar(QStringLiteral("x"))});
        QVERIFY(out.endsWith(QLatin1Char('\n')));
        QVERIFY(!out.contains(QStringLiteral("#version")));
        QVERIFY(!out.contains(QStringLiteral("#line")));
    }

    // spliceAfterVersion inserts the block right after #version and emits a
    // `#line 2 0` so the author's line 2 keeps its number.
    void testSpliceAfterVersion()
    {
        const QString src = QStringLiteral("#version 450\nvoid main() {}\n");
        const QString out = PhosphorShaders::spliceAfterVersion(src, QStringLiteral("#define p_x customParams[0].x\n"));
        // #version stays first.
        QVERIFY2(out.startsWith(QStringLiteral("#version 450\n")), qPrintable(out));
        // Block present, then the #line fixup, then the author line.
        const int defPos = out.indexOf(QStringLiteral("#define p_x"));
        const int linePos = out.indexOf(QStringLiteral("#line 2 0"));
        const int mainPos = out.indexOf(QStringLiteral("void main()"));
        QVERIFY2(defPos > 0, qPrintable(out));
        QVERIFY2(linePos > defPos, qPrintable(out));
        QVERIFY2(mainPos > linePos, qPrintable(out));
    }

    // An empty block is a no-op.
    void testSpliceEmptyBlock()
    {
        const QString src = QStringLiteral("#version 450\nvoid main() {}\n");
        QCOMPARE(PhosphorShaders::spliceAfterVersion(src, QString()), src);
    }

    // The #line fixup uses the actual #version line number, not a hardcoded 2,
    // so a leading comment block before #version is handled.
    void testSpliceVersionNotOnFirstLine()
    {
        const QString src = QStringLiteral("// banner\n// more\n#version 450\nvoid main() {}\n");
        const QString out = PhosphorShaders::spliceAfterVersion(src, QStringLiteral("#define p_x customParams[0].x\n"));
        // #version is line 3, so the author's next line is renumbered to 4.
        QVERIFY2(out.contains(QStringLiteral("#line 4 0")), qPrintable(out));
    }

    // A param whose id isn't a valid GLSL identifier must claim NO lane even when
    // it carries an explicit slot — parseShaderMetadata forces it to slot -1, so it
    // gets no define and no upload. Otherwise it would upload to its explicit lane
    // while a valid auto-slot param (the lane was never reserved) collides onto it.
    void testInvalidIdWithExplicitSlotClaimsNoLane()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QByteArray meta =
            R"({"name":"t","parameters":[{"id":"a-b","type":"float","slot":0},{"id":"good","type":"float"}]})";
        QFile f(QDir(tmp.path()).filePath(QStringLiteral("metadata.json")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(meta);
        f.close();

        const ShaderRegistry::ShaderInfo info = ShaderRegistry::parsePackMetadata(tmp.path());
        int badSlot = -99, goodSlot = -99;
        for (const ShaderRegistry::ParameterInfo& p : info.parameters) {
            if (p.id == QLatin1String("a-b")) {
                badSlot = p.slot;
            } else if (p.id == QLatin1String("good")) {
                goodSlot = p.slot;
            }
        }
        QCOMPARE(badSlot, -1); // invalid id → no lane (overrides the explicit slot 0)
        QCOMPARE(goodSlot, 0); // valid id takes lane 0 uncontested
        const QString out = ShaderRegistry::paramPreamble(info);
        QVERIFY2(out.contains(QStringLiteral("#define p_good customParams[0].x")), qPrintable(out));
        QVERIFY2(!out.contains(QStringLiteral("#define p_a")), qPrintable(out));
    }

    // No #version: best-effort prepend rather than dropping the block.
    void testSpliceNoVersionPrepends()
    {
        const QString src = QStringLiteral("void main() {}\n");
        const QString block = QStringLiteral("#define p_x customParams[0].x\n");
        const QString out = PhosphorShaders::spliceAfterVersion(src, block);
        QVERIFY(out.startsWith(block));
    }

    // Zone-side T1.1: ShaderRegistry::paramPreamble maps each declared param's
    // EXPLICIT slot to the SAME GLSL accessor ParameterInfo::uniformName() /
    // translateParamsToUniforms upload to — scalar slot N → customParams[N/4].
    // <xyzw>, color slot N → customColors[N], image slot N → uTexture<N>. Pin the
    // mapping so a migrated zone pack's p_<id> can never drift off the lane the
    // value lands in.
    void testZoneParamPreambleLaneMatch()
    {
        ShaderRegistry::ShaderInfo info;
        ShaderRegistry::ParameterInfo speed;
        speed.id = QStringLiteral("speed");
        speed.type = QStringLiteral("float");
        speed.slot = 21; // 21/4 = 5, 21%4 = 1 → customParams[5].y
        ShaderRegistry::ParameterInfo tint;
        tint.id = QStringLiteral("tint");
        tint.type = QStringLiteral("color");
        tint.slot = 2; // → customColors[2]
        ShaderRegistry::ParameterInfo logo;
        logo.id = QStringLiteral("logo");
        logo.type = QStringLiteral("image");
        logo.slot = 1; // → uTexture1
        info.parameters = {speed, tint, logo};

        const QString out = ShaderRegistry::paramPreamble(info);
        QVERIFY2(out.contains(QStringLiteral("#define p_speed customParams[5].y")), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("#define p_tint customColors[2]")), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("#define p_logo uTexture1")), qPrintable(out));

        // The scalar/color accessors must denote the SAME UBO lane the runtime
        // uploads to (uniformName is the 1-based wire key for that lane).
        QCOMPARE(speed.uniformName(), QStringLiteral("customParams6_y")); // customParams[5].y
        QCOMPARE(tint.uniformName(), QStringLiteral("customColor3")); // customColors[2]
    }
};

QTEST_MAIN(TestShaderParamPreamble)
#include "test_shader_param_preamble.moc"
