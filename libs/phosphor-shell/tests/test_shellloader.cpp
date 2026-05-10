// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/ShellLoader.h>

#include <QStandardPaths>
#include <QTest>

using namespace PhosphorShell;

/// ShellLoader has the security-sensitive job of mapping a shell name
/// (supplied by the embedder) to a config-dir path that resolve() reads
/// from. The validation below is the trust boundary — these tests pin
/// the boundary's behaviour so a later refactor can't loosen it
/// silently.
class TestShellLoader : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void initTestCase()
    {
        // Use a deterministic XDG location for the test process. Without
        // this, resolve() walks the developer's real config dirs and the
        // test outcome depends on whether they happen to have a shell.qml
        // installed.
        QStandardPaths::setTestModeEnabled(true);
    }

    void testRejectsPathTraversal()
    {
        ShellLoader loader(QStringLiteral("../etc/passwd"));
        QVERIFY(loader.resolve().isEmpty());
        QVERIFY(loader.shellConfigDir().isEmpty());
    }

    void testRejectsAbsolutePath()
    {
        ShellLoader loader(QStringLiteral("/etc"));
        QVERIFY(loader.resolve().isEmpty());
        QVERIFY(loader.shellConfigDir().isEmpty());
    }

    void testRejectsBackslash()
    {
        ShellLoader loader(QStringLiteral("a\\b"));
        QVERIFY(loader.resolve().isEmpty());
        QVERIFY(loader.shellConfigDir().isEmpty());
    }

    void testRejectsDot()
    {
        ShellLoader loader(QStringLiteral("."));
        QVERIFY(loader.resolve().isEmpty());
    }

    void testRejectsDotDot()
    {
        ShellLoader loader(QStringLiteral(".."));
        QVERIFY(loader.resolve().isEmpty());
    }

    void testRejectsEmpty()
    {
        ShellLoader loader(QStringLiteral(""));
        QVERIFY(loader.resolve().isEmpty());
        QVERIFY(loader.shellConfigDir().isEmpty());
    }

    void testValidNameProducesConfigDir()
    {
        ShellLoader loader(QStringLiteral("phosphor-shell"));
        const QString configDir = loader.shellConfigDir();
        QVERIFY(!configDir.isEmpty());
        QVERIFY(configDir.endsWith(QLatin1String("/phosphor-shell")));
        // resolve() returns empty when no shell.qml exists at the path,
        // which is the expected state in the test environment.
        QCOMPARE(loader.resolve(), QUrl());
    }

    void testDefaultName()
    {
        ShellLoader loader; // default = "phosphor-shell"
        QVERIFY(loader.shellConfigDir().endsWith(QLatin1String("/phosphor-shell")));
    }
};

QTEST_MAIN(TestShellLoader)
#include "test_shellloader.moc"
