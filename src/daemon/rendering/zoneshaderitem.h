// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorRendering/ShaderEffect.h>
#include <PhosphorRendering/ZoneShaderCommon.h>
#include <PhosphorRendering/ZoneShaderNodeRhi.h>
#include <PhosphorRendering/ZoneUniformExtension.h>
#include <PhosphorShaders/IUniformExtension.h>

#include "zoneshadernoderhi.h"

#include <plasmazones_rendering_export.h>
#include <QMutex>
#include <QVariantList>
#include <QVector>
#include <atomic>
#include <memory>

QT_BEGIN_NAMESPACE
class QSGNode;
QT_END_NAMESPACE

namespace PlasmaZones {

/**
 * @brief QQuickItem for rendering zone overlays with custom shaders
 *
 * Inherits from PhosphorRendering::ShaderEffect which provides all base
 * shader rendering: Shadertoy uniforms, multipass, custom params/colors,
 * textures, status. This subclass adds zone-specific functionality:
 *
 * - Zone data array (QVariantList zones property for QML)
 * - Zone counts (zoneCount, highlightedCount)
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
    // Registered manually via qmlRegisterType in daemon/main.cpp and
    // editor/main.cpp under the "PlasmaZones" module URI. QML_ELEMENT here
    // would be inert (no qt_add_qml_module target exists) and misleading.

    // Zone data (zone-specific, not in parent)
    Q_PROPERTY(QVariantList zones READ zones WRITE setZones NOTIFY zonesChanged FINAL)
    Q_PROPERTY(int zoneCount READ zoneCount NOTIFY zoneCountChanged FINAL)
    Q_PROPERTY(int highlightedCount READ highlightedCount NOTIFY highlightedCountChanged FINAL)
    /** Index of the zone under the cursor for hover highlight (preview mode). -1 = none. Avoids full zones churn on
     * mouse move. */
    Q_PROPERTY(
        int hoveredZoneIndex READ hoveredZoneIndex WRITE setHoveredZoneIndex NOTIFY hoveredZoneIndexChanged FINAL)

    // Labels payload (sparse pre-rendered zone-number glyph tiles for shader pass)
    Q_PROPERTY(PhosphorRendering::ZoneLabelTexture labelsTexture READ labelsTexture WRITE setLabelsTexture NOTIFY
                   labelsTextureChanged FINAL)

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
    PhosphorRendering::ZoneLabelTexture labelsTexture() const;
    void setLabelsTexture(const PhosphorRendering::ZoneLabelTexture& labels);

    /**
     * @brief Get a thread-safe copy of zone data for rendering
     *
     * This method is safe to call from the render thread. It acquires
     * a mutex briefly to copy the current zone state.
     *
     * @return Snapshot of current zone data
     */
    PhosphorRendering::ZoneDataSnapshot getZoneDataSnapshot() const;

    /**
     * @brief Get parsed zone rectangles (thread-safe)
     * @return Vector of normalized zone rectangles
     */
    QVector<PhosphorRendering::ZoneRect> zoneRects() const;

    /**
     * @brief Get parsed zone fill colors (thread-safe)
     * @return Vector of zone fill colors
     */
    QVector<PhosphorRendering::ZoneColor> zoneFillColors() const;

    /**
     * @brief Get parsed zone border colors (thread-safe)
     * @return Vector of zone border colors
     */
    QVector<PhosphorRendering::ZoneColor> zoneBorderColors() const;

    // Note: reloadShader() is inherited from ShaderEffect (Q_INVOKABLE). Call
    // that directly from QML / C++ — no zone-specific alias needed.

    /**
     * @brief Refuse external uniform-extension replacement.
     *
     * ZoneShaderItem owns an internal ZoneUniformExtension (created in its
     * constructor) that writes the zone UBO region the GLSL side expects.
     * Replacing it with an arbitrary extension would either crash rendering
     * (layout mismatch with common.glsl) or silently produce wrong output.
     * We override the base setter to refuse + log rather than silently
     * accept and ignore, so misuse fails loud at the earliest possible call
     * site.
     */
    void setUniformExtension(std::shared_ptr<PhosphorShaders::IUniformExtension> extension) override;

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
     * @brief Override the parent's render-node factory to produce ZoneShaderNodeRhi.
     *
     * Both ShaderEffect subclasses (this and shader-render's RenderEffect) now
     * route node creation through the parent's createShaderNode() hook, so the
     * node-type contract lives in one place. The override of updatePaintNode
     * above still exists because the load semantics differ (vertex shader
     * loaded before fragment, plus zone data sync) — a future refactor that
     * folds those into hook methods would remove the override entirely without
     * touching this factory.
     */
    PhosphorRendering::ShaderNodeRhi* createShaderNode() override;

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

    // Labels texture (main thread writes, render thread reads via updatePaintNode)
    PhosphorRendering::ZoneLabelTexture m_labelsTexture;
    mutable QMutex m_labelsTextureMutex;

    // Thread-safe zone data storage
    // Protected by m_zoneDataMutex for render thread access
    mutable QMutex m_zoneDataMutex;
    PhosphorRendering::ZoneDataSnapshot m_zoneData;

    // ZoneUniformExtension owned HERE (not on the node) so its lifetime
    // matches the QML-visible item rather than the transient QSGRenderNode.
    // Registered on the base class via ShaderEffect::setUniformExtension in
    // the constructor; parent's syncBasePropertiesToNode pushes it down to
    // each render node that gets created for this item.
    std::shared_ptr<PhosphorRendering::ZoneUniformExtension> m_zoneExtension;

    // Dirty flags for render thread synchronization
    std::atomic<bool> m_zoneDataDirty{false};
    std::atomic<int> m_dataVersion{0};
};

} // namespace PlasmaZones
