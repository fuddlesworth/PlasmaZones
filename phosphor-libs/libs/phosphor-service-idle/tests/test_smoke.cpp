// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Smoke test for phosphor-service-idle. Pins the deterministic plumbing
// contract: QML-registration idempotency and inert construction. With no live
// compositor (guiless QCoreApplication — no QPA at all) the idle protocols are
// unavailable, so the service constructs, reports unsupported, and never
// crashes. The live idle / inhibit lifecycle needs a real compositor and is
// exercised via the CLI demo; the pure stage-ladder logic is unit-tested in
// test_statemachine.cpp.

#include <PhosphorServiceIdle/IdleService.h>
#include <PhosphorServiceIdle/QmlRegistration.h>

#include <QTest>

using namespace PhosphorServiceIdle;

class IdleSmokeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void registerQmlTypesIsIdempotent();
    void constructsInert();
    void supportedIsQueryableWithoutCompositor();
};

void IdleSmokeTest::registerQmlTypesIsIdempotent()
{
    // The std::call_once guard must make a second call a no-op (no crash, no
    // duplicate-registration fault). A hot-reloading shell relies on this.
    registerQmlTypes();
    registerQmlTypes();
}

void IdleSmokeTest::constructsInert()
{
    // Constructing without a live compositor must not crash; the service simply
    // has nothing to monitor.
    IdleService service;
    Q_UNUSED(service);
}

void IdleSmokeTest::supportedIsQueryableWithoutCompositor()
{
    // With no QPA / no compositor there is no Wayland session, so the idle
    // protocol is unavailable and the query reports false. The point is that the
    // query is safe and returns without a live session.
    IdleService service;
    QVERIFY(!service.isSupported());
}

QTEST_GUILESS_MAIN(IdleSmokeTest)
#include "test_smoke.moc"
