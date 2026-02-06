// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include "constants.h"
#include <QObject>
#include <QRect>
#include <QUuid>
#include <QString>
#include <QColor>
#include <QJsonObject>

namespace PlasmaZones {

/**
 * @brief Represents a single zone within a layout
 *
 * A zone is a rectangular area on the screen where windows can be snapped.
 * Zones support custom colors, names, and keyboard shortcuts for ricer-friendly
 * customization.
 *
 * Note: Zone inherits from QObject and follows Qt's object model.
 * QObjects should NOT be copied - use clone() to create duplicates.
 */
class PLASMAZONES_EXPORT Zone : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QUuid id READ id CONSTANT)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QRectF geometry READ geometry WRITE setGeometry NOTIFY geometryChanged)
    Q_PROPERTY(QRectF relativeGeometry READ relativeGeometry WRITE setRelativeGeometry NOTIFY relativeGeometryChanged)
    Q_PROPERTY(int zoneNumber READ zoneNumber WRITE setZoneNumber NOTIFY zoneNumberChanged)
    Q_PROPERTY(QColor highlightColor READ highlightColor WRITE setHighlightColor NOTIFY highlightColorChanged)
    Q_PROPERTY(QColor inactiveColor READ inactiveColor WRITE setInactiveColor NOTIFY inactiveColorChanged)
    Q_PROPERTY(QColor borderColor READ borderColor WRITE setBorderColor NOTIFY borderColorChanged)
    Q_PROPERTY(qreal activeOpacity READ activeOpacity WRITE setActiveOpacity NOTIFY activeOpacityChanged)
    Q_PROPERTY(qreal inactiveOpacity READ inactiveOpacity WRITE setInactiveOpacity NOTIFY inactiveOpacityChanged)
    Q_PROPERTY(int borderWidth READ borderWidth WRITE setBorderWidth NOTIFY borderWidthChanged)
    Q_PROPERTY(int borderRadius READ borderRadius WRITE setBorderRadius NOTIFY borderRadiusChanged)
    Q_PROPERTY(bool isHighlighted READ isHighlighted WRITE setHighlighted NOTIFY highlightedChanged)
    Q_PROPERTY(bool useCustomColors READ useCustomColors WRITE setUseCustomColors NOTIFY useCustomColorsChanged)

public:
    explicit Zone(QObject* parent = nullptr);
    explicit Zone(const QRectF& geometry, QObject* parent = nullptr);
    ~Zone() override = default;

    // QObjects should not be copied - use clone() instead
    Zone(const Zone&) = delete;
    Zone& operator=(const Zone&) = delete;

    /**
     * @brief Creates a copy of this zone with a new unique ID
     * @param parent Parent object for the new zone
     * @return New Zone instance with copied properties but new ID
     */
    Q_INVOKABLE Zone* clone(QObject* parent = nullptr) const;

    /**
     * @brief Copies properties from another zone (excluding ID)
     * @param other Source zone to copy from
     */
    void copyPropertiesFrom(const Zone& other);

    bool operator==(const Zone& other) const;

    // Identification
    QUuid id() const
    {
        return m_id;
    }
    QString name() const
    {
        return m_name;
    }
    void setName(const QString& name);

    // Geometry (absolute pixel coordinates)
    QRectF geometry() const
    {
        return m_geometry;
    }
    void setGeometry(const QRectF& geometry);

    // Relative geometry (0.0-1.0 normalized coordinates for resolution independence)
    QRectF relativeGeometry() const
    {
        return m_relativeGeometry;
    }
    void setRelativeGeometry(const QRectF& relativeGeometry);

    // Zone numbering for keyboard navigation
    int zoneNumber() const
    {
        return m_zoneNumber;
    }
    void setZoneNumber(int number);

    // Ricer-friendly appearance customization
    QColor highlightColor() const
    {
        return m_highlightColor;
    }
    void setHighlightColor(const QColor& color);

    QColor inactiveColor() const
    {
        return m_inactiveColor;
    }
    void setInactiveColor(const QColor& color);

    QColor borderColor() const
    {
        return m_borderColor;
    }
    void setBorderColor(const QColor& color);

    qreal activeOpacity() const
    {
        return m_activeOpacity;
    }
    void setActiveOpacity(qreal opacity);

    qreal inactiveOpacity() const
    {
        return m_inactiveOpacity;
    }
    void setInactiveOpacity(qreal opacity);

    int borderWidth() const
    {
        return m_borderWidth;
    }
    void setBorderWidth(int width);

    int borderRadius() const
    {
        return m_borderRadius;
    }
    void setBorderRadius(int radius);

    bool isHighlighted() const
    {
        return m_isHighlighted;
    }
    void setHighlighted(bool highlighted);

    bool useCustomColors() const
    {
        return m_useCustomColors;
    }
    void setUseCustomColors(bool useCustom);

    // Geometry calculations
    Q_INVOKABLE bool containsPoint(const QPointF& point) const;
    Q_INVOKABLE qreal distanceToPoint(const QPointF& point) const;
    Q_INVOKABLE QRectF calculateAbsoluteGeometry(const QRectF& screenGeometry) const;
    Q_INVOKABLE QRectF applyPadding(int padding) const;

    // Serialization
    QJsonObject toJson() const;
    static Zone* fromJson(const QJsonObject& json, QObject* parent = nullptr);

Q_SIGNALS:
    void nameChanged();
    void geometryChanged();
    void relativeGeometryChanged();
    void zoneNumberChanged();
    void highlightColorChanged();
    void inactiveColorChanged();
    void borderColorChanged();
    void activeOpacityChanged();
    void inactiveOpacityChanged();
    void borderWidthChanged();
    void borderRadiusChanged();
    void highlightedChanged();
    void useCustomColorsChanged();

private:
    QUuid m_id;
    QString m_name;
    QRectF m_geometry;
    QRectF m_relativeGeometry;
    int m_zoneNumber = 0;

    // Appearance (ricer-friendly) - using constants for defaults
    QColor m_highlightColor = Defaults::HighlightColor;
    QColor m_inactiveColor = Defaults::InactiveColor;
    QColor m_borderColor = Defaults::BorderColor;
    qreal m_activeOpacity = Defaults::Opacity;
    qreal m_inactiveOpacity = Defaults::InactiveOpacity;
    int m_borderWidth = Defaults::BorderWidth;
    int m_borderRadius = Defaults::BorderRadius;
    bool m_isHighlighted = false;
    bool m_useCustomColors = false;
};

} // namespace PlasmaZones
