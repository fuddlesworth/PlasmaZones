// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceSession/PhosphorServiceSession.h>

#include <QDBusConnection>
#include <QTest>

using namespace PhosphorServiceSession;

class SmokeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // registerQmlTypes() is std::call_once-guarded, so a hot-reloading shell can
    // call it from every fresh QQmlEngine without Qt's duplicate-registration
    // warning. Calling it twice must be a no-op, not a crash.
    void registrationIsIdempotent()
    {
        registerQmlTypes();
        registerQmlTypes();
    }

    // Constructing the host with no live logind must not crash or block: the
    // service loads inert (no calls are issued at construction in the skeleton).
    void constructsInertWithoutLogind()
    {
        SessionHost production;
        Q_UNUSED(production)

        // The DI ctor against the session bus is the fake-logind test seam; with
        // no Manager bound it is equally inert.
        SessionHost injected(QDBusConnection::sessionBus(), QStringLiteral("org.freedesktop.login1"));
        Q_UNUSED(injected)
    }
};

QTEST_GUILESS_MAIN(SmokeTest)

#include "test_smoke.moc"
