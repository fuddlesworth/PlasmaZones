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
#include <PhosphorShaders/ShaderIncludeResolver.h>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTest>

using PhosphorShaders::EntryCandidate;

class TestShaderEntryPoint : public QObject
{
    Q_OBJECT

    static QList<EntryCandidate> zoneCandidates()
    {
        return {EntryCandidate{QStringLiteral("pzZone"), QStringLiteral("void main() { /* ZONE WRAP */ }\n")},
                EntryCandidate{QStringLiteral("pzImage"), QStringLiteral("void main() { /* IMAGE WRAP */ }\n")}};
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
        QVERIFY(!PhosphorShaders::definesMain(QStringLiteral("// void main() { } is just docs\nvec4 pzZone() { }\n")));
    }
    void testDefinesMainIgnoresBlockComment()
    {
        QVERIFY(!PhosphorShaders::definesMain(QStringLiteral("/* void main() {} */\nvec4 pzImage(vec2 c) { }\n")));
    }
    void testDefinesMainNotSubstring()
    {
        // `mymain` / `domain` must not satisfy the whole-word `main`.
        QVERIFY(!PhosphorShaders::definesMain(QStringLiteral("void mymain() { }\nfloat domain() { return 0.0; }\n")));
    }

    // ── definesFunction (definition, not call) ────────────────────────────

    void testDefinesFunctionPlain()
    {
        QVERIFY(PhosphorShaders::definesFunction(QStringLiteral("vec4 pzZone(ZoneCtx z) {\n return z.fillColor;\n}\n"),
                                                 QStringLiteral("pzZone")));
    }
    void testDefinesFunctionMultilineParams()
    {
        const QString src = QStringLiteral("vec4 pzZone(\n   ZoneCtx z,\n   float k)\n{\n return vec4(0.0);\n}\n");
        QVERIFY(PhosphorShaders::definesFunction(src, QStringLiteral("pzZone")));
    }
    void testDefinesFunctionRejectsCall()
    {
        // A call, not a definition — no trailing `{` after the arg list.
        QVERIFY(!PhosphorShaders::definesFunction(QStringLiteral("void main() {\n color = pzZone(z);\n}\n"),
                                                  QStringLiteral("pzZone")));
    }
    void testDefinesFunctionWholeWord()
    {
        QVERIFY(!PhosphorShaders::definesFunction(QStringLiteral("vec4 pzZoneHelper(int i) { return vec4(0.0); }\n"),
                                                  QStringLiteral("pzZone")));
    }

    // ── composeEntryPoint ─────────────────────────────────────────────────

    void testComposePassThroughWhenMainPresent()
    {
        const QString src = QStringLiteral(
            "#version 450\nvec4 pzZone(ZoneCtx z) { return z.fillColor; }\n"
            "void main() { fragColor = vec4(1.0); }\n");
        // Author main() wins: returned unchanged, no wrapper appended.
        const QString out = PhosphorShaders::composeEntryPoint(src, zoneCandidates());
        QCOMPARE(out, src);
        QVERIFY(!out.contains(QStringLiteral("ZONE WRAP")));
    }
    void testComposeWrapsZoneEntry()
    {
        const QString src = QStringLiteral("#version 450\nvec4 pzZone(ZoneCtx z) { return z.fillColor; }\n");
        const QString out = PhosphorShaders::composeEntryPoint(src, zoneCandidates());
        QVERIFY2(out.startsWith(src), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("ZONE WRAP")), qPrintable(out));
        QVERIFY2(!out.contains(QStringLiteral("IMAGE WRAP")), qPrintable(out));
    }
    void testComposePrefersFirstCandidate()
    {
        // Both pzZone and pzImage defined → the first candidate (pzZone) wins.
        const QString src = QStringLiteral(
            "vec4 pzZone(ZoneCtx z) { return vec4(0.0); }\n"
            "vec4 pzImage(vec2 c) { return vec4(0.0); }\n");
        const QString out = PhosphorShaders::composeEntryPoint(src, zoneCandidates());
        QVERIFY2(out.contains(QStringLiteral("ZONE WRAP")), qPrintable(out));
        QVERIFY2(!out.contains(QStringLiteral("IMAGE WRAP")), qPrintable(out));
    }
    void testComposeWrapsImageWhenOnlyImage()
    {
        const QString src = QStringLiteral("vec4 pzImage(vec2 fragCoord) { return vec4(fragCoord, 0.0, 1.0); }\n");
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

    // ── Non-regression: every bundled pack keeps its own main() ───────────
    // T1.4 wrapping is opt-in. Every shipped zone and animation .frag defines
    // main() today, so composeEntryPoint MUST be a pass-through for all of them
    // — a fresh entry-only pack is the only thing that should ever get wrapped.
    // If a future pack drops main() without an entry function, this catches it.

    void testBundledFragShadersDefineMain_data()
    {
        QTest::addColumn<QString>("fragPath");
        const QStringList roots = {QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/shaders"),
                                   QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/animations")};
        bool any = false;
        for (const QString& root : roots) {
            QDirIterator it(root, {QStringLiteral("*.frag")}, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString path = it.next();
                QTest::newRow(
                    qPrintable(QFileInfo(path).dir().dirName() + QLatin1Char('/') + QFileInfo(path).fileName()))
                    << path;
                any = true;
            }
        }
        if (!any) {
            QSKIP("no bundled .frag shaders found — running outside source tree");
        }
    }

    void testBundledFragShadersDefineMain()
    {
        QFETCH(QString, fragPath);
        QFile f(fragPath);
        QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(fragPath));
        const QString raw = QString::fromUtf8(f.readAll());

        // Expand includes so a main() arriving from a shared header counts too,
        // exactly as the runtime composition will see it.
        const QString dir = QFileInfo(fragPath).absolutePath();
        const QStringList includePaths = {dir + QStringLiteral("/shared"), dir,
                                          QFileInfo(dir).absolutePath() + QStringLiteral("/shared")};
        QString err;
        QString expanded = PhosphorShaders::ShaderIncludeResolver::expandIncludes(raw, dir, includePaths, &err);
        if (expanded.isEmpty()) {
            expanded = raw; // include resolution is best-effort here; raw still carries main()
        }

        QVERIFY2(PhosphorShaders::definesMain(expanded),
                 qPrintable(QStringLiteral("bundled shader lacks a detectable main(): ") + fragPath));
        // And therefore the harness leaves it byte-identical.
        QCOMPARE(PhosphorShaders::composeEntryPoint(expanded, zoneCandidates()), expanded);
    }
};

QTEST_MAIN(TestShaderEntryPoint)
#include "test_shader_entry_point.moc"
