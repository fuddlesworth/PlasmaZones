// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <QObject>
#include <QString>
#include <QStringList>

namespace PlasmaZones {

/// In-process notices of a window's close and of a prune sweep, for the
/// sibling adaptors that keep their own per-window state. A plain QObject,
/// not an adaptor signal, so neither notice reaches the session bus.
class PLASMAZONES_EXPORT WindowLifecycleRelay : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

Q_SIGNALS:
    /// Emitted once by WindowTrackingAdaptor::windowClosed after its teardown.
    void windowClosed(const QString& windowId);
    /// The instance ids alive at a WindowTrackingAdaptor::pruneStaleWindows sweep.
    void stalePruned(const QStringList& aliveInstances);
};

} // namespace PlasmaZones
