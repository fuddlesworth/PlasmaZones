// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>

namespace PlasmaZones {

class PlasmaZonesEffect;

/**
 * @brief Handles snapping integration for PlasmaZones.
 *
 * The snap-mode counterpart to AutotileHandler. Owns the snap-side effect
 * state (border/title-bar tracking, focus-follows-mouse, restore cache, and
 * drag bookkeeping) and delegates window lookups, geometry application, and
 * border rendering back to the effect through the m_effect back-pointer.
 *
 * Scaffold milestone: this is the empty shell. The snap state and behavior
 * currently living on PlasmaZonesEffect are migrated in here in subsequent
 * milestones; for now the class only establishes the construction, ownership,
 * and friend wiring that mirror AutotileHandler.
 */
class SnapHandler : public QObject
{
    Q_OBJECT

public:
    explicit SnapHandler(PlasmaZonesEffect* effect, QObject* parent = nullptr);

private:
    PlasmaZonesEffect* m_effect;
};

} // namespace PlasmaZones
