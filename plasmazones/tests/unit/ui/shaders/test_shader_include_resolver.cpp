// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pins ShaderIncludeResolver's #line bracketing + source-string legend.
// The resolver flattens #include directives into one blob; without #line
// directives a glslang/driver diagnostic reports a line number in that blob,
// not in the author's file. These tests assert the directives are emitted with
// the right numbers and that the outSourcePaths legend lets a caller map a
// `<source-string>:<line>` diagnostic back to a file path.

#include <PhosphorShaders/ShaderIncludeResolver.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TestShaderIncludeResolver : public QObject
{
    Q_OBJECT

private:
    static void writeFile(const QString& path, const QString& content)
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write(content.toUtf8());
    }

private Q_SLOTS:

    // A single include is bracketed by `#line 1 1` (the header's own line 1)
    // and `#line <n> 0` (the parent resumes at the line after the #include),
    // and the header body is inlined between them.
    void testSingleIncludeBracketing()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString headerPath = tmp.filePath(QStringLiteral("header.glsl"));
        writeFile(headerPath, QStringLiteral("float helper() { return 1.0; }\n"));

        // #version on line 1, #include on line 2, body on line 3.
        const QString source = QStringLiteral("#version 450\n#include \"header.glsl\"\nvoid main() {}\n");

        QString err;
        QStringList legend;
        const QString out =
            PhosphorShaders::ShaderIncludeResolver::expandIncludes(source, tmp.path(), {}, &err, nullptr, &legend);

        QVERIFY2(err.isEmpty(), qPrintable(err));
        QVERIFY(!out.isEmpty());
        // Header inlined.
        QVERIFY(out.contains(QStringLiteral("float helper()")));
        // Entering the include: its first line is line 1 of source string 1.
        QVERIFY2(out.contains(QStringLiteral("#line 1 1")), qPrintable(out));
        // Resuming the parent: the line after the #include (source line 3) in
        // source string 0.
        QVERIFY2(out.contains(QStringLiteral("#line 3 0")), qPrintable(out));
        // Nothing precedes #version (the resolver must never emit a #line
        // before the version directive).
        QVERIFY(out.trimmed().startsWith(QStringLiteral("#version 450")));
    }

    // The legend maps source-string number → path. Index 0 is the top-level
    // (left empty for the caller to fill); index 1 is the first include.
    void testSourcePathLegend()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString headerPath = tmp.filePath(QStringLiteral("header.glsl"));
        writeFile(headerPath, QStringLiteral("// header\n"));

        const QString source = QStringLiteral("#version 450\n#include \"header.glsl\"\n");

        QString err;
        QStringList legend;
        PhosphorShaders::ShaderIncludeResolver::expandIncludes(source, tmp.path(), {}, &err, nullptr, &legend);

        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(legend.size(), 2);
        QVERIFY(legend.at(0).isEmpty()); // top-level: caller fills it
        QCOMPARE(QFileInfo(legend.at(1)).canonicalFilePath(), QFileInfo(headerPath).canonicalFilePath());
    }

    // Nested includes get distinct, depth-first source-string numbers, and each
    // level resumes its own parent correctly.
    void testNestedIncludeNumbering()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString innerPath = tmp.filePath(QStringLiteral("inner.glsl"));
        const QString outerPath = tmp.filePath(QStringLiteral("outer.glsl"));
        writeFile(innerPath, QStringLiteral("// inner\n"));
        // outer.glsl includes inner.glsl on its own line 1.
        writeFile(outerPath, QStringLiteral("#include \"inner.glsl\"\n// outer tail\n"));

        const QString source = QStringLiteral("#version 450\n#include \"outer.glsl\"\nvoid main() {}\n");

        QString err;
        QStringList legend;
        const QString out =
            PhosphorShaders::ShaderIncludeResolver::expandIncludes(source, tmp.path(), {}, &err, nullptr, &legend);

        QVERIFY2(err.isEmpty(), qPrintable(err));
        // outer is source string 1, inner is source string 2.
        QCOMPARE(legend.size(), 3);
        QCOMPARE(QFileInfo(legend.at(1)).canonicalFilePath(), QFileInfo(outerPath).canonicalFilePath());
        QCOMPARE(QFileInfo(legend.at(2)).canonicalFilePath(), QFileInfo(innerPath).canonicalFilePath());
        QVERIFY2(out.contains(QStringLiteral("#line 1 1")), qPrintable(out)); // enter outer
        QVERIFY2(out.contains(QStringLiteral("#line 1 2")), qPrintable(out)); // enter inner
        // After inner, outer resumes at its line 2 (the "// outer tail").
        QVERIFY2(out.contains(QStringLiteral("#line 2 1")), qPrintable(out));
        // After outer, the top-level resumes at line 3 ("void main").
        QVERIFY2(out.contains(QStringLiteral("#line 3 0")), qPrintable(out));
    }

    // A missing include is an error, not a silent passthrough.
    void testMissingIncludeErrors()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString source = QStringLiteral("#version 450\n#include \"nope.glsl\"\n");
        QString err;
        const QString out =
            PhosphorShaders::ShaderIncludeResolver::expandIncludes(source, tmp.path(), {}, &err, nullptr, nullptr);
        QVERIFY(!err.isEmpty());
        QVERIFY(out.isEmpty());
    }

    // The reserved generated-preamble sidecar (p_generated.glsl) is an
    // editor-only autocomplete aid: an #include of it is SKIPPED, not resolved —
    // it neither errors when the file is absent nor inlines its body when
    // present (the real preamble is spliced at load). See T2.2.
    void testGeneratedPreambleIncludeSkipped()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString sidecarName =
            QString::fromLatin1(PhosphorShaders::ShaderIncludeResolver::GeneratedPreambleInclude);
        const QString source =
            QStringLiteral("#version 450\n#include \"") + sidecarName + QStringLiteral("\"\nvoid main() {}\n");

        // (a) Sidecar ABSENT: skipped, not a "not found" error.
        QString err;
        const QString out = PhosphorShaders::ShaderIncludeResolver::expandIncludes(source, tmp.path(), {}, &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QVERIFY2(out.contains(QStringLiteral("void main()")), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("[include skipped: generated preamble]")), qPrintable(out));

        // (b) Sidecar PRESENT: still skipped by reserved name, so its body never
        // reaches the output (no double-define with the spliced preamble).
        writeFile(tmp.filePath(sidecarName), QStringLiteral("#define p_marker customParams[0].x\n"));
        QString err2;
        const QString out2 = PhosphorShaders::ShaderIncludeResolver::expandIncludes(source, tmp.path(), {}, &err2);
        QVERIFY2(err2.isEmpty(), qPrintable(err2));
        QVERIFY2(!out2.contains(QStringLiteral("p_marker")), qPrintable(out2));
    }

    // An EMPTY but valid include file must inline cleanly (nothing), NOT be
    // mistaken for a read failure — even with no error sink. readAll() yields a
    // null QString for an empty file, so the resolver must distinguish that from a
    // genuine open-failure (which also returns null). Tested with outError=nullptr
    // to exercise the path where the failure guard can't lean on the error sink.
    void testEmptyIncludeInlinesCleanly()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        writeFile(tmp.filePath(QStringLiteral("empty.glsl")), QString());
        const QString source = QStringLiteral("#version 450\n#include \"empty.glsl\"\nvoid main() {}\n");
        const QString out =
            PhosphorShaders::ShaderIncludeResolver::expandIncludes(source, tmp.path(), {}, nullptr, nullptr, nullptr);
        QVERIFY2(!out.isEmpty(), "empty include must not be treated as a failure");
        QVERIFY2(out.contains(QStringLiteral("void main()")), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("#line 1 1")), qPrintable(out)); // the include was still bracketed
    }

    // Back-compat: the legend out-param is optional; omitting it still expands.
    void testLegendOptional()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        writeFile(tmp.filePath(QStringLiteral("header.glsl")), QStringLiteral("// header\n"));
        const QString source = QStringLiteral("#version 450\n#include \"header.glsl\"\n");
        QString err;
        const QString out = PhosphorShaders::ShaderIncludeResolver::expandIncludes(source, tmp.path(), {}, &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QVERIFY(out.contains(QStringLiteral("#line 1 1")));
    }
};

QTEST_MAIN(TestShaderIncludeResolver)
#include "test_shader_include_resolver.moc"
