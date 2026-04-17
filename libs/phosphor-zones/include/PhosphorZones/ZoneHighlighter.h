// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QObject>
#include <QVector>

namespace PlasmaZones {

class Zone;

/**
 * @brief Manages zone highlighting state
 *
 * Separates UI state from detection logic. ZoneDetector runs the detection
 * algorithms; this class manages the visual highlighting.
 */
class PLASMAZONES_EXPORT ZoneHighlighter : public QObject
{
    Q_OBJECT

public:
    explicit ZoneHighlighter(QObject* parent = nullptr);
    ~ZoneHighlighter() override = default;

    /**
     * @brief Highlight a single zone
     * @param zone Zone to highlight
     */
    Q_INVOKABLE void highlightZone(Zone* zone);

    /**
     * @brief Highlight multiple zones
     * @param zones Zones to highlight
     */
    Q_INVOKABLE void highlightZones(const QVector<Zone*>& zones);

    /**
     * @brief Clear all highlights
     */
    Q_INVOKABLE void clearHighlights();

Q_SIGNALS:
    void zoneHighlighted(Zone* zone);
    void zonesHighlighted(const QVector<Zone*>& zones);
    void highlightsCleared();

private:
    QVector<Zone*> m_highlightedZones;
};

} // namespace PlasmaZones
