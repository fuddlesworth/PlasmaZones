// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Smoke test for phosphor-service-lock. Pins the deterministic plumbing
// contract: QML-registration idempotency and inert construction. With no live
// compositor (offscreen QPA) the session-lock protocol is unavailable, so the
// service constructs, reports unsupported, and never crashes. The PAM
// authentication and the lock state machine are unit-tested separately; the live
// lock path needs a real compositor and is exercised via the CLI demo.

#include <PhosphorServiceLock/LockService.h>
#include <PhosphorServiceLock/QmlRegistration.h>

#include <QTest>

using namespace PhosphorServiceLock;

class LockSmokeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void registerQmlTypesIsIdempotent();
    void constructsInert();
    void supportedIsQueryableWithoutCompositor();
};

void LockSmokeTest::registerQmlTypesIsIdempotent()
{
    // The std::call_once guard must make a second call a no-op (no crash, no
    // duplicate-registration fault). A hot-reloading shell relies on this.
    registerQmlTypes();
    registerQmlTypes();
}

void LockSmokeTest::constructsInert()
{
    // Constructing without a live compositor must not crash; the service simply
    // cannot lock.
    LockService service;
    Q_UNUSED(service);
}

void LockSmokeTest::supportedIsQueryableWithoutCompositor()
{
    // Under the offscreen platform there is no Wayland session, so the
    // session-lock protocol is unavailable and the query reports false.
    LockService service;
    QVERIFY(!service.isSupported());
}

QTEST_GUILESS_MAIN(LockSmokeTest)
#include "test_smoke.moc"
