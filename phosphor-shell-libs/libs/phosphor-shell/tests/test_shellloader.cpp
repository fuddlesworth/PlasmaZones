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

    void testRejectsBareForwardSlash()
    {
        // Distinct from the traversal test: "foo/bar" exercises the
        // forward-slash branch without also touching the dotdot branch,
        // so a future refactor that drops the slash check (but keeps
        // the dotdot check) would surface here rather than hide behind
        // testRejectsPathTraversal's combined coverage.
        ShellLoader loader(QStringLiteral("foo/bar"));
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
        // Use a name guaranteed not to collide with any installed
        // shell.qml on the host. `setTestModeEnabled(true)` redirects
        // the user-config dir to a temp location, but it does NOT
        // redirect system data dirs (XDG_DATA_DIRS) — so a developer
        // running this test on a machine where phosphor-shell is
        // installed system-wide would find /usr/share/phosphor-shell/
        // shell.qml. A unique sentinel name keeps the test environment-
        // independent.
        ShellLoader loader(QStringLiteral("phosphorshell-test-no-such-shell-xyz"));
        const QString configDir = loader.shellConfigDir();
        QVERIFY(!configDir.isEmpty());
        QVERIFY(configDir.endsWith(QLatin1String("/phosphorshell-test-no-such-shell-xyz")));
        // resolve() returns empty when no shell.qml exists at the path.
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
