// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceBrightness/QmlRegistration.h>

#include <QTest>

class TestPhosphorServiceBrightnessSmoke : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testRegisterQmlTypesIsIdempotent()
    {
        // The call_once guard must make registration safe to invoke from every
        // engine setup (a hot-reloading shell does exactly that), so a second
        // call must not crash or re-register.
        PhosphorServiceBrightness::registerQmlTypes();
        PhosphorServiceBrightness::registerQmlTypes();
        QVERIFY(true);
    }
};

QTEST_GUILESS_MAIN(TestPhosphorServiceBrightnessSmoke)
#include "test_smoke.moc"
