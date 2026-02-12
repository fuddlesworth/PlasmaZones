// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zonehighlighter.h"
#include "zone.h"

namespace PlasmaZones {

ZoneHighlighter::ZoneHighlighter(QObject* parent)
    : QObject(parent)
{
}

void ZoneHighlighter::highlightZone(Zone* zone)
{
    clearHighlights();

    if (zone) {
        zone->setHighlighted(true);
        m_highlightedZones.append(zone);
        Q_EMIT zoneHighlighted(zone);
    }
}

void ZoneHighlighter::highlightZones(const QVector<Zone*>& zones)
{
    clearHighlights();

    for (auto* zone : zones) {
        if (zone) {
            zone->setHighlighted(true);
            m_highlightedZones.append(zone);
        }
    }

    if (!zones.isEmpty()) {
        Q_EMIT zonesHighlighted(zones);
    }
}

void ZoneHighlighter::clearHighlights()
{
    for (auto* zone : m_highlightedZones) {
        if (zone) {
            zone->setHighlighted(false);
        }
    }
    m_highlightedZones.clear();
    Q_EMIT highlightsCleared();
}

} // namespace PlasmaZones
