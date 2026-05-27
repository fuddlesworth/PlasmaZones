// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Tests for PluginLoader. Uses a CMake-built fake plugin .so as a
// fixture (see tests/fake_plugin/). Each test sets up its own
// temporary plugin root, copies the fixture in, exercises the
// loader, and tears down.

#include <PhosphorRegistry/IBarWidgetFactory.h>
#include <PhosphorRegistry/PluginLoader.h>
#include <PhosphorRegistry/Registry.h>

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#ifndef PHOSPHOR_REGISTRY_FAKE_PLUGIN_DIR
#error "PHOSPHOR_REGISTRY_FAKE_PLUGIN_DIR must be defined by tests/CMakeLists.txt"
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

private:
    // Helper: install the fake plugin (cpp built as MODULE + manifest)
    // into pluginRoot/<subdir>. Returns the installed plugin
    // directory path.
    QString installFakePlugin(const QString& pluginRoot, const QString& subdir) const;
};

QString TestPluginLoader::installFakePlugin(const QString& pluginRoot, const QString& subdir) const
{
    const QString destDir = QDir(pluginRoot).absoluteFilePath(subdir);
    QDir().mkpath(destDir);
    const QString fixtureSo = QDir(QStringLiteral(PHOSPHOR_REGISTRY_FAKE_PLUGIN_DIR))
                                  .absoluteFilePath(QStringLiteral("libphosphor_registry_test_fake_plugin.so"));
    const QString fixtureManifest =
        QDir(QStringLiteral(PHOSPHOR_REGISTRY_FAKE_PLUGIN_DIR)).absoluteFilePath(QStringLiteral("manifest.json"));

    // Rename to subdir-name on the install side so the manifest's
    // id can match the directory basename. Manifest enforces that
    // id == dir basename.
    const QString destSo = QDir(destDir).absoluteFilePath(subdir + QStringLiteral(".so"));
    const QString destManifest = QDir(destDir).absoluteFilePath(QStringLiteral("manifest.json"));

    QFile::copy(fixtureSo, destSo);
    // Manifest's id field also needs to match subdir. The fixture
    // ships with id="fake-plugin"; tests use that subdir name.
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

    loader.scanAndLoad();
    loader.rescanNow();

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
    loader.rescanNow();
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
    loader.rescanNow();

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
    loader.rescanNow();

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
    loader.rescanNow();
    QCOMPARE(loadedSpy.count(), 1);

    // Second rescan with no on-disk changes: no new pluginLoaded.
    loader.rescanNow();
    QCOMPARE(loadedSpy.count(), 1);
    QCOMPARE(registry.size(), 1);
}

QTEST_MAIN(TestPluginLoader)
#include "test_phosphor_registry_pluginloader.moc"
