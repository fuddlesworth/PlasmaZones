// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <PhosphorWayland/ForeignToplevel.h>

#include <QTest>
#include <QSignalSpy>
#include <QGuiApplication>

using namespace PhosphorWayland;

/// Offscreen tests — the layer-shell QPA never loads, so the manager has no
/// real Wayland connection. We can still verify the API contract: support
/// detection, idempotent stop, empty initial toplevel set, signal wiring.
/// End-to-end protocol behaviour is covered by manual testing on a live
/// compositor (KWin / sway / Hyprland).
class TestForeignToplevel : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testIsSupported_offscreen_returnsFalse()
    {
        QVERIFY(!ForeignToplevelManager::isSupported());
    }

    void testManager_constructsCleanly_offscreen()
    {
        // The manager must not crash when there's no compositor binding.
        // Earlier draft dereferenced LayerShellIntegration::instance()
        // without a null check — an offscreen QGuiApplication doesn't
        // load the QPA plugin and instance() returns nullptr.
        ForeignToplevelManager manager;
        QCOMPARE(manager.toplevels().size(), 0);
    }

    void testStop_isIdempotent()
    {
        ForeignToplevelManager manager;
        // Calling stop() twice on a manager that never bound is a no-op,
        // both times. Earlier draft set d->stopped=true the first time and
        // skipped the wl_proxy call the second time without updating any
        // observable state — verifying via the public API surface.
        manager.stop();
        manager.stop();
        QCOMPARE(manager.toplevels().size(), 0);
    }

    void testToplevelAddedSignal_hasCorrectSignature()
    {
        // Compile-time check: QSignalSpy fails to construct if the signal
        // signature doesn't match. Catches accidental signature drift in
        // the public API (e.g. someone changing it to pass by const&).
        ForeignToplevelManager manager;
        QSignalSpy addedSpy(&manager, &ForeignToplevelManager::toplevelAdded);
        QSignalSpy removedSpy(&manager, &ForeignToplevelManager::toplevelRemoved);
        QVERIFY(addedSpy.isValid());
        QVERIFY(removedSpy.isValid());
        // No actual toplevels arrive in offscreen mode, so spies stay empty.
        QCOMPARE(addedSpy.count(), 0);
        QCOMPARE(removedSpy.count(), 0);
    }

    void testManager_destructsCleanly_emptyState()
    {
        // Regression: earlier draft iterated d->toplevels and called
        // deleteLater() on each in the destructor. With an empty hash that's
        // a no-op, but we still want the destructor to exit cleanly without
        // touching the LayerShellIntegration singleton.
        {
            ForeignToplevelManager manager;
            Q_UNUSED(manager)
        }
        // If we got here, dtor did not crash.
        QVERIFY(true);
    }

    void testManager_destructsCleanly_afterStop()
    {
        ForeignToplevelManager manager;
        manager.stop();
        // dtor after stop() should still be safe — both paths null the
        // protocol-side manager pointer differently.
        QVERIFY(true);
    }
};

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    TestForeignToplevel tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_foreigntoplevel.moc"
