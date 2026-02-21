// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MonocleAlgorithm.h"
#include "../AlgorithmRegistry.h"
#include "../TilingState.h"
#include "core/constants.h"
#include <KLocalizedString>

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

QString MonocleAlgorithm::name() const
{
    return i18n("Monocle");
}

QString MonocleAlgorithm::description() const
{
    return i18n("Single fullscreen window, others hidden");
}

QString MonocleAlgorithm::icon() const noexcept
{
    return QStringLiteral("view-fullscreen");
}

QVector<QRect> MonocleAlgorithm::calculateZones(const TilingParams &params) const
{
    const int windowCount = params.windowCount;
    const auto &screenGeometry = params.screenGeometry;
    const int outerGap = params.outerGap;

    QVector<QRect> zones;

    if (windowCount <= 0 || !screenGeometry.isValid()) {
        return zones;
    }

    // In monocle mode, every window gets the gap-inset area.
    // No inner gaps since windows are stacked, not side-by-side.
    const QRect area = innerRect(screenGeometry, outerGap);
    zones.reserve(windowCount);
    for (int i = 0; i < windowCount; ++i) {
        zones.append(area);
    }

    return zones;
}

} // namespace PlasmaZones
