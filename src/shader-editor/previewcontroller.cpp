// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "previewcontroller.h"
#include "../core/shaderincluderesolver.h"
#include "../daemon/rendering/zonelabeltexturebuilder.h"
#include "shaderpackageio.h"

#include <cmath>

#include <QColor>
#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QPalette>
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

    // Audio test spectrum timer — generates synthetic data when audio is enabled
    connect(&m_audioTimer, &QTimer::timeout, this, [this]() {
        QList<float> spectrum(32);
        const qreal t = m_iTime;
        for (int i = 0; i < 32; ++i) {
            spectrum[i] = static_cast<float>(0.5 + 0.5 * std::sin(t * 3.0 + i * 0.4));
        }
        QVariantList vl;
        vl.reserve(32);
        for (float v : spectrum) {
            vl.append(v);
        }
        m_audioSpectrum = QVariant::fromValue(vl);
        Q_EMIT audioSpectrumChanged();
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
    return m_showLabels ? m_labelsTexture : QImage();
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
        if (m_audioEnabled) m_audioTimer.start(33);
    } else {
        m_animationTimer.stop();
        m_fpsTimer.stop();
        m_audioTimer.stop();
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
    setZoneLayoutIndex((m_zoneLayoutIndex + 1) % ZoneLayoutCount);
}

void PreviewController::setZoneLayoutIndex(int index)
{
    if (index < 0 || index >= ZoneLayoutCount || index == m_zoneLayoutIndex) {
        return;
    }
    m_zoneLayoutIndex = index;
    buildZoneLayout();
    Q_EMIT zoneLayoutNameChanged();

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

void PreviewController::setShowLabels(bool show)
{
    if (m_showLabels == show) return;
    m_showLabels = show;
    Q_EMIT showLabelsChanged();
    Q_EMIT labelsTextureChanged();
}

void PreviewController::setAudioEnabled(bool enabled)
{
    if (m_audioEnabled == enabled) return;
    m_audioEnabled = enabled;

    if (enabled && m_animating) {
        // Generate synthetic test audio spectrum at ~30Hz
        m_audioTimer.start(33);
    } else {
        m_audioTimer.stop();
        m_audioSpectrum = QVariant();
        Q_EMIT audioSpectrumChanged();
    }
    Q_EMIT audioEnabledChanged();
}

void PreviewController::setMousePos(const QPointF& pos)
{
    if (m_mousePos == pos) return;
    m_mousePos = pos;
    Q_EMIT mousePosChanged();

    // Hit-test zones to find which one the mouse is over
    int newIndex = -1;
    if (pos.x() >= 0 && pos.y() >= 0) {
        for (int i = 0; i < m_zones.size(); ++i) {
            const QVariantMap z = m_zones.at(i).toMap();
            const qreal zx = z.value(QStringLiteral("x")).toDouble();
            const qreal zy = z.value(QStringLiteral("y")).toDouble();
            const qreal zw = z.value(QStringLiteral("width")).toDouble();
            const qreal zh = z.value(QStringLiteral("height")).toDouble();
            if (pos.x() >= zx && pos.x() < zx + zw && pos.y() >= zy && pos.y() < zy + zh) {
                newIndex = i;
                break;
            }
        }
    }
    if (m_hoveredZoneIndex != newIndex) {
        m_hoveredZoneIndex = newIndex;
        Q_EMIT hoveredZoneIndexChanged();
    }
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

    if (status == StatusReady) {
        m_compilePending = false;
        if (!m_animating) {
            setAnimating(true);
        }
    } else if (status == StatusError) {
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
        m_status = StatusError;
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
        QString vertError;
        const QString expandedVert =
            ShaderIncludeResolver::expandIncludes(vertSource, currentFileDir, includePaths, &vertError);

        if (expandedVert.isNull()) {
            m_errorLog = vertError.isEmpty() ? QStringLiteral("Vertex shader include expansion failed") : vertError;
            Q_EMIT errorLogChanged();
            m_status = StatusError;
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

        const QString uniformName = ShaderPackageIO::computeUniformName(type, slot);
        if (uniformName.isEmpty()) {
            continue;
        }

        if (type == QLatin1String("color")) {
            const QColor color(defaultVal.toString());
            if (color.isValid()) {
                uniforms[uniformName] = color.name(QColor::HexArgb);
            }
        } else if (type == QLatin1String("bool")) {
            uniforms[uniformName] = defaultVal.toBool() ? 1.0 : 0.0;
        } else {
            uniforms[uniformName] = defaultVal.toDouble();
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

    if (m_hoveredZoneIndex != -1) {
        m_hoveredZoneIndex = -1;
        Q_EMIT hoveredZoneIndexChanged();
    }

    const qreal W = m_previewWidth;
    const qreal H = m_previewHeight;
    constexpr qreal gap = 6.0;

    // The ZoneShaderItem fills the entire QQuickWidget; the header bar (28px)
    // and info bar (20px) render on top. Position zones between them with padding.
    constexpr qreal headerH = 28.0;
    constexpr qreal infoBarH = 24.0;
    constexpr qreal pad = 4.0;
    const qreal monX = pad;
    const qreal monY = headerH + pad;
    const qreal monW = W - 2 * pad;
    const qreal monH = H - headerH - infoBarH - 2 * pad;

    // Derive zone colors from the system palette highlight color
    const QPalette pal = QGuiApplication::palette();
    const QColor highlight = pal.color(QPalette::Highlight);

    auto makeZone = [&highlight](int zoneNumber, qreal x, qreal y, qreal w, qreal h) -> QVariantMap {
        QVariantMap zone;
        zone[QStringLiteral("id")] = QUuid::createUuid().toString();
        zone[QStringLiteral("x")] = x;
        zone[QStringLiteral("y")] = y;
        zone[QStringLiteral("width")] = w;
        zone[QStringLiteral("height")] = h;
        zone[QStringLiteral("zoneNumber")] = zoneNumber;
        zone[QStringLiteral("isHighlighted")] = false;
        // Fill: very transparent tint of highlight (shader content shows through)
        zone[QStringLiteral("fillR")] = highlight.redF();
        zone[QStringLiteral("fillG")] = highlight.greenF();
        zone[QStringLiteral("fillB")] = highlight.blueF();
        zone[QStringLiteral("fillA")] = 0.15;
        // Border: semi-transparent highlight accent
        zone[QStringLiteral("borderR")] = highlight.redF();
        zone[QStringLiteral("borderG")] = highlight.greenF();
        zone[QStringLiteral("borderB")] = highlight.blueF();
        zone[QStringLiteral("borderA")] = 0.5;
        zone[QStringLiteral("shaderBorderRadius")] = 8.0;
        zone[QStringLiteral("shaderBorderWidth")] = 1.5;
        return zone;
    };

    switch (m_zoneLayoutIndex) {
    case 0: // Single zone
        m_zones.append(makeZone(1, monX, monY, monW, monH));
        break;

    case 1: { // 2-column split (default — matches mockup)
        const qreal colW = (monW - gap) / 2.0;
        m_zones.append(makeZone(1, monX, monY, colW, monH));
        m_zones.append(makeZone(2, monX + colW + gap, monY, colW, monH));
        break;
    }

    case 2: { // 3-column
        const qreal colW = (monW - 2 * gap) / 3.0;
        for (int i = 0; i < 3; ++i) {
            m_zones.append(makeZone(i + 1, monX + i * (colW + gap), monY, colW, monH));
        }
        break;
    }

    case 3: { // 4-grid (2x2)
        const qreal cellW = (monW - gap) / 2.0;
        const qreal cellH = (monH - gap) / 2.0;
        int num = 1;
        for (int row = 0; row < 2; ++row) {
            for (int col = 0; col < 2; ++col) {
                m_zones.append(makeZone(num++, monX + col * (cellW + gap), monY + row * (cellH + gap), cellW, cellH));
            }
        }
        break;
    }

    default:
        m_zones.append(makeZone(1, monX, monY, monW, monH));
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
