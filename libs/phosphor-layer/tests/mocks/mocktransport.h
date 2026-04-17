// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorLayer/ILayerShellTransport.h>
#include <PhosphorLayer/SurfaceFactory.h>

#include <QMap>
#include <QQuickWindow>
#include <QSize>

#include <memory>

namespace PhosphorLayer::Testing {

/// Helper: build a SurfaceFactory::Deps without triggering
/// -Wmissing-field-initializers. GCC warns on brace/designated inits
/// that skip fields with default member initializers — centralising the
/// construction here keeps each call site ergonomic and warning-free.
inline PhosphorLayer::SurfaceFactory::Deps makeDeps(PhosphorLayer::ILayerShellTransport* transport,
                                                    PhosphorLayer::IScreenProvider* screens,
                                                    PhosphorLayer::IQmlEngineProvider* engineProvider = nullptr)
{
    PhosphorLayer::SurfaceFactory::Deps d;
    d.transport = transport;
    d.screens = screens;
    d.engineProvider = engineProvider;
    return d;
}

class MockTransportHandle : public ITransportHandle
{
public:
    explicit MockTransportHandle(QQuickWindow* win)
        : m_window(win)
    {
    }

    QQuickWindow* window() const override
    {
        return m_window;
    }
    bool isConfigured() const override
    {
        return m_configured;
    }
    QSize configuredSize() const override
    {
        return m_size;
    }

    void setMargins(QMargins m) override
    {
        m_margins = m;
    }
    void setLayer(Layer l) override
    {
        m_layer = l;
    }
    void setExclusiveZone(int z) override
    {
        m_exclusiveZone = z;
    }
    void setKeyboardInteractivity(KeyboardInteractivity k) override
    {
        m_keyboard = k;
    }
    void setAnchors(Anchors a) override
    {
        m_anchors = a;
    }

    // Test drivers
    void simulateConfigure(QSize s)
    {
        m_configured = true;
        m_size = s;
    }

    QQuickWindow* const m_window;
    bool m_configured = false;
    QSize m_size;
    QMargins m_margins;
    Layer m_layer = Layer::Overlay;
    int m_exclusiveZone = -1;
    KeyboardInteractivity m_keyboard = KeyboardInteractivity::None;
    Anchors m_anchors = AnchorNone;
};

/// In-memory transport that records every attach() call and hands out
/// MockTransportHandles the tests can drive.
class MockTransport : public ILayerShellTransport
{
public:
    bool isSupported() const override
    {
        return m_supported;
    }

    std::unique_ptr<ITransportHandle> attach(QQuickWindow* win, const TransportAttachArgs& args) override
    {
        ++m_attachCount;
        m_lastArgs = args;
        m_lastWindow = win;
        AttachRecord rec;
        rec.args = args;
        rec.window = win;
        rec.sizeAtAttach = win ? win->size() : QSize();
        m_attachRecords.append(rec);
        if (m_rejectNextAttach) {
            m_rejectNextAttach = false;
            return nullptr;
        }
        auto handle = std::make_unique<MockTransportHandle>(win);
        m_lastHandle = handle.get();
        return handle;
    }

    [[nodiscard]] CompositorLostCookie addCompositorLostCallback(CompositorLostCallback cb) override
    {
        if (!cb) {
            return 0;
        }
        const CompositorLostCookie cookie = ++m_nextCookie;
        m_lostCallbacks.insert(cookie, std::move(cb));
        return cookie;
    }

    void removeCompositorLostCallback(CompositorLostCookie cookie) override
    {
        m_lostCallbacks.remove(cookie);
    }

    // Test drivers ─────────────────────────────────────────────────────

    void setSupported(bool v)
    {
        m_supported = v;
    }
    void rejectNextAttach()
    {
        m_rejectNextAttach = true;
    }
    void simulateCompositorLost()
    {
        // Copy before iterating so callbacks that unsubscribe themselves
        // mid-fire don't invalidate our iterator.
        const auto snapshot = m_lostCallbacks.values();
        for (const auto& cb : snapshot) {
            if (cb) {
                cb();
            }
        }
    }

    /// Record every attach() call for tests that need ordering / per-call
    /// arg inspection. m_lastArgs / m_lastWindow mirror the last entry for
    /// the single-attach shorthand.
    struct AttachRecord
    {
        TransportAttachArgs args;
        QQuickWindow* window = nullptr;
        QSize sizeAtAttach; ///< win->size() captured at attach-time
    };

    bool m_supported = true;
    bool m_rejectNextAttach = false;
    int m_attachCount = 0;
    TransportAttachArgs m_lastArgs;
    QQuickWindow* m_lastWindow = nullptr;
    MockTransportHandle* m_lastHandle = nullptr;
    QList<AttachRecord> m_attachRecords;
    QMap<CompositorLostCookie, CompositorLostCallback> m_lostCallbacks;
    CompositorLostCookie m_nextCookie = 0;
};

} // namespace PhosphorLayer::Testing
