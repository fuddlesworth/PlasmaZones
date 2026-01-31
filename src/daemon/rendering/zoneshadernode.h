// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QSGRenderNode>
#include <QQuickItem>
#include <QColor>
#include <QRectF>
#include <QVector2D>
#include <QVector4D>
#include <QMatrix4x4>
#include <QString>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QElapsedTimer>
#include <array>
#include <memory>

namespace PlasmaZones {

/**
 * @brief Maximum number of zones supported by the shader
 *
 * Limited by uniform buffer size constraints and practical usage.
 * 64 zones allows for complex layouts while maintaining performance.
 */
constexpr int MaxZones = 64;

/**
 * @brief GPU uniform buffer layout following std140 rules
 *
 * std140 alignment rules:
 * - float/int: 4 bytes, align to 4
 * - vec2: 8 bytes, align to 8
 * - vec3/vec4: 16 bytes, align to 16
 * - mat4: 64 bytes (4 vec4), align to 16
 * - arrays: element size rounded up to vec4 (16 bytes), align to 16
 *
 * Total size: 4400 bytes (4416 with 16-byte alignment padding)
 */
struct alignas(16) ZoneShaderUniforms
{
    // Transform and opacity from Qt scene graph (offset 0)
    float qt_Matrix[16]; // mat4: 64 bytes at offset 0
    float qt_Opacity; // float: 4 bytes at offset 64

    // Shader timing uniforms (Shadertoy-compatible)
    float iTime; // float: 4 bytes at offset 68
    float iTimeDelta; // float: 4 bytes at offset 72
    int iFrame; // int: 4 bytes at offset 76

    // Resolution (vec2 aligned to 8 bytes)
    float iResolution[2]; // vec2: 8 bytes at offset 80

    // Zone counts
    int zoneCount; // int: 4 bytes at offset 88
    int highlightedCount; // int: 4 bytes at offset 92

    // Mouse position uniform (replaces previous padding)
    // iMouse.xy = mouse position in pixels
    // iMouse.zw = mouse position normalized (0-1)
    float iMouse[4]; // vec4: 16 bytes at offset 96-111

    // Custom shader parameters as arrays for cleaner slot-based access
    // 16 float parameters total (slots 0-15), accessed as customParams[slot/4][slot%4]
    float customParams[4][4]; // vec4[4]: 64 bytes at offset 112 (slots 0-15)

    // Custom colors for shader effects (8 color slots)
    float customColors[8][4]; // vec4[8]: 128 bytes at offset 176 (color slots 0-7)

    // Zone data arrays (each element is vec4, naturally aligned)
    // zoneRects: x, y, width, height (normalized 0-1 coordinates)
    float zoneRects[MaxZones][4]; // vec4[64]: 1024 bytes at offset 304 (176 + 128)

    // zoneFillColors: RGBA fill color for each zone
    float zoneFillColors[MaxZones][4]; // vec4[64]: 1024 bytes at offset 1328 (304 + 1024)

    // zoneBorderColors: RGBA border color for each zone
    float zoneBorderColors[MaxZones][4]; // vec4[64]: 1024 bytes at offset 2352 (1328 + 1024)

    // zoneParams: x=borderRadius, y=borderWidth, z=isHighlighted (0/1), w=zoneNumber
    float zoneParams[MaxZones][4]; // vec4[64]: 1024 bytes at offset 3376 (2352 + 1024)

    // Total: 4400 bytes, padded to 4416 for 16-byte alignment
};

// Verify struct size at compile time (approximately, accounting for alignment)
static_assert(sizeof(ZoneShaderUniforms) <= 8192, "ZoneShaderUniforms exceeds expected size");

/**
 * @brief Zone data for passing to the shader node
 */
struct ZoneData
{
    QRectF rect; // Zone rectangle in normalized coordinates (0-1)
    QColor fillColor; // Fill color with alpha
    QColor borderColor; // Border color with alpha
    float borderRadius = 0.0f; // Corner radius in pixels
    float borderWidth = 2.0f; // Border width in pixels
    bool isHighlighted = false; // Whether zone is currently highlighted
    int zoneNumber = 0; // Zone number for display (1-based)
};

/**
 * @brief QSGRenderNode for direct OpenGL zone rendering
 *
 * Uses direct OpenGL calls with uniform buffers to work around QTBUG-50493.
 * Supports up to 64 zones with Shadertoy-compatible uniforms.
 *
 * @note Requires OpenGL 3.3+ or OpenGL ES 3.0+ for UBO support
 */
class ZoneShaderNode : public QSGRenderNode
{
public:
    /**
     * @brief Construct a new Zone Shader Node
     * @param item The QQuickItem this node renders for (used for geometry)
     */
    explicit ZoneShaderNode(QQuickItem* item);

    /**
     * @brief Destructor - releases OpenGL resources
     */
    ~ZoneShaderNode() override;

    // QSGRenderNode interface

    /**
     * @brief Reports which OpenGL states this node changes
     * @return Flags indicating modified states
     */
    StateFlags changedStates() const override;

    /**
     * @brief Reports rendering flags for the scene graph
     * @return Flags indicating rendering requirements
     */
    RenderingFlags flags() const override;

    /**
     * @brief Returns the bounding rectangle for this node
     * @return Rectangle in item coordinates
     */
    QRectF rect() const override;

    /**
     * @brief Prepare resources before rendering
     *
     * Called on the render thread before render(). Use for:
     * - Creating/updating OpenGL resources
     * - Uploading uniform data
     * - Compiling shaders if needed
     */
    void prepare() override;

    /**
     * @brief Perform the actual OpenGL rendering
     * @param state Contains the current MVP matrix and other render state
     */
    void render(const RenderState* state) override;

    /**
     * @brief Release all OpenGL resources
     *
     * Called when the node is removed from the scene or context is lost.
     */
    void releaseResources() override;

    // Zone data setters (called from updatePaintNode on GUI thread)

    /**
     * @brief Set all zone data at once
     * @param zones Vector of zone data structures
     */
    void setZones(const QVector<ZoneData>& zones);

    /**
     * @brief Set data for a single zone
     * @param index Zone index (0-63)
     * @param data Zone data to set
     */
    void setZone(int index, const ZoneData& data);

    /**
     * @brief Set the number of active zones
     * @param count Number of zones (0-64)
     */
    void setZoneCount(int count);

    /**
     * @brief Set highlighted zone indices
     * @param indices List of zone indices that are highlighted
     */
    void setHighlightedZones(const QVector<int>& indices);

    /**
     * @brief Clear all highlights
     */
    void clearHighlights();

    // Shader timing setters

    /**
     * @brief Set the shader time uniform
     * @param time Time in seconds since shader start
     */
    void setTime(float time);

    /**
     * @brief Set the time delta between frames
     * @param delta Time in seconds since last frame
     */
    void setTimeDelta(float delta);

    /**
     * @brief Set the frame counter
     * @param frame Frame number since start
     */
    void setFrame(int frame);

    /**
     * @brief Set the resolution uniform
     * @param width Width in pixels
     * @param height Height in pixels
     */
    void setResolution(float width, float height);

    /**
     * @brief Set the mouse position uniform
     * @param pos Mouse position in pixels
     */
    void setMousePosition(const QPointF& pos);

    // Custom shader parameters

    /**
     * @brief Set custom parameter vec4 (slot 1)
     * @param params Four float parameters
     */
    void setCustomParams1(const QVector4D& params);

    /**
     * @brief Set custom parameter vec4 (slot 2, params 4-7)
     * @param params Four float parameters
     */
    void setCustomParams2(const QVector4D& params);

    /**
     * @brief Set custom parameter vec4 (slot 3, params 8-11)
     * @param params Four float parameters
     */
    void setCustomParams3(const QVector4D& params);

    /**
     * @brief Set custom parameter vec4 (slot 4, params 12-15)
     * @param params Four float parameters
     */
    void setCustomParams4(const QVector4D& params);

    /**
     * @brief Set custom color (slot 1)
     * @param color Color value
     */
    void setCustomColor1(const QColor& color);

    /**
     * @brief Set custom color (slot 2)
     * @param color Color value
     */
    void setCustomColor2(const QColor& color);

    /**
     * @brief Set custom color (slot 3)
     * @param color Color value
     */
    void setCustomColor3(const QColor& color);

    /**
     * @brief Set custom color (slot 4)
     * @param color Color value
     */
    void setCustomColor4(const QColor& color);

    /**
     * @brief Set custom color (slot 5)
     * @param color Color value
     */
    void setCustomColor5(const QColor& color);

    /**
     * @brief Set custom color (slot 6)
     * @param color Color value
     */
    void setCustomColor6(const QColor& color);

    /**
     * @brief Set custom color (slot 7)
     * @param color Color value
     */
    void setCustomColor7(const QColor& color);

    /**
     * @brief Set custom color (slot 8)
     * @param color Color value
     */
    void setCustomColor8(const QColor& color);

    // Shader loading

    /**
     * @brief Load vertex shader from file
     * @param path Path to vertex shader file (.vert or .glsl)
     * @return true if loaded successfully
     */
    bool loadVertexShader(const QString& path);

    /**
     * @brief Load fragment shader from file
     * @param path Path to fragment shader file (.frag or .glsl)
     * @return true if loaded successfully
     */
    bool loadFragmentShader(const QString& path);

    /**
     * @brief Set vertex shader source directly
     * @param source GLSL source code
     */
    void setVertexShaderSource(const QString& source);

    /**
     * @brief Set fragment shader source directly
     * @param source GLSL source code
     */
    void setFragmentShaderSource(const QString& source);

    /**
     * @brief Check if shaders are compiled and ready
     * @return true if shader program is linked and valid
     */
    bool isShaderReady() const
    {
        return m_shaderReady;
    }

    /**
     * @brief Get the last shader compilation error
     * @return Error string, empty if no error
     */
    QString shaderError() const
    {
        return m_shaderError;
    }

    /**
     * @brief Mark node as needing shader recompilation
     */
    void invalidateShader()
    {
        m_shaderDirty = true;
    }

    /**
     * @brief Mark node as needing uniform buffer update
     */
    void invalidateUniforms()
    {
        m_uniformsDirty = true;
    }

private:
    // OpenGL resource initialization
    bool initializeGL();
    bool createShaderProgram();
    bool createBuffers();
    void destroyGL();

    // Uniform buffer management
    void updateUniformBuffer();
    void syncUniformsFromData();

    // The item we're rendering for
    QQuickItem* m_item;

    // OpenGL resources
    std::unique_ptr<QOpenGLShaderProgram> m_program;
    std::unique_ptr<QOpenGLVertexArrayObject> m_vao;
    std::unique_ptr<QOpenGLBuffer> m_vbo;
    GLuint m_ubo = 0;
    GLuint m_uboBindingPoint = 0;

    // Shader sources
    QString m_vertexShaderSource;
    QString m_fragmentShaderSource;
    QString m_shaderError;

    // State flags
    bool m_initialized = false;
    bool m_shaderReady = false;
    bool m_shaderDirty = true;
    bool m_uniformsDirty = true;
    bool m_geometryDirty = true;

    // Uniform data (CPU side)
    ZoneShaderUniforms m_uniforms;

    // Zone data cache (for easier manipulation)
    QVector<ZoneData> m_zones;
    QVector<int> m_highlightedIndices;

    // Timing
    float m_time = 0.0f;
    float m_timeDelta = 0.0f;
    int m_frame = 0;
    float m_width = 0.0f;
    float m_height = 0.0f;

    // Mouse position (pixels and normalized)
    QPointF m_mousePosition;

    // Custom parameters (16 floats in 4 vec4s)
    QVector4D m_customParams1;
    QVector4D m_customParams2;
    QVector4D m_customParams3;
    QVector4D m_customParams4;

    // Custom colors (8 colors)
    QColor m_customColor1 = Qt::white;
    QColor m_customColor2 = Qt::white;
    QColor m_customColor3 = Qt::white;
    QColor m_customColor4 = Qt::white;
    QColor m_customColor5 = Qt::white;
    QColor m_customColor6 = Qt::white;
    QColor m_customColor7 = Qt::white;
    QColor m_customColor8 = Qt::white;
};

// Inline implementations for simple setters

inline void ZoneShaderNode::setTime(float time)
{
    m_time = time;
    m_uniformsDirty = true;
}

inline void ZoneShaderNode::setTimeDelta(float delta)
{
    m_timeDelta = delta;
    m_uniformsDirty = true;
}

inline void ZoneShaderNode::setFrame(int frame)
{
    m_frame = frame;
    m_uniformsDirty = true;
}

inline void ZoneShaderNode::setResolution(float width, float height)
{
    if (m_width != width || m_height != height) {
        m_width = width;
        m_height = height;
        m_uniformsDirty = true;
    }
}

inline void ZoneShaderNode::setMousePosition(const QPointF& pos)
{
    if (m_mousePosition != pos) {
        m_mousePosition = pos;
        m_uniformsDirty = true;
    }
}

inline void ZoneShaderNode::setCustomParams1(const QVector4D& params)
{
    m_customParams1 = params;
    m_uniformsDirty = true;
}

inline void ZoneShaderNode::setCustomParams2(const QVector4D& params)
{
    m_customParams2 = params;
    m_uniformsDirty = true;
}

inline void ZoneShaderNode::setCustomParams3(const QVector4D& params)
{
    m_customParams3 = params;
    m_uniformsDirty = true;
}

inline void ZoneShaderNode::setCustomParams4(const QVector4D& params)
{
    m_customParams4 = params;
    m_uniformsDirty = true;
}

inline void ZoneShaderNode::setCustomColor1(const QColor& color)
{
    m_customColor1 = color;
    m_uniformsDirty = true;
}

inline void ZoneShaderNode::setCustomColor2(const QColor& color)
{
    m_customColor2 = color;
    m_uniformsDirty = true;
}

inline void ZoneShaderNode::setCustomColor3(const QColor& color)
{
    m_customColor3 = color;
    m_uniformsDirty = true;
}

inline void ZoneShaderNode::setCustomColor4(const QColor& color)
{
    m_customColor4 = color;
    m_uniformsDirty = true;
}

inline void ZoneShaderNode::setCustomColor5(const QColor& color)
{
    m_customColor5 = color;
    m_uniformsDirty = true;
}

inline void ZoneShaderNode::setCustomColor6(const QColor& color)
{
    m_customColor6 = color;
    m_uniformsDirty = true;
}

inline void ZoneShaderNode::setCustomColor7(const QColor& color)
{
    m_customColor7 = color;
    m_uniformsDirty = true;
}

inline void ZoneShaderNode::setCustomColor8(const QColor& color)
{
    m_customColor8 = color;
    m_uniformsDirty = true;
}

inline void ZoneShaderNode::setZoneCount(int count)
{
    if (count >= 0 && count <= MaxZones) {
        m_zones.resize(count);
        m_uniformsDirty = true;
    }
}

inline void ZoneShaderNode::clearHighlights()
{
    m_highlightedIndices.clear();
    for (auto& zone : m_zones) {
        zone.isHighlighted = false;
    }
    m_uniformsDirty = true;
}

} // namespace PlasmaZones
