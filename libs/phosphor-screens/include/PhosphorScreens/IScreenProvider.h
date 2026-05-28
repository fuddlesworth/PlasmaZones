// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "PhosphorScreens/PhysicalScreen.h"
#include "phosphorscreenscore_export.h"

#include <QObject>
#include <QVector>

namespace PhosphorScreens {

/**
 * @brief Pluggable source of the connected-output set and its lifecycle.
 *
 * ScreenManager enumerates outputs and reacts to add / remove / move /
 * resize through this interface instead of touching `QGuiApplication` and
 * `QScreen` directly. The production implementation (`QtScreenProvider`)
 * is a thin wrapper over Qt; a test implementation (`FakeScreenProvider`)
 * synthesizes outputs with arbitrary geometry and fires the lifecycle
 * signals on demand — which is what makes the geometry-recompute path
 * regression-testable (QScreen itself cannot be constructed by test code).
 *
 * Mirrors the existing IPanelSource / IConfigStore seam pattern: the
 * concrete provider is injected via ScreenManagerConfig and owned by the
 * consumer; it must outlive the ScreenManager that holds the pointer.
 *
 * Threading: ScreenManager calls `screens()` / `primaryScreen()` from the
 * GUI thread; implementations must emit the signals on the GUI thread.
 */
class PHOSPHORSCREENSCORE_EXPORT IScreenProvider : public QObject
{
    Q_OBJECT
public:
    explicit IScreenProvider(QObject* parent = nullptr)
        : QObject(parent)
    {
        // PhysicalScreen rides this interface's signals — register it so a
        // queued connection or QSignalSpy can marshal it. qRegisterMetaType
        // is idempotent, so paying it per provider construction is harmless.
        qRegisterMetaType<PhysicalScreen>();
    }
    ~IScreenProvider() override = default;

    /// Every currently-connected output. Order is not significant.
    virtual QVector<PhysicalScreen> screens() const = 0;

    /// The primary output, or an invalid PhysicalScreen if there is none.
    virtual PhysicalScreen primaryScreen() const = 0;

Q_SIGNALS:
    /// A new output connected. The PhysicalScreen carries its geometry at
    /// emission time — which may still be a transient origin that settles
    /// via a subsequent @ref screenGeometryChanged.
    void screenAdded(const PhysicalScreen& screen);

    /// An output disconnected. Identified by connector name; its geometry
    /// is whatever was last known.
    void screenRemoved(const PhysicalScreen& screen);

    /// A connected output's geometry changed (move, resize, rotate,
    /// scale). The PhysicalScreen carries the NEW geometry — consumers
    /// replace any stored snapshot with this one.
    void screenGeometryChanged(const PhysicalScreen& screen);
};

} // namespace PhosphorScreens
