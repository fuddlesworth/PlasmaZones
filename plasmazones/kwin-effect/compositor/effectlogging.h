// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QLoggingCategory>

namespace PlasmaZones {

/// The effect's logging categories, declared once for every TU in the
/// effect. Defined in plasmazoneseffect/plasmazoneseffect.cpp.
Q_DECLARE_LOGGING_CATEGORY(lcEffect)
Q_DECLARE_LOGGING_CATEGORY(lcEffectDiag)
Q_DECLARE_LOGGING_CATEGORY(lcStripDiag)

} // namespace PlasmaZones
