// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zoneshadernode.h"

#include <QDebug>
#include <QFile>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFunctions>
#include <QQuickWindow>
#include <QTextStream>

#include <cstring>

#include "../../core/logging.h"

namespace PlasmaZones {

namespace {

// Fullscreen quad vertices: position (x, y) and UV (u, v)
// Triangle strip: (-1,-1), (1,-1), (-1,1), (1,1)
constexpr float QuadVertices[] = {
    // Position      UV
    -1.0f, -1.0f,   0.0f, 0.0f,  // Bottom-left
     1.0f, -1.0f,   1.0f, 0.0f,  // Bottom-right
    -1.0f,  1.0f,   0.0f, 1.0f,  // Top-left
     1.0f,  1.0f,   1.0f, 1.0f,  // Top-right
};

constexpr int PositionAttrib = 0;
constexpr int TexCoordAttrib = 1;

// UBO binding point
constexpr GLuint UBOBindingPoint = 0;

// Maps m_customParams1-4 to customParams[0-3] in the UBO
// Each vec4 holds 4 slots: customParams1.xyzw = slots 0-3, etc.
// Same deal for colors: m_customColor1-4 → customColors[0-3]
constexpr int UniformVecIndex1 = 0;
constexpr int UniformVecIndex2 = 1;
constexpr int UniformVecIndex3 = 2;
constexpr int UniformVecIndex4 = 3;

// Component indices within each vec4
constexpr int ComponentX = 0;
constexpr int ComponentY = 1;
constexpr int ComponentZ = 2;
constexpr int ComponentW = 3;

} // anonymous namespace

// =============================================================================
// Constructor / Destructor
// =============================================================================

ZoneShaderNode::ZoneShaderNode(QQuickItem* item)
    : m_item(item)
{
    Q_ASSERT(item != nullptr);

    // Initialize uniform struct to zero
    std::memset(&m_uniforms, 0, sizeof(m_uniforms));

    // Set default custom parameters
    m_customParams1 = QVector4D(0.5f, 2.0f, 0.0f, 0.0f);
    m_customParams2 = QVector4D(0.0f, 0.0f, 0.0f, 0.0f);

    // Set identity matrix as default
    QMatrix4x4 identity;
    std::memcpy(m_uniforms.qt_Matrix, identity.constData(), 16 * sizeof(float));
    m_uniforms.qt_Opacity = 1.0f;

    // Shader sources must be set via setVertexShaderSource/setFragmentShaderSource
    // No fallback shaders - if loading fails, the QML layer falls back to normal overlay

    qCDebug(PlasmaZones::lcOverlay) << "ZoneShaderNode created for item:" << item;
}

ZoneShaderNode::~ZoneShaderNode()
{
    // Qt should call releaseResources() before we get here, but sometimes it doesn't
    // (context loss, weird shutdown order, etc). If there's no GL context we can't
    // clean up properly - the resources leak, but OS reclaims them on exit anyway.
    if (m_initialized) {
        qCWarning(PlasmaZones::lcOverlay) << "ZoneShaderNode destroyed with active GL resources - "
                               << "attempting cleanup";
        // Try to clean up if we have a valid context
        QOpenGLContext* ctx = QOpenGLContext::currentContext();
        if (ctx) {
            destroyGL();
        } else {
            // No context available - resources will leak but we can't do anything about it
            qCWarning(PlasmaZones::lcOverlay) << "No GL context available for cleanup - resources may leak";
            m_initialized = false;
        }
    }
}

// =============================================================================
// QSGRenderNode Interface
// =============================================================================

QSGRenderNode::StateFlags ZoneShaderNode::changedStates() const
{
    // Report which OpenGL states we modify
    return BlendState | DepthState | StencilState | ScissorState | CullState;
}

QSGRenderNode::RenderingFlags ZoneShaderNode::flags() const
{
    // Request alpha blending support and indicate we handle transforms
    return BoundedRectRendering | DepthAwareRendering | OpaqueRendering;
}

QRectF ZoneShaderNode::rect() const
{
    // Return the bounding rectangle in item coordinates
    if (m_item) {
        return QRectF(0, 0, m_item->width(), m_item->height());
    }
    return QRectF();
}

void ZoneShaderNode::prepare()
{
    // Initialize OpenGL resources on first call
    if (!m_initialized) {
        if (!initializeGL()) {
            qCWarning(PlasmaZones::lcOverlay) << "Failed to initialize OpenGL resources";
            return;
        }
    }

    // Recompile shader if source changed
    if (m_shaderDirty) {
        if (!createShaderProgram()) {
            qCWarning(PlasmaZones::lcOverlay) << "Failed to compile shader:" << m_shaderError;
        }
        m_shaderDirty = false;
    }

    // Update uniform buffer if data changed
    if (m_uniformsDirty) {
        syncUniformsFromData();
        updateUniformBuffer();
        m_uniformsDirty = false;
    }
}

void ZoneShaderNode::render(const RenderState* state)
{
    if (!m_initialized || !m_shaderReady || !m_program) {
        return;
    }

    // Get OpenGL functions
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) {
        qCWarning(PlasmaZones::lcOverlay) << "No current OpenGL context";
        return;
    }

    QOpenGLFunctions* f = ctx->functions();
    QOpenGLExtraFunctions* ef = ctx->extraFunctions();
    if (!f || !ef) {
        qCWarning(PlasmaZones::lcOverlay) << "Could not get OpenGL functions";
        return;
    }

    // =========================================================================
    // Save ALL OpenGL state that we modify (critical for scene graph integrity)
    // =========================================================================

    // Save blend state
    GLboolean blendEnabled = f->glIsEnabled(GL_BLEND);
    GLboolean depthEnabled = f->glIsEnabled(GL_DEPTH_TEST);
    GLboolean cullEnabled = f->glIsEnabled(GL_CULL_FACE);
    GLint blendSrcRgb, blendDstRgb, blendSrcAlpha, blendDstAlpha;
    f->glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRgb);
    f->glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRgb);
    f->glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha);
    f->glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha);

    // Save scissor state
    GLboolean scissorEnabled = f->glIsEnabled(GL_SCISSOR_TEST);
    GLint prevScissorBox[4];
    f->glGetIntegerv(GL_SCISSOR_BOX, prevScissorBox);

    // Save viewport state
    GLint prevViewport[4];
    f->glGetIntegerv(GL_VIEWPORT, prevViewport);

    // Save VAO state
    GLint prevVAO = 0;
    f->glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVAO);

    // Save shader program state
    GLint prevProgram = 0;
    f->glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);

    // Save UBO binding at our binding point
    GLint prevUBO = 0;
    ef->glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, UBOBindingPoint, &prevUBO);

    // =========================================================================
    // Configure rendering state
    // =========================================================================

    f->glEnable(GL_BLEND);
    f->glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    f->glDisable(GL_DEPTH_TEST);
    f->glDisable(GL_CULL_FACE);

    // Set scissor from RenderState
    if (state && state->scissorEnabled()) {
        const QRect scissor = state->scissorRect();
        f->glEnable(GL_SCISSOR_TEST);
        f->glScissor(scissor.x(), scissor.y(), scissor.width(), scissor.height());
    } else {
        f->glDisable(GL_SCISSOR_TEST);
    }

    // For fullscreen quad rendering with clip-space vertices, use identity matrix
    // The scene graph projection would transform our -1..1 clip-space coords incorrectly
    QMatrix4x4 identity;
    std::memcpy(m_uniforms.qt_Matrix, identity.constData(), 16 * sizeof(float));
    m_uniforms.qt_Opacity = static_cast<float>(inheritedOpacity());

    // Note: Per-frame debug logging removed to avoid log spam.
    // Enable via QT_LOGGING_RULES="PlasmaZones.Overlay.debug=true" if needed for debugging.

    // Re-upload uniforms with updated matrix/opacity (partial update)
    updateUniformBuffer();

    // Bind shader program
    m_program->bind();

    // Bind VAO
    m_vao->bind();

    // Bind UBO to binding point
    ef->glBindBufferBase(GL_UNIFORM_BUFFER, UBOBindingPoint, m_ubo);

    // Set the viewport to match the item's geometry
    QRectF itemRect = rect();
    if (m_item && m_item->window()) {
        // Convert item coordinates to window coordinates
        QPointF topLeft = m_item->mapToScene(QPointF(0, 0));
        qreal dpr = m_item->window()->devicePixelRatio();
        int vpX = static_cast<int>(topLeft.x() * dpr);
        int vpY = static_cast<int>(topLeft.y() * dpr);
        int vpW = static_cast<int>(itemRect.width() * dpr);
        int vpH = static_cast<int>(itemRect.height() * dpr);

        // Note: Qt uses bottom-left origin for OpenGL, adjust Y
        int windowHeight = static_cast<int>(m_item->window()->height() * dpr);
        vpY = windowHeight - vpY - vpH;

        f->glViewport(vpX, vpY, vpW, vpH);
    }

    // Draw fullscreen quad (triangle strip, 4 vertices)
    f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // =========================================================================
    // Restore ALL OpenGL state (critical for scene graph integrity)
    // =========================================================================

    // Restore UBO binding
    ef->glBindBufferBase(GL_UNIFORM_BUFFER, UBOBindingPoint, prevUBO);

    // Restore VAO (must unbind ours first, then bind previous)
    m_vao->release();
    if (prevVAO != 0) {
        ef->glBindVertexArray(prevVAO);
    }

    // Restore shader program
    m_program->release();
    if (prevProgram != 0) {
        f->glUseProgram(prevProgram);
    }

    // Restore viewport
    f->glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

    // Restore scissor state
    if (scissorEnabled) {
        f->glEnable(GL_SCISSOR_TEST);
    } else {
        f->glDisable(GL_SCISSOR_TEST);
    }
    f->glScissor(prevScissorBox[0], prevScissorBox[1], prevScissorBox[2], prevScissorBox[3]);

    // Restore blend state
    if (blendEnabled) {
        f->glEnable(GL_BLEND);
    } else {
        f->glDisable(GL_BLEND);
    }
    f->glBlendFuncSeparate(blendSrcRgb, blendDstRgb, blendSrcAlpha, blendDstAlpha);

    // Restore depth state
    if (depthEnabled) {
        f->glEnable(GL_DEPTH_TEST);
    } else {
        f->glDisable(GL_DEPTH_TEST);
    }

    // Restore cull state
    if (cullEnabled) {
        f->glEnable(GL_CULL_FACE);
    } else {
        f->glDisable(GL_CULL_FACE);
    }
}

void ZoneShaderNode::releaseResources()
{
    destroyGL();
}

// =============================================================================
// Zone Data Management
// =============================================================================

void ZoneShaderNode::setZones(const QVector<ZoneData>& zones)
{
    int count = qMin(zones.size(), MaxZones);
    m_zones = zones.mid(0, count);
    m_uniformsDirty = true;
}

void ZoneShaderNode::setZone(int index, const ZoneData& data)
{
    if (index >= 0 && index < MaxZones) {
        if (index >= m_zones.size()) {
            m_zones.resize(index + 1);
        }
        m_zones[index] = data;
        m_uniformsDirty = true;
    }
}

void ZoneShaderNode::setHighlightedZones(const QVector<int>& indices)
{
    m_highlightedIndices = indices;

    // Update highlight flags in zone data
    for (int i = 0; i < m_zones.size(); ++i) {
        m_zones[i].isHighlighted = indices.contains(i);
    }
    m_uniformsDirty = true;
}

// =============================================================================
// Shader Loading
// =============================================================================

bool ZoneShaderNode::loadVertexShader(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(PlasmaZones::lcOverlay) << "Failed to open vertex shader file:" << path;
        return false;
    }

    QTextStream stream(&file);
    m_vertexShaderSource = stream.readAll();
    m_shaderDirty = true;
    return true;
}

bool ZoneShaderNode::loadFragmentShader(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(PlasmaZones::lcOverlay) << "Failed to open fragment shader file:" << path;
        return false;
    }

    QTextStream stream(&file);
    m_fragmentShaderSource = stream.readAll();
    m_shaderDirty = true;
    return true;
}

void ZoneShaderNode::setVertexShaderSource(const QString& source)
{
    if (m_vertexShaderSource != source) {
        m_vertexShaderSource = source;
        m_shaderDirty = true;
    }
}

void ZoneShaderNode::setFragmentShaderSource(const QString& source)
{
    if (m_fragmentShaderSource != source) {
        m_fragmentShaderSource = source;
        m_shaderDirty = true;
    }
}

// =============================================================================
// OpenGL Resource Management
// =============================================================================

bool ZoneShaderNode::initializeGL()
{
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) {
        qCWarning(PlasmaZones::lcOverlay) << "No OpenGL context available";
        return false;
    }

    QOpenGLFunctions* f = ctx->functions();
    QOpenGLExtraFunctions* ef = ctx->extraFunctions();
    if (!f || !ef) {
        qCWarning(PlasmaZones::lcOverlay) << "Could not get OpenGL functions";
        return false;
    }

    // Create VAO
    m_vao = std::make_unique<QOpenGLVertexArrayObject>();
    if (!m_vao->create()) {
        qCWarning(PlasmaZones::lcOverlay) << "Failed to create VAO";
        return false;
    }

    // Create VBO and upload quad vertices
    if (!createBuffers()) {
        qCWarning(PlasmaZones::lcOverlay) << "Failed to create buffers";
        return false;
    }

    // Create UBO
    ef->glGenBuffers(1, &m_ubo);
    if (m_ubo == 0) {
        qCWarning(PlasmaZones::lcOverlay) << "Failed to create UBO";
        return false;
    }

    // Allocate UBO storage
    ef->glBindBuffer(GL_UNIFORM_BUFFER, m_ubo);
    ef->glBufferData(GL_UNIFORM_BUFFER, sizeof(ZoneShaderUniforms), nullptr, GL_DYNAMIC_DRAW);
    ef->glBindBuffer(GL_UNIFORM_BUFFER, 0);

    // Create initial shader program
    if (!createShaderProgram()) {
        qCWarning(PlasmaZones::lcOverlay) << "Failed to create initial shader program:" << m_shaderError;
        // Continue without shader - we can try again later
    }

    m_initialized = true;
    m_shaderDirty = false;

    qCDebug(PlasmaZones::lcOverlay) << "ZoneShaderNode OpenGL initialized successfully";
    return true;
}

bool ZoneShaderNode::createBuffers()
{
    // Create and bind VBO
    m_vbo = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
    if (!m_vbo->create()) {
        qCWarning(PlasmaZones::lcOverlay) << "Failed to create VBO";
        return false;
    }

    m_vao->bind();
    m_vbo->bind();

    // Upload vertex data
    m_vbo->allocate(QuadVertices, sizeof(QuadVertices));

    // Set up vertex attributes - ensure we have a valid context
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) {
        qCWarning(PlasmaZones::lcOverlay) << "No GL context available for vertex attribute setup";
        m_vbo->release();
        m_vao->release();
        return false;
    }
    QOpenGLFunctions* f = ctx->functions();

    // Position attribute (location 0): 2 floats at offset 0, stride 16 bytes
    f->glEnableVertexAttribArray(PositionAttrib);
    f->glVertexAttribPointer(PositionAttrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);

    // TexCoord attribute (location 1): 2 floats at offset 8 bytes, stride 16 bytes
    f->glEnableVertexAttribArray(TexCoordAttrib);
    f->glVertexAttribPointer(TexCoordAttrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                             reinterpret_cast<void*>(2 * sizeof(float)));

    m_vbo->release();
    m_vao->release();

    return true;
}

bool ZoneShaderNode::createShaderProgram()
{
    m_shaderReady = false;
    m_shaderError.clear();

    // Validate shader sources - if empty, report error (no fallback)
    // The QML layer will handle fallback to normal overlay
    if (m_vertexShaderSource.isEmpty()) {
        m_shaderError = QStringLiteral("Vertex shader source is empty");
        qCWarning(PlasmaZones::lcOverlay) << m_shaderError;
        return false;
    }
    if (m_fragmentShaderSource.isEmpty()) {
        m_shaderError = QStringLiteral("Fragment shader source is empty");
        qCWarning(PlasmaZones::lcOverlay) << m_shaderError;
        return false;
    }

    // Create shader program
    m_program = std::make_unique<QOpenGLShaderProgram>();

    // Compile vertex shader
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, m_vertexShaderSource)) {
        m_shaderError = QStringLiteral("Vertex shader compilation failed: ") + m_program->log();
        qCWarning(PlasmaZones::lcOverlay) << m_shaderError;
        m_program.reset();
        return false;
    }

    // Compile fragment shader
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, m_fragmentShaderSource)) {
        m_shaderError = QStringLiteral("Fragment shader compilation failed: ") + m_program->log();
        qCWarning(PlasmaZones::lcOverlay) << m_shaderError;
        m_program.reset();
        return false;
    }

    // Bind attribute locations before linking
    m_program->bindAttributeLocation("position", PositionAttrib);
    m_program->bindAttributeLocation("texCoord", TexCoordAttrib);

    // Link program
    if (!m_program->link()) {
        m_shaderError = QStringLiteral("Shader program linking failed: ") + m_program->log();
        qCWarning(PlasmaZones::lcOverlay) << m_shaderError;
        m_program.reset();
        return false;
    }

    // Get uniform block index and bind to binding point
    // The ZoneUniforms block is REQUIRED for the shader to function properly
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (ctx) {
        QOpenGLExtraFunctions* ef = ctx->extraFunctions();
        if (ef) {
            GLuint blockIndex = ef->glGetUniformBlockIndex(m_program->programId(), "ZoneUniforms");
            if (blockIndex != GL_INVALID_INDEX) {
                ef->glUniformBlockBinding(m_program->programId(), blockIndex, UBOBindingPoint);
            } else {
                // UBO block is required - fail if not found
                m_shaderError = QStringLiteral("Required UBO block 'ZoneUniforms' not found in shader. "
                                               "Shader must define this uniform block for zone rendering.");
                qCWarning(PlasmaZones::lcOverlay) << m_shaderError;
                m_program.reset();
                return false;
            }
        } else {
            m_shaderError = QStringLiteral("Could not get OpenGL extra functions for UBO setup");
            qCWarning(PlasmaZones::lcOverlay) << m_shaderError;
            m_program.reset();
            return false;
        }
    } else {
        m_shaderError = QStringLiteral("No OpenGL context available for shader setup");
        qCWarning(PlasmaZones::lcOverlay) << m_shaderError;
        m_program.reset();
        return false;
    }

    m_shaderReady = true;
    qCDebug(PlasmaZones::lcOverlay) << "Shader program compiled and linked successfully";
    return true;
}

void ZoneShaderNode::destroyGL()
{
    if (!m_initialized) {
        return;
    }

    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (ctx) {
        QOpenGLExtraFunctions* ef = ctx->extraFunctions();
        if (ef && m_ubo != 0) {
            ef->glDeleteBuffers(1, &m_ubo);
            m_ubo = 0;
        }
    } else if (m_ubo != 0) {
        // No context available but we have a UBO - it will leak
        qCWarning(PlasmaZones::lcOverlay) << "No GL context for UBO cleanup - buffer" << m_ubo << "will leak";
        m_ubo = 0; // Clear the handle even though we couldn't delete it
    }

    m_vbo.reset();
    m_vao.reset();
    m_program.reset();

    m_initialized = false;
    m_shaderReady = false;

    qCDebug(PlasmaZones::lcOverlay) << "ZoneShaderNode OpenGL resources released";
}

// =============================================================================
// Uniform Buffer Management
// =============================================================================

void ZoneShaderNode::syncUniformsFromData()
{
    // Update timing uniforms
    m_uniforms.iTime = m_time;
    m_uniforms.iTimeDelta = m_timeDelta;
    m_uniforms.iFrame = m_frame;

    // Update resolution
    m_uniforms.iResolution[0] = m_width;
    m_uniforms.iResolution[1] = m_height;

    // Update mouse position (xy = pixels, zw = normalized 0-1)
    m_uniforms.iMouse[0] = static_cast<float>(m_mousePosition.x());
    m_uniforms.iMouse[1] = static_cast<float>(m_mousePosition.y());
    m_uniforms.iMouse[2] = m_width > 0 ? static_cast<float>(m_mousePosition.x() / m_width) : 0.0f;
    m_uniforms.iMouse[3] = m_height > 0 ? static_cast<float>(m_mousePosition.y() / m_height) : 0.0f;

    // Update zone counts
    m_uniforms.zoneCount = m_zones.size();

    // Calculate highlighted count from actual zone data (not from stale m_highlightedIndices)
    int highlightedCount = 0;
    for (const auto& zone : m_zones) {
        if (zone.isHighlighted) {
            ++highlightedCount;
        }
    }
    m_uniforms.highlightedCount = highlightedCount;

    // Pack custom params into the UBO (4 vec4s = 16 float slots)
    m_uniforms.customParams[UniformVecIndex1][ComponentX] = m_customParams1.x();
    m_uniforms.customParams[UniformVecIndex1][ComponentY] = m_customParams1.y();
    m_uniforms.customParams[UniformVecIndex1][ComponentZ] = m_customParams1.z();
    m_uniforms.customParams[UniformVecIndex1][ComponentW] = m_customParams1.w();

    m_uniforms.customParams[UniformVecIndex2][ComponentX] = m_customParams2.x();
    m_uniforms.customParams[UniformVecIndex2][ComponentY] = m_customParams2.y();
    m_uniforms.customParams[UniformVecIndex2][ComponentZ] = m_customParams2.z();
    m_uniforms.customParams[UniformVecIndex2][ComponentW] = m_customParams2.w();

    m_uniforms.customParams[UniformVecIndex3][ComponentX] = m_customParams3.x();
    m_uniforms.customParams[UniformVecIndex3][ComponentY] = m_customParams3.y();
    m_uniforms.customParams[UniformVecIndex3][ComponentZ] = m_customParams3.z();
    m_uniforms.customParams[UniformVecIndex3][ComponentW] = m_customParams3.w();

    m_uniforms.customParams[UniformVecIndex4][ComponentX] = m_customParams4.x();
    m_uniforms.customParams[UniformVecIndex4][ComponentY] = m_customParams4.y();
    m_uniforms.customParams[UniformVecIndex4][ComponentZ] = m_customParams4.z();
    m_uniforms.customParams[UniformVecIndex4][ComponentW] = m_customParams4.w();

    // Update custom colors (4 color slots)
    m_uniforms.customColors[UniformVecIndex1][ComponentX] = static_cast<float>(m_customColor1.redF());
    m_uniforms.customColors[UniformVecIndex1][ComponentY] = static_cast<float>(m_customColor1.greenF());
    m_uniforms.customColors[UniformVecIndex1][ComponentZ] = static_cast<float>(m_customColor1.blueF());
    m_uniforms.customColors[UniformVecIndex1][ComponentW] = static_cast<float>(m_customColor1.alphaF());

    m_uniforms.customColors[UniformVecIndex2][ComponentX] = static_cast<float>(m_customColor2.redF());
    m_uniforms.customColors[UniformVecIndex2][ComponentY] = static_cast<float>(m_customColor2.greenF());
    m_uniforms.customColors[UniformVecIndex2][ComponentZ] = static_cast<float>(m_customColor2.blueF());
    m_uniforms.customColors[UniformVecIndex2][ComponentW] = static_cast<float>(m_customColor2.alphaF());

    m_uniforms.customColors[UniformVecIndex3][ComponentX] = static_cast<float>(m_customColor3.redF());
    m_uniforms.customColors[UniformVecIndex3][ComponentY] = static_cast<float>(m_customColor3.greenF());
    m_uniforms.customColors[UniformVecIndex3][ComponentZ] = static_cast<float>(m_customColor3.blueF());
    m_uniforms.customColors[UniformVecIndex3][ComponentW] = static_cast<float>(m_customColor3.alphaF());

    m_uniforms.customColors[UniformVecIndex4][ComponentX] = static_cast<float>(m_customColor4.redF());
    m_uniforms.customColors[UniformVecIndex4][ComponentY] = static_cast<float>(m_customColor4.greenF());
    m_uniforms.customColors[UniformVecIndex4][ComponentZ] = static_cast<float>(m_customColor4.blueF());
    m_uniforms.customColors[UniformVecIndex4][ComponentW] = static_cast<float>(m_customColor4.alphaF());

    // Update zone data arrays
    for (int i = 0; i < MaxZones; ++i) {
        if (i < m_zones.size()) {
            const ZoneData& zone = m_zones[i];

            // Zone rect (normalized 0-1)
            m_uniforms.zoneRects[i][0] = static_cast<float>(zone.rect.x());
            m_uniforms.zoneRects[i][1] = static_cast<float>(zone.rect.y());
            m_uniforms.zoneRects[i][2] = static_cast<float>(zone.rect.width());
            m_uniforms.zoneRects[i][3] = static_cast<float>(zone.rect.height());

            // Fill color
            m_uniforms.zoneFillColors[i][0] = static_cast<float>(zone.fillColor.redF());
            m_uniforms.zoneFillColors[i][1] = static_cast<float>(zone.fillColor.greenF());
            m_uniforms.zoneFillColors[i][2] = static_cast<float>(zone.fillColor.blueF());
            m_uniforms.zoneFillColors[i][3] = static_cast<float>(zone.fillColor.alphaF());

            // Border color
            m_uniforms.zoneBorderColors[i][0] = static_cast<float>(zone.borderColor.redF());
            m_uniforms.zoneBorderColors[i][1] = static_cast<float>(zone.borderColor.greenF());
            m_uniforms.zoneBorderColors[i][2] = static_cast<float>(zone.borderColor.blueF());
            m_uniforms.zoneBorderColors[i][3] = static_cast<float>(zone.borderColor.alphaF());

            // Zone params: borderRadius, borderWidth, isHighlighted, zoneNumber
            m_uniforms.zoneParams[i][0] = zone.borderRadius;
            m_uniforms.zoneParams[i][1] = zone.borderWidth;
            m_uniforms.zoneParams[i][2] = zone.isHighlighted ? 1.0f : 0.0f;
            m_uniforms.zoneParams[i][3] = static_cast<float>(zone.zoneNumber);
        } else {
            // Clear unused zone slots
            std::memset(m_uniforms.zoneRects[i], 0, sizeof(m_uniforms.zoneRects[i]));
            std::memset(m_uniforms.zoneFillColors[i], 0, sizeof(m_uniforms.zoneFillColors[i]));
            std::memset(m_uniforms.zoneBorderColors[i], 0, sizeof(m_uniforms.zoneBorderColors[i]));
            std::memset(m_uniforms.zoneParams[i], 0, sizeof(m_uniforms.zoneParams[i]));
        }
    }
}

void ZoneShaderNode::updateUniformBuffer()
{
    if (m_ubo == 0) {
        return;
    }

    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) {
        return;
    }

    QOpenGLExtraFunctions* ef = ctx->extraFunctions();
    if (!ef) {
        return;
    }

    // Upload uniform data using glBufferSubData for efficient partial updates
    ef->glBindBuffer(GL_UNIFORM_BUFFER, m_ubo);
    ef->glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(ZoneShaderUniforms), &m_uniforms);
    ef->glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

} // namespace PlasmaZones
