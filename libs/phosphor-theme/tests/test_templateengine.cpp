// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTheme/TemplateEngine.h>

#include <QColor>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>

using namespace PhosphorTheme;

class TestTemplateEngine : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void render_defaultFieldIsHex();
    void render_rgbFieldVariants();
    void render_alphaFieldUsesThreeDecimals();
    void render_rgbAndRgbaJoin();
    void render_unknownTokenPreservesPlaceholder();
    void render_unknownFieldFallsBackToHex();
    void render_handlesNoTokensCleanly();
    void render_handlesMultiplePlaceholdersInOneTemplate();
    void render_invalidColorValuePreservesPlaceholder();
    void renderFile_roundtripsTemplateToOutput();
    void renderFile_returnsFalseOnMissingTemplate();

private:
    QVariantMap fixture() const
    {
        QVariantMap m;
        m.insert(QStringLiteral("primary"), QColor("#3B82F6"));
        m.insert(QStringLiteral("on_primary"), QColor::fromRgb(240, 249, 255));
        // Forced alpha to exercise the alpha + hexa paths.
        m.insert(QStringLiteral("scrim"), QColor::fromRgba(qRgba(0, 0, 0, 128)));
        return m;
    }
};

void TestTemplateEngine::render_defaultFieldIsHex()
{
    const auto out = TemplateEngine::render(QStringLiteral("color: {{primary}};"), fixture());
    QCOMPARE(out, QStringLiteral("color: #3B82F6;"));
}

void TestTemplateEngine::render_rgbFieldVariants()
{
    const auto m = fixture();
    QCOMPARE(TemplateEngine::render(QStringLiteral("{{primary.r}}"), m), QStringLiteral("59"));
    QCOMPARE(TemplateEngine::render(QStringLiteral("{{primary.red}}"), m), QStringLiteral("59"));
    QCOMPARE(TemplateEngine::render(QStringLiteral("{{primary.g}}"), m), QStringLiteral("130"));
    QCOMPARE(TemplateEngine::render(QStringLiteral("{{primary.blue}}"), m), QStringLiteral("246"));
}

void TestTemplateEngine::render_alphaFieldUsesThreeDecimals()
{
    const auto m = fixture();
    // Opaque colors render alpha as "1.000".
    QCOMPARE(TemplateEngine::render(QStringLiteral("{{primary.alpha}}"), m), QStringLiteral("1.000"));
    // 128/255 ≈ 0.502.
    QCOMPARE(TemplateEngine::render(QStringLiteral("{{scrim.a}}"), m), QStringLiteral("0.502"));
}

void TestTemplateEngine::render_rgbAndRgbaJoin()
{
    const auto m = fixture();
    QCOMPARE(TemplateEngine::render(QStringLiteral("{{primary.rgb}}"), m), QStringLiteral("59, 130, 246"));
    QCOMPARE(TemplateEngine::render(QStringLiteral("{{primary.rgba}}"), m), QStringLiteral("59, 130, 246, 1.000"));
}

void TestTemplateEngine::render_unknownTokenPreservesPlaceholder()
{
    const auto out = TemplateEngine::render(QStringLiteral("a {{missing}} b"), fixture());
    QCOMPARE(out, QStringLiteral("a {{missing}} b"));
}

void TestTemplateEngine::render_unknownFieldFallsBackToHex()
{
    const auto out = TemplateEngine::render(QStringLiteral("{{primary.lol}}"), fixture());
    QCOMPARE(out, QStringLiteral("#3B82F6"));
}

void TestTemplateEngine::render_handlesNoTokensCleanly()
{
    QCOMPARE(TemplateEngine::render(QStringLiteral("hello"), fixture()), QStringLiteral("hello"));
}

void TestTemplateEngine::render_handlesMultiplePlaceholdersInOneTemplate()
{
    // Adjacent + interleaved placeholders. Verifies the global match
    // iterator doesn't skip occurrences when several share a line.
    const auto src = QStringLiteral("{{primary}} | {{on_primary.rgb}} | {{primary.r}},{{primary.g}}");
    QCOMPARE(TemplateEngine::render(src, fixture()), QStringLiteral("#3B82F6 | 240, 249, 255 | 59,130"));
}

void TestTemplateEngine::render_invalidColorValuePreservesPlaceholder()
{
    // A token whose value isn't a QColor (here: a plain string that
    // doesn't parse) must surface visibly via the preserved
    // placeholder + warning, never silently substitute as an empty
    // string.
    QVariantMap broken;
    broken.insert(QStringLiteral("primary"), QStringLiteral("not-a-color"));
    const auto out = TemplateEngine::render(QStringLiteral("[{{primary}}]"), broken);
    QCOMPARE(out, QStringLiteral("[{{primary}}]"));
}

void TestTemplateEngine::renderFile_roundtripsTemplateToOutput()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString in = tmp.filePath(QStringLiteral("in.template"));
    const QString out = tmp.filePath(QStringLiteral("out.css"));

    {
        QFile f(in);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("accent: {{primary}};\nrgb: {{primary.rgb}};\n");
    }

    QVERIFY(TemplateEngine::renderFile(in, out, fixture()));

    QFile rendered(out);
    QVERIFY(rendered.open(QIODevice::ReadOnly));
    const auto contents = QString::fromUtf8(rendered.readAll());
    QCOMPARE(contents, QStringLiteral("accent: #3B82F6;\nrgb: 59, 130, 246;\n"));
}

void TestTemplateEngine::renderFile_returnsFalseOnMissingTemplate()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString out = tmp.filePath(QStringLiteral("out.css"));
    QVERIFY(!TemplateEngine::renderFile(QStringLiteral("/definitely/does/not/exist.template"), out, fixture()));
    // Output must not have been created on failure.
    QVERIFY(!QFile::exists(out));
}

QTEST_MAIN(TestTemplateEngine)
#include "test_templateengine.moc"
