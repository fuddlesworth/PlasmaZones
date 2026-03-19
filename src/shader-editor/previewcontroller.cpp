// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "previewcontroller.h"
#include "../core/shaderincluderesolver.h"
#include "../core/shaderregistry.h"
#include "../daemon/cavaservice.h"
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
#include <QStandardPaths>
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

    // Audio test spectrum timer — generates synthetic "music-like" data when audio is enabled.
    // Simulates bass kicks, mid-range variation, and treble sparkle to look like real audio.
    connect(&m_audioTimer, &QTimer::timeout, this, [this]() {
        const int barCount = m_audioBarCount;
        QList<float> spectrum(barCount);
        const qreal t = m_iTime;

        // Simulated bass kick (bars 0-7): periodic thumps
        const float kick = static_cast<float>(std::max(0.0, std::sin(t * 4.2)) * 0.8);
        const float kickDecay = static_cast<float>(std::max(0.0, 1.0 - std::fmod(t * 2.1, 1.0) * 2.5));
        const float bassLevel = kick * kickDecay;

        for (int i = 0; i < barCount; ++i) {
            const float freq = static_cast<float>(i) / std::max(barCount - 1.0f, 1.0f); // 0=bass, 1=treble
            float val = 0.0f;

            if (freq < 0.25f) {
                // Bass: kick-driven with slow wobble
                val = bassLevel * (1.0f - freq * 3.0f)
                    + 0.15f * static_cast<float>(0.5 + 0.5 * std::sin(t * 1.7 + i * 0.3));
            } else if (freq < 0.65f) {
                // Mids: rhythmic variation, multiple overlapping patterns
                val = 0.2f + 0.4f * static_cast<float>(
                    0.5 + 0.5 * std::sin(t * 5.3 + i * 0.8))
                    * static_cast<float>(0.5 + 0.5 * std::sin(t * 3.1 + i * 1.3));
                val += bassLevel * 0.15f; // bass leaks into mids
            } else {
                // Treble: fast flickering sparkle
                val = 0.1f + 0.3f * static_cast<float>(
                    std::max(0.0, std::sin(t * 11.0 + i * 2.1) * std::sin(t * 7.3 + i * 1.7)));
                val += 0.1f * static_cast<float>(0.5 + 0.5 * std::sin(t * 13.7 + i * 3.1));
            }

            spectrum[i] = std::clamp(val, 0.0f, 1.0f);
        }

        QVariantList vl;
        vl.reserve(barCount);
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

    // Clear buffer docs when switching to a new fragment (new package load).
    // Buffer docs from the previous package are about to be deleted.
    for (auto it = m_bufferDocs.begin(); it != m_bufferDocs.end(); ++it) {
        if (it.value()) {
            disconnect(it.value(), &KTextEditor::Document::textChanged, this, &PreviewController::onDocumentTextChanged);
        }
    }
    m_bufferDocs.clear();

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

void PreviewController::setBufferDocument(const QString& filename, KTextEditor::Document* doc)
{
    // Disconnect old document if replacing
    if (m_bufferDocs.contains(filename)) {
        KTextEditor::Document* oldDoc = m_bufferDocs.value(filename);
        if (oldDoc) {
            disconnect(oldDoc, &KTextEditor::Document::textChanged, this, &PreviewController::onDocumentTextChanged);
        }
    }

    if (doc) {
        m_bufferDocs[filename] = doc;
        connect(doc, &KTextEditor::Document::textChanged, this, &PreviewController::onDocumentTextChanged);
        m_recompileTimer.start();
    } else {
        m_bufferDocs.remove(filename);
    }
}

void PreviewController::updateMultipassConfig(const QString& metadataJson)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(metadataJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return;
    }

    const QJsonObject root = doc.object();

    // Parse wallpaper flag (independent of multipass)
    const bool newUseWallpaper = root.value(QStringLiteral("wallpaper")).toBool(false);
    if (newUseWallpaper != m_useWallpaper) {
        m_useWallpaper = newUseWallpaper;
        Q_EMIT useWallpaperChanged();

        if (m_useWallpaper && m_wallpaperTexture.isNull()) {
            loadWallpaperTexture();
        }
    }

    // Check if multipass is explicitly disabled
    if (root.contains(QStringLiteral("multipass")) && !root.value(QStringLiteral("multipass")).toBool()) {
        bool changed = false;
        if (!m_bufferShaderOrder.isEmpty()) { m_bufferShaderOrder.clear(); changed = true; }
        if (!m_bufferShaderPaths.isEmpty()) { m_bufferShaderPaths.clear(); changed = true; Q_EMIT bufferShaderPathsChanged(); }
        if (m_bufferFeedback) { m_bufferFeedback = false; Q_EMIT bufferFeedbackChanged(); }
        if (m_bufferScale != 1.0) { m_bufferScale = 1.0; Q_EMIT bufferScaleChanged(); }
        if (m_bufferWrap != QLatin1String("clamp")) { m_bufferWrap = QStringLiteral("clamp"); Q_EMIT bufferWrapChanged(); }
        if (changed) m_recompileTimer.start();
        return;
    }

    bool needsRecompile = false;

    // Parse bufferShaders array (ordered filenames)
    const QJsonArray bufferArray = root.value(QStringLiteral("bufferShaders")).toArray();
    QStringList newOrder;
    newOrder.reserve(bufferArray.size());
    for (const QJsonValue& v : bufferArray) {
        newOrder.append(v.toString());
    }
    if (newOrder != m_bufferShaderOrder) {
        m_bufferShaderOrder = newOrder;
        needsRecompile = true;
    }

    // Parse bufferFeedback
    const bool newFeedback = root.value(QStringLiteral("bufferFeedback")).toBool(false);
    if (newFeedback != m_bufferFeedback) {
        m_bufferFeedback = newFeedback;
        Q_EMIT bufferFeedbackChanged();
    }

    // Parse bufferScale
    const qreal newScale = root.value(QStringLiteral("bufferScale")).toDouble(1.0);
    if (!qFuzzyCompare(1.0 + newScale, 1.0 + m_bufferScale)) {
        m_bufferScale = newScale;
        Q_EMIT bufferScaleChanged();
    }

    // Parse bufferWrap
    const QString newWrap = root.value(QStringLiteral("bufferWrap")).toString(QStringLiteral("clamp"));
    if (newWrap != m_bufferWrap) {
        m_bufferWrap = newWrap;
        Q_EMIT bufferWrapChanged();
    }

    if (needsRecompile) {
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
        // Resume from current iTime rather than resetting to 0.
        // Restart the elapsed clock and adjust so the first frame
        // picks up where we left off (m_iTime is preserved across pause).
        m_clock.start();
        m_lastTime = 0.0;
        m_timeOffset = m_iTime;
        m_animationTimer.start();
        m_fpsTimer.start();
        if (m_audioEnabled && !m_audioLive) m_audioTimer.start(33);
        if (m_audioEnabled && m_audioLive && m_cavaService) m_cavaService->start();
    } else {
        m_animationTimer.stop();
        m_fpsTimer.stop();
        m_audioTimer.stop();
        if (m_cavaService) m_cavaService->stop();
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

void PreviewController::setUserTexture(int slot, const QUrl& fileUrl)
{
    if (slot < 0 || slot >= 4) return;
    const QString path = fileUrl.toLocalFile();
    if (path.isEmpty() || !QFileInfo::exists(path)) return;
    if (m_userTexturePaths[slot] == path) return;
    m_userTexturePaths[slot] = path;
    const QString key = QStringLiteral("uTexture%1").arg(slot);
    m_shaderParams[key] = path;
    Q_EMIT userTexturePathsChanged();
    Q_EMIT shaderParamsChanged();
}

void PreviewController::clearUserTexture(int slot)
{
    if (slot < 0 || slot >= 4) return;
    if (m_userTexturePaths[slot].isEmpty()) return;
    m_userTexturePaths[slot].clear();
    const QString key = QStringLiteral("uTexture%1").arg(slot);
    m_shaderParams.remove(key);
    Q_EMIT userTexturePathsChanged();
    Q_EMIT shaderParamsChanged();
}

QStringList PreviewController::userTexturePaths() const
{
    return {m_userTexturePaths[0], m_userTexturePaths[1],
            m_userTexturePaths[2], m_userTexturePaths[3]};
}

void PreviewController::resetTime()
{
    m_iTime = 0.0;
    m_lastTime = 0.0;
    m_timeOffset = 0.0;
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
        if (m_audioLive) {
            // Real audio via CAVA
            ensureCavaService();
            m_cavaService->setBarCount(m_audioBarCount);
            m_cavaService->start();
        } else {
            // Synthetic test audio at ~30Hz
            m_audioTimer.start(33);
        }
    } else {
        m_audioTimer.stop();
        if (m_cavaService) {
            m_cavaService->stop();
        }
        m_audioSpectrum = QVariant();
        Q_EMIT audioSpectrumChanged();
    }
    Q_EMIT audioEnabledChanged();
}

void PreviewController::setAudioBarCount(int count)
{
    count = qBound(8, count, 256);
    // Stereo only — force even (CAVA outputs L/R interleaved pairs)
    count = count & ~1;
    if (m_audioBarCount == count) return;
    m_audioBarCount = count;
    // CavaService::setBarCount() internally calls restartAsync() if running
    if (m_cavaService) {
        m_cavaService->setBarCount(count);
    }
    Q_EMIT audioBarCountChanged();
}

void PreviewController::ensureCavaService()
{
    if (m_cavaService) return;
    m_cavaService = new CavaService(this);
    connect(m_cavaService, &CavaService::spectrumUpdated, this, [this](const QVector<float>& spectrum) {
        QVariantList vl;
        vl.reserve(spectrum.size());
        for (float v : spectrum) {
            vl.append(v);
        }
        m_audioSpectrum = QVariant::fromValue(vl);
        Q_EMIT audioSpectrumChanged();
    });
}

bool PreviewController::cavaAvailable() const
{
    return CavaService::isAvailable();
}

void PreviewController::setAudioLive(bool live)
{
    if (m_audioLive == live) return;
    m_audioLive = live;

    if (live && m_audioEnabled) {
        // Switch to real audio: stop synthetic timer, start CAVA
        m_audioTimer.stop();
        ensureCavaService();
        m_cavaService->setBarCount(m_audioBarCount);
        m_cavaService->start();
    } else if (!live && m_audioEnabled) {
        // Switch to synthetic: stop CAVA, start synthetic timer
        if (m_cavaService) {
            m_cavaService->stop();
        }
        if (m_animating) {
            m_audioTimer.start(33);
        }
    }

    Q_EMIT audioLiveChanged();
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
    m_shaderParams = params;
    // Re-inject user texture paths so they survive parameter resets
    for (int i = 0; i < 4; ++i) {
        if (!m_userTexturePaths[i].isEmpty()) {
            m_shaderParams[QStringLiteral("uTexture%1").arg(i)] = m_userTexturePaths[i];
        }
    }
    Q_EMIT shaderParamsChanged();
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
    m_iTime = m_timeOffset + now;
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
    // 4. The user shader directory (for user-installed shared includes)
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
    // Add ALL standard shader directories as include paths so that shared
    // includes (common.glsl, audio.glsl, etc.) are found regardless of
    // whether the user dir or system dir is checked first.
    const QStringList shaderDirs = QStandardPaths::locateAll(
        QStandardPaths::GenericDataLocation,
        QStringLiteral("plasmazones/shaders"),
        QStandardPaths::LocateDirectory);
    for (const QString& dir : shaderDirs) {
        if (!includePaths.contains(dir)) {
            includePaths.append(dir);
        }
    }

    // The current file directory for relative includes: use shader dir if set, else temp dir
    const QString currentFileDir = m_shaderDir.isEmpty() ? m_tempDir.path() : m_shaderDir;

    // Skip recompile if source hasn't changed (include buffer docs in hash)
    const QString fragSource = m_fragDoc->text();
    const QString vertSource = m_vertDoc ? m_vertDoc->text() : QString();
    QCryptographicHash hasher(QCryptographicHash::Md5);
    hasher.addData((fragSource + vertSource).toUtf8());
    for (const QString& bufName : m_bufferShaderOrder) {
        KTextEditor::Document* bufDoc = m_bufferDocs.value(bufName);
        if (bufDoc) {
            hasher.addData(bufDoc->text().toUtf8());
        }
    }
    const QByteArray sourceHash = hasher.result();
    if (sourceHash == m_lastCompiledHash) {
        return;
    }

    // Expand fragment shader includes
    QString fragError;
    const QString expandedFrag = ShaderIncludeResolver::expandIncludes(
        fragSource, currentFileDir, includePaths, &fragError, QStringLiteral("effect.frag"));

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
        const QString expandedVert = ShaderIncludeResolver::expandIncludes(
            vertSource, currentFileDir, includePaths, &vertError, QStringLiteral("zone.vert"));

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

    // Expand and write buffer pass shaders (in metadata-declared order)
    QStringList newBufferPaths;
    for (const QString& bufName : m_bufferShaderOrder) {
        KTextEditor::Document* bufDoc = m_bufferDocs.value(bufName);
        if (!bufDoc) {
            continue;
        }

        QString bufError;
        const QString expandedBuf = ShaderIncludeResolver::expandIncludes(
            bufDoc->text(), currentFileDir, includePaths, &bufError, bufName);

        if (expandedBuf.isNull()) {
            m_errorLog = bufError.isEmpty()
                ? QStringLiteral("Buffer pass %1 include expansion failed").arg(bufName)
                : bufError;
            Q_EMIT errorLogChanged();
            m_status = StatusError;
            Q_EMIT statusChanged();
            qCWarning(lcPreview) << "Buffer pass include expansion failed:" << bufName << m_errorLog;
            return;
        }

        const QString bufPath = m_tempDir.filePath(bufName);
        QFile bufFile(bufPath);
        if (!bufFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            qCWarning(lcPreview) << "Failed to write buffer pass:" << bufFile.errorString();
            return;
        }
        bufFile.write(expandedBuf.toUtf8());
        bufFile.flush();
        newBufferPaths.append(QFileInfo(bufPath).absoluteFilePath());
    }

    if (newBufferPaths != m_bufferShaderPaths) {
        m_bufferShaderPaths = newBufferPaths;
        Q_EMIT bufferShaderPathsChanged();
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

void PreviewController::loadWallpaperTexture()
{
    const QImage image = ShaderRegistry::loadWallpaperImage();
    if (image.isNull()) {
        qCDebug(lcPreview) << "No desktop wallpaper available for preview";
        return;
    }

    m_wallpaperTexture = image;
    Q_EMIT wallpaperTextureChanged();
    qCDebug(lcPreview) << "Loaded wallpaper texture:" << image.size();
}

} // namespace PlasmaZones
