// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pins the T1.4 entry-point harness: definesMain / definesFunction detection
// (comment-stripped, definition-not-call) and composeEntryPoint dispatch
// (author main() wins; else wrap the first matching entry function; else pass
// through so the compiler's missing-main() error stands). Getting detection
// wrong would either wrap a pack that already has main() (duplicate main) or
// fail to wrap an entry-only pack (missing main), so these cases are the
// contract the runtimes depend on.

#include <PhosphorShaders/ShaderEntryPoint.h>

#include <QTest>

using PhosphorShaders::EntryCandidate;

class TestShaderEntryPoint : public QObject
{
    Q_OBJECT

    static QList<EntryCandidate> zoneCandidates()
    {
        return {EntryCandidate{QStringLiteral("pZone"), QStringLiteral("void main() { /* ZONE WRAP */ }\n")},
                EntryCandidate{QStringLiteral("pImage"), QStringLiteral("void main() { /* IMAGE WRAP */ }\n")}};
    }

private Q_SLOTS:

    // ── definesMain ───────────────────────────────────────────────────────

    void testDefinesMainPlain()
    {
        QVERIFY(
            PhosphorShaders::definesMain(QStringLiteral("#version 450\nvoid main() {\n fragColor = vec4(1.0);\n}\n")));
    }
    void testDefinesMainVoidArgAndKnr()
    {
        QVERIFY(PhosphorShaders::definesMain(QStringLiteral("void main(void)\n{\n}\n")));
    }
    void testDefinesMainIgnoresLineComment()
    {
        QVERIFY(!PhosphorShaders::definesMain(QStringLiteral("// void main() { } is just docs\nvec4 pZone() { }\n")));
    }
    void testDefinesMainIgnoresBlockComment()
    {
        QVERIFY(!PhosphorShaders::definesMain(QStringLiteral("/* void main() {} */\nvec4 pImage(vec2 c) { }\n")));
    }
    void testDefinesMainNotSubstring()
    {
        // `mymain` / `domain` must not satisfy the whole-word `main`.
        QVERIFY(!PhosphorShaders::definesMain(QStringLiteral("void mymain() { }\nfloat domain() { return 0.0; }\n")));
    }

    // ── definesFunction (definition, not call) ────────────────────────────

    void testDefinesFunctionPlain()
    {
        QVERIFY(PhosphorShaders::definesFunction(QStringLiteral("vec4 pZone(ZoneCtx z) {\n return z.fillColor;\n}\n"),
                                                 QStringLiteral("pZone")));
    }
    void testDefinesFunctionMultilineParams()
    {
        const QString src = QStringLiteral("vec4 pZone(\n   ZoneCtx z,\n   float k)\n{\n return vec4(0.0);\n}\n");
        QVERIFY(PhosphorShaders::definesFunction(src, QStringLiteral("pZone")));
    }
    void testDefinesFunctionRejectsCall()
    {
        // A call, not a definition — no trailing `{` after the arg list.
        QVERIFY(!PhosphorShaders::definesFunction(QStringLiteral("void main() {\n color = pZone(z);\n}\n"),
                                                  QStringLiteral("pZone")));
    }
    void testDefinesFunctionWholeWord()
    {
        QVERIFY(!PhosphorShaders::definesFunction(QStringLiteral("vec4 pZoneHelper(int i) { return vec4(0.0); }\n"),
                                                  QStringLiteral("pZone")));
    }

    // ── composeEntryPoint ─────────────────────────────────────────────────

    void testComposePassThroughWhenMainPresent()
    {
        const QString src = QStringLiteral(
            "#version 450\nvec4 pZone(ZoneCtx z) { return z.fillColor; }\n"
            "void main() { fragColor = vec4(1.0); }\n");
        // Author main() wins: returned unchanged, no wrapper appended.
        const QString out = PhosphorShaders::composeEntryPoint(src, zoneCandidates());
        QCOMPARE(out, src);
        QVERIFY(!out.contains(QStringLiteral("ZONE WRAP")));
    }
    void testComposeWrapsZoneEntry()
    {
        const QString src = QStringLiteral("#version 450\nvec4 pZone(ZoneCtx z) { return z.fillColor; }\n");
        const QString out = PhosphorShaders::composeEntryPoint(src, zoneCandidates());
        QVERIFY2(out.startsWith(src), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("ZONE WRAP")), qPrintable(out));
        QVERIFY2(!out.contains(QStringLiteral("IMAGE WRAP")), qPrintable(out));
    }
    void testComposePrefersFirstCandidate()
    {
        // Both pZone and pImage defined → the first candidate (pZone) wins.
        const QString src = QStringLiteral(
            "vec4 pZone(ZoneCtx z) { return vec4(0.0); }\n"
            "vec4 pImage(vec2 c) { return vec4(0.0); }\n");
        const QString out = PhosphorShaders::composeEntryPoint(src, zoneCandidates());
        QVERIFY2(out.contains(QStringLiteral("ZONE WRAP")), qPrintable(out));
        QVERIFY2(!out.contains(QStringLiteral("IMAGE WRAP")), qPrintable(out));
    }
    void testComposeWrapsImageWhenOnlyImage()
    {
        const QString src = QStringLiteral("vec4 pImage(vec2 fragCoord) { return vec4(fragCoord, 0.0, 1.0); }\n");
        const QString out = PhosphorShaders::composeEntryPoint(src, zoneCandidates());
        QVERIFY2(out.contains(QStringLiteral("IMAGE WRAP")), qPrintable(out));
    }
    void testComposePassThroughWhenNothingMatches()
    {
        // No main(), no known entry — return unchanged so the compiler's
        // missing-main() error (not a silent rewrite) surfaces.
        const QString src = QStringLiteral("float helper(float x) { return x * 2.0; }\n");
        QCOMPARE(PhosphorShaders::composeEntryPoint(src, zoneCandidates()), src);
    }

    // ── stripGlslComments ─────────────────────────────────────────────────

    void testStripPreservesNewlineCount()
    {
        const QString src = QStringLiteral("a\n/* one\ntwo */\nb // trailing\nc\n");
        const QString stripped = PhosphorShaders::stripGlslComments(src);
        QCOMPARE(stripped.count(QLatin1Char('\n')), src.count(QLatin1Char('\n')));
        QVERIFY(!stripped.contains(QStringLiteral("one")));
        QVERIFY(!stripped.contains(QStringLiteral("trailing")));
        QVERIFY(stripped.contains(QLatin1Char('a')));
        QVERIFY(stripped.contains(QLatin1Char('b')));
        QVERIFY(stripped.contains(QLatin1Char('c')));
    }

    // (A bundled-pack scan asserting every pack defines main() lived here under
    // T1.4, when wrapping was purely opt-in. Packs are now being migrated TO the
    // entry convention, so that premise no longer holds. The real per-pack
    // assembly+bake invariant is covered more strongly by
    // test_zone_entry_scaffold (testEveryBundledZoneFragBakes) and
    // test_animation_shader_preamble_bake, which bake every pack through the full
    // runtime assembly.)
};

QTEST_MAIN(TestShaderEntryPoint)
#include "test_shader_entry_point.moc"
