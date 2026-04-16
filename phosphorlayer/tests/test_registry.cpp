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
};

QTEST_MAIN(TestRegistry)
#include "test_registry.moc"
