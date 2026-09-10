// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "overvieweffect.h"

namespace PlasmaZones::Overview {

KWIN_EFFECT_FACTORY_SUPPORTED(OverviewEffect, "metadata.json", return OverviewEffect::supported();)

} // namespace PlasmaZones::Overview

#include "main.moc"
