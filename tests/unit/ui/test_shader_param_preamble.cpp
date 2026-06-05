// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pins buildParamPreamble() — the generated `#define pz_<id> <accessor>` block
// that lets shader authors read parameters by name instead of hand-decoding a
// customParams[N].xyzw lane. Auto-slotting must match the lane the runtime
// uploads to, so these tests pin the declaration-order numbering, the
// independent scalar/color/image pools, explicit-slot honouring, and the
// skip-don't-break behaviour for bad input.

#include <PhosphorShaders/ShaderParamPreamble.h>

#include <QTest>

using PhosphorShaders::PreambleParam;

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
        QVERIFY2(out.contains(QStringLiteral("#define pz_speed customParams[0].x")), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("#define pz_flow customParams[0].y")), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("#define pz_scale customParams[0].z")), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("#define pz_detail customParams[0].w")), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("#define pz_shift customParams[1].x")), qPrintable(out));
    }

    // Color and scalar pools advance independently — a color does not consume a
    // scalar sub-slot (mirrors the runtime's two-allocator contract).
    void testPoolsAreIndependent()
    {
        const QString out = PhosphorShaders::buildParamPreamble(
            {color(QStringLiteral("tint")), scalar(QStringLiteral("amount")), color(QStringLiteral("glow"))});
        QVERIFY2(out.contains(QStringLiteral("#define pz_tint customColors[0]")), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("#define pz_amount customParams[0].x")), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("#define pz_glow customColors[1]")), qPrintable(out));
    }

    // Image params map to uTexture<slot>.
    void testImagePool()
    {
        const QString out =
            PhosphorShaders::buildParamPreamble({image(QStringLiteral("logo")), image(QStringLiteral("mask"))});
        QVERIFY2(out.contains(QStringLiteral("#define pz_logo uTexture0")), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("#define pz_mask uTexture1")), qPrintable(out));
    }

    // An explicit slot is honoured verbatim (the zone path carries them today).
    void testExplicitSlotHonoured()
    {
        const QString out = PhosphorShaders::buildParamPreamble({scalar(QStringLiteral("opacity"), 8)});
        QVERIFY2(out.contains(QStringLiteral("#define pz_opacity customParams[2].x")), qPrintable(out));
    }

    // A bad identifier or out-of-range slot is skipped with a comment, never a
    // broken #define — the block must always compile.
    void testInvalidInputsAreSkippedNotBroken()
    {
        const QString out =
            PhosphorShaders::buildParamPreamble({scalar(QStringLiteral("has space")), scalar(QStringLiteral("good")),
                                                 scalar(QStringLiteral("waytoobig"), 99)});
        QVERIFY2(!out.contains(QStringLiteral("#define pz_has space")), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("#define pz_good customParams[0].x")), qPrintable(out));
        QVERIFY2(!out.contains(QStringLiteral("#define pz_waytoobig")), qPrintable(out));
        // The skipped entries leave a comment trail.
        QVERIFY2(out.contains(QStringLiteral("// pz: skipped")), qPrintable(out));
    }

    // A leading digit in the id is fine — the pz_ prefix guarantees a valid
    // leading identifier character.
    void testLeadingDigitIdIsOkUnderPrefix()
    {
        const QString out = PhosphorShaders::buildParamPreamble({scalar(QStringLiteral("3dDepth"))});
        QVERIFY2(out.contains(QStringLiteral("#define pz_3dDepth customParams[0].x")), qPrintable(out));
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
        const QString out =
            PhosphorShaders::spliceAfterVersion(src, QStringLiteral("#define pz_x customParams[0].x\n"));
        // #version stays first.
        QVERIFY2(out.startsWith(QStringLiteral("#version 450\n")), qPrintable(out));
        // Block present, then the #line fixup, then the author line.
        const int defPos = out.indexOf(QStringLiteral("#define pz_x"));
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
        const QString out =
            PhosphorShaders::spliceAfterVersion(src, QStringLiteral("#define pz_x customParams[0].x\n"));
        // #version is line 3, so the author's next line is renumbered to 4.
        QVERIFY2(out.contains(QStringLiteral("#line 4 0")), qPrintable(out));
    }

    // No #version: best-effort prepend rather than dropping the block.
    void testSpliceNoVersionPrepends()
    {
        const QString src = QStringLiteral("void main() {}\n");
        const QString block = QStringLiteral("#define pz_x customParams[0].x\n");
        const QString out = PhosphorShaders::spliceAfterVersion(src, block);
        QVERIFY(out.startsWith(block));
    }
};

QTEST_MAIN(TestShaderParamPreamble)
#include "test_shader_param_preamble.moc"
