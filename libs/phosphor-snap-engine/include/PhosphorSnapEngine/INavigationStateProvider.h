// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorsnapengine_export.h>

#include <QRect>
#include <QString>

namespace PhosphorSnapEngine {

/**
 * @brief Narrow read-only interface for compositor-layer state queries.
 *
 * SnapEngine's navigation entry points need three string shadows
 * (last-active window, last-active screen, last-cursor screen) and
 * one frame-geometry accessor that live on the daemon's
 * WindowTrackingAdaptor. Rather than holding an opaque QObject* and
 * calling methods by string via QMetaObject::invokeMethod, the engine
 * now depends on this typed interface.
 *
 * The daemon's WindowTrackingAdaptor must implement (or wrap)
 * INavigationStateProvider so that setNavigationStateProvider() can
 * accept a typed pointer. The interface is intentionally minimal —
 * if a future navigation method needs additional adaptor state, add
 * a virtual here rather than reaching back into a QObject*.
 *
 * Not a QObject — pure data queries with no lifecycle signals.
 */
class PHOSPHORSNAPENGINE_EXPORT INavigationStateProvider
{
public:
    INavigationStateProvider() = default;
    virtual ~INavigationStateProvider();

    /// Screen that most recently contained the mouse cursor.
    virtual QString lastCursorScreenName() const = 0;

    /// Screen that most recently held the active (focused) window.
    virtual QString lastActiveScreenName() const = 0;

    /// Window ID that most recently received keyboard focus.
    virtual QString lastActiveWindowId() const = 0;

    /// Live frame geometry for @p windowId as reported by the compositor.
    /// Returns an invalid QRect when the window is unknown.
    virtual QRect frameGeometry(const QString& windowId) const = 0;

protected:
    INavigationStateProvider(const INavigationStateProvider&) = default;
    INavigationStateProvider& operator=(const INavigationStateProvider&) = default;
};

} // namespace PhosphorSnapEngine
