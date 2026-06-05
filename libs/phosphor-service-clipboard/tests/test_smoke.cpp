// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Smoke test for phosphor-service-clipboard. Pins the deterministic plumbing
// contract: QML-registration idempotency and inert construction. With no live
// compositor (offscreen QPA) the data-control protocol is unavailable, so the
// service constructs, reports unsupported, and never crashes. The live clipboard
// watch / read / write needs a real compositor and is exercised via the CLI
// demo; the history model and persistence logic are unit-tested separately.

#include <PhosphorServiceClipboard/ClipboardService.h>
#include <PhosphorServiceClipboard/QmlRegistration.h>

#include <QTest>

using namespace PhosphorServiceClipboard;

class ClipboardSmokeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void registerQmlTypesIsIdempotent();
    void constructsInert();
    void supportedIsQueryableWithoutCompositor();
};

void ClipboardSmokeTest::registerQmlTypesIsIdempotent()
{
    // The std::call_once guard must make a second call a no-op (no crash, no
    // duplicate-registration fault). A hot-reloading shell relies on this.
    registerQmlTypes();
    registerQmlTypes();
}

void ClipboardSmokeTest::constructsInert()
{
    // Constructing without a live compositor must not crash; the service simply
    // has nothing to watch.
    ClipboardService service;
    Q_UNUSED(service);
}

void ClipboardSmokeTest::supportedIsQueryableWithoutCompositor()
{
    // Under the offscreen platform there is no Wayland session, so the
    // data-control protocol is unavailable and the query reports false.
    ClipboardService service;
    QVERIFY(!service.isSupported());
}

QTEST_GUILESS_MAIN(ClipboardSmokeTest)
#include "test_smoke.moc"
