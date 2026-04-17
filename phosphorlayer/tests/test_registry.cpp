// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/PhosphorLayer.h>

#include "mocks/mockscreenprovider.h"
#include "mocks/mocktransport.h"

#include <QGuiApplication>
#include <QQuickItem>
#include <QTest>

using namespace PhosphorLayer;
using PhosphorLayer::Testing::MockScreenProvider;
using PhosphorLayer::Testing::MockTransport;

class TestRegistry : public QObject
{
    Q_OBJECT
private:
    SurfaceConfig makeConfig(QScreen* s)
    {
        SurfaceConfig c;
        c.role = Roles::CornerToast;
        c.contentItem = std::make_unique<QQuickItem>();
        c.screen = s;
        c.debugName = QStringLiteral("toast-") + (s ? s->name() : QStringLiteral("none"));
        return c;
    }

private Q_SLOTS:
    void createForAllScreensBuildsOnePerScreen()
    {
        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));
        ScreenSurfaceRegistry<> reg(&f, &s);

        const int n = s.screens().size();
        QVERIFY(n >= 1); // offscreen QPA provides at least one screen

        auto built = reg.createForAllScreens([&](QScreen* screen) {
            return f.create(makeConfig(screen));
        });

        QCOMPARE(built.size(), n);
        QCOMPARE(reg.surfaces().size(), n);
        for (QScreen* screen : s.screens()) {
            QVERIFY(reg.surfaceForScreen(screen) != nullptr);
        }
    }

    void createForAllScreensIsIdempotent()
    {
        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));
        ScreenSurfaceRegistry<> reg(&f, &s);

        const auto first = reg.createForAllScreens([&](QScreen* screen) {
            return f.create(makeConfig(screen));
        });
        const auto second = reg.createForAllScreens([&](QScreen* screen) {
            return f.create(makeConfig(screen));
        });

        QCOMPARE(first.size(), second.size());
        for (int i = 0; i < first.size(); ++i) {
            QCOMPARE(first[i], second[i]); // same pointer — no duplicate surface
        }
    }

    void syncToScreensDropsRemoved()
    {
        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));
        ScreenSurfaceRegistry<> reg(&f, &s);

        if (s.screens().size() < 2) {
            QSKIP("Need at least two screens — offscreen QPA only provides one by default");
        }

        reg.createForAllScreens([&](QScreen* screen) {
            return f.create(makeConfig(screen));
        });
        const int initial = reg.surfaces().size();

        QList<QScreen*> reduced = s.screens();
        reduced.removeLast();
        s.setScreens(reduced);

        reg.syncToScreens([&](QScreen* screen) {
            return f.create(makeConfig(screen));
        });

        QCOMPARE(reg.surfaces().size(), initial - 1);
    }

    void clearDestroysAllSurfaces()
    {
        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));
        ScreenSurfaceRegistry<> reg(&f, &s);
        reg.createForAllScreens([&](QScreen* screen) {
            return f.create(makeConfig(screen));
        });
        QVERIFY(!reg.surfaces().isEmpty());
        reg.clear();
        QVERIFY(reg.surfaces().isEmpty());
    }

    void adoptSurfaceRegistersWithScreen()
    {
        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));
        ScreenSurfaceRegistry<> reg(&f, &s);

        QScreen* primary = s.primary();
        QVERIFY(primary);
        auto* surface = f.create(makeConfig(primary));
        QVERIFY(surface);

        reg.adoptSurface(primary, surface);
        QCOMPARE(reg.surfaceForScreen(primary), surface);
        QCOMPARE(reg.surfaces().size(), 1);
    }

    void adoptSurfaceReplacesPriorAndDeletesIt()
    {
        // Bug regression: QHash::insert silently replaced the prior QPointer
        // without deleteLater on the displaced surface, leaking it until
        // engine teardown (or forever if the registry outlived the engine).
        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));
        ScreenSurfaceRegistry<> reg(&f, &s);

        QScreen* primary = s.primary();
        auto* first = f.create(makeConfig(primary));
        auto* second = f.create(makeConfig(primary));
        QVERIFY(first);
        QVERIFY(second);
        QVERIFY(first != second);

        QPointer<Surface> watcher(first);
        reg.adoptSurface(primary, first);
        reg.adoptSurface(primary, second);

        QCOMPARE(reg.surfaceForScreen(primary), second);

        // Drain deleteLater so the watcher nulls — proves the prior surface
        // was handed to deleteLater() rather than silently dropped.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        QVERIFY(watcher.isNull());
    }

    void zeroScreenProviderYieldsEmptyRegistry()
    {
        // Hot-unplug of the last screen (laptop lid closed, single external
        // disconnected) is a realistic state. Registry must tolerate an
        // empty screens() list: creation is a no-op, syncToScreens drops
        // every tracked surface, no crash on subsequent queries.
        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));
        ScreenSurfaceRegistry<> reg(&f, &s);

        // Seed the registry with a real surface.
        reg.createForAllScreens([&](QScreen* screen) {
            return f.create(makeConfig(screen));
        });
        QVERIFY(!reg.surfaces().isEmpty());

        // Simulate "all screens removed". Provider reports empty; the
        // factory will reject any new creates (no primary), so
        // syncToScreens must remove everything without invoking the
        // builder. Without this behaviour the registry would keep dangling
        // QPointers to surfaces bound to removed screens.
        s.setScreens({});
        QCOMPARE(s.screens().size(), 0);
        QCOMPARE(s.primary(), nullptr);

        reg.syncToScreens([&](QScreen* screen) {
            return f.create(makeConfig(screen)); // never called
        });
        QCOMPARE(reg.surfaces().size(), 0);
        QCOMPARE(reg.surfaceForScreen(nullptr), nullptr);
    }

    void adoptSurfaceSameSurfaceIsIdempotent()
    {
        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));
        ScreenSurfaceRegistry<> reg(&f, &s);

        QScreen* primary = s.primary();
        auto* surface = f.create(makeConfig(primary));
        QVERIFY(surface);

        QPointer<Surface> watcher(surface);
        reg.adoptSurface(primary, surface);
        reg.adoptSurface(primary, surface);

        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        // Re-adopting the same surface must NOT schedule it for deletion.
        QVERIFY(!watcher.isNull());
        QCOMPARE(reg.surfaceForScreen(primary), surface);
    }
};

QTEST_MAIN(TestRegistry)
#include "test_registry.moc"
