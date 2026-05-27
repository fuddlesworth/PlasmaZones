// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Tests for PluginLoader. Uses CMake-built fake plugin .so fixtures
// (see tests/fake_plugin/ and tests/fake_plugin_idmismatch/). Each
// test sets up its own temporary plugin root, copies a fixture in,
// exercises the loader, and tears down.

#include <PhosphorRegistry/IBarWidgetFactory.h>
#include <PhosphorRegistry/PluginLoader.h>
#include <PhosphorRegistry/Registry.h>

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#ifndef PHOSPHOR_REGISTRY_FAKE_PLUGIN_DIR
#error "PHOSPHOR_REGISTRY_FAKE_PLUGIN_DIR must be defined by tests/CMakeLists.txt"
#endif
#ifndef PHOSPHOR_REGISTRY_FAKE_PLUGIN_IDMISMATCH_DIR
#error "PHOSPHOR_REGISTRY_FAKE_PLUGIN_IDMISMATCH_DIR must be defined by tests/CMakeLists.txt"
#endif

using namespace PhosphorRegistry;

class TestPluginLoader : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void loadsPluginAtScan();
    void unloadsRemovedPluginOnRescan();
    void ignoresInvalidManifest();
    void ignoresPluginWithoutSo();
    void duplicateIdAcrossRescansNoOp();
    void rejectsFactoryIdManifestMismatch();
    void rejectsAbiVersionMismatch();
    void rejectsPathTraversalId();
    void emitsRescanCompletedSignal();

private:
    // Helper: install the canonical fake plugin (built as MODULE) +
    // its manifest into pluginRoot/<subdir>. The .so is renamed to
    // <subdir>.so so the manifest's id == directory basename rule
    // can be satisfied. Returns the installed plugin directory path.
    QString installFakePlugin(const QString& pluginRoot, const QString& subdir) const;

    // Helper: install the id-mismatch fake plugin (factory returns
    // id="fake-other"; manifest says id="id-mismatch-plugin"). The
    // subdir argument MUST equal the manifest id (the manifest's
    // dir-basename rule applies), so callers always pass
    // "id-mismatch-plugin".
    QString installFakePluginIdMismatch(const QString& pluginRoot) const;
};

QString TestPluginLoader::installFakePlugin(const QString& pluginRoot, const QString& subdir) const
{
    const QString destDir = QDir(pluginRoot).absoluteFilePath(subdir);
    QDir().mkpath(destDir);
    const QString fixtureSo = QDir(QStringLiteral(PHOSPHOR_REGISTRY_FAKE_PLUGIN_DIR))
                                  .absoluteFilePath(QStringLiteral("libphosphor_registry_test_fake_plugin.so"));
    const QString fixtureManifest =
        QDir(QStringLiteral(PHOSPHOR_REGISTRY_FAKE_PLUGIN_DIR)).absoluteFilePath(QStringLiteral("manifest.json"));

    const QString destSo = QDir(destDir).absoluteFilePath(subdir + QStringLiteral(".so"));
    const QString destManifest = QDir(destDir).absoluteFilePath(QStringLiteral("manifest.json"));

    QFile::copy(fixtureSo, destSo);
    QFile::copy(fixtureManifest, destManifest);

    return destDir;
}

QString TestPluginLoader::installFakePluginIdMismatch(const QString& pluginRoot) const
{
    const QString subdir = QStringLiteral("id-mismatch-plugin");
    const QString destDir = QDir(pluginRoot).absoluteFilePath(subdir);
    QDir().mkpath(destDir);
    const QString fixtureSo =
        QDir(QStringLiteral(PHOSPHOR_REGISTRY_FAKE_PLUGIN_IDMISMATCH_DIR))
            .absoluteFilePath(QStringLiteral("libphosphor_registry_test_fake_plugin_idmismatch.so"));
    const QString fixtureManifest = QDir(QStringLiteral(PHOSPHOR_REGISTRY_FAKE_PLUGIN_IDMISMATCH_DIR))
                                        .absoluteFilePath(QStringLiteral("manifest.json"));

    const QString destSo = QDir(destDir).absoluteFilePath(subdir + QStringLiteral(".so"));
    const QString destManifest = QDir(destDir).absoluteFilePath(QStringLiteral("manifest.json"));

    QFile::copy(fixtureSo, destSo);
    QFile::copy(fixtureManifest, destManifest);

    return destDir;
}

void TestPluginLoader::loadsPluginAtScan()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString pluginRoot = tempDir.path();
    installFakePlugin(pluginRoot, QStringLiteral("fake-plugin"));

    Registry<IBarWidgetFactory> registry;
    PluginLoader loader(&registry, pluginRoot);
    QSignalSpy loadedSpy(&loader, &PluginLoader::pluginLoaded);

    // scanAndLoad() triggers a synchronous initial scan via
    // WatchedDirectorySet::registerDirectory; no rescanNow() needed.
    loader.scanAndLoad();

    QCOMPARE(loadedSpy.count(), 1);
    QCOMPARE(loadedSpy.first().at(0).toString(), QStringLiteral("fake-plugin"));
    QCOMPARE(registry.size(), 1);
    QCOMPARE(loader.loadedPluginIds(), QStringList{QStringLiteral("fake-plugin")});
    auto factory = registry.factory(QStringLiteral("fake-plugin"));
    QVERIFY(factory);
    QCOMPARE(factory->displayName(), QStringLiteral("Fake Plugin"));
    QCOMPARE(factory->capabilities(), QStringList{QStringLiteral("bar.widget")});
}

void TestPluginLoader::unloadsRemovedPluginOnRescan()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString pluginRoot = tempDir.path();
    const QString installedDir = installFakePlugin(pluginRoot, QStringLiteral("fake-plugin"));

    Registry<IBarWidgetFactory> registry;
    PluginLoader loader(&registry, pluginRoot);
    QSignalSpy unloadedSpy(&loader, &PluginLoader::pluginUnloaded);

    loader.scanAndLoad();
    QCOMPARE(registry.size(), 1);

    // Remove the plugin directory and rescan.
    QDir(installedDir).removeRecursively();
    loader.rescanNow();

    QCOMPARE(unloadedSpy.count(), 1);
    QCOMPARE(unloadedSpy.first().at(0).toString(), QStringLiteral("fake-plugin"));
    QCOMPARE(registry.size(), 0);
}

void TestPluginLoader::ignoresInvalidManifest()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString pluginRoot = tempDir.path();
    const QString pluginDir = QDir(pluginRoot).absoluteFilePath(QStringLiteral("bad-plugin"));
    QDir().mkpath(pluginDir);
    QFile mf(QDir(pluginDir).absoluteFilePath(QStringLiteral("manifest.json")));
    QVERIFY(mf.open(QIODevice::WriteOnly | QIODevice::Text));
    mf.write("{ broken json");
    mf.close();

    Registry<IBarWidgetFactory> registry;
    PluginLoader loader(&registry, pluginRoot);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("PluginLoader: refusing.*malformed JSON")));
    loader.scanAndLoad();

    QCOMPARE(registry.size(), 0);
    QVERIFY(loader.loadedPluginIds().isEmpty());
}

void TestPluginLoader::ignoresPluginWithoutSo()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString pluginRoot = tempDir.path();
    const QString pluginDir = QDir(pluginRoot).absoluteFilePath(QStringLiteral("manifest-only"));
    QDir().mkpath(pluginDir);
    QFile mf(QDir(pluginDir).absoluteFilePath(QStringLiteral("manifest.json")));
    QVERIFY(mf.open(QIODevice::WriteOnly | QIODevice::Text));
    mf.write(QStringLiteral("{\"id\":\"manifest-only\",\"displayName\":\"Bad\",\"abi\":1}").toUtf8());
    mf.close();

    Registry<IBarWidgetFactory> registry;
    PluginLoader loader(&registry, pluginRoot);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("no \\.so under")));
    loader.scanAndLoad();

    QCOMPARE(registry.size(), 0);
}

void TestPluginLoader::duplicateIdAcrossRescansNoOp()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString pluginRoot = tempDir.path();
    installFakePlugin(pluginRoot, QStringLiteral("fake-plugin"));

    Registry<IBarWidgetFactory> registry;
    PluginLoader loader(&registry, pluginRoot);
    QSignalSpy loadedSpy(&loader, &PluginLoader::pluginLoaded);

    loader.scanAndLoad();
    QCOMPARE(loadedSpy.count(), 1);

    // Second rescan with no on-disk changes: no new pluginLoaded.
    loader.rescanNow();
    QCOMPARE(loadedSpy.count(), 1);
    QCOMPARE(registry.size(), 1);
}

void TestPluginLoader::rejectsFactoryIdManifestMismatch()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString pluginRoot = tempDir.path();
    installFakePluginIdMismatch(pluginRoot);

    Registry<IBarWidgetFactory> registry;
    PluginLoader loader(&registry, pluginRoot);
    QSignalSpy loadedSpy(&loader, &PluginLoader::pluginLoaded);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("factory id.*does not match manifest id")));
    loader.scanAndLoad();

    QCOMPARE(loadedSpy.count(), 0);
    QCOMPARE(registry.size(), 0);
    QVERIFY(loader.loadedPluginIds().isEmpty());
}

void TestPluginLoader::rejectsAbiVersionMismatch()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString pluginRoot = tempDir.path();
    const QString pluginDir = QDir(pluginRoot).absoluteFilePath(QStringLiteral("future-plugin"));
    QDir().mkpath(pluginDir);
    QFile mf(QDir(pluginDir).absoluteFilePath(QStringLiteral("manifest.json")));
    QVERIFY(mf.open(QIODevice::WriteOnly | QIODevice::Text));
    mf.write(QStringLiteral("{\"id\":\"future-plugin\",\"displayName\":\"Future\",\"abi\":99}").toUtf8());
    mf.close();

    Registry<IBarWidgetFactory> registry;
    PluginLoader loader(&registry, pluginRoot);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("abi mismatch")));
    loader.scanAndLoad();

    QCOMPARE(registry.size(), 0);
}

void TestPluginLoader::rejectsPathTraversalId()
{
    // Manifest::parseObject runs isSafeId() against the id field.
    // Use a traversal sequence ("..") inside the id — this is a
    // valid directory name on the filesystem so the directory
    // enumeration step doesn't silently drop the candidate, then
    // isSafeId must reject the id at the manifest-parse stage.
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString pluginRoot = tempDir.path();
    const QString pluginDir = QDir(pluginRoot).absoluteFilePath(QStringLiteral("foo..bar"));
    QDir().mkpath(pluginDir);
    QFile mf(QDir(pluginDir).absoluteFilePath(QStringLiteral("manifest.json")));
    QVERIFY(mf.open(QIODevice::WriteOnly | QIODevice::Text));
    mf.write(QStringLiteral("{\"id\":\"foo..bar\",\"displayName\":\"X\",\"abi\":1}").toUtf8());
    mf.close();

    Registry<IBarWidgetFactory> registry;
    PluginLoader loader(&registry, pluginRoot);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("unsafe characters")));
    loader.scanAndLoad();

    QCOMPARE(registry.size(), 0);
}

void TestPluginLoader::emitsRescanCompletedSignal()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString pluginRoot = tempDir.path();
    installFakePlugin(pluginRoot, QStringLiteral("fake-plugin"));

    Registry<IBarWidgetFactory> registry;
    PluginLoader loader(&registry, pluginRoot);
    QSignalSpy rescanSpy(&loader, &PluginLoader::rescanCompleted);

    loader.scanAndLoad();
    QCOMPARE(rescanSpy.count(), 1);

    loader.rescanNow();
    QCOMPARE(rescanSpy.count(), 2);
}

QTEST_MAIN(TestPluginLoader)
#include "test_phosphor_registry_pluginloader.moc"
