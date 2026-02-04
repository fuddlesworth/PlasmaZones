// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zoneshadernoderhi.h"

#include <QFile>
#include <QFileInfo>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTextStream>
#include <cstring>

#include "../../core/logging.h"
#include "../../core/shaderincluderesolver.h"

#include <rhi/qshaderbaker.h>

namespace PlasmaZones {

namespace RhiConstants {

static constexpr float QuadVertices[] = {
    -1.0f, -1.0f, 0.0f, 0.0f,
    1.0f,  -1.0f, 1.0f, 0.0f,
    -1.0f, 1.0f,  0.0f, 1.0f,
    1.0f,  1.0f,  1.0f, 1.0f,
};

static constexpr int UniformVecIndex1 = 0;
static constexpr int UniformVecIndex2 = 1;
static constexpr int UniformVecIndex3 = 2;
static constexpr int UniformVecIndex4 = 3;
static constexpr int ComponentX = 0;
static constexpr int ComponentY = 1;
static constexpr int ComponentZ = 2;
static constexpr int ComponentW = 3;

} // namespace RhiConstants

ZoneShaderNodeRhi::ZoneShaderNodeRhi(QQuickItem* item)
    : m_item(item)
{
    Q_ASSERT(item != nullptr);
    std::memset(&m_uniforms, 0, sizeof(m_uniforms));
    QMatrix4x4 identity;
    std::memcpy(m_uniforms.qt_Matrix, identity.constData(), 16 * sizeof(float));
    m_uniforms.qt_Opacity = 1.0f;
    m_customParams1 = QVector4D(0.5f, 2.0f, 0.0f, 0.0f);
    m_customParams2 = QVector4D(0.0f, 0.0f, 0.0f, 0.0f);
}

ZoneShaderNodeRhi::~ZoneShaderNodeRhi()
{
    releaseRhiResources();
}

QSGRenderNode::StateFlags ZoneShaderNodeRhi::changedStates() const
{
    return QSGRenderNode::ViewportState | QSGRenderNode::ScissorState;
}

QSGRenderNode::RenderingFlags ZoneShaderNodeRhi::flags() const
{
    return QSGRenderNode::BoundedRectRendering | QSGRenderNode::DepthAwareRendering
           | QSGRenderNode::OpaqueRendering | QSGRenderNode::NoExternalRendering;
}

QRectF ZoneShaderNodeRhi::rect() const
{
    if (m_item) {
        return QRectF(0, 0, m_item->width(), m_item->height());
    }
    return QRectF();
}

void ZoneShaderNodeRhi::prepare()
{
    if (!m_item || !m_item->window()) {
        return;
    }
    QRhi* rhi = m_item->window()->rhi();
    if (!rhi) {
        return;
    }
    QRhiCommandBuffer* cb = commandBuffer();
    QRhiRenderTarget* rt = renderTarget();
    if (!cb || !rt) {
        return;
    }

    if (!m_initialized) {
        m_initialized = true;
        // Create VBO (fullscreen quad)
        m_vbo.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(RhiConstants::QuadVertices)));
        if (!m_vbo->create()) {
            m_shaderError = QStringLiteral("Failed to create vertex buffer");
            return;
        }
        m_ubo.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(ZoneShaderUniforms)));
        if (!m_ubo->create()) {
            m_shaderError = QStringLiteral("Failed to create uniform buffer");
            return;
        }
    }

    if (m_shaderDirty) {
        m_shaderDirty = false;
        m_shaderReady = false;
        m_shaderError.clear();
        if (m_vertexShaderSource.isEmpty() || m_fragmentShaderSource.isEmpty()) {
            m_shaderError = QStringLiteral("Vertex or fragment shader source is empty");
            return;
        }
        // Desktop OpenGL 330 + OpenGL ES 300/310/320 (Qt may use ES on Linux/EGL).
        const QList<QShaderBaker::GeneratedShader> targets = {
            { QShader::GlslShader, QShaderVersion(330) },
            { QShader::GlslShader, QShaderVersion(300, QShaderVersion::GlslEs) },
            { QShader::GlslShader, QShaderVersion(310, QShaderVersion::GlslEs) },
            { QShader::GlslShader, QShaderVersion(320, QShaderVersion::GlslEs) },
        };
        QShaderBaker vertexBaker;
        vertexBaker.setGeneratedShaderVariants({ QShader::StandardShader });
        vertexBaker.setGeneratedShaders(targets);
        vertexBaker.setSourceString(m_vertexShaderSource.toUtf8(), QShader::VertexStage);
        m_vertexShader = vertexBaker.bake();
        if (!m_vertexShader.isValid()) {
            const QString msg = vertexBaker.errorMessage();
            m_shaderError = QStringLiteral("Vertex shader: ") + (msg.isEmpty() ? QStringLiteral("compilation failed (no details)") : msg);
            return;
        }
        QShaderBaker fragmentBaker;
        fragmentBaker.setGeneratedShaderVariants({ QShader::StandardShader });
        fragmentBaker.setGeneratedShaders(targets);
        fragmentBaker.setSourceString(m_fragmentShaderSource.toUtf8(), QShader::FragmentStage);
        m_fragmentShader = fragmentBaker.bake();
        if (!m_fragmentShader.isValid()) {
            const QString msg = fragmentBaker.errorMessage();
            m_shaderError = QStringLiteral("Fragment shader: ") + (msg.isEmpty() ? QStringLiteral("compilation failed (no details)") : msg);
            return;
        }
        m_shaderReady = true;
        m_pipeline.reset();
        m_srb.reset();
    }

    if (!m_shaderReady) {
        return;
    }

    if (!ensurePipeline()) {
        return;
    }

    if (m_uniformsDirty) {
        syncUniformsFromData();
        m_uniformsDirty = false;
    }

    QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
    if (batch) {
        batch->updateDynamicBuffer(m_ubo.get(), 0, sizeof(ZoneShaderUniforms), &m_uniforms);
        if (!m_vboUploaded) {
            batch->uploadStaticBuffer(m_vbo.get(), RhiConstants::QuadVertices);
            m_vboUploaded = true;
        }
        cb->resourceUpdate(batch);
    }
}

void ZoneShaderNodeRhi::render(const RenderState* state)
{
    Q_UNUSED(state)
    if (!m_shaderReady || !m_pipeline || !m_srb) {
        return;
    }
    QRhiCommandBuffer* cb = commandBuffer();
    QRhiRenderTarget* rt = renderTarget();
    if (!cb || !rt) {
        return;
    }

    QSize outputSize = rt->pixelSize();
    cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
    cb->setGraphicsPipeline(m_pipeline.get());
    cb->setShaderResources();
    QRhiCommandBuffer::VertexInput vbufBinding(m_vbo.get(), 0);
    cb->setVertexInput(0, 1, &vbufBinding);
    cb->draw(4);
}

void ZoneShaderNodeRhi::releaseResources()
{
    releaseRhiResources();
}

void ZoneShaderNodeRhi::setZones(const QVector<ZoneData>& zones)
{
    int count = qMin(zones.size(), MaxZones);
    m_zones = zones.mid(0, count);
    m_uniformsDirty = true;
}

void ZoneShaderNodeRhi::setZone(int index, const ZoneData& data)
{
    if (index >= 0 && index < MaxZones) {
        if (index >= m_zones.size()) {
            m_zones.resize(index + 1);
        }
        m_zones[index] = data;
        m_uniformsDirty = true;
    }
}

void ZoneShaderNodeRhi::setZoneCount(int count)
{
    if (count >= 0 && count <= MaxZones) {
        m_zones.resize(count);
        m_uniformsDirty = true;
    }
}

void ZoneShaderNodeRhi::setHighlightedZones(const QVector<int>& indices)
{
    m_highlightedIndices = indices;
    for (int i = 0; i < m_zones.size(); ++i) {
        m_zones[i].isHighlighted = indices.contains(i);
    }
    m_uniformsDirty = true;
}

void ZoneShaderNodeRhi::clearHighlights()
{
    m_highlightedIndices.clear();
    for (auto& zone : m_zones) {
        zone.isHighlighted = false;
    }
    m_uniformsDirty = true;
}

void ZoneShaderNodeRhi::setTime(float time) { m_time = time; m_uniformsDirty = true; }
void ZoneShaderNodeRhi::setTimeDelta(float delta) { m_timeDelta = delta; m_uniformsDirty = true; }
void ZoneShaderNodeRhi::setFrame(int frame) { m_frame = frame; m_uniformsDirty = true; }
void ZoneShaderNodeRhi::setResolution(float width, float height)
{
    if (m_width != width || m_height != height) {
        m_width = width;
        m_height = height;
        m_uniformsDirty = true;
    }
}
void ZoneShaderNodeRhi::setMousePosition(const QPointF& pos) { m_mousePosition = pos; m_uniformsDirty = true; }
void ZoneShaderNodeRhi::setCustomParams1(const QVector4D& params) { m_customParams1 = params; m_uniformsDirty = true; }
void ZoneShaderNodeRhi::setCustomParams2(const QVector4D& params) { m_customParams2 = params; m_uniformsDirty = true; }
void ZoneShaderNodeRhi::setCustomParams3(const QVector4D& params) { m_customParams3 = params; m_uniformsDirty = true; }
void ZoneShaderNodeRhi::setCustomParams4(const QVector4D& params) { m_customParams4 = params; m_uniformsDirty = true; }
void ZoneShaderNodeRhi::setCustomColor1(const QColor& color) { m_customColor1 = color; m_uniformsDirty = true; }
void ZoneShaderNodeRhi::setCustomColor2(const QColor& color) { m_customColor2 = color; m_uniformsDirty = true; }
void ZoneShaderNodeRhi::setCustomColor3(const QColor& color) { m_customColor3 = color; m_uniformsDirty = true; }
void ZoneShaderNodeRhi::setCustomColor4(const QColor& color) { m_customColor4 = color; m_uniformsDirty = true; }
void ZoneShaderNodeRhi::setCustomColor5(const QColor& color) { m_customColor5 = color; m_uniformsDirty = true; }
void ZoneShaderNodeRhi::setCustomColor6(const QColor& color) { m_customColor6 = color; m_uniformsDirty = true; }
void ZoneShaderNodeRhi::setCustomColor7(const QColor& color) { m_customColor7 = color; m_uniformsDirty = true; }
void ZoneShaderNodeRhi::setCustomColor8(const QColor& color) { m_customColor8 = color; m_uniformsDirty = true; }

bool ZoneShaderNodeRhi::loadVertexShader(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_shaderError = QStringLiteral("Failed to open vertex shader: ") + path;
        return false;
    }
    QString raw = QTextStream(&file).readAll();
    const QString currentFileDir = QFileInfo(path).absolutePath();
    const QString shadersRootDir = QFileInfo(currentFileDir).absolutePath();
    const QString systemShaderDir = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                                          QStringLiteral("plasmazones/shaders"),
                                                          QStandardPaths::LocateDirectory);
    QStringList includePaths{currentFileDir};
    if (!shadersRootDir.isEmpty() && shadersRootDir != currentFileDir) {
        includePaths.append(shadersRootDir);
    }
    if (!systemShaderDir.isEmpty() && !includePaths.contains(systemShaderDir)) {
        includePaths.append(systemShaderDir);
    }
    QString err;
    m_vertexShaderSource = ShaderIncludeResolver::expandIncludes(raw, currentFileDir, includePaths, &err);
    if (!err.isEmpty()) {
        m_shaderError = QStringLiteral("Vertex shader include: ") + err;
        return false;
    }
    m_shaderDirty = true;
    return true;
}

bool ZoneShaderNodeRhi::loadFragmentShader(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_shaderError = QStringLiteral("Failed to open fragment shader: ") + path;
        return false;
    }
    QString raw = QTextStream(&file).readAll();
    const QString currentFileDir = QFileInfo(path).absolutePath();
    const QString shadersRootDir = QFileInfo(currentFileDir).absolutePath();
    const QString systemShaderDir = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                                          QStringLiteral("plasmazones/shaders"),
                                                          QStandardPaths::LocateDirectory);
    QStringList includePaths{currentFileDir};
    if (!shadersRootDir.isEmpty() && shadersRootDir != currentFileDir) {
        includePaths.append(shadersRootDir);
    }
    if (!systemShaderDir.isEmpty() && !includePaths.contains(systemShaderDir)) {
        includePaths.append(systemShaderDir);
    }
    QString err;
    m_fragmentShaderSource = ShaderIncludeResolver::expandIncludes(raw, currentFileDir, includePaths, &err);
    if (!err.isEmpty()) {
        m_shaderError = QStringLiteral("Fragment shader include: ") + err;
        return false;
    }
    m_shaderDirty = true;
    return true;
}

void ZoneShaderNodeRhi::setVertexShaderSource(const QString& source)
{
    if (m_vertexShaderSource != source) {
        m_vertexShaderSource = source;
        m_shaderDirty = true;
    }
}

void ZoneShaderNodeRhi::setFragmentShaderSource(const QString& source)
{
    if (m_fragmentShaderSource != source) {
        m_fragmentShaderSource = source;
        m_shaderDirty = true;
    }
}

bool ZoneShaderNodeRhi::isShaderReady() const
{
    return m_shaderReady;
}

QString ZoneShaderNodeRhi::shaderError() const
{
    return m_shaderError;
}

void ZoneShaderNodeRhi::invalidateShader()
{
    m_shaderDirty = true;
}

void ZoneShaderNodeRhi::invalidateUniforms()
{
    m_uniformsDirty = true;
}

bool ZoneShaderNodeRhi::ensurePipeline()
{
    QRhi* rhi = m_item ? m_item->window() ? m_item->window()->rhi() : nullptr : nullptr;
    QRhiRenderTarget* rt = renderTarget();
    if (!rhi || !rt || !m_shaderReady) {
        return false;
    }

    QRhiRenderPassDescriptor* rpDesc = rt->renderPassDescriptor();
    if (!rpDesc) {
        return false;
    }

    QVector<quint32> format = rpDesc->serializedFormat();
    if (m_pipeline && m_renderPassFormat != format) {
        m_pipeline.reset();
        m_srb.reset();
    }
    m_renderPassFormat = format;

    if (!m_srb) {
        m_srb.reset(rhi->newShaderResourceBindings());
        m_srb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_ubo.get())
        });
        if (!m_srb->create()) {
            m_shaderError = QStringLiteral("Failed to create shader resource bindings");
            return false;
        }
    }

    if (!m_pipeline) {
        m_pipeline.reset(rhi->newGraphicsPipeline());
        m_pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        m_pipeline->setShaderStages({
            { QRhiShaderStage::Vertex, m_vertexShader },
            { QRhiShaderStage::Fragment, m_fragmentShader }
        });
        QRhiVertexInputLayout inputLayout;
        inputLayout.setBindings({ { 4 * sizeof(float) } });
        inputLayout.setAttributes({
            { 0, 0, QRhiVertexInputAttribute::Float2, 0 },
            { 0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float) }
        });
        m_pipeline->setVertexInputLayout(inputLayout);
        m_pipeline->setShaderResourceBindings(m_srb.get());
        m_pipeline->setRenderPassDescriptor(rpDesc);
        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        m_pipeline->setTargetBlends({ blend });
        if (!m_pipeline->create()) {
            m_shaderError = QStringLiteral("Failed to create graphics pipeline");
            m_pipeline.reset();
            return false;
        }
    }
    return true;
}

void ZoneShaderNodeRhi::syncUniformsFromData()
{
    m_uniforms.iTime = m_time;
    m_uniforms.iTimeDelta = m_timeDelta;
    m_uniforms.iFrame = m_frame;
    m_uniforms.iResolution[0] = m_width;
    m_uniforms.iResolution[1] = m_height;
    m_uniforms.iMouse[0] = static_cast<float>(m_mousePosition.x());
    m_uniforms.iMouse[1] = static_cast<float>(m_mousePosition.y());
    m_uniforms.iMouse[2] = m_width > 0 ? static_cast<float>(m_mousePosition.x() / m_width) : 0.0f;
    m_uniforms.iMouse[3] = m_height > 0 ? static_cast<float>(m_mousePosition.y() / m_height) : 0.0f;
    m_uniforms.zoneCount = m_zones.size();
    int highlightedCount = 0;
    for (const auto& zone : m_zones) {
        if (zone.isHighlighted) ++highlightedCount;
    }
    m_uniforms.highlightedCount = highlightedCount;

    m_uniforms.customParams[RhiConstants::UniformVecIndex1][RhiConstants::ComponentX] = m_customParams1.x();
    m_uniforms.customParams[RhiConstants::UniformVecIndex1][RhiConstants::ComponentY] = m_customParams1.y();
    m_uniforms.customParams[RhiConstants::UniformVecIndex1][RhiConstants::ComponentZ] = m_customParams1.z();
    m_uniforms.customParams[RhiConstants::UniformVecIndex1][RhiConstants::ComponentW] = m_customParams1.w();
    m_uniforms.customParams[RhiConstants::UniformVecIndex2][RhiConstants::ComponentX] = m_customParams2.x();
    m_uniforms.customParams[RhiConstants::UniformVecIndex2][RhiConstants::ComponentY] = m_customParams2.y();
    m_uniforms.customParams[RhiConstants::UniformVecIndex2][RhiConstants::ComponentZ] = m_customParams2.z();
    m_uniforms.customParams[RhiConstants::UniformVecIndex2][RhiConstants::ComponentW] = m_customParams2.w();
    m_uniforms.customParams[RhiConstants::UniformVecIndex3][RhiConstants::ComponentX] = m_customParams3.x();
    m_uniforms.customParams[RhiConstants::UniformVecIndex3][RhiConstants::ComponentY] = m_customParams3.y();
    m_uniforms.customParams[RhiConstants::UniformVecIndex3][RhiConstants::ComponentZ] = m_customParams3.z();
    m_uniforms.customParams[RhiConstants::UniformVecIndex3][RhiConstants::ComponentW] = m_customParams3.w();
    m_uniforms.customParams[RhiConstants::UniformVecIndex4][RhiConstants::ComponentX] = m_customParams4.x();
    m_uniforms.customParams[RhiConstants::UniformVecIndex4][RhiConstants::ComponentY] = m_customParams4.y();
    m_uniforms.customParams[RhiConstants::UniformVecIndex4][RhiConstants::ComponentZ] = m_customParams4.z();
    m_uniforms.customParams[RhiConstants::UniformVecIndex4][RhiConstants::ComponentW] = m_customParams4.w();

    auto setColor = [this](int idx, const QColor& c) {
        m_uniforms.customColors[idx][0] = static_cast<float>(c.redF());
        m_uniforms.customColors[idx][1] = static_cast<float>(c.greenF());
        m_uniforms.customColors[idx][2] = static_cast<float>(c.blueF());
        m_uniforms.customColors[idx][3] = static_cast<float>(c.alphaF());
    };
    setColor(0, m_customColor1);
    setColor(1, m_customColor2);
    setColor(2, m_customColor3);
    setColor(3, m_customColor4);
    setColor(4, m_customColor5);
    setColor(5, m_customColor6);
    setColor(6, m_customColor7);
    setColor(7, m_customColor8);

    for (int i = 0; i < MaxZones; ++i) {
        if (i < m_zones.size()) {
            const ZoneData& zone = m_zones[i];
            m_uniforms.zoneRects[i][0] = static_cast<float>(zone.rect.x());
            m_uniforms.zoneRects[i][1] = static_cast<float>(zone.rect.y());
            m_uniforms.zoneRects[i][2] = static_cast<float>(zone.rect.width());
            m_uniforms.zoneRects[i][3] = static_cast<float>(zone.rect.height());
            m_uniforms.zoneFillColors[i][0] = static_cast<float>(zone.fillColor.redF());
            m_uniforms.zoneFillColors[i][1] = static_cast<float>(zone.fillColor.greenF());
            m_uniforms.zoneFillColors[i][2] = static_cast<float>(zone.fillColor.blueF());
            m_uniforms.zoneFillColors[i][3] = static_cast<float>(zone.fillColor.alphaF());
            m_uniforms.zoneBorderColors[i][0] = static_cast<float>(zone.borderColor.redF());
            m_uniforms.zoneBorderColors[i][1] = static_cast<float>(zone.borderColor.greenF());
            m_uniforms.zoneBorderColors[i][2] = static_cast<float>(zone.borderColor.blueF());
            m_uniforms.zoneBorderColors[i][3] = static_cast<float>(zone.borderColor.alphaF());
            m_uniforms.zoneParams[i][0] = zone.borderRadius;
            m_uniforms.zoneParams[i][1] = zone.borderWidth;
            m_uniforms.zoneParams[i][2] = zone.isHighlighted ? 1.0f : 0.0f;
            m_uniforms.zoneParams[i][3] = static_cast<float>(zone.zoneNumber);
        } else {
            std::memset(m_uniforms.zoneRects[i], 0, sizeof(m_uniforms.zoneRects[i]));
            std::memset(m_uniforms.zoneFillColors[i], 0, sizeof(m_uniforms.zoneFillColors[i]));
            std::memset(m_uniforms.zoneBorderColors[i], 0, sizeof(m_uniforms.zoneBorderColors[i]));
            std::memset(m_uniforms.zoneParams[i], 0, sizeof(m_uniforms.zoneParams[i]));
        }
    }
}

void ZoneShaderNodeRhi::releaseRhiResources()
{
    m_pipeline.reset();
    m_srb.reset();
    m_ubo.reset();
    m_vbo.reset();
    m_vertexShader = QShader();
    m_fragmentShader = QShader();
    m_renderPassFormat.clear();
    m_initialized = false;
    m_vboUploaded = false;
    m_shaderReady = false;
    m_shaderDirty = true;  // Force re-bake on next prepare() after context loss
}

} // namespace PlasmaZones
