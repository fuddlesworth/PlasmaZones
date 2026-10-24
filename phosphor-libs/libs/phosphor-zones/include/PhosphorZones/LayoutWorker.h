// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorZones/LayoutComputeTypes.h>
#include <phosphorzones_export.h>
#include <QObject>

namespace PhosphorZones {

class PHOSPHORZONES_EXPORT LayoutWorker : public QObject
{
    Q_OBJECT

public:
    explicit LayoutWorker(QObject* parent = nullptr);

    /// The computation computeGeometries runs, as a pure function of the
    /// snapshot, so the GUI thread can run the same one in place when a
    /// result arrives computed from inputs the layout no longer holds.
    static LayoutComputeResult compute(const LayoutSnapshot& snapshot, uint64_t generation);

public Q_SLOTS:
    void computeGeometries(const PhosphorZones::LayoutSnapshot& snapshot, uint64_t generation);

Q_SIGNALS:
    void geometriesReady(const PhosphorZones::LayoutComputeResult& result);
};

} // namespace PhosphorZones
