// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "zoneshadercommon.h"

#include <PhosphorRendering/ShaderEffect.h>

#include <plasmazones_rendering_export.h>
#include <QImage>
#include <QMutex>
#include <QVariantList>
#include <QVector>
#include <array>
#include <atomic>

QT_BEGIN_NAMESPACE
class QSGNode;
QT_END_NAMESPACE

namespace PlasmaZones {

class ZoneShaderNodeRhi;

/**
 * @brief QQuickItem for rendering zone overlays with custom shaders
 *
 * Inherits from PhosphorRendering::ShaderEffect which provides all base
 * shader rendering: Shadertoy uniforms, multipass, custom params/colors,
 * textures, status. This subclass adds zone-specific functionality:
 *
 * - Zone data array (QVariantList zones property for QML)
 * - Zone counting (zoneCount, highlightedCount)
 * - Hovered zone highlight (hoveredZoneIndex)
 * - Labels texture (pre-rendered zone numbers)
 * - ZoneShaderNodeRhi creation (delegates to PhosphorRendering::ShaderNodeRhi)
 *
 * Usage in QML:
 * @code
 * ZoneShaderItem {
 *     anchors.fill: parent
 *     zones: zoneDataProvider.zones
 *     shaderSource: "qrc:/shaders/neon.frag"
 *     customColor1: "#ff8800"
 * }
 * @endcode
 */
class PLASMAZONES_RENDERING_EXPORT ZoneShaderItem : public PhosphorRendering::ShaderEffect
{
    Q_OBJECT
    QML_ELEMENT

    // Zone data (zone-specific, not in parent)
    Q_PROPERTY(QVariantList zones READ zones WRITE setZones NOTIFY zonesChanged FINAL)
    Q_PROPERTY(int zoneCount READ zoneCount NOTIFY zoneCountChanged FINAL)
    Q_PROPERTY(int highlightedCount READ highlightedCount NOTIFY highlightedCountChanged FINAL)
    /** Zone index under cursor for hover highlight (preview mode). -1 = none. Avoids full zones churn on mouse move. */
    Q_PROPERTY(
        int hoveredZoneIndex READ hoveredZoneIndex WRITE setHoveredZoneIndex NOTIFY hoveredZoneIndexChanged FINAL)

    // Labels texture (pre-rendered zone numbers for shader pass)
    Q_PROPERTY(QImage labelsTexture READ labelsTexture WRITE setLabelsTexture NOTIFY labelsTextureChanged FINAL)

public:
    explicit ZoneShaderItem(QQuickItem* parent = nullptr);
    ~ZoneShaderItem() override;

    // Zone data getters/setters
    const QVariantList& zones() const
    {
        return m_zones;
    }
    void setZones(const QVariantList& zones);

    int zoneCount() const
    {
        return m_zoneCount;
    }
    int highlightedCount() const
    {
        return m_highlightedCount;
    }

    int hoveredZoneIndex() const
    {
        return m_hoveredZoneIndex;
    }
    void setHoveredZoneIndex(int index);

    // Labels texture getter/setter
    QImage labelsTexture() const;
    void setLabelsTexture(const QImage& image);

    /**
     * @brief Get a thread-safe copy of zone data for rendering
     *
     * This method is safe to call from the render thread. It acquires
     * a mutex briefly to copy the current zone state.
     *
     * @return Snapshot of current zone data
     */
    ZoneDataSnapshot getZoneDataSnapshot() const;

    /**
     * @brief Get parsed zone rectangles (thread-safe)
     * @return Vector of normalized zone rectangles
     */
    QVector<ZoneRect> zoneRects() const;

    /**
     * @brief Get parsed zone fill colors (thread-safe)
     * @return Vector of zone fill colors
     */
    QVector<ZoneColor> zoneFillColors() const;

    /**
     * @brief Get parsed zone border colors (thread-safe)
     * @return Vector of zone border colors
     */
    QVector<ZoneColor> zoneBorderColors() const;

    /**
     * @brief Force reload of shader from source (callable from QML).
     * Alias for reloadShader() for backward compatibility.
     */
    Q_INVOKABLE void loadShader()
    {
        reloadShader();
    }

    /**
     * @brief Override setShaderParams to extract customParams/customColor/uTexture values.
     *
     * PlasmaZones shader metadata uses keys like "customParams1_x", "customColor1",
     * "uTexture0" which must be parsed and applied to the corresponding properties.
     */
    void setShaderParams(const QVariantMap& params) override;

Q_SIGNALS:
    void zonesChanged();
    void zoneCountChanged();
    void highlightedCountChanged();
    void hoveredZoneIndexChanged();
    void labelsTextureChanged();

protected:
    /**
     * @brief Create or update the scene graph node for rendering
     *
     * Creates a ZoneShaderNodeRhi (subclass of PhosphorRendering::ShaderNodeRhi)
     * and syncs zone-specific data. The parent class handles all base shader
     * rendering via its own updatePaintNode, but we override to create the
     * zone-specific node type and sync zone data.
     *
     * @param oldNode Previous node (may be nullptr)
     * @param data Update paint node data from Qt
     * @return Updated or new scene graph node
     */
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;

    /**
     * @brief Handle geometry changes — re-parse zones with new resolution
     */
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

    /**
     * @brief Handle component completion
     */
    void componentComplete() override;

private:
    /**
     * @brief Parse zone data from QVariantList to internal structures
     */
    void parseZoneData();

    /**
     * @brief Update only highlight flags in m_zoneData (no full parse).
     */
    void updateHoveredHighlightOnly();

    // Zone data (main thread access)
    QVariantList m_zones;
    int m_zoneCount = 0;
    int m_highlightedCount = 0;
    int m_hoveredZoneIndex = -1;

    // User texture data (parsed from shaderParams, bindings 7-10)
    std::array<QString, 4> m_userTexturePaths;
    std::array<QImage, 4> m_userTextureImages;
    std::array<QString, 4> m_userTextureWraps;
    std::array<int, 4> m_userTextureSvgSizes = {1024, 1024, 1024, 1024};

    // Labels texture (main thread writes, render thread reads via updatePaintNode)
    QImage m_labelsTexture;
    mutable QMutex m_labelsTextureMutex;

    // Thread-safe zone data storage
    // Protected by m_zoneDataMutex for render thread access
    mutable QMutex m_zoneDataMutex;
    ZoneDataSnapshot m_zoneData;

    // Render node tracking for safe teardown
    ZoneShaderNodeRhi* m_zoneRenderNode = nullptr;

    // Dirty flags for render thread synchronization
    std::atomic<bool> m_zoneDataDirty{false};
    std::atomic<int> m_dataVersion{0};
};

} // namespace PlasmaZones
