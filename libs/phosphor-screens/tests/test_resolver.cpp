// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// ScreenResolver behavioural tests. The daemon path requires a live
// D-Bus service, which isn't available in the offscreen QPA test
// environment — we exercise the fallback branch and the "daemon not on
// bus" short-circuit (both are the load-bearing safety nets).

#include <PhosphorScreens/Resolver.h>
#include <PhosphorScreens/ScreenIdentity.h>

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QPoint>
#include <QScreen>
#include <QTest>

using Phosphor::Screens::ResolverEndpoint;
using Phosphor::Screens::ScreenResolver;

class TestResolver : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testFallbackReturnsQtScreenWhenDaemonAbsent()
    {
        auto* primary = QGuiApplication::primaryScreen();
        if (!primary) {
            QSKIP("no primary screen available under offscreen QPA");
        }

        // Endpoint pointing at a service that definitely isn't registered.
        ResolverEndpoint bogus;
        bogus.service = QStringLiteral("org.nonexistent.Phosphor.Tests.NoDaemon");

        const QString id = ScreenResolver::effectiveScreenAt(primary->geometry().center(), bogus, /*timeoutMs=*/50);

        // Must fall back to the canonical EDID-style identifier (NOT the
        // connector name). Consumers route both daemon and fallback paths
        // through one resolver and expect one ID shape.
        QCOMPARE(id, Phosphor::Screens::ScreenIdentity::identifierFor(primary));
    }

    void testDaemonAbsentShortCircuitsQuickly()
    {
        // When the service isn't on the bus, the resolver must skip the
        // D-Bus call entirely instead of burning the full timeout waiting
        // on auto-activation. Regression guard for the pre-resolver
        // behaviour where blocking auto-start froze the UI for seconds.
        ResolverEndpoint bogus;
        bogus.service = QStringLiteral("org.nonexistent.Phosphor.Tests.Timeout");

        QElapsedTimer t;
        t.start();
        (void)ScreenResolver::effectiveScreenAt(QPoint(0, 0), bogus, /*timeoutMs=*/2000);
        // Generous cushion: the short-circuit involves a single
        // isServiceRegistered query; anything over ~500ms means the
        // resolver actually dispatched the async call.
        QVERIFY2(t.elapsed() < 500, qPrintable(QStringLiteral("short-circuit took %1 ms").arg(t.elapsed())));
    }

    void testCursorOverloadUsesCurrentCursorPos()
    {
        // Thin wrapper — exercise the signature so a refactor that breaks
        // QCursor integration gets caught. No assumption about what cursor
        // position the test environment reports, just that the call
        // returns something consistent with the endpoint contract
        // (non-crash, string-typed result).
        ResolverEndpoint bogus;
        bogus.service = QStringLiteral("org.nonexistent.Phosphor.Tests.Cursor");
        const QString id = ScreenResolver::effectiveScreenAtCursor(bogus, /*timeoutMs=*/50);
        Q_UNUSED(id);
        QVERIFY(true);
    }
};

QTEST_MAIN(TestResolver)
#include "test_resolver.moc"
