// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Tests for PluginLoader. Uses CMake-built fake plugin .so fixtures
// (see tests/fake_plugin/ and tests/fake_plugin_idmismatch/). Each
// test sets up its own temporary plugin root, copies a fixture in,
// exercises the loader, and tears down.
//
// Lifecycle / rescan-reentry / destructor-pin slots live in the
// sibling TU test_phosphor_registry_pluginloader_lifecycle.cpp. Both
// TUs share the WarningCapture instrumentation + plugin-fixture install
// helpers via test_pluginloader_helpers.h.

#include "test_pluginloader_helpers.h"

#include <PhosphorRegistry/IBarWidgetFactory.h>
#include <PhosphorRegistry/PluginLoader.h>
#include <PhosphorRegistry/Registry.h>

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

using namespace PhosphorRegistry;
using namespace PhosphorRegistryTestHelpers;

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
    void rejectsSymlinkedSoEscapingRoot();
    void rejectsSymlinkedSubdirEscapingRoot();
    void rejectsGroupOrWorldWritableSo();
    void warnsOnceForMultipleSoFilesThenLoads();
    void multipleSoFailureStillReportsFailureReason();
    void rejectsNullFactoryReturn();
    void rejectsPluginWithoutEntryPoint();
    void rejectsCorruptSoFile();
    void loadsNewPluginAddedOnRescan();
    void emitsRescanCompletedSignal();
    void liveWidgetCountReturnsMinusOneForLoadedPlugin();
    void liveWidgetCountReturnsMinusOneForUnknownPlugin();
    void pluginRootReturnsConfiguredPath();
    void pluginRootResolvesXdgWhenEmpty();
};

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

void TestPluginLoader::rejectsSymlinkedSoEscapingRoot()
{
    // A plugin directory is user-writable, so a symlinked `<id>.so`
    // pointing at a payload OUTSIDE the plugin root would smuggle
    // arbitrary code past the containment boundary if the loader
    // followed it. The .so enumeration uses QDir::NoSymLinks, so the
    // symlink is never a load candidate; the directory then looks like
    // a manifest-only plugin and is refused.
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QTemporaryDir externalDir; // deliberately OUTSIDE the plugin root
    QVERIFY(externalDir.isValid());

    // Stage a real, loadable .so outside the plugin root.
    const QString fixtureSo = QDir(QStringLiteral(PHOSPHOR_REGISTRY_FAKE_PLUGIN_DIR))
                                  .absoluteFilePath(QStringLiteral("libphosphor_registry_test_fake_plugin.so"));
    QVERIFY(QFileInfo::exists(fixtureSo));
    const QString externalSo = QDir(externalDir.path()).absoluteFilePath(QStringLiteral("smuggled.so"));
    QVERIFY(QFile::copy(fixtureSo, externalSo));

    const QString pluginRoot = tempDir.path();
    const QString pluginDir = QDir(pluginRoot).absoluteFilePath(QStringLiteral("evil-plugin"));
    QVERIFY(QDir().mkpath(pluginDir));
    QFile mf(QDir(pluginDir).absoluteFilePath(QStringLiteral("manifest.json")));
    QVERIFY(mf.open(QIODevice::WriteOnly | QIODevice::Text));
    mf.write(
        QStringLiteral("{\"id\":\"evil-plugin\",\"displayName\":\"Evil\",\"abi\":%1}").arg(PluginAbiVersion).toUtf8());
    mf.close();

    // The only .so reachable from the plugin dir is a symlink to the
    // out-of-root payload.
    const QString symlinkSo = QDir(pluginDir).absoluteFilePath(QStringLiteral("evil-plugin.so"));
    QVERIFY(QFile::link(externalSo, symlinkSo));
    QVERIFY(QFileInfo(symlinkSo).isSymLink());

    Registry<IBarWidgetFactory> registry;
    PluginLoader loader(&registry, pluginRoot);
    QSignalSpy loadedSpy(&loader, &PluginLoader::pluginLoaded);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("no \\.so under")));
    loader.scanAndLoad();

    QCOMPARE(loadedSpy.count(), 0);
    QCOMPARE(registry.size(), 0);
    QVERIFY(loader.loadedPluginIds().isEmpty());
}

void TestPluginLoader::rejectsSymlinkedSubdirEscapingRoot()
{
    // The plugin root is user-writable; a symlinked plugin *directory*
    // pointing at a tree outside the root would smuggle a whole plugin
    // past the containment boundary if the loader followed it. The
    // subdirectory enumeration uses QDir::NoSymLinks, so the symlinked
    // directory is never even a discovery candidate.
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QTemporaryDir externalRoot; // deliberately OUTSIDE the plugin root
    QVERIFY(externalRoot.isValid());

    // A complete, valid plugin staged outside the plugin root.
    QString externalPluginDir;
    QVERIFY(installFakePlugin(externalRoot.path(), QStringLiteral("fake-plugin"), externalPluginDir));

    // Symlink <root>/fake-plugin -> <external>/fake-plugin. Its basename
    // matches the manifest id, so it WOULD load if symlinks were followed.
    const QString pluginRoot = tempDir.path();
    const QString linkPath = QDir(pluginRoot).absoluteFilePath(QStringLiteral("fake-plugin"));
    QVERIFY(QFile::link(externalPluginDir, linkPath));
    QVERIFY(QFileInfo(linkPath).isSymLink());

    Registry<IBarWidgetFactory> registry;
    PluginLoader loader(&registry, pluginRoot);
    QSignalSpy loadedSpy(&loader, &PluginLoader::pluginLoaded);
    loader.scanAndLoad();

    // The containment assertions, same as rejectsSymlinkedSoEscapingRoot's.
    // Without them this test asserted only that the skip was quiet, which a
    // loader that happily followed the symlink also satisfies — it would have
    // passed on the very escape it is named for.
    QCOMPARE(loadedSpy.count(), 0);
    QCOMPARE(registry.size(), 0);
    QVERIFY(loader.loadedPluginIds().isEmpty());

    // The guarded skip is silent (the symlinked dir is never a
    // discovery candidate); lock that contract — no warning should fire.
    QStringList captured;
    {
        WarningCapture capture(captured);
        loader.rescanNow();
    }
    QVERIFY2(captured.isEmpty(), qPrintable(captured.join(QLatin1Char('\n'))));
}

void TestPluginLoader::rejectsGroupOrWorldWritableSo()
{
    // A .so any local user or group member can overwrite between scans
    // is a code-execution vector inside the shell process. Install a
    // valid plugin, loosen the .so's mode, and confirm the StrictModes
    // guard refuses it.
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString pluginRoot = tempDir.path();
    QString installedDir;
    QVERIFY(installFakePlugin(pluginRoot, QStringLiteral("fake-plugin"), installedDir));

    const QString destSo = QDir(installedDir).absoluteFilePath(QStringLiteral("fake-plugin.so"));
    QVERIFY(QFileInfo::exists(destSo));
    // Owner rwx plus group + other write — the exact bit pattern the
    // guard refuses. (The plugin directory itself stays at its mkpath
    // mode, which a default umask leaves without group/other write, so
    // the .so is the deterministic trigger.)
    QFile soFile(destSo);
    QVERIFY(soFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                                  | QFileDevice::WriteGroup | QFileDevice::WriteOther));

    Registry<IBarWidgetFactory> registry;
    PluginLoader loader(&registry, pluginRoot);
    QSignalSpy loadedSpy(&loader, &PluginLoader::pluginLoaded);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("group/world-writable")));
    loader.scanAndLoad();

    QCOMPARE(loadedSpy.count(), 0);
    QCOMPARE(registry.size(), 0);
    QVERIFY(loader.loadedPluginIds().isEmpty());
}

void TestPluginLoader::warnsOnceForMultipleSoFilesThenLoads()
{
    // Two .so files in one plugin directory is a packaging mistake; the
    // loader warns, picks the lexicographically-first, and (when it
    // loads) succeeds. The advisory fires exactly once.
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString pluginRoot = tempDir.path();
    QString installedDir;
    QVERIFY(installFakePlugin(pluginRoot, QStringLiteral("fake-plugin"), installedDir));

    // A second copy of the SAME valid binary, named lexicographically
    // first so it is the one picked. Its factory id is still
    // "fake-plugin", so it matches the manifest and loads.
    const QString existingSo = QDir(installedDir).absoluteFilePath(QStringLiteral("fake-plugin.so"));
    const QString extraSo = QDir(installedDir).absoluteFilePath(QStringLiteral("aaa-extra.so"));
    QVERIFY(QFile::copy(existingSo, extraSo));

    Registry<IBarWidgetFactory> registry;
    PluginLoader loader(&registry, pluginRoot);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("contains 2 \\.so files")));
    loader.scanAndLoad();

    QCOMPARE(registry.size(), 1);
    QCOMPARE(loader.loadedPluginIds(), QStringList{QStringLiteral("fake-plugin")});

    // Second rescan: the plugin is already loaded, so the directory is
    // skipped and the advisory does NOT re-fire (a re-fire would trip an
    // unexpected-message failure here).
    loader.rescanNow();
    QCOMPARE(registry.size(), 1);
}

void TestPluginLoader::multipleSoFailureStillReportsFailureReason()
{
    // When the picked .so of a multi-.so directory fails, the multi-.so
    // advisory must NOT mask the failure reason: both warnings surface
    // (separate warn-once latches). QTest::ignoreMessage fails the test
    // if either expected warning is missing.
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString pluginRoot = tempDir.path();
    QString installedDir;
    QVERIFY(installFakePlugin(pluginRoot, QStringLiteral("fake-plugin"), installedDir));

    // Lexicographically-first .so is the id-mismatch binary (its factory
    // id != "fake-plugin"), so the picked .so fails the id check.
    const QString mismatchSo =
        QDir(QStringLiteral(PHOSPHOR_REGISTRY_FAKE_PLUGIN_IDMISMATCH_DIR))
            .absoluteFilePath(QStringLiteral("libphosphor_registry_test_fake_plugin_idmismatch.so"));
    QVERIFY(QFileInfo::exists(mismatchSo));
    const QString firstSo = QDir(installedDir).absoluteFilePath(QStringLiteral("aaa-mismatch.so"));
    QVERIFY(QFile::copy(mismatchSo, firstSo));

    Registry<IBarWidgetFactory> registry;
    PluginLoader loader(&registry, pluginRoot);
    // Emission order matches code order: the advisory fires before the
    // load is attempted, the id-mismatch fires during the load.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("contains 2 \\.so files")));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("factory id.*does not match manifest id")));
    loader.scanAndLoad();

    QCOMPARE(registry.size(), 0);
    QVERIFY(loader.loadedPluginIds().isEmpty());
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

QTEST_MAIN(TestPluginLoader)
#include "test_phosphor_registry_pluginloader.moc"
