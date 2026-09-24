// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorZones/ZoneHighlighter.h>
#include <PhosphorZones/Zone.h>

namespace PhosphorZones {

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
    if (m_highlightedZones.isEmpty()) {
        return;
    }
    for (auto* zone : m_highlightedZones) {
        if (zone) {
            zone->setHighlighted(false);
        }
    }
    m_highlightedZones.clear();
    Q_EMIT highlightsCleared();
}

} // namespace PhosphorZones
