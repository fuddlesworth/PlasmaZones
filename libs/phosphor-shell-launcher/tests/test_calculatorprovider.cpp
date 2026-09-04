// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// CalculatorProvider — the evaluator (precedence, associativity, unary
// minus, functions, the rejections), the "is this a calculation at all"
// gate that keeps bare numbers from producing a row, the display format,
// and the provider surface: one row that outranks any fuzzy match, and a
// copy that refuses cleanly with no GUI clipboard to copy to.

#include <PhosphorShellLauncher/CalculatorProvider.h>
#include <PhosphorShellLauncher/FuzzyMatcher.h>

#include <QSignalSpy>
#include <QtTest/QtTest>

using PhosphorRegistry::ILauncherProvider;
using PhosphorShellLauncher::CalculatorProvider;
using PhosphorShellLauncher::FuzzyMatcher;

class TestCalculatorProvider : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void evaluatesWithPrecedenceAndAssociativity();
    void rejectsMalformedAndUndefined();
    void bareNumbersAreNotCalculations();
    void formatsIntegersAndReals();
    void yieldsOneRowThatOutranksAnyFuzzyMatch();
    void copyRefusesWithoutAGuiClipboard();
    void deeplyNestedInputIsRejectedRatherThanCrashing();
    void functionNamesAreCaseInsensitive();
    void nonAsciiDigitsAreNotNumbers();
};

// The query comes straight from a text field, so the parse depth is
// attacker-controlled in the ordinary sense that a paste can be any length.
// Without the depth guard in calculatorprovider.cpp this recursion overflows
// the stack and takes the whole shell process down; verified by raising the
// limit and watching this input segfault. Keep both halves: the deep input
// must be refused, and ordinary nesting must still evaluate, or a "fix" that
// simply rejects all parentheses would pass.
void TestCalculatorProvider::deeplyNestedInputIsRejectedRatherThanCrashing()
{
    const QString deep(200000, u'(');
    QVERIFY(!CalculatorProvider::evaluate(deep).has_value());

    const QString deepButClosed = QString(50000, u'(') + QStringLiteral("1") + QString(50000, u')');
    QVERIFY(!CalculatorProvider::evaluate(deepButClosed).has_value());

    // Nesting a person might actually type still works.
    QCOMPARE(CalculatorProvider::evaluate(QStringLiteral("((((1+1))))")).value(), 2.0);
}

void TestCalculatorProvider::functionNamesAreCaseInsensitive()
{
    // Every other match in the launcher is case-insensitive, so a capitalised
    // function name must not be a parse error.
    QCOMPARE(CalculatorProvider::evaluate(QStringLiteral("SQRT(9)")).value(), 3.0);
    QCOMPARE(CalculatorProvider::evaluate(QStringLiteral("Abs(-4)")).value(), 4.0);
    QCOMPARE(CalculatorProvider::evaluate(QStringLiteral("sQrT(16)+ABS(-2)")).value(), 6.0);
}

void TestCalculatorProvider::nonAsciiDigitsAreNotNumbers()
{
    // QChar::isDigit() accepts these, but the C-locale conversion does not.
    // Scanning them as digits consumed the literal and then failed the whole
    // expression; refusing them up front is the same answer for a simpler
    // reason. Arabic-Indic and fullwidth digits.
    QVERIFY(!CalculatorProvider::evaluate(QString::fromUtf8("١+١")).has_value());
    QVERIFY(!CalculatorProvider::evaluate(QString::fromUtf8("１+１")).has_value());
}

void TestCalculatorProvider::evaluatesWithPrecedenceAndAssociativity()
{
    const auto ev = [](const char* s) {
        return CalculatorProvider::evaluate(QString::fromUtf8(s));
    };
    QCOMPARE(ev("2+3*4").value(), 14.0);
    QCOMPARE(ev("(2+3)*4").value(), 20.0);
    QCOMPARE(ev("10/4").value(), 2.5);
    QCOMPARE(ev("7%3").value(), 1.0);
    // ^ is right-associative: 2^3^2 = 2^9.
    QCOMPARE(ev("2^3^2").value(), 512.0);
    // Unary minus is the base of ^ in this grammar (factor := unary ('^'
    // factor)?), so -2^2 is (-2)^2 = 4, unlike the mathematical reading
    // -(2^2). Pinned so a grammar refactor cannot silently flip it; if it
    // is ever changed to the mathematical reading, change this on purpose.
    QCOMPARE(ev("-2^2").value(), 4.0);
    QCOMPARE(ev("-(2^2)").value(), -4.0);
    QCOMPARE(ev("--3").value(), 3.0);
    QCOMPARE(ev("sqrt(16)+abs(-2)").value(), 6.0);
    QCOMPARE(ev("1.5e2*2").value(), 300.0);
    QCOMPARE(ev(" 1 + 1 ").value(), 2.0);
}

void TestCalculatorProvider::rejectsMalformedAndUndefined()
{
    const auto ev = [](const char* s) {
        return CalculatorProvider::evaluate(QString::fromUtf8(s));
    };
    QVERIFY(!ev("").has_value());
    QVERIFY(!ev("2+").has_value());
    QVERIFY(!ev("(2+3").has_value());
    QVERIFY(!ev("2+2 apples").has_value());
    QVERIFY(!ev("1/0").has_value());
    QVERIFY(!ev("5%0").has_value());
    QVERIFY(!ev("sqrt(-1)").has_value());
    QVERIFY(!ev("nope(3)").has_value());
    // "2e" is not 2 with a stray letter; it is not an expression.
    QVERIFY(!ev("2e").has_value());
    // Overflow to infinity is not an answer.
    QVERIFY(!ev("10^400").has_value());
}

void TestCalculatorProvider::bareNumbersAreNotCalculations()
{
    QVERIFY(!CalculatorProvider::isCalculation(QStringLiteral("5")));
    QVERIFY(!CalculatorProvider::isCalculation(QStringLiteral("3.14")));
    QVERIFY(!CalculatorProvider::isCalculation(QStringLiteral("-5")));
    QVERIFY(CalculatorProvider::isCalculation(QStringLiteral("5+0")));
    QVERIFY(CalculatorProvider::isCalculation(QStringLiteral("(5)")));
    QVERIFY(CalculatorProvider::isCalculation(QStringLiteral("sqrt(25)")));
}

void TestCalculatorProvider::formatsIntegersAndReals()
{
    QCOMPARE(CalculatorProvider::format(14.0), QStringLiteral("14"));
    QCOMPARE(CalculatorProvider::format(-3.0), QStringLiteral("-3"));
    QCOMPARE(CalculatorProvider::format(2.5), QStringLiteral("2.5"));
    // C locale: no thousands separator, so it pastes into a shell.
    QCOMPARE(CalculatorProvider::format(1234567.0), QStringLiteral("1234567"));
    // Ten significant digits, no trailing zeros.
    QCOMPARE(CalculatorProvider::format(1.0 / 3.0), QStringLiteral("0.3333333333"));
}

void TestCalculatorProvider::yieldsOneRowThatOutranksAnyFuzzyMatch()
{
    CalculatorProvider provider;
    QSignalSpy changed(&provider, &ILauncherProvider::resultsChanged);

    provider.setQuery(QStringLiteral("2*(3+4)"));
    QCOMPARE(changed.count(), 1);
    const auto rows = provider.results();
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().title, QStringLiteral("14"));
    QCOMPARE(rows.first().subtitle, QStringLiteral("2*(3+4)"));
    QVERIFY(rows.first().score > FuzzyMatcher::perfectScore(7));
    QVERIFY(!rows.first().hasAlternateAction());

    // Not a calculation: no row, but still announced so the model drops
    // the previous answer.
    provider.setQuery(QStringLiteral("firefox"));
    QCOMPARE(changed.count(), 2);
    QVERIFY(provider.results().isEmpty());
}

void TestCalculatorProvider::copyRefusesWithoutAGuiClipboard()
{
    // QTEST_GUILESS_MAIN: a QCoreApplication, no clipboard. The provider
    // must refuse (so the surface stays open) rather than crash.
    CalculatorProvider provider;
    provider.setQuery(QStringLiteral("1+1"));
    QVERIFY(!provider.activate(QStringLiteral("answer"), ILauncherProvider::Activation::Primary));
    QVERIFY(!provider.activate(QStringLiteral("answer"), ILauncherProvider::Activation::Alternate));
    QVERIFY(!provider.activate(QStringLiteral("nope"), ILauncherProvider::Activation::Primary));
}

QTEST_GUILESS_MAIN(TestCalculatorProvider)

#include "test_calculatorprovider.moc"
