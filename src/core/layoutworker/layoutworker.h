// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "types.h"
#include <QObject>

namespace PlasmaZones {

/**
 * @brief Pure-compute worker for layout zone geometry.
 *
 * Lives on a dedicated QThread (moved via moveToThread). Receives
 * immutable LayoutSnapshot inputs, produces LayoutComputeResult
 * outputs. Has no access to any QObject on the main thread.
 */
class LayoutWorker : public QObject
{
    Q_OBJECT

public:
    explicit LayoutWorker(QObject* parent = nullptr);

public Q_SLOTS:
    void computeGeometries(const PlasmaZones::LayoutSnapshot& snapshot, uint64_t generation);

Q_SIGNALS:
    void geometriesReady(const PlasmaZones::LayoutComputeResult& result);
};

} // namespace PlasmaZones
