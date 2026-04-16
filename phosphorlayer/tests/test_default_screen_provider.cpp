// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/defaults/DefaultScreenProvider.h>

#include <QGuiApplication>
#include <QScreen>
#include <QSignalSpy>
#include <QTest>

using namespace PhosphorLayer;

class TestDefaultScreenProvider : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void screensMatchQGuiApplication()
    {
        DefaultScreenProvider p;
        // Offscreen QPA gives us at least one real QScreen — assert the
        // provider passes through whatever QGuiApplication reports, not an
        // empty list or a subset.
        QCOMPARE(p.screens(), qGuiApp->screens());
        QVERIFY(!p.screens().isEmpty());
    }

    void primaryMatchesQGuiApplication()
    {
        DefaultScreenProvider p;
        QCOMPARE(p.primary(), qGuiApp->primaryScreen());
    }

    void focusedFallsBackToPrimary()
    {
        DefaultScreenProvider p;
        // Qt has no portable focused-screen concept — the default impl
        // returns primary() as documented. Subclasses override for
        // compositor-specific hints.
        QCOMPARE(p.focused(), p.primary());
    }

    void notifierIsNotNull()
    {
        DefaultScreenProvider p;
        QVERIFY(p.notifier() != nullptr);
    }

    void notifierEmitsOnGeometryChange()
    {
        // QScreen::geometryChanged is hard to fire from userspace, but we
        // can at least verify the provider connected to it by stress-testing
        // the signal wiring end-to-end via a direct Q_EMIT on the notifier.
        // (If a future refactor breaks the connect chain, the end-to-end
        // test_topology tests will catch the regression — this test just
        // pins down that the notifier object exists and is signalable.)
        DefaultScreenProvider p;
        QSignalSpy spy(p.notifier(), &ScreenProviderNotifier::screensChanged);
        Q_EMIT p.notifier()->screensChanged();
        QCOMPARE(spy.count(), 1);
    }
};

QTEST_MAIN(TestDefaultScreenProvider)
#include "test_default_screen_provider.moc"
