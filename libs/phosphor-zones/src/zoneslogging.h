// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Zones-local logging categories. Distinct names from the daemon-side
// lcZone / lcLayout categories so we don't collide at link time —
// daemon and library each own their own log filtering knob
// (org.plasmazones.zones / org.phosphor.zones respectively).

#include <QLoggingCategory>

namespace PhosphorZones {

Q_DECLARE_LOGGING_CATEGORY(lcZonesLib)
Q_DECLARE_LOGGING_CATEGORY(lcLayoutLib)

} // namespace PhosphorZones
