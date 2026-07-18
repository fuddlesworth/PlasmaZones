// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Pins the argument-substitution contract of PhosphorLocalizedContext,
// the QML i18n context object. No translation catalog is loaded here, so
// QCoreApplication::translate() returns the source string and every
// assertion exercises the substitution path itself.

#include <QTest>

#include "phosphor_qml_i18n.h"

class TestLocalizedContextSubstitution : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void plainPassthrough();
    void singleArg();
    void multipleArgs();
    void argValueContainingMarkerIsNotResubstituted();
    void unmatchedPlaceholderStaysLiteral();
    void repeatedPlaceholder();
    void trailingAndLonePercent();
    void i18ncSubstitutes();
    void i18npSelectsFormAndSubstitutesN();
    void i18npDoesNotSubstitutePositionalMarkers();

private:
    PhosphorLocalizedContext m_ctx;
};

void TestLocalizedContextSubstitution::plainPassthrough()
{
    QCOMPARE(m_ctx.i18n(QStringLiteral("No layout assigned")), QStringLiteral("No layout assigned"));
}

void TestLocalizedContextSubstitution::singleArg()
{
    QCOMPARE(m_ctx.i18n(QStringLiteral("%1 zones"), 4), QStringLiteral("4 zones"));
}

void TestLocalizedContextSubstitution::multipleArgs()
{
    QCOMPARE(m_ctx.i18n(QStringLiteral("→ %1 (%2)"), QStringLiteral("Grid"), QStringLiteral("4 zones")),
             QStringLiteral("→ Grid (4 zones)"));
}

void TestLocalizedContextSubstitution::argValueContainingMarkerIsNotResubstituted()
{
    // A user-controlled value (e.g. a layout named "%2") must come out
    // verbatim; the marker it contains is data, not a placeholder. A
    // sequential replace-per-argument pass would corrupt this to
    // "→ Z (Z)".
    QCOMPARE(m_ctx.i18n(QStringLiteral("→ %1 (%2)"), QStringLiteral("%2"), QStringLiteral("Z")),
             QStringLiteral("→ %2 (Z)"));
}

void TestLocalizedContextSubstitution::unmatchedPlaceholderStaysLiteral()
{
    QCOMPARE(m_ctx.i18n(QStringLiteral("%1 and %3"), QStringLiteral("a")), QStringLiteral("a and %3"));
}

void TestLocalizedContextSubstitution::repeatedPlaceholder()
{
    QCOMPARE(m_ctx.i18n(QStringLiteral("%1%1"), QStringLiteral("x")), QStringLiteral("xx"));
}

void TestLocalizedContextSubstitution::trailingAndLonePercent()
{
    QCOMPARE(m_ctx.i18n(QStringLiteral("100%"), 1), QStringLiteral("100%"));
    QCOMPARE(m_ctx.i18n(QStringLiteral("a % b"), 1), QStringLiteral("a % b"));
}

void TestLocalizedContextSubstitution::i18ncSubstitutes()
{
    QCOMPARE(m_ctx.i18nc(QStringLiteral("@info"), QStringLiteral("Lock all in %1"), QStringLiteral("Colors")),
             QStringLiteral("Lock all in Colors"));
}

void TestLocalizedContextSubstitution::i18npSelectsFormAndSubstitutesN()
{
    QCOMPARE(m_ctx.i18np(QStringLiteral("%n zone"), QStringLiteral("%n zones"), 1), QStringLiteral("1 zone"));
    QCOMPARE(m_ctx.i18np(QStringLiteral("%n zone"), QStringLiteral("%n zones"), 4), QStringLiteral("4 zones"));
    QCOMPARE(m_ctx.i18np(QStringLiteral("%n zone"), QStringLiteral("%n zones"), 0), QStringLiteral("0 zones"));
    QCOMPARE(m_ctx.i18ncp(QStringLiteral("@info"), QStringLiteral("%n use"), QStringLiteral("%n uses"), 2),
             QStringLiteral("2 uses"));
}

void TestLocalizedContextSubstitution::i18npDoesNotSubstitutePositionalMarkers()
{
    // Documents the contract that broke the layout cards: i18np only
    // substitutes %n. Positional markers pass through untouched, so
    // plural strings must use %n and compose an outer i18n() for any
    // extra values.
    QCOMPARE(m_ctx.i18np(QStringLiteral("%1 zone"), QStringLiteral("%1 zones"), 4), QStringLiteral("%1 zones"));
}

QTEST_GUILESS_MAIN(TestLocalizedContextSubstitution)
#include "test_localized_context_substitution.moc"
