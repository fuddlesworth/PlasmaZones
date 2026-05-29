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
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QtMessageHandler>

#ifndef PHOSPHOR_REGISTRY_FAKE_PLUGIN_DIR
#error "PHOSPHOR_REGISTRY_FAKE_PLUGIN_DIR must be defined by tests/CMakeLists.txt"
#endif
#ifndef PHOSPHOR_REGISTRY_FAKE_PLUGIN_SECONDARY_DIR
#error "PHOSPHOR_REGISTRY_FAKE_PLUGIN_SECONDARY_DIR must be defined by tests/CMakeLists.txt"
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

namespace {

// File-scope sink that captures every qWarning emitted while the
// scoped installer below is active. We can't enforce the
// "no qWarning fires" contract via the process-wide
// QT_FATAL_WARNINGS env var because the sibling tests in this
// binary rely on QTest::ignoreMessage to suppress intentional
// warning paths (ignoresInvalidManifest, rejectsAbiVersionMismatch,
// ...), and Qt's default handler aborts on a fatal-warning trigger
// before the ignore-message infrastructure can match. Per-test
// instrumentation it is.
//
// g_warningSink is global (not an instance member) because
// qInstallMessageHandler takes a plain function pointer — the
// callback has no `this` to capture, so the sink pointer has to
// live somewhere with static storage duration. The prior-handler
// pointer, however, moves onto the instance below so nesting two
// WarningCapture scopes can save+restore correctly without the
// inner scope clobbering the outer's saved handler.
QStringList* g_warningSink = nullptr;

void warningCapturingHandler(QtMsgType type, const QMessageLogContext& context, const QString& message);

// RAII guard: installs warningCapturingHandler on construction and
// restores the previous handler on destruction. Pointed at a
// caller-owned QStringList so each test gets a fresh sink with no
// cross-test bleed-through. Restoring the handler in the destructor
// (not just clearing the sink) protects sibling tests that rely on
// the default handler's QTest::ignoreMessage routing.
//
// The prior handler is held per-instance so nesting is structurally
// safe: an outer WarningCapture's destructor restores the handler
// that was live BEFORE the outer scope opened, not whatever the
// inner scope happened to leave behind. Today the test file never
// nests two captures, but the global previously made nesting silently
// corrupt — a future test that wraps an existing captured block
// would have leaked warningCapturingHandler as "the prior handler"
// for the inner scope's restore, leaving warningCapturingHandler
// installed indefinitely after both scopes unwound.
//
// The g_warningSink global is still required (the message handler
// callback is a C-style function pointer with no `this`), but a
// Q_ASSERT in the constructor catches the only remaining nesting
// hazard: two concurrent captures would clobber the sink pointer
// and re-route the outer scope's captured warnings into the inner
// scope's sink. Asserting g_warningSink is null on entry surfaces
// that misuse immediately in debug builds.
class WarningCapture
{
public:
    explicit WarningCapture(QStringList& sink)
    {
        Q_ASSERT(g_warningSink == nullptr);
        Q_ASSERT(s_activeCapture == nullptr);
        g_warningSink = &sink;
        s_activeCapture = this;
        m_priorHandler = qInstallMessageHandler(warningCapturingHandler);
    }
    ~WarningCapture()
    {
        qInstallMessageHandler(m_priorHandler);
        s_activeCapture = nullptr;
        g_warningSink = nullptr;
    }
    Q_DISABLE_COPY_MOVE(WarningCapture)

    QtMessageHandler priorHandler() const
    {
        return m_priorHandler;
    }

    // Single-active pointer the C-style message-handler callback
    // chains through to invoke the saved prior handler. Set in
    // the ctor, cleared in the dtor — the matching Q_ASSERT in
    // the ctor guarantees nesting can't quietly swap the active
    // capture out from under the outer scope.
    static WarningCapture* s_activeCapture;

private:
    QtMessageHandler m_priorHandler = nullptr;
};

WarningCapture* WarningCapture::s_activeCapture = nullptr;

void warningCapturingHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    if (type == QtWarningMsg && g_warningSink) {
        g_warningSink->append(message);
    }
    if (WarningCapture::s_activeCapture && WarningCapture::s_activeCapture->priorHandler()) {
        WarningCapture::s_activeCapture->priorHandler()(type, context, message);
    }
}

} // namespace

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
    void rescanReentryFromPluginUnloadedSlotIsSafe();
    void rescanReentryRemovingSecondPluginExercisesConstFindGuard();

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

    [[nodiscard]] bool installFakePluginSecondary(const QString& pluginRoot, const QString& subdir,
                                                  QString& destDir) const
    {
        // Distinct manifest id ("fake-plugin-secondary") so the loader
        // can hold BOTH the primary and secondary fixtures in m_plugins
        // at the same time. Required by the constFind-early-continue
        // regression test, which needs the outer scan loop's snapshot
        // to contain two ids so the inner rescan can clear one of them
        // between iterations.
        return installPluginFixture(pluginRoot, subdir, QStringLiteral(PHOSPHOR_REGISTRY_FAKE_PLUGIN_SECONDARY_DIR),
                                    QStringLiteral("libphosphor_registry_test_fake_plugin_secondary"), destDir);
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

void TestPluginLoader::rescanReentryFromPluginUnloadedSlotIsSafe()
{
    // Companion to rescanReentryRemovingSecondPluginExercisesConstFindGuard
    // below. This test pins the *safety* half of the contract: a slot
    // wired to pluginUnloaded that calls back into rescanNow() must
    // not crash and must not flood the journal with warnings. The
    // sibling test pins the *coverage* half by constructing the
    // exact two-plugin scenario where the constFind-with-missing-
    // entry branch fires on the outer loop's second iteration.
    //
    // The constFind path was downgraded from qWarning to qDebug in
    // the same audit pass: legitimate re-entry handling on the
    // happy path should not emit a warning. Pin that here by
    // installing a per-test qWarning sink — any qWarning that fires
    // across the loader call sequence below shows up in the captured
    // list, which we then assert is empty.
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString pluginRoot = tempDir.path();
    QString installedDir;
    QVERIFY(installFakePlugin(pluginRoot, QStringLiteral("fake-plugin"), installedDir));

    Registry<IBarWidgetFactory> registry;
    PluginLoader loader(&registry, pluginRoot);
    QSignalSpy unloadedSpy(&loader, &PluginLoader::pluginUnloaded);
    QSignalSpy rescanSpy(&loader, &PluginLoader::rescanCompleted);

    loader.scanAndLoad();
    QCOMPARE(registry.size(), 1);

    // Wire a slot that calls rescanNow from inside pluginUnloaded.
    // This is the exact re-entry pattern the guard protects against:
    // the registry's factoryUnregistered (fired before pluginUnloaded)
    // or pluginUnloaded itself can trigger arbitrary user code that
    // calls back into loader.rescanNow(). If the inner rescan tried
    // to re-process an already-removed entry without the constFind
    // guard, the outer iteration would dereference a stale shared_ptr.
    bool reentered = false;
    QObject::connect(&loader, &PluginLoader::pluginUnloaded, &loader, [&]() {
        if (!reentered) {
            reentered = true;
            loader.rescanNow();
        }
    });

    QDir(installedDir).removeRecursively();

    // Capture qWarnings ONLY across the re-entry sequence. The
    // capture is scoped so sibling tests in this binary keep their
    // default message routing (QTest::ignoreMessage interop).
    QStringList capturedWarnings;
    {
        WarningCapture capture(capturedWarnings);
        loader.rescanNow();
    }

    QCOMPARE(unloadedSpy.count(), 1);
    QCOMPARE(unloadedSpy.first().at(0).toString(), QStringLiteral("fake-plugin"));
    QCOMPARE(registry.size(), 0);
    QVERIFY(loader.loadedPluginIds().isEmpty());

    // The slot re-entered exactly once. The inner rescan ran (it
    // finds no plugins, no work to do) and completed cleanly. Outer
    // rescan completed afterwards. Total rescanCompleted fires:
    //   1 from scanAndLoad's initial scan (above)
    //   1 from the reentrant inner rescanNow
    //   1 from the outer rescanNow
    QVERIFY(reentered);
    QCOMPARE(rescanSpy.count(), 3);

    // The contract: legitimate re-entry on the happy path emits no
    // qWarning. A regression that re-elevates the early-continue
    // from qDebug to qWarning would surface a captured entry here.
    QVERIFY2(capturedWarnings.isEmpty(),
             qPrintable(QStringLiteral("unexpected qWarning during re-entry: %1")
                            .arg(capturedWarnings.join(QStringLiteral("\n")))));
}

void TestPluginLoader::rescanReentryRemovingSecondPluginExercisesConstFindGuard()
{
    // Coverage test for the constFind-with-missing-entry branch in
    // pluginloader.cpp's performScanCycle unload loop. The branch
    // exists for the race where a slot wired to pluginUnloaded (or,
    // transitively, to the registry's factoryUnregistered) mutates
    // m_plugins out from under the iterating outer cycle. Exercising
    // it requires TWO distinct plugins in m_plugins so the outer
    // loop's keys-snapshot has two ids:
    //
    //   currentIds = [a, b]
    //
    // We then:
    //
    //   1. Remove BOTH plugin directories on disk before the outer
    //      rescan starts, so the outer scan sees an empty
    //      discoveredIds set and both ids are marked for unload.
    //   2. Wire a slot on pluginUnloaded that, on `a`'s unload, calls
    //      rescanNow(). The inner rescan re-runs performScanCycle,
    //      which finds no plugins on disk, then iterates ITS own
    //      m_plugins.keys() snapshot (which still contains `b` because
    //      the outer loop is mid-iteration on `a`). The inner cycle
    //      removes `b` from m_plugins via the standard unload path.
    //   3. Control returns to the outer cycle. The outer loop advances
    //      to its second snapshot id — `b` — and calls constFind,
    //      which returns m_plugins.constEnd() because the inner rescan
    //      already removed it. The early-continue branch fires.
    //
    // Without the constFind guard the outer loop would
    // m_plugins[pluginId] a non-existent entry, deref a
    // default-constructed shared_ptr's null library, and crash. With
    // the guard the loop logs once at qDebug and continues cleanly.
    //
    // The per-test WarningCapture below pins the qDebug-vs-qWarning
    // half of the contract: any regression that re-elevates the
    // early-continue to qWarning shows up in the captured list and
    // fails the test.
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString pluginRoot = tempDir.path();
    QString primaryDir;
    QString secondaryDir;
    QVERIFY(installFakePlugin(pluginRoot, QStringLiteral("fake-plugin"), primaryDir));
    QVERIFY(installFakePluginSecondary(pluginRoot, QStringLiteral("fake-plugin-secondary"), secondaryDir));

    Registry<IBarWidgetFactory> registry;
    PluginLoader loader(&registry, pluginRoot);
    QSignalSpy loadedSpy(&loader, &PluginLoader::pluginLoaded);
    QSignalSpy unloadedSpy(&loader, &PluginLoader::pluginUnloaded);

    loader.scanAndLoad();
    QCOMPARE(loadedSpy.count(), 2);
    QCOMPARE(registry.size(), 2);
    QCOMPARE(loader.loadedPluginIds().size(), 2);

    // Wire the re-entry slot. The latch makes the test deterministic
    // and iteration-order independent: only the FIRST pluginUnloaded
    // triggers the inner rescan, regardless of which plugin id the
    // outer cycle dequeues first from its keys-snapshot. Without
    // the latch BOTH pluginUnloaded fires (one per plugin) would
    // schedule a recursive rescanNow; the second one would be a
    // no-op (m_plugins already empty) but the order in which the
    // outer loop processed `a` vs `b` would silently change which
    // pluginUnloaded carried the re-entrant call. The latch pins
    // a single observable re-entry path — the inner rescan runs
    // EXACTLY once and the outer loop's second iteration is the
    // one that exercises the constFind early-continue branch. This
    // is determinism, not stack-overflow defense (a QHash with two
    // entries can't blow the stack on a recursive walk).
    bool reentered = false;
    QObject::connect(&loader, &PluginLoader::pluginUnloaded, &loader, [&]() {
        if (!reentered) {
            reentered = true;
            loader.rescanNow();
        }
    });

    // Remove BOTH plugin directories before the outer rescan starts.
    // The outer scan's discoveredIds will be empty and the unload
    // loop's snapshot will hold both ids.
    QVERIFY(QDir(primaryDir).removeRecursively());
    QVERIFY(QDir(secondaryDir).removeRecursively());

    // Capture qWarnings ONLY across the rescan that exercises the
    // early-continue branch. Sibling tests in this binary that rely
    // on the default handler (QTest::ignoreMessage) are unaffected.
    QStringList capturedWarnings;
    {
        WarningCapture capture(capturedWarnings);
        loader.rescanNow();
    }

    // Both plugins were unloaded exactly once each. The constFind
    // guard fired during the outer cycle's second iteration, so the
    // outer loop didn't double-process the entry the inner cycle
    // had already removed.
    QCOMPARE(unloadedSpy.count(), 2);
    QCOMPARE(registry.size(), 0);
    QVERIFY(loader.loadedPluginIds().isEmpty());
    QVERIFY(reentered);

    // The early-continue branch must stay at qDebug. Any captured
    // qWarning here is either a regression that re-elevated the
    // branch or a different warning we should investigate.
    QVERIFY2(capturedWarnings.isEmpty(),
             qPrintable(QStringLiteral("unexpected qWarning during re-entry: %1")
                            .arg(capturedWarnings.join(QStringLiteral("\n")))));
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
