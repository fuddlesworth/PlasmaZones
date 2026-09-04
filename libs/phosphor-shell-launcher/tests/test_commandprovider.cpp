// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// CommandProvider — the gate that decides whether typed text is a
// runnable command (first word resolves on PATH or as an absolute
// executable) and the floor score that keeps the raw command below any
// application that matches the same text. Nothing here starts a process.

#include <PhosphorShellLauncher/CommandProvider.h>
#include <PhosphorShellLauncher/FuzzyMatcher.h>

#include <QSignalSpy>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using PhosphorRegistry::ILauncherProvider;
using PhosphorShellLauncher::CommandProvider;
using PhosphorShellLauncher::FuzzyMatcher;

class TestCommandProvider : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void resolvesTheFirstWordOnPath();
    void offersOneRowOnlyForRunnableText();
    void rowScoresAtTheFloor();
    void activateRefusesAnUnknownRowAndEmptyQuery();
};

void TestCommandProvider::resolvesTheFirstWordOnPath()
{
    // /bin/sh exists on every host this builds on, and "sh" resolves.
    QVERIFY(!QStandardPaths::findExecutable(QStringLiteral("sh")).isEmpty());
    QVERIFY(!CommandProvider::resolveProgram(QStringLiteral("sh")).isEmpty());
    QVERIFY(!CommandProvider::resolveProgram(QStringLiteral("sh -c 'echo hi'")).isEmpty());
    QVERIFY(!CommandProvider::resolveProgram(QStringLiteral("  sh  ")).isEmpty());
    QCOMPARE(CommandProvider::resolveProgram(QStringLiteral("/bin/sh -c true")), QStringLiteral("/bin/sh"));

    QVERIFY(CommandProvider::resolveProgram(QString()).isEmpty());
    QVERIFY(CommandProvider::resolveProgram(QStringLiteral("phosphor-no-such-binary-xyz")).isEmpty());

    // An absolute path that EXISTS and is readable but is not executable.
    // The only negative above fails both tests at once, so weakening the
    // executable check to a mere existence check passed the suite while
    // offering any readable file as a runnable command.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString notRunnable = QDir(dir.path()).filePath(QStringLiteral("data.txt"));
    QFile f(notRunnable);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write("not a program\n");
    f.close();
    QVERIFY(QFileInfo::exists(notRunnable));
    QVERIFY(CommandProvider::resolveProgram(notRunnable).isEmpty());
    QVERIFY(CommandProvider::resolveProgram(QStringLiteral("/nonexistent/path/to/thing")).isEmpty());
    // An ordinary search term is not a command.
    QVERIFY(CommandProvider::resolveProgram(QStringLiteral("fire")).isEmpty());
}

void TestCommandProvider::offersOneRowOnlyForRunnableText()
{
    CommandProvider provider;
    QSignalSpy changed(&provider, &ILauncherProvider::resultsChanged);

    provider.setQuery(QStringLiteral("sh -c true"));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(provider.results().size(), 1);
    QCOMPARE(provider.results().first().title, QStringLiteral("sh -c true"));
    QVERIFY(provider.results().first().hasAlternateAction());

    provider.setQuery(QStringLiteral("fire"));
    QCOMPARE(changed.count(), 2);
    QVERIFY(provider.results().isEmpty());
}

void TestCommandProvider::rowScoresAtTheFloor()
{
    CommandProvider provider;
    provider.setQuery(QStringLiteral("sh"));
    // Below the weakest possible fuzzy match (a single ScoreMatch with no
    // bonus), so an app named anything like the command wins.
    QVERIFY(provider.results().first().score < FuzzyMatcher::ScoreMatch);
    QVERIFY(provider.results().first().score > 0);
}

void TestCommandProvider::activateRefusesAnUnknownRowAndEmptyQuery()
{
    CommandProvider provider;
    provider.setQuery(QStringLiteral("sh"));
    QVERIFY(!provider.activate(QStringLiteral("nope"), ILauncherProvider::Activation::Primary));
    provider.setQuery(QString());
    QVERIFY(!provider.activate(QStringLiteral("run"), ILauncherProvider::Activation::Primary));
}

QTEST_GUILESS_MAIN(TestCommandProvider)

#include "test_commandprovider.moc"
