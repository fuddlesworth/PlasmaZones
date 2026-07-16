// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Lifecycle / rescan re-entry tests for PluginLoader. Split out of
// test_phosphor_registry_pluginloader.cpp so the primary TU stays
// under the project's 1000-line guideline. Shares the plugin-fixture install
// helpers and the WarningCapture instrumentation via
// test_pluginloader_helpers.h — drift between the two binaries on
// scaffolding behaviour is structurally impossible.

#include "test_pluginloader_helpers.h"

#include <PhosphorRegistry/IBarWidgetFactory.h>
#include <PhosphorRegistry/PluginLoader.h>
#include <PhosphorRegistry/Registry.h>

#include <QDir>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

using namespace PhosphorRegistry;
using namespace PhosphorRegistryTestHelpers;

class TestPluginLoaderLifecycle : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void destructorPinsLibraryBeforeFactoryDestruction();
    void destructorWithoutPriorRescanDoesNotCrash();
    void rescanReentryFromPluginUnloadedSlotIsSafe();
    void rescanReentryRemovingSecondPluginExercisesConstFindGuard();
};

void TestPluginLoaderLifecycle::destructorPinsLibraryBeforeFactoryDestruction()
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

void TestPluginLoaderLifecycle::destructorWithoutPriorRescanDoesNotCrash()
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

void TestPluginLoaderLifecycle::rescanReentryFromPluginUnloadedSlotIsSafe()
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
    // Hoisted into a named constant because the literal `3` reads
    // as magic at the call site — each `+1` documents one of the
    // three rescan triggers above so a regression that changes
    // the cycle count surfaces with the explanation attached.
    constexpr int expectedRescanCount = 1 /*scanAndLoad*/ + 1 /*reentrant inner*/ + 1 /*outer*/;
    QCOMPARE(rescanSpy.count(), expectedRescanCount);

    // The contract: legitimate re-entry on the happy path emits no
    // qWarning. A regression that re-elevates the early-continue
    // from qDebug to qWarning would surface a captured entry here.
    QVERIFY2(capturedWarnings.isEmpty(),
             qPrintable(QStringLiteral("unexpected qWarning during re-entry: %1")
                            .arg(capturedWarnings.join(QStringLiteral("\n")))));
}

void TestPluginLoaderLifecycle::rescanReentryRemovingSecondPluginExercisesConstFindGuard()
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

QTEST_MAIN(TestPluginLoaderLifecycle)
#include "test_phosphor_registry_pluginloader_lifecycle.moc"
