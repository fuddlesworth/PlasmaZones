// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <plasmazones_export.h>
#include <QQuickItem>
#include <QUrl>
#include <QSizeF>
#include <QVector4D>
#include <QVariantList>
#include <QVariantMap>
#include <QRectF>
#include <QColor>
#include <QMutex>
#include <QVector>
#include <atomic>

QT_BEGIN_NAMESPACE
class QSGNode;
QT_END_NAMESPACE

namespace PlasmaZones {

/**
 * @brief Parsed zone rectangle data for shader rendering
 *
 * Stores zone geometry normalized to [0,1] coordinates for GPU processing.
 * Safe to copy between threads.
 */
struct ZoneRect
{
    float x = 0.0f; ///< Left edge (0-1)
    float y = 0.0f; ///< Top edge (0-1)
    float width = 0.0f; ///< Width (0-1)
    float height = 0.0f; ///< Height (0-1)
    int zoneNumber = 0; ///< Zone number for display
    bool highlighted = false; ///< Whether this zone is highlighted
    float borderRadius = 8.0f; ///< Corner radius in pixels (for shader)
    float borderWidth = 2.0f; ///< Border width in pixels (for shader)
};

/**
 * @brief Parsed zone color data for shader rendering
 *
 * Stores RGBA colors normalized to [0,1] for GPU processing.
 */
struct ZoneColor
{
    float r = 0.0f; ///< Red component (0-1)
    float g = 0.0f; ///< Green component (0-1)
    float b = 0.0f; ///< Blue component (0-1)
    float a = 1.0f; ///< Alpha component (0-1)

    ZoneColor() = default;
    ZoneColor(float red, float green, float blue, float alpha = 1.0f)
        : r(red)
        , g(green)
        , b(blue)
        , a(alpha)
    {
    }

    static ZoneColor fromQColor(const QColor& color)
    {
        return ZoneColor(static_cast<float>(color.redF()), static_cast<float>(color.greenF()),
                         static_cast<float>(color.blueF()), static_cast<float>(color.alphaF()));
    }

    QVector4D toVector4D() const
    {
        return QVector4D(r, g, b, a);
    }
};

/**
 * @brief Thread-safe zone data snapshot for render thread
 *
 * This structure holds a complete copy of zone state that can be
 * safely read by the render thread while the main thread updates.
 */
struct ZoneDataSnapshot
{
    QVector<ZoneRect> rects;
    QVector<ZoneColor> fillColors;
    QVector<ZoneColor> borderColors;
    int zoneCount = 0;
    int highlightedCount = 0;
    int version = 0; ///< Incremented on each update for change detection
};

/**
 * @brief QQuickItem for rendering zone overlays with custom shaders
 *
 * Renders zones using GLSL fragment shaders via Qt's scene graph.
 * Supports dynamic shader loading, animated uniforms, and custom parameters.
 *
 * Zone data is synchronized from the main thread to the render thread
 * using double-buffering with mutex protection.
 *
 * Usage in QML:
 * @code
 * ZoneShaderItem {
 *     anchors.fill: parent
 *     zones: zoneDataProvider.zones
 *     shaderSource: "qrc:/shaders/neon.frag"
 *     customColor1: Qt.vector4d(1.0, 0.5, 0.0, 1.0)
 * }
 * @endcode
 */
class PLASMAZONES_EXPORT ZoneShaderItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    // Animation uniforms
    Q_PROPERTY(qreal iTime READ iTime WRITE setITime NOTIFY iTimeChanged FINAL)
    Q_PROPERTY(qreal iTimeDelta READ iTimeDelta WRITE setITimeDelta NOTIFY iTimeDeltaChanged FINAL)
    Q_PROPERTY(int iFrame READ iFrame WRITE setIFrame NOTIFY iFrameChanged FINAL)

    // Resolution uniform
    Q_PROPERTY(QSizeF iResolution READ iResolution WRITE setIResolution NOTIFY iResolutionChanged FINAL)

    // Mouse position uniform
    Q_PROPERTY(QPointF iMouse READ iMouse WRITE setIMouse NOTIFY iMouseChanged FINAL)

    // Zone data
    Q_PROPERTY(QVariantList zones READ zones WRITE setZones NOTIFY zonesChanged FINAL)
    Q_PROPERTY(int zoneCount READ zoneCount NOTIFY zoneCountChanged FINAL)
    Q_PROPERTY(int highlightedCount READ highlightedCount NOTIFY highlightedCountChanged FINAL)

    // Shader source
    Q_PROPERTY(QUrl shaderSource READ shaderSource WRITE setShaderSource NOTIFY shaderSourceChanged FINAL)
    Q_PROPERTY(QVariantMap shaderParams READ shaderParams WRITE setShaderParams NOTIFY shaderParamsChanged FINAL)

    // Custom parameters (16 floats in 4 vec4s, slots 0-15)
    // All use consolidated customParamsChanged signal
    Q_PROPERTY(QVector4D customParams1 READ customParams1 WRITE setCustomParams1 NOTIFY customParamsChanged FINAL)
    Q_PROPERTY(QVector4D customParams2 READ customParams2 WRITE setCustomParams2 NOTIFY customParamsChanged FINAL)
    Q_PROPERTY(QVector4D customParams3 READ customParams3 WRITE setCustomParams3 NOTIFY customParamsChanged FINAL)
    Q_PROPERTY(QVector4D customParams4 READ customParams4 WRITE setCustomParams4 NOTIFY customParamsChanged FINAL)

    // Custom colors (8 colors)
    // All use consolidated customColorsChanged signal
    Q_PROPERTY(QVector4D customColor1 READ customColor1 WRITE setCustomColor1 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QVector4D customColor2 READ customColor2 WRITE setCustomColor2 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QVector4D customColor3 READ customColor3 WRITE setCustomColor3 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QVector4D customColor4 READ customColor4 WRITE setCustomColor4 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QVector4D customColor5 READ customColor5 WRITE setCustomColor5 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QVector4D customColor6 READ customColor6 WRITE setCustomColor6 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QVector4D customColor7 READ customColor7 WRITE setCustomColor7 NOTIFY customColorsChanged FINAL)
    Q_PROPERTY(QVector4D customColor8 READ customColor8 WRITE setCustomColor8 NOTIFY customColorsChanged FINAL)

    // Status
    Q_PROPERTY(Status status READ status NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString errorLog READ errorLog NOTIFY errorLogChanged FINAL)

public:
    /**
     * @brief Shader loading and compilation status
     */
    enum class Status {
        Null, ///< No shader loaded
        Loading, ///< Shader is being loaded/compiled
        Ready, ///< Shader compiled successfully
        Error ///< Shader compilation failed
    };
    Q_ENUM(Status)

    explicit ZoneShaderItem(QQuickItem* parent = nullptr);
    ~ZoneShaderItem() override;

    // Animation getters/setters
    qreal iTime() const
    {
        return m_iTime;
    }
    void setITime(qreal time);

    qreal iTimeDelta() const
    {
        return m_iTimeDelta;
    }
    void setITimeDelta(qreal delta);

    int iFrame() const
    {
        return m_iFrame;
    }
    void setIFrame(int frame);

    // Resolution getter/setter
    QSizeF iResolution() const
    {
        return m_iResolution;
    }
    void setIResolution(const QSizeF& resolution);

    // Mouse position getter/setter
    QPointF iMouse() const
    {
        return m_iMouse;
    }
    void setIMouse(const QPointF& mouse);

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

    // Shader source getter/setter
    QUrl shaderSource() const
    {
        return m_shaderSource;
    }
    void setShaderSource(const QUrl& source);

    QVariantMap shaderParams() const
    {
        return m_shaderParams;
    }
    void setShaderParams(const QVariantMap& params);

    // Custom parameters getters/setters (16 floats in 4 vec4s)
    QVector4D customParams1() const
    {
        return m_customParams1;
    }
    void setCustomParams1(const QVector4D& params);

    QVector4D customParams2() const
    {
        return m_customParams2;
    }
    void setCustomParams2(const QVector4D& params);

    QVector4D customParams3() const
    {
        return m_customParams3;
    }
    void setCustomParams3(const QVector4D& params);

    QVector4D customParams4() const
    {
        return m_customParams4;
    }
    void setCustomParams4(const QVector4D& params);

    // Custom color getters/setters (8 colors)
    QVector4D customColor1() const
    {
        return m_customColor1;
    }
    void setCustomColor1(const QVector4D& color);

    QVector4D customColor2() const
    {
        return m_customColor2;
    }
    void setCustomColor2(const QVector4D& color);

    QVector4D customColor3() const
    {
        return m_customColor3;
    }
    void setCustomColor3(const QVector4D& color);

    QVector4D customColor4() const
    {
        return m_customColor4;
    }
    void setCustomColor4(const QVector4D& color);

    QVector4D customColor5() const
    {
        return m_customColor5;
    }
    void setCustomColor5(const QVector4D& color);

    QVector4D customColor6() const
    {
        return m_customColor6;
    }
    void setCustomColor6(const QVector4D& color);

    QVector4D customColor7() const
    {
        return m_customColor7;
    }
    void setCustomColor7(const QVector4D& color);

    QVector4D customColor8() const
    {
        return m_customColor8;
    }
    void setCustomColor8(const QVector4D& color);

    // Status getters
    Status status() const
    {
        return m_status;
    }
    QString errorLog() const
    {
        return m_errorLog;
    }

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
     * @brief Force reload of shader from source (callable from QML)
     */
    Q_INVOKABLE void loadShader();

Q_SIGNALS:
    void iTimeChanged();
    void iTimeDeltaChanged();
    void iFrameChanged();
    void iResolutionChanged();
    void iMouseChanged();
    void zonesChanged();
    void zoneCountChanged();
    void highlightedCountChanged();
    void shaderSourceChanged();
    void shaderParamsChanged();
    void customParamsChanged(); // Emitted when any customParams1-4 changes
    void customColorsChanged(); // Emitted when any customColor1-8 changes
    void statusChanged();
    void errorLogChanged();

protected:
    /**
     * @brief Create or update the scene graph node for rendering
     *
     * Called by Qt's scene graph on the render thread. This method
     * synchronizes zone data and updates shader uniforms.
     *
     * @param oldNode Previous node (may be nullptr)
     * @param data Update paint node data from Qt
     * @return Updated or new scene graph node
     */
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;

    /**
     * @brief Handle geometry changes
     */
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

    /**
     * @brief Handle component completion
     */
    void componentComplete() override;

private:
    /**
     * @brief Parse zone data from QVariantList to internal structures
     *
     * Converts the QML-friendly QVariantList format to optimized
     * internal structures for shader rendering.
     */
    void parseZoneData();

    /**
     * @brief Set error status with message
     */
    void setError(const QString& error);

    /**
     * @brief Set status and emit signal if changed
     */
    void setStatus(Status status);

    /** @brief Get custom color by index (1–8). Used by setShaderParams loop. */
    QVector4D customColorByIndex(int index) const;
    /** @brief Set custom color by index (1–8). Used by setShaderParams loop. */
    void setCustomColorByIndex(int index, const QVector4D& color);

    // Animation state
    qreal m_iTime = 0.0;
    qreal m_iTimeDelta = 0.0;
    int m_iFrame = 0;

    // Resolution
    QSizeF m_iResolution;

    // Mouse position
    QPointF m_iMouse;

    // Zone data (main thread access)
    QVariantList m_zones;
    int m_zoneCount = 0;
    int m_highlightedCount = 0;

    // Shader configuration
    QUrl m_shaderSource;
    QVariantMap m_shaderParams;

    // Custom shader parameters
    QVector4D m_customParams1;
    QVector4D m_customParams2;
    QVector4D m_customParams3;
    QVector4D m_customParams4;
    QVector4D m_customColor1 = QVector4D(1.0f, 0.5f, 0.0f, 1.0f); // Default orange highlight
    QVector4D m_customColor2 = QVector4D(0.2f, 0.2f, 0.2f, 0.8f); // Default gray inactive
    QVector4D m_customColor3 = QVector4D(1.0f, 1.0f, 1.0f, 0.0f); // Default white, alpha 0 = not set
    QVector4D m_customColor4 = QVector4D(0.0f, 0.0f, 0.0f, 0.0f); // Default black, alpha 0 = not set
    QVector4D m_customColor5 = QVector4D(0.0f, 0.0f, 0.0f, 0.0f); // Default black, alpha 0 = not set
    QVector4D m_customColor6 = QVector4D(0.0f, 0.0f, 0.0f, 0.0f); // Default black, alpha 0 = not set
    QVector4D m_customColor7 = QVector4D(0.0f, 0.0f, 0.0f, 0.0f); // Default black, alpha 0 = not set
    QVector4D m_customColor8 = QVector4D(0.0f, 0.0f, 0.0f, 0.0f); // Default black, alpha 0 = not set

    // Status
    Status m_status = Status::Null;
    QString m_errorLog;

    // Thread-safe zone data storage
    // Protected by m_zoneDataMutex for render thread access
    mutable QMutex m_zoneDataMutex;
    ZoneDataSnapshot m_zoneData;

    // Dirty flags for render thread synchronization
    std::atomic<bool> m_zoneDataDirty{false};
    std::atomic<bool> m_shaderDirty{false};
    std::atomic<int> m_dataVersion{0};
};

} // namespace PlasmaZones
