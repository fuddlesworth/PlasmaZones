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
#ifndef PHOSPHOR_REGISTRY_FAKE_PLUGIN_NULLFACTORY_DIR
#error "PHOSPHOR_REGISTRY_FAKE_PLUGIN_NULLFACTORY_DIR must be defined by tests/CMakeLists.txt"
#endif
#ifndef PHOSPHOR_REGISTRY_FAKE_PLUGIN_NOENTRY_DIR
#error "PHOSPHOR_REGISTRY_FAKE_PLUGIN_NOENTRY_DIR must be defined by tests/CMakeLists.txt"
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
    void rejectsNullFactoryReturn();
    void rejectsPluginWithoutEntryPoint();
    void rejectsCorruptSoFile();
    void loadsNewPluginAddedOnRescan();
    void emitsRescanCompletedSignal();
    void liveWidgetCountReturnsMinusOneForLoadedPlugin();
    void liveWidgetCountReturnsMinusOneForUnknownPlugin();
    void pluginRootReturnsConfiguredPath();
    void pluginRootResolvesXdgWhenEmpty();
    void destructorPinsLibraryBeforeFactoryDestruction();
    void destructorWithoutPriorRescanDoesNotCrash();

private:
    // Helper: install one of the templated fake-plugin fixtures into
    // pluginRoot/<subdir>. fixtureDir is the build-tree path to the
    // pre-built .so + manifest.json (defined by tests/CMakeLists.txt
    // PHOSPHOR_REGISTRY_FAKE_PLUGIN_*_DIR macros). soBasename is
    // the .so's filename WITHOUT extension (e.g.
    // "libphosphor_registry_test_fake_plugin"). The .so is copied
    // into the destination as <subdir>.so so the loader's manifest-
    // id-vs-directory-basename rule resolves cleanly. Out-param
    // destDir receives the installed plugin directory path on
    // success. Returns false on any setup failure so the caller can
    // QVERIFY the result — Qt's QVERIFY macro inside a helper only
    // returns from the helper, so wrapping the checks in throwaway
    // lambdas (the previous shape) silently swallowed failures and
    // left the test running against an incomplete fixture.
    [[nodiscard]] bool installPluginFixture(const QString& pluginRoot, const QString& subdir, const QString& fixtureDir,
                                            const QString& soBasename, QString& destDir) const;

    [[nodiscard]] bool installFakePlugin(const QString& pluginRoot, const QString& subdir, QString& destDir) const
    {
        return installPluginFixture(pluginRoot, subdir, QStringLiteral(PHOSPHOR_REGISTRY_FAKE_PLUGIN_DIR),
                                    QStringLiteral("libphosphor_registry_test_fake_plugin"), destDir);
    }

    [[nodiscard]] bool installFakePluginIdMismatch(const QString& pluginRoot, QString& destDir) const
    {
        return installPluginFixture(pluginRoot, QStringLiteral("id-mismatch-plugin"),
                                    QStringLiteral(PHOSPHOR_REGISTRY_FAKE_PLUGIN_IDMISMATCH_DIR),
                                    QStringLiteral("libphosphor_registry_test_fake_plugin_idmismatch"), destDir);
    }

    [[nodiscard]] bool installFakePluginNullFactory(const QString& pluginRoot, QString& destDir) const
    {
        return installPluginFixture(pluginRoot, QStringLiteral("null-factory-plugin"),
                                    QStringLiteral(PHOSPHOR_REGISTRY_FAKE_PLUGIN_NULLFACTORY_DIR),
                                    QStringLiteral("libphosphor_registry_test_fake_plugin_nullfactory"), destDir);
    }

    [[nodiscard]] bool installFakePluginNoEntry(const QString& pluginRoot, QString& destDir) const
    {
        return installPluginFixture(pluginRoot, QStringLiteral("no-entry-plugin"),
                                    QStringLiteral(PHOSPHOR_REGISTRY_FAKE_PLUGIN_NOENTRY_DIR),
                                    QStringLiteral("libphosphor_registry_test_fake_plugin_noentry"), destDir);
    }
};

bool TestPluginLoader::installPluginFixture(const QString& pluginRoot, const QString& subdir, const QString& fixtureDir,
                                            const QString& soBasename, QString& destDir) const
{
    // No QVERIFY here: QVERIFY inside a non-test-slot helper only
    // returns from the helper itself (in the previous shape, even
    // worse: from throwaway lambdas inside the helper). Each
    // failure path returns false so the caller's QVERIFY surfaces
    // the problem at the test slot scope.
    destDir = QDir(pluginRoot).absoluteFilePath(subdir);
    if (!QDir().mkpath(destDir)) {
        qWarning().noquote() << "installPluginFixture: mkpath failed for" << destDir;
        return false;
    }
    const QString fixtureSo = QDir(fixtureDir).absoluteFilePath(soBasename + QStringLiteral(".so"));
    const QString fixtureManifest = QDir(fixtureDir).absoluteFilePath(QStringLiteral("manifest.json"));
    if (!QFileInfo::exists(fixtureSo)) {
        qWarning().noquote() << "installPluginFixture: missing fixture .so:" << fixtureSo;
        return false;
    }
    if (!QFileInfo::exists(fixtureManifest)) {
        qWarning().noquote() << "installPluginFixture: missing fixture manifest:" << fixtureManifest;
        return false;
    }

    const QString destSo = QDir(destDir).absoluteFilePath(subdir + QStringLiteral(".so"));
    const QString destManifest = QDir(destDir).absoluteFilePath(QStringLiteral("manifest.json"));

    if (!QFile::copy(fixtureSo, destSo)) {
        qWarning().noquote() << "installPluginFixture: failed to copy" << fixtureSo << "->" << destSo;
        return false;
    }
    if (!QFile::copy(fixtureManifest, destManifest)) {
        qWarning().noquote() << "installPluginFixture: failed to copy" << fixtureManifest << "->" << destManifest;
        return false;
    }
    return true;
}

void TestPluginLoader::loadsPluginAtScan()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString pluginRoot = tempDir.path();
    QString installedDir;
    QVERIFY(installFakePlugin(pluginRoot, QStringLiteral("fake-plugin"), installedDir));

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
    QString installedDir;
    QVERIFY(installFakePlugin(pluginRoot, QStringLiteral("fake-plugin"), installedDir));

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
    // ABI value follows the build-time constant so the test pins the
    // "no .so under" rejection path regardless of future ABI bumps.
    // Hardcoding `1` would silently shift the test to the abi-mismatch
    // path if PluginAbiVersion were ever incremented to 2.
    mf.write(
        QStringLiteral("{\"id\":\"manifest-only\",\"displayName\":\"Bad\",\"abi\":%1}").arg(PluginAbiVersion).toUtf8());
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
    QString installedDir;
    QVERIFY(installFakePlugin(pluginRoot, QStringLiteral("fake-plugin"), installedDir));

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
    QString installedDir;
    QVERIFY(installFakePluginIdMismatch(pluginRoot, installedDir));

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
    // PluginAbiVersion + 99 guarantees a mismatch across any future
    // bump of the constant, mirroring parseObject_rejectsAbiMismatch
    // in test_phosphor_registry_manifest.cpp.
    mf.write(QStringLiteral("{\"id\":\"future-plugin\",\"displayName\":\"Future\",\"abi\":%1}")
                 .arg(PluginAbiVersion + 99)
                 .toUtf8());
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
    mf.write(QStringLiteral("{\"id\":\"foo..bar\",\"displayName\":\"X\",\"abi\":%1}").arg(PluginAbiVersion).toUtf8());
    mf.close();

    Registry<IBarWidgetFactory> registry;
    PluginLoader loader(&registry, pluginRoot);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("unsafe characters")));
    loader.scanAndLoad();

    QCOMPARE(registry.size(), 0);
}

void TestPluginLoader::rejectsNullFactoryReturn()
{
    // Plugin entry point returns nullptr (e.g. construction failed
    // because a required external service was unavailable). The
    // loader must refuse, log, and clean up the QLibrary mapping.
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString pluginRoot = tempDir.path();
    QString installedDir;
    QVERIFY(installFakePluginNullFactory(pluginRoot, installedDir));

    Registry<IBarWidgetFactory> registry;
    PluginLoader loader(&registry, pluginRoot);
    QSignalSpy loadedSpy(&loader, &PluginLoader::pluginLoaded);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("entry point returned null")));
    loader.scanAndLoad();

    QCOMPARE(loadedSpy.count(), 0);
    QCOMPARE(registry.size(), 0);
    QVERIFY(loader.loadedPluginIds().isEmpty());
}

void TestPluginLoader::rejectsPluginWithoutEntryPoint()
{
    // Plugin .so loads cleanly via dlopen but does not export
    // phosphor_registry_create_factory. The loader logs a warning
    // and unloads.
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString pluginRoot = tempDir.path();
    QString installedDir;
    QVERIFY(installFakePluginNoEntry(pluginRoot, installedDir));

    Registry<IBarWidgetFactory> registry;
    PluginLoader loader(&registry, pluginRoot);
    QSignalSpy loadedSpy(&loader, &PluginLoader::pluginLoaded);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("missing entry point")));
    loader.scanAndLoad();

    QCOMPARE(loadedSpy.count(), 0);
    QCOMPARE(registry.size(), 0);
}

void TestPluginLoader::rejectsCorruptSoFile()
{
    // Plugin .so is structurally invalid (not an ELF). dlopen
    // refuses; QLibrary::load returns false; the loader logs a
    // warning and moves on.
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString pluginRoot = tempDir.path();
    const QString pluginDir = QDir(pluginRoot).absoluteFilePath(QStringLiteral("corrupt-plugin"));
    QDir().mkpath(pluginDir);
    QFile soFile(QDir(pluginDir).absoluteFilePath(QStringLiteral("corrupt-plugin.so")));
    QVERIFY(soFile.open(QIODevice::WriteOnly));
    soFile.write("this is not an ELF binary");
    soFile.close();
    QFile mfFile(QDir(pluginDir).absoluteFilePath(QStringLiteral("manifest.json")));
    QVERIFY(mfFile.open(QIODevice::WriteOnly | QIODevice::Text));
    mfFile.write(
        QStringLiteral("{\"id\":\"corrupt-plugin\",\"displayName\":\"X\",\"abi\":%1}").arg(PluginAbiVersion).toUtf8());
    mfFile.close();

    Registry<IBarWidgetFactory> registry;
    PluginLoader loader(&registry, pluginRoot);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("failed to load")));
    loader.scanAndLoad();

    QCOMPARE(registry.size(), 0);
}

void TestPluginLoader::loadsNewPluginAddedOnRescan()
{
    // Hot-reload's central differentiator: install a plugin AFTER
    // the loader is armed, then trigger a rescan, expect it to be
    // discovered + registered without any restart.
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString pluginRoot = tempDir.path();

    Registry<IBarWidgetFactory> registry;
    PluginLoader loader(&registry, pluginRoot);
    QSignalSpy loadedSpy(&loader, &PluginLoader::pluginLoaded);
    loader.scanAndLoad();
    QCOMPARE(loadedSpy.count(), 0);
    QCOMPARE(registry.size(), 0);

    // Drop a plugin into the watched root.
    QString installedDir;
    QVERIFY(installFakePlugin(pluginRoot, QStringLiteral("fake-plugin"), installedDir));
    loader.rescanNow();

    QCOMPARE(loadedSpy.count(), 1);
    QCOMPARE(loadedSpy.first().at(0).toString(), QStringLiteral("fake-plugin"));
    QCOMPARE(registry.size(), 1);
}

void TestPluginLoader::emitsRescanCompletedSignal()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString pluginRoot = tempDir.path();
    QString installedDir;
    QVERIFY(installFakePlugin(pluginRoot, QStringLiteral("fake-plugin"), installedDir));

    Registry<IBarWidgetFactory> registry;
    PluginLoader loader(&registry, pluginRoot);
    QSignalSpy rescanSpy(&loader, &PluginLoader::rescanCompleted);

    loader.scanAndLoad();
    QCOMPARE(rescanSpy.count(), 1);

    loader.rescanNow();
    QCOMPARE(rescanSpy.count(), 2);
}

void TestPluginLoader::liveWidgetCountReturnsMinusOneForLoadedPlugin()
{
    // Phase 1.3 leaves widget tracking unwired; liveWidgetCount
    // returns -1 ("untracked"). Lock the contract so a future
    // partial Phase-5 wiring doesn't return 0 (which would imply
    // "no live widgets" — a semantic shift) before the full refcount
    // path lands.
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString pluginRoot = tempDir.path();
    QString installedDir;
    QVERIFY(installFakePlugin(pluginRoot, QStringLiteral("fake-plugin"), installedDir));

    Registry<IBarWidgetFactory> registry;
    PluginLoader loader(&registry, pluginRoot);
    loader.scanAndLoad();
    QCOMPARE(loader.liveWidgetCount(QStringLiteral("fake-plugin")), -1);
}

void TestPluginLoader::liveWidgetCountReturnsMinusOneForUnknownPlugin()
{
    // Distinct from the loaded-plugin case: an unknown id today also
    // returns -1 because the implementation is unconditional, but a
    // future Phase-5 implementation might split "untracked" (-1)
    // from "unknown plugin" (0 or some other sentinel). Pin both
    // separately so the next implementer sees both cases explicitly.
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Registry<IBarWidgetFactory> registry;
    PluginLoader loader(&registry, tempDir.path());
    QCOMPARE(loader.liveWidgetCount(QStringLiteral("nonexistent")), -1);
}

void TestPluginLoader::pluginRootReturnsConfiguredPath()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Registry<IBarWidgetFactory> registry;
    PluginLoader loader(&registry, tempDir.path());
    QCOMPARE(loader.pluginRoot(), tempDir.path());
}

void TestPluginLoader::pluginRootResolvesXdgWhenEmpty()
{
    Registry<IBarWidgetFactory> registry;
    PluginLoader loader(&registry, QString());
    const QString resolved = loader.pluginRoot();
    QVERIFY(!resolved.isEmpty());
    QVERIFY(resolved.endsWith(QStringLiteral("phosphor/plugins")));
}

void TestPluginLoader::destructorPinsLibraryBeforeFactoryDestruction()
{
    // Smoke test for the library-pin-before-factory-destroy invariant
    // documented in pluginloader.cpp's performScanCycle unload block.
    // Sequence: load a plugin → remove its directory → rescan
    // (loader moves the QLibrary into m_pinnedLibraries) → drop the
    // PluginLoader. If a future refactor reversed the move-then-
    // destroy ordering, the factory destructor would dispatch through
    // a vtable in a freshly-unmapped .so and segfault under
    // QTEST_MAIN's exit path. Today the test simply asserting "no
    // crash" suffices.
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString pluginRoot = tempDir.path();
    QString installedDir;
    QVERIFY(installFakePlugin(pluginRoot, QStringLiteral("fake-plugin"), installedDir));

    {
        Registry<IBarWidgetFactory> registry;
        PluginLoader loader(&registry, pluginRoot);
        loader.scanAndLoad();
        QCOMPARE(registry.size(), 1);

        QDir(installedDir).removeRecursively();
        loader.rescanNow();
        QCOMPARE(registry.size(), 0);
        // loader + registry destruct here; m_pinnedLibraries holds
        // the .so until ~PluginLoader runs, and only after the
        // pinned-library destructor unmaps does the test exit.
    }
}

void TestPluginLoader::destructorWithoutPriorRescanDoesNotCrash()
{
    // Companion to destructorPinsLibraryBeforeFactoryDestruction.
    // That test exercises the unload-via-rescan path (entry moves
    // into m_pinnedLibraries before destruct). This test exercises
    // the second tear-down path: load the plugin, then drop the
    // PluginLoader WITHOUT removing the directory or rescanning.
    // The factory still lives in m_plugins, not m_pinnedLibraries,
    // and ~PluginLoader iterates m_plugins.keys() to unregister
    // before clear(). The ordering rule (QLibrary must outlive
    // the factory destructor's vtable dispatch) applies here too:
    // m_plugins.clear() runs the LoadedPlugin destructor, which
    // destroys the factory shared_ptr (calling the in-.so factory
    // destructor) before its unique_ptr<QLibrary> destructs. If a
    // future refactor reordered LoadedPlugin's members so the
    // library is destroyed first, the factory destructor would
    // jump into freed memory and crash here.
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString pluginRoot = tempDir.path();
    QString installedDir;
    QVERIFY(installFakePlugin(pluginRoot, QStringLiteral("fake-plugin"), installedDir));

    {
        Registry<IBarWidgetFactory> registry;
        PluginLoader loader(&registry, pluginRoot);
        loader.scanAndLoad();
        QCOMPARE(registry.size(), 1);
        // No rescan, no directory removal — fall straight through
        // to ~PluginLoader. If ordering is wrong, this crashes.
    }
}

QTEST_MAIN(TestPluginLoader)
#include "test_phosphor_registry_pluginloader.moc"
