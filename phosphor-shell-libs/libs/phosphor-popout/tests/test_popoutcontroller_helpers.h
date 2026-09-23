// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

// Shared fixtures for the PopoutController test split. The controller's
// tests fall into three groups (arbitration, dismissed-callback +
// destructor, signal-ordering); each group lives in its own test_*.cpp.
// FakeTransport, ReentrantFakeTransport, and makeRequest are the common
// pieces that every group consumes.

#include <PhosphorPopout/IPopoutTransport.h>
#include <PhosphorPopout/PopoutRequest.h>

#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>

// Minimal in-memory transport. Records every openSurface call, returns
// monotonically incrementing handles, and exposes a `dismiss()` helper
// that fires the controller's dismissed callback the way a real
// layer-shell would when a surface goes away on its own. No QML, no
// Wayland, no Qt event loop required.
class FakeTransport : public PhosphorPopout::IPopoutTransport
{
public:
    QString openSurface(const PhosphorPopout::PopoutRequest& request) override
    {
        if (refuseNextOpen) {
            refuseNextOpen = false;
            return {};
        }
        const QString handle = QStringLiteral("h%1").arg(++counter);
        alive.insert(handle, request.popoutId);
        openLog.append(request.popoutId);
        return handle;
    }

    void closeSurface(const QString& handle) override
    {
        // Counter is bumped on every entry, even for unknown
        // handles, so the test suite can pin "no transport call at
        // all" contracts. closeLog stays gated on alive so it only
        // captures handles the transport actually owned.
        ++closeSurfaceCalls;
        if (!alive.contains(handle)) {
            return;
        }
        closeLog.append(alive.value(handle));
        alive.remove(handle);
    }

    void setSurfaceDismissedCallback(std::function<void(const QString&)> cb) override
    {
        // Flip whenever an empty std::function is handed in so the
        // destructor-detaches-callback test can assert the controller
        // actually drove the detach (rather than the lambda being
        // discarded as a side effect of QPointer cleanup).
        if (!cb) {
            ++clearCallbackCalls;
        }
        dismissedCb = std::move(cb);
    }

    // Test helper. Simulates the surface dismissing itself. Mirrors
    // how a real layer-shell signals focus loss, a click outside the
    // surface, or compositor revocation. Routes through the callback
    // the controller registered. The callback removes the handle from
    // the controller's tables.
    void dismiss(const QString& handle)
    {
        if (!alive.contains(handle)) {
            return;
        }
        alive.remove(handle);
        if (dismissedCb) {
            dismissedCb(handle);
        }
    }

    QHash<QString, QString> alive;
    QStringList openLog;
    QStringList closeLog;
    int closeSurfaceCalls = 0;
    int counter = 0;
    int clearCallbackCalls = 0;
    bool refuseNextOpen = false;
    std::function<void(const QString&)> dismissedCb;
};

// Re-entrant fake. Synchronously fires the dismissed callback from
// inside closeSurface, violating the IPopoutTransport contract on
// purpose so the controller's `inSelfTeardown` guard is exercised.
class ReentrantFakeTransport : public PhosphorPopout::IPopoutTransport
{
public:
    QString openSurface(const PhosphorPopout::PopoutRequest& request) override
    {
        const QString handle = QStringLiteral("rh%1").arg(++counter);
        alive.insert(handle, request.popoutId);
        return handle;
    }
    void closeSurface(const QString& handle) override
    {
        ++closeSurfaceCalls;
        if (!alive.contains(handle)) {
            return;
        }
        alive.remove(handle);
        // Contract violation, on purpose. Real transports must not
        // do this. The guard exists so a misbehaving transport
        // cannot corrupt the controller's tables.
        if (dismissedCb) {
            // Optional cross-handle echo: when set, fire dismissed
            // for a DIFFERENT live handle than the one being closed.
            // This is the scenario the ScopedTrue guard actually
            // protects against. Same-handle echo is benign because
            // removeEntry erases the entry before calling closeSurface.
            // Cross-handle echo would let the re-entrant callback
            // remove a sibling out from under an in-flight closeAll
            // iteration if the guard were absent.
            const QString echoHandle = crossHandleEcho.value(handle, handle);
            dismissedCb(echoHandle);
        }
    }
    void setSurfaceDismissedCallback(std::function<void(const QString&)> cb) override
    {
        dismissedCb = std::move(cb);
    }
    QHash<QString, QString> alive;
    QHash<QString, QString> crossHandleEcho; // closed-handle -> echo-handle
    int closeSurfaceCalls = 0;
    int counter = 0;
    std::function<void(const QString&)> dismissedCb;
};

namespace PhosphorPopoutTest {

inline PhosphorPopout::PopoutRequest
makeRequest(const QString& id, PhosphorPopout::ExclusiveMode mode = PhosphorPopout::ExclusiveMode::Cooperative,
            const QString& scope = QStringLiteral("default"))
{
    PhosphorPopout::PopoutRequest req;
    req.popoutId = id;
    req.scope = scope;
    req.exclusive = mode;
    return req;
}

} // namespace PhosphorPopoutTest
