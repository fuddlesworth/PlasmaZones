// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// CalculatorProvider's activation, with a clipboard to copy to.
//
// The sibling test_calculatorprovider runs guiless on purpose, to pin the
// headless refusal: under a QCoreApplication there is no clipboard, and
// asking for one there once segfaulted the suite. That leaves the leg that
// actually copies untested, which is a gap worth closing on its own and was
// also the only positive activation available anywhere among the launcher
// providers. This runs the same provider under a QGuiApplication.

#include <PhosphorRegistry/ILauncherProvider.h>
#include <PhosphorShellLauncher/CalculatorProvider.h>

#include <QClipboard>
#include <QGuiApplication>
#include <QTest>

using PhosphorShellLauncher::CalculatorProvider;
using Activation = PhosphorRegistry::ILauncherProvider::Activation;

class TestCalculatorProviderClipboard : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void activatingTheAnswerPutsItOnTheClipboard();
    void anUnansweredQueryStillRefuses();
};

void TestCalculatorProviderClipboard::activatingTheAnswerPutsItOnTheClipboard()
{
    QClipboard* clipboard = QGuiApplication::clipboard();
    QVERIFY(clipboard);
    clipboard->clear();

    CalculatorProvider provider;
    provider.setQuery(QStringLiteral("2 + 3 * 4"));
    const auto results = provider.results();
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().title, QStringLiteral("14"));

    QVERIFY2(provider.activate(QStringLiteral("answer"), Activation::Primary), "the copy really happened");
    QCOMPARE(clipboard->text(), QStringLiteral("14"));
}

void TestCalculatorProviderClipboard::anUnansweredQueryStillRefuses()
{
    CalculatorProvider provider;
    // Not a calculation, so there is no answer to copy even though a
    // clipboard exists. The refusal has to come from the empty answer, not
    // from the missing clipboard that the guiless test exercises.
    provider.setQuery(QStringLiteral("firefox"));
    QVERIFY(provider.results().isEmpty());
    QVERIFY(!provider.activate(QStringLiteral("answer"), Activation::Primary));
}

QTEST_MAIN(TestCalculatorProviderClipboard)
#include "test_calculatorprovider_clipboard.moc"
