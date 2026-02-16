// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MonocleAlgorithm.h"
#include "../AlgorithmRegistry.h"
#include "../TilingState.h"
#include "core/constants.h"

namespace PlasmaZones {

// Self-registration: Monocle provides focused single-window workflow (priority 40)
// Higher priority = registered later in the list, appearing after core layouts
namespace {
AlgorithmRegistrar<MonocleAlgorithm> s_monocleRegistrar(DBus::AutotileAlgorithm::Monocle, 40);
}

MonocleAlgorithm::MonocleAlgorithm(QObject *parent)
    : TilingAlgorithm(parent)
{
}

QString MonocleAlgorithm::name() const noexcept
{
    return QStringLiteral("Monocle");
}

QString MonocleAlgorithm::description() const
{
    return tr("Single fullscreen window, others hidden");
}

QString MonocleAlgorithm::icon() const noexcept
{
    return QStringLiteral("view-fullscreen");
}

QVector<QRect> MonocleAlgorithm::calculateZones(int windowCount, const QRect &screenGeometry,
                                                 const TilingState & /*state*/) const
{
    QVector<QRect> zones;

    if (windowCount <= 0 || !screenGeometry.isValid()) {
        return zones;
    }

    // In monocle mode, every window gets the full screen geometry.
    // The autotiling engine handles which window is visible based on
    // focus and the monocleHideOthers configuration setting.
    zones.reserve(windowCount);
    for (int i = 0; i < windowCount; ++i) {
        zones.append(screenGeometry);
    }

    return zones;
}

} // namespace PlasmaZones
