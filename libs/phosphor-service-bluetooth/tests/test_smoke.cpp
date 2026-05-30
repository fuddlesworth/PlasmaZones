// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceBluetooth/QmlRegistration.h>

#include <QTest>

class TestPhosphorServiceBluetoothSmoke : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testRegisterQmlTypesIsIdempotent()
    {
        // The call_once guard must make registration safe to invoke from
        // every engine setup (a hot-reloading shell does exactly that), so a
        // second call must not crash or re-register.
        PhosphorServiceBluetooth::registerQmlTypes();
        PhosphorServiceBluetooth::registerQmlTypes();
        QVERIFY(true);
    }
};

QTEST_GUILESS_MAIN(TestPhosphorServiceBluetoothSmoke)
#include "test_smoke.moc"
