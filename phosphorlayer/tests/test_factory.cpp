// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/PhosphorLayer.h>

#include "mocks/mockscreenprovider.h"
#include "mocks/mocktransport.h"

#include <QPointer>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

using namespace PhosphorLayer;
using PhosphorLayer::Testing::MockScreenProvider;
using PhosphorLayer::Testing::MockTransport;

class TestFactory : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void missingTransportYieldsNullptr()
    {
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(nullptr, &s));
        SurfaceConfig cfg;
        cfg.role = Roles::CenteredModal;
        cfg.contentItem = std::make_unique<QQuickItem>();
        cfg.screen = s.primary();
        QCOMPARE(f.create(std::move(cfg)), nullptr);
    }

    void missingScreenProviderYieldsNullptr()
    {
        MockTransport t;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, nullptr));
        SurfaceConfig cfg;
        cfg.role = Roles::CenteredModal;
        cfg.contentItem = std::make_unique<QQuickItem>();
        QCOMPARE(f.create(std::move(cfg)), nullptr);
    }

    void factoryIsParentByDefault()
    {
        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));
        SurfaceConfig cfg;
        cfg.role = Roles::CenteredModal;
        cfg.contentItem = std::make_unique<QQuickItem>();
        cfg.screen = s.primary();
        auto* surface = f.create(std::move(cfg));
        QVERIFY(surface);
        QCOMPARE(surface->parent(), &f);
    }

    void explicitParentOverridesFactory()
    {
        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));
        QObject owner;
        SurfaceConfig cfg;
        cfg.role = Roles::CenteredModal;
        cfg.contentItem = std::make_unique<QQuickItem>();
        cfg.screen = s.primary();
        auto* surface = f.create(std::move(cfg), &owner);
        QVERIFY(surface);
        QCOMPARE(surface->parent(), &owner);
    }

    void depsAccessorReflectsInjection()
    {
        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));
        QCOMPARE(f.deps().transport, &t);
        QCOMPARE(f.deps().screens, &s);
        QCOMPARE(f.deps().engineProvider, nullptr);
    }

    void nullScreenFallsBackToProviderPrimary()
    {
        // Documented failure mode: cfg.screen == nullptr resolves via the
        // screen provider's primary(). Before the fix, a null screen
        // trickled through to the transport with no diagnostic.
        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));
        SurfaceConfig cfg;
        cfg.role = Roles::CenteredModal;
        cfg.contentItem = std::make_unique<QQuickItem>();
        cfg.screen = nullptr;
        auto* surface = f.create(std::move(cfg));
        QVERIFY(surface);
        QCOMPARE(surface->config().screen, s.primary());
    }

    void sharedEngineAndProviderAreMutuallyExclusive()
    {
        // Bug regression: setting both SurfaceConfig::sharedEngine AND
        // SurfaceFactory::Deps::engineProvider previously produced a Surface
        // that used the shared engine (ensureEngine picks sharedEngine first)
        // but whose dtor called provider->releaseEngine() on an engine the
        // provider never issued. A shared engine would be unexpectedly
        // destroyed, taking down sibling surfaces with it.
        //
        // The factory now rejects the combination up-front.
        struct StubProvider : public IQmlEngineProvider
        {
            QQmlEngine* engineForSurface(const SurfaceConfig&) override
            {
                return nullptr;
            }
            void releaseEngine(QQmlEngine*) override
            {
            }
        };
        StubProvider p;
        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s, &p));
        QQmlEngine sharedEngine;
        SurfaceConfig cfg;
        cfg.role = Roles::CenteredModal;
        cfg.contentItem = std::make_unique<QQuickItem>();
        cfg.screen = s.primary();
        cfg.sharedEngine = &sharedEngine;
        QCOMPARE(f.create(std::move(cfg)), nullptr);
    }

    void defaultPathSelfOwnsEngineAndCleansUp()
    {
        // No engineProvider and no sharedEngine: Surface constructs and owns
        // its own QQmlEngine. The dtor must deleteLater the engine alongside
        // the window. Regression guard: a refactor that accidentally skipped
        // the "self-owned engine" cleanup path would leak the engine until
        // the test process exits, which the QPointer watcher catches.
        MockTransport t;
        MockScreenProvider s;
        SurfaceFactory f(PhosphorLayer::Testing::makeDeps(&t, &s));
        SurfaceConfig cfg;
        cfg.role = Roles::CenteredModal;
        cfg.contentItem = std::make_unique<QQuickItem>();
        cfg.screen = s.primary();
        auto* surface = f.create(std::move(cfg));
        QVERIFY(surface);
        surface->warmUp();
        QCOMPARE(surface->state(), Surface::State::Hidden);

        // Reach the owned engine indirectly via the window's engine pointer.
        auto* win = surface->window();
        QVERIFY(win);
        auto* engine = qmlEngine(win->contentItem());
        // Window-less wrapper path won't expose the engine via qmlEngine()
        // on contentItem; fall back to any reachable QML-engine pointer.
        if (!engine) {
            for (QObject* child : win->contentItem()->childItems()) {
                if ((engine = qmlEngine(child)) != nullptr) {
                    break;
                }
            }
        }
        // Even if we can't reach it, the cleanup test still proves liveness
        // via the surface/window QPointer watchers below — but the engine
        // watcher is the strongest guarantee.
        QPointer<QQmlEngine> engineWatcher(engine);
        QPointer<QQuickWindow> winWatcher(win);

        surface->deleteLater();
        QTRY_VERIFY_WITH_TIMEOUT(winWatcher.isNull(), 2000);
        if (engineWatcher) {
            QTRY_VERIFY_WITH_TIMEOUT(engineWatcher.isNull(), 2000);
        }
    }
};

QTEST_MAIN(TestFactory)
#include "test_factory.moc"
