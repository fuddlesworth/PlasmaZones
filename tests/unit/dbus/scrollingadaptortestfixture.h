// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The ScrollingAdaptor test fixture, shared by test_scrolling_adaptor (the
// read-side surface: strip snapshots, screen set, relays, teardown) and
// test_scrolling_adaptor_verbs (the wire verbs: the wheel pair and the
// absolute setters). One engine with headless geometry providers, one
// adaptor on a throwaway parent, one owned screen. Plain mixin rather than a
// QObject base: each suite keeps its own Q_OBJECT and forwards init() and
// cleanup() here, so automoc sees exactly one test class per TU.

#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorScrollEngine/ScrollEngine.h>

#include "dbus/scrollingadaptor/scrollingadaptor.h"

#include <QObject>
#include <QRect>
#include <QString>

namespace PlasmaZones {

class ScrollingAdaptorTestFixture
{
protected:
    void setUpFixture()
    {
        // The two stubbed seams: no IWindowTrackingService (so window ids
        // stay raw, uncanonicalized) and no ScreenManager (so geometry
        // comes from the injected providers below instead of real outputs).
        m_engine = new PhosphorScrollEngine::ScrollEngine(nullptr, nullptr);
        // Headless geometry seam: without a work area the strip resolves no
        // rects at all, and visibleStripJson would return "[]" for every
        // screen — including the one it is supposed to describe.
        //
        // Neither provider is at the origin: a (0,0) work area would let a
        // dropped origin-subtraction term pass unnoticed (x/1920 lands well
        // outside 0..1). The two rects also differ, but which basis the
        // payload normalizes against is NOT pinned here: every rect assertion
        // in test_scrolling_adaptor.cpp cross-checks the wire payload against
        // the engine's own relative rects, so a swap to the available geometry
        // would normalize against 760 instead of 800, stay inside 0..1, and
        // agree with the engine either way. Known coverage gap, kept because
        // an absolute expected value would have to be derived from a live run.
        const auto available = [](const QString&) {
            return QRect(1920, 40, 1200, 760);
        };
        const auto screen = [](const QString&) {
            return QRect(1920, 0, 1200, 800);
        };
        m_engine->setScreenGeometryProviders(available, screen);
        // Well-behaved-compositor echo, same as ScrollTestUtils'
        // makeProviderEngine: every activation request is answered with a
        // windowFocused report so the engine's pending-self-activation queue
        // drains — without it the next simulated USER focus of that window is
        // consumed as the missing echo.
        QObject::connect(m_engine, &PhosphorEngine::PlacementEngineBase::activateWindowRequested, m_engine,
                         [this](const QString& windowId) {
                             m_engine->windowFocused(windowId, m_engine->screenForTrackedWindow(windowId));
                         });
        m_parent = new QObject(nullptr);
        m_adaptor = new ScrollingAdaptor(m_engine, m_parent);
        // The adaptor fails CLOSED without a context gate, so the fixture
        // installs an open one the way the daemon installs its real one in
        // the same pass that creates the adaptor. The two gate tests swap in
        // a refusing gate and restore this.
        m_adaptor->setContextGateProvider([](const QString&) {
            return false;
        });
        m_engine->setActiveScreens({QStringLiteral("DP-1")});
    }

    void tearDownFixture()
    {
        // Note the shape: this teardown always clears the engine BEFORE the
        // adaptor dies, so the live-connection destruction order is the one
        // path these tests never exercise.
        m_adaptor->clearEngine();
        delete m_parent;
        m_parent = nullptr;
        m_adaptor = nullptr;
        delete m_engine;
        m_engine = nullptr;
    }

    PhosphorScrollEngine::ScrollEngine* m_engine = nullptr;
    QObject* m_parent = nullptr;
    ScrollingAdaptor* m_adaptor = nullptr;
};

} // namespace PlasmaZones
