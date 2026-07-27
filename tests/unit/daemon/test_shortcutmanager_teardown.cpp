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
    RecordingBackend(QStringList* registers, QStringList* unregisters)
        : m_registers(registers)
        , m_unregisters(unregisters)
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

    void flush() override
    {
        Q_EMIT ready();
    }

private:
    QStringList* m_registers;
    QStringList* m_unregisters;
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
        manager.setBackendForTesting(std::make_unique<RecordingBackend>(&registers, &unregisters));

        manager.registerShortcuts();
        QVERIFY2(!registers.isEmpty(), "expected the catalog to reach the backend on registerShortcuts()");

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
        manager.setBackendForTesting(std::make_unique<RecordingBackend>(&registers, &unregisters));
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
