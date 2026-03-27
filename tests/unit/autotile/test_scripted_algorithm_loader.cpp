// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <optional>

#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QSignalSpy>

#include "autotile/AlgorithmRegistry.h"
#include "autotile/TilingAlgorithm.h"
#include "autotile/algorithms/ScriptedAlgorithmLoader.h"

using namespace PlasmaZones;

/**
 * @brief RAII guard for XDG environment variables — restores on destruction even if test fails
 */
class XdgEnvGuard
{
public:
    XdgEnvGuard()
    {
        m_savedDataDirs = qgetenv("XDG_DATA_DIRS");
        m_savedDataHome = qgetenv("XDG_DATA_HOME");
        m_hadDataDirs = qEnvironmentVariableIsSet("XDG_DATA_DIRS");
        m_hadDataHome = qEnvironmentVariableIsSet("XDG_DATA_HOME");
    }
    ~XdgEnvGuard()
    {
        if (m_hadDataDirs)
            qputenv("XDG_DATA_DIRS", m_savedDataDirs);
        else
            qunsetenv("XDG_DATA_DIRS");
        if (m_hadDataHome)
            qputenv("XDG_DATA_HOME", m_savedDataHome);
        else
            qunsetenv("XDG_DATA_HOME");
    }
    XdgEnvGuard(const XdgEnvGuard&) = delete;
    XdgEnvGuard& operator=(const XdgEnvGuard&) = delete;

private:
    QByteArray m_savedDataDirs;
    QByteArray m_savedDataHome;
    bool m_hadDataDirs = false;
    bool m_hadDataHome = false;
};

/**
 * @brief Helper to write a .js script file in a given directory
 */
static QString writeScript(const QString& dirPath, const QString& filename, const QString& content)
{
    QString path = dirPath + QStringLiteral("/") + filename;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return QString();
    f.write(content.toUtf8());
    f.close();
    return path;
}

/**
 * @brief Minimal valid script content with a given name
 */
static QString validScript(const QString& name)
{
    return QStringLiteral(
               "// @name %1\n"
               "// @description Test algorithm\n"
               "function calculateZones(params) {\n"
               "    return [{ x: 0, y: 0, width: 100, height: 100 }];\n"
               "}\n")
        .arg(name);
}

class TestScriptedAlgorithmLoader : public QObject
{
    Q_OBJECT

private:
    /**
     * @brief Unregister any script:* algorithms left over from a test
     */
    void cleanupScriptedAlgorithms(const QStringList& ids)
    {
        auto* registry = AlgorithmRegistry::instance();
        for (const QString& id : ids) {
            if (registry->hasAlgorithm(id)) {
                registry->unregisterAlgorithm(id);
            }
        }
    }

private Q_SLOTS:

    // =========================================================================
    // scanAndRegister tests
    // =========================================================================

    void testScanAndRegister_registersValidScripts()
    {
        XdgEnvGuard envGuard;

        QTemporaryDir xdgRoot;
        QVERIFY(xdgRoot.isValid());
        QString algoDir = xdgRoot.path() + QStringLiteral("/plasmazones/algorithms");
        QDir().mkpath(algoDir);

        writeScript(algoDir, QStringLiteral("gamma.js"), validScript(QStringLiteral("Gamma")));

        qputenv("XDG_DATA_DIRS", xdgRoot.path().toUtf8());
        qputenv("XDG_DATA_HOME", xdgRoot.path().toUtf8());

        std::optional<ScriptedAlgorithmLoader> loader;
        loader.emplace();
        loader->scanAndRegister();

        auto* registry = AlgorithmRegistry::instance();
        QVERIFY(registry->hasAlgorithm(QStringLiteral("script:gamma")));

        loader.reset();
        loader.emplace();
    }

    // =========================================================================
    // Filename validation tests
    // =========================================================================

    void testFilenameValidation_invalidCharsRejected()
    {
        XdgEnvGuard envGuard;

        // The loader validates filenames against ^[a-zA-Z0-9_-]+$
        // Files with spaces, dots (in basename), or other special chars are rejected
        QTemporaryDir xdgRoot;
        QVERIFY(xdgRoot.isValid());
        QString algoDir = xdgRoot.path() + QStringLiteral("/plasmazones/algorithms");
        QDir().mkpath(algoDir);

        // Valid filename
        writeScript(algoDir, QStringLiteral("valid-name.js"), validScript(QStringLiteral("Valid")));
        // Invalid: spaces
        writeScript(algoDir, QStringLiteral("has space.js"), validScript(QStringLiteral("HasSpace")));
        // Invalid: dots in basename
        writeScript(algoDir, QStringLiteral("has.dot.js"), validScript(QStringLiteral("HasDot")));
        // Invalid: special chars
        writeScript(algoDir, QStringLiteral("special!char.js"), validScript(QStringLiteral("Special")));

        qputenv("XDG_DATA_DIRS", xdgRoot.path().toUtf8());
        qputenv("XDG_DATA_HOME", xdgRoot.path().toUtf8());

        std::optional<ScriptedAlgorithmLoader> loader;
        loader.emplace();
        loader->scanAndRegister();

        auto* registry = AlgorithmRegistry::instance();
        QVERIFY(registry->hasAlgorithm(QStringLiteral("script:valid-name")));
        QVERIFY(!registry->hasAlgorithm(QStringLiteral("script:has space")));
        QVERIFY(!registry->hasAlgorithm(QStringLiteral("script:has.dot")));
        QVERIFY(!registry->hasAlgorithm(QStringLiteral("script:special!char")));

        loader.reset();
        loader.emplace();
    }

    // =========================================================================
    // User overrides system priority
    // =========================================================================

    void testUserOverridesSystem()
    {
        XdgEnvGuard envGuard;

        QTemporaryDir xdgRoot;
        QVERIFY(xdgRoot.isValid());

        // System dir
        QString systemAlgoDir = xdgRoot.path() + QStringLiteral("/system/plasmazones/algorithms");
        QDir().mkpath(systemAlgoDir);
        writeScript(systemAlgoDir, QStringLiteral("shared.js"), validScript(QStringLiteral("SystemVersion")));

        // User dir
        QString userAlgoDir = xdgRoot.path() + QStringLiteral("/user/plasmazones/algorithms");
        QDir().mkpath(userAlgoDir);
        writeScript(userAlgoDir, QStringLiteral("shared.js"), validScript(QStringLiteral("UserVersion")));

        qputenv("XDG_DATA_DIRS", (xdgRoot.path() + QStringLiteral("/system")).toUtf8());
        qputenv("XDG_DATA_HOME", (xdgRoot.path() + QStringLiteral("/user")).toUtf8());

        std::optional<ScriptedAlgorithmLoader> loader;
        loader.emplace();
        loader->scanAndRegister();

        auto* registry = AlgorithmRegistry::instance();
        QVERIFY(registry->hasAlgorithm(QStringLiteral("script:shared")));

        // The user version should have overridden the system version
        auto* algo = registry->algorithm(QStringLiteral("script:shared"));
        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("UserVersion"));

        loader.reset();
        loader.emplace();
    }

    // =========================================================================
    // Stale script cleanup on rescan
    // =========================================================================

    void testStaleScriptCleanup()
    {
        XdgEnvGuard envGuard;

        QTemporaryDir xdgRoot;
        QVERIFY(xdgRoot.isValid());
        QString algoDir = xdgRoot.path() + QStringLiteral("/plasmazones/algorithms");
        QDir().mkpath(algoDir);

        QString scriptPath =
            writeScript(algoDir, QStringLiteral("ephemeral.js"), validScript(QStringLiteral("Ephemeral")));
        QVERIFY(!scriptPath.isEmpty());

        qputenv("XDG_DATA_DIRS", xdgRoot.path().toUtf8());
        qputenv("XDG_DATA_HOME", xdgRoot.path().toUtf8());

        std::optional<ScriptedAlgorithmLoader> loader;
        loader.emplace();
        loader->scanAndRegister();

        auto* registry = AlgorithmRegistry::instance();
        QVERIFY(registry->hasAlgorithm(QStringLiteral("script:ephemeral")));

        // Delete the script file
        QVERIFY(QFile::remove(scriptPath));

        // Rescan — the stale algorithm should be unregistered
        loader->scanAndRegister();
        QVERIFY(!registry->hasAlgorithm(QStringLiteral("script:ephemeral")));

        loader.reset();
        loader.emplace();
    }

    // =========================================================================
    // ensureUserDirectoryExists
    // =========================================================================

    void testEnsureUserDirectoryExists()
    {
        XdgEnvGuard envGuard;

        QTemporaryDir xdgRoot;
        QVERIFY(xdgRoot.isValid());

        QString expectedDir = xdgRoot.path() + QStringLiteral("/plasmazones/algorithms");

        // Make sure it doesn't exist yet
        QVERIFY(!QDir(expectedDir).exists());

        qputenv("XDG_DATA_HOME", xdgRoot.path().toUtf8());

        ScriptedAlgorithmLoader loader;
        loader.ensureUserDirectoryExists();

        // The directory should now exist
        QVERIFY(QDir(expectedDir).exists());
    }
};

QTEST_MAIN(TestScriptedAlgorithmLoader)
#include "test_scripted_algorithm_loader.moc"
