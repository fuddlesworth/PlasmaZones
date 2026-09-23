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
 * `QScreen` directly. The production implementation (`QtPhysicalScreenSource`)
 * is a thin wrapper over Qt; a test implementation (`FakePhysicalScreenSource`)
 * synthesizes outputs with arbitrary geometry and fires the lifecycle
 * signals on demand — which is what makes the geometry-recompute path
 * regression-testable (QScreen itself cannot be constructed by test code).
 *
 * Mirrors the existing IPanelSource / IConfigStore seam pattern: the
 * concrete provider is injected via ScreenManagerConfig and owned by the
 * consumer; it must outlive the ScreenManager that holds the pointer.
 *
 * NOT to be confused with `PhosphorLayer::IScreenProvider`, which this was
 * called until the two names collided in review. They are different seams
 * at different layers and neither library links the other. This one answers
 * "which physical outputs exist, and what just happened to them", in
 * @ref PhysicalScreen value snapshots, and deliberately does NOT hand out
 * `QScreen*` — that is the whole point, since QScreen cannot be constructed
 * by test code. The layer one answers "which `QScreen*` should this surface
 * attach to" and must traffic in Qt's type because layer-shell surfaces bind
 * to real Qt screens. Merging them would defeat one or the other.
 *
 * Threading: ScreenManager calls `screens()` / `primaryScreen()` from the
 * GUI thread; implementations must emit the signals on the GUI thread.
 */
class PHOSPHORSCREENSCORE_EXPORT IPhysicalScreenSource : public QObject
{
    Q_OBJECT
public:
    explicit IPhysicalScreenSource(QObject* parent = nullptr)
        : QObject(parent)
    {
        // PhysicalScreen rides this interface's signals — register it so a
        // queued connection or QSignalSpy can marshal it. qRegisterMetaType
        // is idempotent, so paying it per provider construction is harmless.
        qRegisterMetaType<PhysicalScreen>();
    }
    ~IPhysicalScreenSource() override = default;

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
