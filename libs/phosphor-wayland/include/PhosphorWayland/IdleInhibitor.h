// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorwayland_export.h>

#include <QObject>
#include <QWindow>

#include <memory>

namespace PhosphorWayland {

/// Prevents the compositor from idling the output that the associated
/// surface is visible on. The inhibitor is active for as long as this
/// object lives and the surface remains visible. Destroying the object
/// (or the surface) lifts the inhibition.
///
/// Usage from QML (inside a window):
///
///     IdleInhibitor { surface: Window.window }
///
/// Until `surface` is set the inhibitor stays inactive (`active` is
/// false); nothing happens and no inhibitor object is created.
class PHOSPHORWAYLAND_EXPORT IdleInhibitor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QWindow* surface READ surface WRITE setSurface NOTIFY surfaceChanged)
    Q_PROPERTY(bool active READ isActive NOTIFY activeChanged)

public:
    explicit IdleInhibitor(QObject* parent = nullptr);
    ~IdleInhibitor() override;

    [[nodiscard]] QWindow* surface() const;
    void setSurface(QWindow* window);

    [[nodiscard]] bool isActive() const;

    static bool isSupported();

Q_SIGNALS:
    void surfaceChanged();
    void activeChanged();

private:
    void createInhibitor();
    void destroyInhibitor();

    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorWayland
