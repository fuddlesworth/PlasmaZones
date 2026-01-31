// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QString>

namespace PlasmaZones {

class IZoneDetector;
class ILayoutManager;
class ISettings;

/**
 * @brief D-Bus adaptor for zone detection operations
 *
 * Provides D-Bus interface: org.plasmazones.ZoneDetection
 * Single responsibility: Zone detection queries
 *
 * Uses interface types for Dependency Inversion Principle (DIP)
 */
class PLASMAZONES_EXPORT ZoneDetectionAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.ZoneDetection")

public:
    explicit ZoneDetectionAdaptor(IZoneDetector* detector, ILayoutManager* layoutManager, ISettings* settings,
                                  QObject* parent = nullptr);
    ~ZoneDetectionAdaptor() override = default;

public Q_SLOTS:
    // Zone detection for cursor position
    QString detectZoneAtPosition(int x, int y);
    QStringList detectMultiZoneAtPosition(int x, int y);
    QString getZoneGeometry(const QString& zoneId);
    QString getZoneGeometryForScreen(const QString& zoneId, const QString& screenName);
    QStringList getZonesForScreen(const QString& screenName);

    // Zone navigation - get adjacent zone in a direction
    // direction: "left", "right", "up", "down"
    QString getAdjacentZone(const QString& currentZoneId, const QString& direction);

    /**
     * @brief Get the first (edge) zone in a given direction
     *
     * Used when a window is not yet snapped and user presses a navigation key.
     * Returns the zone at the edge of the layout in the specified direction:
     *   - left: leftmost zone (smallest x)
     *   - right: rightmost zone (largest x + width)
     *   - up: topmost zone (smallest y)
     *   - down: bottommost zone (largest y + height)
     *
     * @param direction Direction string ("left", "right", "up", "down")
     * @return Zone ID of the edge zone, or empty string if no zones available
     */
    QString getFirstZoneInDirection(const QString& direction);

    // Get zone info by zone number (1-indexed)
    QString getZoneByNumber(int zoneNumber);

    // Get all zone geometries for the active layout
    QStringList getAllZoneGeometries();

    /**
     * @brief Get current keyboard modifier state
     *
     * Returns Qt::KeyboardModifiers as an integer bitmask.
     * This queries the actual keyboard state, not cached values.
     *
     * Bitmask values:
     *   0x02000000 = Shift
     *   0x04000000 = Control
     *   0x08000000 = Alt
     *   0x10000000 = Meta
     *
     * @return Modifier bitmask (0 if no modifiers pressed)
     */
    int getKeyboardModifiers();

    /**
     * @brief Detect zone at position and return modifier state
     *
     * Combined call that returns both zone ID and current keyboard modifiers.
     * More efficient than two separate D-Bus calls.
     *
     * @param x Screen X coordinate
     * @param y Screen Y coordinate
     * @return String in format "zoneId;modifiers" (e.g., "uuid-here;33554432" for Shift)
     *         Empty string if no zone found, modifiers still appended after semicolon
     */
    QString detectZoneWithModifiers(int x, int y);

Q_SIGNALS:
    void zoneDetected(const QString& zoneId, const QString& geometry);

private:
    IZoneDetector* m_zoneDetector; // Interface type (DIP)
    ILayoutManager* m_layoutManager; // Interface type (DIP)
    ISettings* m_settings; // For zonePadding setting
};

} // namespace PlasmaZones
