// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Pins the teardown contract of ShortcutManager::unregisterShortcuts()
// (discussion #851): releasing the registration on the daemon stop() path
// must NOT call IBackend::unregisterShortcut for the persistent catalog
// entries. On KGlobalAccel that call purges the id's on-disk
// kglobalshortcutsrc record — the store holding the user's System Settings
// customisations — so a sweep there wiped every customised binding on each
// service restart. Teardown releases grabs by destroying the backend, whose
// destructor preserves persistent records.
//
// The explicit adhoc release path is the intended purge and must keep
// purging; the second case pins that the fix did not overreach.
//
// The same store is at risk from a second direction: parking a shortcut
// family whose feature is switched off. That park routes through
// IBackend::suspendShortcut, which releases the grab WITHOUT touching the
// persistent record — the whole point of the separate verb. RecordingBackend
// therefore records suspends apart from unregisters, so a park that
// regressed into a purge shows up as a failure here rather than as a silent
// wipe of the user's chords. Note the base class's default suspendShortcut
// forwards to unregisterShortcut, which is right for the grab-only backends
// (Portal, D-Bus) that own no persistent record but would be exactly the
// wrong answer for KGlobalAccel — so this fake overrides it, as the real
// KGlobalAccel backend does.

#include "daemon/controllers/shortcutmanager.h"

#include "config/settings.h"
#include "helpers/IsolatedConfigGuard.h"

#include <PhosphorShortcuts/IBackend.h>

#include <QKeySequence>
#include <QStringList>
#include <QTest>

#include <memory>

using PlasmaZones::Settings;
using PlasmaZones::ShortcutManager;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {

// Records calls into test-owned lists (passed by pointer) so the record
// survives the backend's destruction — unregisterShortcuts() destroys the
// injected backend as part of teardown.
class RecordingBackend : public PhosphorShortcuts::IBackend
{
    Q_OBJECT
public:
    RecordingBackend(QStringList* registers, QStringList* unregisters, QStringList* suspends)
        : m_registers(registers)
        , m_unregisters(unregisters)
        , m_suspends(suspends)
    {
    }

    void registerShortcut(const QString& id, const QKeySequence& /*defaultSeq*/, const QKeySequence& /*currentSeq*/,
                          const QString& /*description*/, bool /*persistent*/) override
    {
        m_registers->append(id);
    }

    void updateShortcut(const QString& /*id*/, const QKeySequence& /*defaultSeq*/,
                        const QKeySequence& /*newTrigger*/) override
    {
    }

    void unregisterShortcut(const QString& id) override
    {
        m_unregisters->append(id);
    }

    // Overridden away from the base default (which forwards to
    // unregisterShortcut) so a park is distinguishable from a purge, the
    // same split KGlobalAccelBackend implements.
    void suspendShortcut(const QString& id) override
    {
        m_suspends->append(id);
    }

    void flush() override
    {
        Q_EMIT ready();
    }

private:
    QStringList* m_registers;
    QStringList* m_unregisters;
    QStringList* m_suspends;
};

} // namespace

class TestShortcutManagerTeardown : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<IsolatedConfigGuard> m_configGuard;

private Q_SLOTS:
    void init()
    {
        m_configGuard = std::make_unique<IsolatedConfigGuard>();
    }

    void cleanup()
    {
        m_configGuard.reset();
    }

    void teardownDoesNotPurgePersistentBindings()
    {
        Settings settings;
        ShortcutManager manager(&settings);
        QStringList registers;
        QStringList unregisters;
        QStringList suspends;
        manager.setBackendForTesting(std::make_unique<RecordingBackend>(&registers, &unregisters, &suspends));

        manager.registerShortcuts();
        QVERIFY2(!registers.isEmpty(), "expected the catalog to reach the backend on registerShortcuts()");

        // Dynamic workspaces default to off, so that family parks on the
        // first settle. Positive control: without this the next assertion
        // would also pass if nothing had been parked at all.
        QVERIFY2(!suspends.isEmpty(),
                 "expected the disabled workspace family to be parked via suspendShortcut on registration");
        QVERIFY2(unregisters.isEmpty(),
                 qPrintable(QStringLiteral("parking a disabled family purged %1 persistent binding(s), e.g. \"%2\" — "
                                           "a park must release the grab without touching the on-disk record")
                                .arg(unregisters.size())
                                .arg(unregisters.value(0))));

        manager.unregisterShortcuts();
        QVERIFY2(unregisters.isEmpty(),
                 qPrintable(QStringLiteral("teardown purged %1 persistent binding(s) from the backend, e.g. \"%2\" — "
                                           "on KGlobalAccel this deletes the user's customised shortcut")
                                .arg(unregisters.size())
                                .arg(unregisters.value(0))));
    }

    void explicitAdhocReleaseStillPurges()
    {
        Settings settings;
        ShortcutManager manager(&settings);
        QStringList registers;
        QStringList unregisters;
        QStringList suspends;
        manager.setBackendForTesting(std::make_unique<RecordingBackend>(&registers, &unregisters, &suspends));
        manager.registerShortcuts();

        const QString id = QStringLiteral("pz.test.adhoc.escape");
        manager.registerAdhocShortcut(id, QKeySequence(QStringLiteral("Escape")), QStringLiteral("Test grab"), [] { });
        QVERIFY(registers.contains(id));

        manager.unregisterAdhocShortcut(id);
        QCOMPARE(unregisters, QStringList{id});
    }
};

QTEST_MAIN(TestShortcutManagerTeardown)
#include "test_shortcutmanager_teardown.moc"
