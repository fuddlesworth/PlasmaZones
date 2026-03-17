// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "previewcontroller.h"
#include "../core/shaderincluderesolver.h"
#include "../daemon/rendering/zonelabeltexturebuilder.h"
#include "shaderpackageio.h"

#include <QColor>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QUuid>

#include <KTextEditor/Document>

Q_LOGGING_CATEGORY(lcPreview, "plasmazones.shadereditor.preview")

namespace PlasmaZones {

PreviewController::PreviewController(QObject* parent)
    : QObject(parent)
{
    // Debounce timer for recompilation
    m_recompileTimer.setSingleShot(true);
    m_recompileTimer.setInterval(300);
    connect(&m_recompileTimer, &QTimer::timeout, this, &PreviewController::onRecompileTimerFired);

    // Animation timer (~60fps)
    m_animationTimer.setInterval(16);
    connect(&m_animationTimer, &QTimer::timeout, this, &PreviewController::onAnimationTimerFired);

    // FPS counter timer (1 second interval)
    m_fpsTimer.setInterval(1000);
    connect(&m_fpsTimer, &QTimer::timeout, this, [this]() {
        if (m_fps != m_fpsCounter) {
            m_fps = m_fpsCounter;
            Q_EMIT fpsChanged();
        }
        m_fpsCounter = 0;
    });

    // Zone layout names
    m_zoneLayoutNames = {
        QStringLiteral("Single Zone"),
        QStringLiteral("2-Column Split"),
        QStringLiteral("3-Column"),
        QStringLiteral("4-Grid"),
    };

    // Temp directory for expanded shader files
    if (!m_tempDir.isValid()) {
        qCWarning(lcPreview) << "Failed to create temporary directory for shader preview";
        return;
    }
    m_expandedFragPath = m_tempDir.filePath(QStringLiteral("effect.frag"));
    m_expandedVertPath = m_tempDir.filePath(QStringLiteral("zone.vert"));

    // Build initial zone layout
    buildZoneLayout();
}

PreviewController::~PreviewController() = default;

void PreviewController::setFragmentDocument(KTextEditor::Document* doc)
{
    if (m_fragDoc) {
        disconnect(m_fragDoc, &KTextEditor::Document::textChanged, this, &PreviewController::onDocumentTextChanged);
    }

    m_fragDoc = doc;

    if (m_fragDoc) {
        connect(m_fragDoc, &KTextEditor::Document::textChanged, this, &PreviewController::onDocumentTextChanged);
        // Trigger initial compilation
        m_recompileTimer.start();
    }
}

void PreviewController::setVertexDocument(KTextEditor::Document* doc)
{
    if (m_vertDoc) {
        disconnect(m_vertDoc, &KTextEditor::Document::textChanged, this, &PreviewController::onDocumentTextChanged);
    }

    m_vertDoc = doc;

    if (m_vertDoc) {
        connect(m_vertDoc, &KTextEditor::Document::textChanged, this, &PreviewController::onDocumentTextChanged);
        // Trigger recompilation with new vertex shader
        m_recompileTimer.start();
    }
}

void PreviewController::setShaderDirectory(const QString& dir)
{
    m_shaderDir = dir;
}

QUrl PreviewController::shaderSource() const
{
    return m_shaderSource;
}

QVariantList PreviewController::zones() const
{
    return m_zones;
}

QVariantMap PreviewController::shaderParams() const
{
    return m_shaderParams;
}

QImage PreviewController::labelsTexture() const
{
    return m_labelsTexture;
}

qreal PreviewController::iTime() const
{
    return m_iTime;
}

qreal PreviewController::iTimeDelta() const
{
    return m_iTimeDelta;
}

int PreviewController::iFrame() const
{
    return m_iFrame;
}

bool PreviewController::animating() const
{
    return m_animating;
}

QString PreviewController::errorLog() const
{
    return m_errorLog;
}

int PreviewController::status() const
{
    return m_status;
}

int PreviewController::fps() const
{
    return m_fps;
}

QString PreviewController::zoneLayoutName() const
{
    if (m_zoneLayoutIndex >= 0 && m_zoneLayoutIndex < m_zoneLayoutNames.size()) {
        return m_zoneLayoutNames.at(m_zoneLayoutIndex);
    }
    return QString();
}

void PreviewController::setAnimating(bool animating)
{
    if (m_animating == animating) {
        return;
    }
    m_animating = animating;

    if (m_animating) {
        m_clock.start();
        m_lastTime = 0.0;
        m_animationTimer.start();
        m_fpsTimer.start();
    } else {
        m_animationTimer.stop();
        m_fpsTimer.stop();
        if (m_fps != 0) {
            m_fps = 0;
            Q_EMIT fpsChanged();
        }
        m_fpsCounter = 0;
    }

    Q_EMIT animatingChanged();
}

void PreviewController::cycleZoneLayout()
{
    m_zoneLayoutIndex = (m_zoneLayoutIndex + 1) % ZoneLayoutCount;
    buildZoneLayout();
    Q_EMIT zoneLayoutNameChanged();

    // Recompile to update shader with new zone data
    if (m_fragDoc) {
        m_recompileTimer.start();
    }
}

void PreviewController::resetTime()
{
    m_iTime = 0.0;
    m_lastTime = 0.0;
    m_iFrame = 0;
    m_clock.restart();
    Q_EMIT iTimeChanged();
    Q_EMIT iTimeDeltaChanged();
    Q_EMIT iFrameChanged();
}

void PreviewController::recompile()
{
    writeExpandedShader();
}

void PreviewController::setShaderParams(const QVariantMap& params)
{
    if (m_shaderParams != params) {
        m_shaderParams = params;
        Q_EMIT shaderParamsChanged();
    }
}

void PreviewController::onShaderStatus(int status, const QString& error)
{
    // ZoneShaderItem's updatePaintNode retries broken shaders every frame.
    // Suppress pure duplicates (same status AND same error).
    if (m_status == status && m_errorLog == error) {
        return;
    }

    m_status = status;
    m_errorLog = error;
    Q_EMIT this->statusChanged();
    Q_EMIT errorLogChanged();

    if (status == 2) {
        m_compilePending = false;
        if (!m_animating) {
            setAnimating(true);
        }
    } else if (status == 3) {
        m_lastCompiledHash.clear();
        // Only stop animation if no new compile has been dispatched since this error.
        // A stale error from a previous compile's updatePaintNode must not stop
        // animation that was just started for the new compile.
        if (!m_compilePending && m_animating) {
            setAnimating(false);
        }
    }
}

void PreviewController::onDocumentTextChanged()
{
    m_recompileTimer.start();
}

void PreviewController::onRecompileTimerFired()
{
    writeExpandedShader();
}

void PreviewController::onAnimationTimerFired()
{
    if (!m_clock.isValid()) {
        m_clock.start();
    }

    const qreal now = m_clock.elapsed() / 1000.0;
    m_iTimeDelta = qMin(now - m_lastTime, 0.1);
    m_lastTime = now;
    m_iTime = now;
    m_iFrame = (m_iFrame + 1) % 1000000000;
    m_fpsCounter++;

    Q_EMIT iTimeChanged();
    Q_EMIT iTimeDeltaChanged();
    Q_EMIT iFrameChanged();
}

void PreviewController::writeExpandedShader()
{
    if (!m_fragDoc) {
        return;
    }

    // Build include paths for #include <...> resolution:
    // 1. The shader package dir itself (for local includes)
    // 2. The parent of the shader package dir (where common.glsl, audio.glsl live)
    // 3. The system shader directory (installed shared includes)
    QStringList includePaths;
    if (!m_shaderDir.isEmpty()) {
        includePaths.append(m_shaderDir);
        // Shared includes (common.glsl, audio.glsl, multipass.glsl) live in the
        // parent directory of each shader package
        QDir parentDir(m_shaderDir);
        if (parentDir.cdUp()) {
            includePaths.append(parentDir.absolutePath());
        }
    }
    const QString systemDir = ShaderPackageIO::systemShaderDirectory();
    if (!systemDir.isEmpty()) {
        includePaths.append(systemDir);
    }

    // The current file directory for relative includes: use shader dir if set, else temp dir
    const QString currentFileDir = m_shaderDir.isEmpty() ? m_tempDir.path() : m_shaderDir;

    // Skip recompile if source hasn't changed
    const QString fragSource = m_fragDoc->text();
    const QString vertSource = m_vertDoc ? m_vertDoc->text() : QString();
    const QByteArray sourceHash = QCryptographicHash::hash(
        (fragSource + vertSource).toUtf8(), QCryptographicHash::Md5);
    if (sourceHash == m_lastCompiledHash) {
        return;
    }

    // Expand fragment shader includes
    QString fragError;
    const QString expandedFrag = ShaderIncludeResolver::expandIncludes(fragSource, currentFileDir, includePaths, &fragError);

    if (expandedFrag.isNull()) {
        m_errorLog = fragError.isEmpty() ? QStringLiteral("Fragment shader include expansion failed") : fragError;
        Q_EMIT errorLogChanged();
        m_status = 3; // Error
        Q_EMIT statusChanged();
        qCWarning(lcPreview) << "Fragment include expansion failed:" << m_errorLog;
        return;
    }

    // Write expanded fragment shader
    {
        QFile file(m_expandedFragPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            qCWarning(lcPreview) << "Failed to write expanded fragment shader:" << file.errorString();
            return;
        }
        file.write(expandedFrag.toUtf8());
        file.flush();
    }

    // Expand and write vertex shader (if document is available)
    if (m_vertDoc) {
        const QString vertSource = m_vertDoc->text();
        QString vertError;
        const QString expandedVert =
            ShaderIncludeResolver::expandIncludes(vertSource, currentFileDir, includePaths, &vertError);

        if (expandedVert.isNull()) {
            m_errorLog = vertError.isEmpty() ? QStringLiteral("Vertex shader include expansion failed") : vertError;
            Q_EMIT errorLogChanged();
            m_status = 3; // Error
            Q_EMIT statusChanged();
            qCWarning(lcPreview) << "Vertex include expansion failed:" << m_errorLog;
            return;
        }

        QFile file(m_expandedVertPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            qCWarning(lcPreview) << "Failed to write expanded vertex shader:" << file.errorString();
            return;
        }
        file.write(expandedVert.toUtf8());
        file.flush();
    }

    // Clear previous error
    if (!m_errorLog.isEmpty()) {
        m_errorLog.clear();
        Q_EMIT errorLogChanged();
    }

    // Set shader source — ZoneShaderItem will pick up zone.vert from the same directory.
    // ZoneShaderItem skips setShaderSource if the URL is unchanged, so we append a
    // query parameter with a revision counter to force reload on every recompile.
    const QUrl newSource = QUrl::fromLocalFile(m_expandedFragPath);
    QUrl uniqueSource = newSource;
    uniqueSource.setQuery(QStringLiteral("rev=%1").arg(++m_shaderRevision));
    m_shaderSource = uniqueSource;
    m_lastCompiledHash = sourceHash;

    // Mark compile pending so stale error callbacks from the previous compile
    // don't stop animation before the new compile has a chance to succeed.
    m_compilePending = true;

    // Start animation BEFORE emitting shaderSourceChanged so that
    // updatePaintNode runs on the next frame to compile the new shader.
    if (!m_animating) {
        setAnimating(true);
    }

    Q_EMIT shaderSourceChanged();
}

void PreviewController::loadDefaultParamsFromMetadata(const QString& metadataJson)
{
    QVariantMap uniforms;

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(metadataJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcPreview) << "Failed to parse metadata for default params:" << parseError.errorString();
        return;
    }

    const QJsonArray params = doc.object().value(QStringLiteral("parameters")).toArray();
    for (const QJsonValue& paramVal : params) {
        const QJsonObject param = paramVal.toObject();
        const QString type = param.value(QStringLiteral("type")).toString();
        const int slot = param.value(QStringLiteral("slot")).toInt(-1);
        const QJsonValue defaultVal = param.value(QStringLiteral("default"));

        if (slot < 0) {
            continue;
        }

        if (type == QLatin1String("color") && slot < 16) {
            // Color: convert hex to "r,g,b,a" format expected by setShaderParams
            const QColor color(defaultVal.toString());
            if (color.isValid()) {
                const QString uniformName = QStringLiteral("customColor%1").arg(slot + 1);
                uniforms[uniformName] = color.name(QColor::HexArgb);
            }
        } else if ((type == QLatin1String("float") || type == QLatin1String("int") || type == QLatin1String("bool"))
                   && slot < 32) {
            // Float/int/bool: packed into customParams vec4s
            const int vecIndex = slot / 4;
            const int component = slot % 4;
            static const char* components[] = {"x", "y", "z", "w"};
            const QString uniformName =
                QStringLiteral("customParams%1_%2").arg(vecIndex + 1).arg(QLatin1String(components[component]));

            if (type == QLatin1String("bool")) {
                uniforms[uniformName] = defaultVal.toBool() ? 1.0 : 0.0;
            } else {
                uniforms[uniformName] = defaultVal.toDouble();
            }
        }
    }

    if (uniforms != m_shaderParams) {
        m_shaderParams = uniforms;
        Q_EMIT shaderParamsChanged();
        qCDebug(lcPreview) << "Loaded" << uniforms.size() << "default shader params from metadata";
    }
}

void PreviewController::setPreviewSize(int width, int height)
{
    constexpr int minSize = 20;
    if (width < minSize || height < minSize) {
        return;
    }
    if (m_previewWidth == width && m_previewHeight == height) {
        return;
    }
    m_previewWidth = width;
    m_previewHeight = height;
    buildZoneLayout();
}

void PreviewController::buildZoneLayout()
{
    m_zones.clear();

    // Use actual preview widget dimensions so zones normalize correctly
    // against iResolution (which is bound to the ZoneShaderItem's pixel size)
    const qreal W = m_previewWidth;
    const qreal H = m_previewHeight;
    constexpr qreal gap = 4.0;

    auto makeZone = [](int zoneNumber, qreal x, qreal y, qreal w, qreal h) -> QVariantMap {
        QVariantMap zone;
        zone[QStringLiteral("id")] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        zone[QStringLiteral("x")] = x;
        zone[QStringLiteral("y")] = y;
        zone[QStringLiteral("width")] = w;
        zone[QStringLiteral("height")] = h;
        zone[QStringLiteral("zoneNumber")] = zoneNumber;
        zone[QStringLiteral("isHighlighted")] = false;
        // Fill: semi-transparent blue
        zone[QStringLiteral("fillR")] = 100.0 / 255.0;
        zone[QStringLiteral("fillG")] = 100.0 / 255.0;
        zone[QStringLiteral("fillB")] = 255.0 / 255.0;
        zone[QStringLiteral("fillA")] = 0.3;
        // Border: opaque blue
        zone[QStringLiteral("borderR")] = 100.0 / 255.0;
        zone[QStringLiteral("borderG")] = 100.0 / 255.0;
        zone[QStringLiteral("borderB")] = 255.0 / 255.0;
        zone[QStringLiteral("borderA")] = 0.8;
        zone[QStringLiteral("shaderBorderRadius")] = 8.0;
        zone[QStringLiteral("shaderBorderWidth")] = 2.0;
        return zone;
    };

    switch (m_zoneLayoutIndex) {
    case 0: // Single zone
        m_zones.append(makeZone(1, gap, gap, W - 2 * gap, H - 2 * gap));
        break;

    case 1: { // 2-column split
        const qreal colW = (W - 3 * gap) / 2.0;
        m_zones.append(makeZone(1, gap, gap, colW, H - 2 * gap));
        m_zones.append(makeZone(2, 2 * gap + colW, gap, colW, H - 2 * gap));
        break;
    }

    case 2: { // 3-column
        const qreal colW = (W - 4 * gap) / 3.0;
        for (int i = 0; i < 3; ++i) {
            const qreal x = gap + i * (colW + gap);
            m_zones.append(makeZone(i + 1, x, gap, colW, H - 2 * gap));
        }
        break;
    }

    case 3: { // 4-grid (2x2)
        const qreal cellW = (W - 3 * gap) / 2.0;
        const qreal cellH = (H - 3 * gap) / 2.0;
        int num = 1;
        for (int row = 0; row < 2; ++row) {
            for (int col = 0; col < 2; ++col) {
                const qreal x = gap + col * (cellW + gap);
                const qreal y = gap + row * (cellH + gap);
                m_zones.append(makeZone(num++, x, y, cellW, cellH));
            }
        }
        break;
    }

    default:
        m_zones.append(makeZone(1, gap, gap, W - 2 * gap, H - 2 * gap));
        break;
    }

    buildLabelsTexture();
    Q_EMIT zonesChanged();
}

void PreviewController::buildLabelsTexture()
{
    m_labelsTexture = ZoneLabelTextureBuilder::build(m_zones, QSize(m_previewWidth, m_previewHeight), Qt::white, true);
    Q_EMIT labelsTextureChanged();
}

} // namespace PlasmaZones
