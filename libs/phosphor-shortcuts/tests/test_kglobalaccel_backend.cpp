// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// OPT-IN live test for KGlobalAccelBackend against a real kglobalaccel
// daemon. Guarded by PHOSPHORSHORTCUTS_KGLOBALACCEL_TESTS=1 following the
// same three-valued-verdict pattern as the GPU-gated tests:
//   1. Env gate unset            → QSKIP (default; CI and plain ctest runs).
//   2. Gate set, no live daemon  → QSKIP with reason (the factory fell back
//      or read-back cannot engage).
//   3. Gate set, daemon reachable → real assertions run.
//
// Every shortcut registered here is TRANSIENT (persistent=false), so the
// backend purges its kglobalshortcutsrc record on unregister and on
// destruction — an opted-in run leaves no residue in the user's
// System Settings even if an assertion fails mid-test. Sequences use
// Meta+Shift+F34/F35, which collide with nothing real.
//
// What this pins that FakeBackend cannot: the read-back contract of the
// REAL backend — currentTriggers engages after a live setShortcut, follows
// an updateShortcut, and disengages (nullopt) after unregisterShortcut.

#include <PhosphorShortcuts/Factory.h>
#include <PhosphorShortcuts/IBackend.h>

#include <QCoreApplication>
#include <QKeySequence>
#include <QSignalSpy>
#include <QTest>

#include <optional>

namespace PhosphorShortcuts::tests {

class TestKGlobalAccelBackend : public QObject
{
    Q_OBJECT

private:
    bool gateOpen() const
    {
        return qEnvironmentVariable("PHOSPHORSHORTCUTS_KGLOBALACCEL_TESTS") == QLatin1String("1");
    }

private Q_SLOTS:
    void initTestCase()
    {
        // KGlobalAccelBackend asserts a non-empty applicationName (it is
        // the persistent component key) — set it before any construction.
        QCoreApplication::setApplicationName(QStringLiteral("phosphorshortcuts_kga_test"));
    }

    void liveReadbackFollowsRegisterUpdateUnregister()
    {
        if (!gateOpen()) {
            QSKIP("Set PHOSPHORSHORTCUTS_KGLOBALACCEL_TESTS=1 to run against a live kglobalaccel daemon");
        }

        const auto backend = createBackend(BackendHint::KGlobalAccel, nullptr);
        QVERIFY(backend != nullptr);

        const QString id = QStringLiteral("pz.test.kga.readback");
        const QKeySequence first(QStringLiteral("Meta+Shift+F35"));
        const QKeySequence second(QStringLiteral("Meta+Shift+F34"));

        backend->registerShortcut(id, first, first, QStringLiteral("PhosphorShortcuts live test"),
                                  /*persistent=*/false);
        backend->flush();

        const std::optional<QStringList> afterRegister = backend->currentTriggers(id);
        if (!afterRegister.has_value() || afterRegister->isEmpty()) {
            // The KGlobalAccel hint fell through (lib built without it) or
            // no daemon answered the grab — nothing meaningful to assert.
            backend->unregisterShortcut(id);
            QSKIP("kglobalaccel daemon unavailable in this session — live read-back not exercisable");
        }
        QVERIFY(afterRegister->contains(first.toString(QKeySequence::PortableText)));

        backend->updateShortcut(id, first, second);
        backend->flush();
        const std::optional<QStringList> afterUpdate = backend->currentTriggers(id);
        QVERIFY(afterUpdate.has_value());
        QVERIFY(afterUpdate->contains(second.toString(QKeySequence::PortableText)));

        backend->unregisterShortcut(id);
        // Unknown id after unregister → cannot-report, NOT engaged-empty:
        // the caller's own stored value is the only remaining answer.
        QVERIFY(!backend->currentTriggers(id).has_value());
    }
};

} // namespace PhosphorShortcuts::tests

QTEST_MAIN(PhosphorShortcuts::tests::TestKGlobalAccelBackend)
#include "test_kglobalaccel_backend.moc"
