// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <PhosphorTheme/TemplateEngine.h>

#include <QColor>
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

QTEST_MAIN(TestTemplateEngine)
#include "test_templateengine.moc"
