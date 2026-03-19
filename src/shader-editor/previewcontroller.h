// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QElapsedTimer>
#include <QFontDatabase>
#include <array>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace KTextEditor {
class Document;
}

namespace PlasmaZones {

class CavaService;

class PreviewController : public QObject
{
    Q_OBJECT

    // Properties exposed to QML
    Q_PROPERTY(QUrl shaderSource READ shaderSource NOTIFY shaderSourceChanged)
    Q_PROPERTY(QVariantList zones READ zones NOTIFY zonesChanged)
    Q_PROPERTY(QVariantMap shaderParams READ shaderParams NOTIFY shaderParamsChanged)
    Q_PROPERTY(QImage labelsTexture READ labelsTexture NOTIFY labelsTextureChanged)
    Q_PROPERTY(bool hasLabelsTexture READ hasLabelsTexture NOTIFY labelsTextureChanged)
    Q_PROPERTY(qreal iTime READ iTime NOTIFY iTimeChanged)
    Q_PROPERTY(qreal iTimeDelta READ iTimeDelta NOTIFY iTimeDeltaChanged)
    Q_PROPERTY(int iFrame READ iFrame NOTIFY iFrameChanged)
    Q_PROPERTY(bool animating READ animating WRITE setAnimating NOTIFY animatingChanged)
    Q_PROPERTY(QString errorLog READ errorLog NOTIFY errorLogChanged)
    Q_PROPERTY(int status READ status NOTIFY statusChanged)
    Q_PROPERTY(int fps READ fps NOTIFY fpsChanged)
    Q_PROPERTY(QString zoneLayoutName READ zoneLayoutName NOTIFY zoneLayoutNameChanged)
    Q_PROPERTY(QString fixedFontFamily READ fixedFontFamily CONSTANT)
    Q_PROPERTY(bool showLabels READ showLabels WRITE setShowLabels NOTIFY showLabelsChanged)
    Q_PROPERTY(bool audioEnabled READ audioEnabled WRITE setAudioEnabled NOTIFY audioEnabledChanged)
    Q_PROPERTY(int audioBarCount READ audioBarCount WRITE setAudioBarCount NOTIFY audioBarCountChanged)
    Q_PROPERTY(bool audioLive READ audioLive WRITE setAudioLive NOTIFY audioLiveChanged)
    Q_PROPERTY(bool cavaAvailable READ cavaAvailable CONSTANT)
    Q_PROPERTY(QVariant audioSpectrum READ audioSpectrum NOTIFY audioSpectrumChanged)
    Q_PROPERTY(QPointF mousePos READ mousePos WRITE setMousePos NOTIFY mousePosChanged)
    Q_PROPERTY(int hoveredZoneIndex READ hoveredZoneIndex NOTIFY hoveredZoneIndexChanged)

    // Multipass rendering
    Q_PROPERTY(QStringList bufferShaderPaths READ bufferShaderPaths NOTIFY bufferShaderPathsChanged)
    Q_PROPERTY(bool bufferFeedback READ bufferFeedback NOTIFY bufferFeedbackChanged)
    Q_PROPERTY(qreal bufferScale READ bufferScale NOTIFY bufferScaleChanged)
    Q_PROPERTY(QString bufferWrap READ bufferWrap NOTIFY bufferWrapChanged)

    // Wallpaper texture
    Q_PROPERTY(QImage wallpaperTexture READ wallpaperTexture NOTIFY wallpaperTextureChanged)
    Q_PROPERTY(bool useWallpaper READ useWallpaper NOTIFY useWallpaperChanged)

public:
    // ZoneShaderItem::Status values (avoids header dependency in shader-editor)
    static constexpr int StatusNull = 0;
    static constexpr int StatusLoading = 1;
    static constexpr int StatusReady = 2;
    static constexpr int StatusError = 3;

    explicit PreviewController(QObject* parent = nullptr);
    ~PreviewController() override;

    void setFragmentDocument(KTextEditor::Document* doc);
    void setVertexDocument(KTextEditor::Document* doc);
    void setBufferDocument(const QString& filename, KTextEditor::Document* doc);
    void setShaderDirectory(const QString& dir);
    void updateMultipassConfig(const QString& metadataJson);

    QUrl shaderSource() const;
    QVariantList zones() const;
    QVariantMap shaderParams() const;
    QImage labelsTexture() const;
    bool hasLabelsTexture() const { return m_showLabels && !m_labelsTexture.isNull(); }
    qreal iTime() const;
    qreal iTimeDelta() const;
    int iFrame() const;
    bool animating() const;
    QString errorLog() const;
    int status() const;
    int fps() const;
    QString zoneLayoutName() const;
    QString fixedFontFamily() const { return QFontDatabase::systemFont(QFontDatabase::FixedFont).family(); }
    bool showLabels() const { return m_showLabels; }
    bool audioEnabled() const { return m_audioEnabled; }
    int audioBarCount() const { return m_audioBarCount; }
    bool audioLive() const { return m_audioLive; }
    bool cavaAvailable() const;
    QStringList userTexturePaths() const;
    QVariant audioSpectrum() const { return m_audioEnabled ? m_audioSpectrum : QVariant(); }
    QPointF mousePos() const { return m_mousePos; }
    int hoveredZoneIndex() const { return m_hoveredZoneIndex; }

    QStringList bufferShaderPaths() const { return m_bufferShaderPaths; }
    bool bufferFeedback() const { return m_bufferFeedback; }
    qreal bufferScale() const { return m_bufferScale; }
    QString bufferWrap() const { return m_bufferWrap; }

    QImage wallpaperTexture() const { return m_wallpaperTexture; }
    bool useWallpaper() const { return m_useWallpaper; }

    void setAnimating(bool animating);
    void setShowLabels(bool show);
    void setAudioEnabled(bool enabled);
    void setAudioBarCount(int count);
    void setAudioLive(bool live);
    void setMousePos(const QPointF& pos);

    Q_INVOKABLE void cycleZoneLayout();
    Q_INVOKABLE void setUserTexture(int slot, const QUrl& fileUrl);
    Q_INVOKABLE void clearUserTexture(int slot);

    Q_PROPERTY(QStringList userTexturePaths READ userTexturePaths NOTIFY userTexturePathsChanged)
    Q_INVOKABLE void resetTime();
    Q_INVOKABLE void recompile();

    int zoneLayoutIndex() const { return m_zoneLayoutIndex; }
    void setZoneLayoutIndex(int index);
    QStringList zoneLayoutNames() const { return m_zoneLayoutNames; }
    Q_INVOKABLE void setPreviewSize(int width, int height);
    void loadDefaultParamsFromMetadata(const QString& metadataJson);
    void setShaderParams(const QVariantMap& params);

    // Called from QML when ZoneShaderItem status/error changes
    Q_INVOKABLE void onShaderStatus(int status, const QString& error);

Q_SIGNALS:
    void shaderSourceChanged();
    void zonesChanged();
    void shaderParamsChanged();
    void labelsTextureChanged();
    void iTimeChanged();
    void iTimeDeltaChanged();
    void iFrameChanged();
    void animatingChanged();
    void errorLogChanged();
    void statusChanged();
    void fpsChanged();
    void zoneLayoutNameChanged();
    void showLabelsChanged();
    void audioEnabledChanged();
    void audioBarCountChanged();
    void audioLiveChanged();
    void userTexturePathsChanged();
    void audioSpectrumChanged();
    void mousePosChanged();
    void hoveredZoneIndexChanged();
    void bufferShaderPathsChanged();
    void bufferFeedbackChanged();
    void bufferScaleChanged();
    void bufferWrapChanged();
    void wallpaperTextureChanged();
    void useWallpaperChanged();

private Q_SLOTS:
    void onDocumentTextChanged();
    void onRecompileTimerFired();
    void onAnimationTimerFired();

private:
    void buildZoneLayout();
    void buildLabelsTexture();
    void writeExpandedShader();
    void loadWallpaperTexture();
    void ensureCavaService();

    QPointer<KTextEditor::Document> m_fragDoc;
    QPointer<KTextEditor::Document> m_vertDoc;
    QMap<QString, QPointer<KTextEditor::Document>> m_bufferDocs;
    QStringList m_bufferShaderOrder; // filenames in execution order from metadata
    QStringList m_bufferShaderPaths; // expanded temp-dir paths
    bool m_bufferFeedback = false;
    qreal m_bufferScale = 1.0;
    QString m_bufferWrap = QStringLiteral("clamp");
    QImage m_wallpaperTexture;
    bool m_useWallpaper = false;
    QString m_shaderDir; // for #include resolution

    QTimer m_recompileTimer; // 300ms debounce
    QTimer m_animationTimer; // ~60fps
    QElapsedTimer m_clock;
    qreal m_lastTime = 0.0;

    QUrl m_shaderSource;
    int m_shaderRevision = 0;
    QVariantList m_zones;
    QVariantMap m_shaderParams;
    QImage m_labelsTexture;
    qreal m_iTime = 0.0;
    qreal m_iTimeDelta = 0.0;
    qreal m_timeOffset = 0.0; // accumulated time before last pause
    int m_iFrame = 0;
    bool m_animating = false;
    QString m_errorLog;
    int m_status = StatusNull;
    int m_fps = 0;
    int m_fpsCounter = 0;
    QTimer m_fpsTimer;

    QTemporaryDir m_tempDir; // temp dir for expanded shader files
    QString m_expandedFragPath; // path in m_tempDir
    QString m_expandedVertPath;
    QByteArray m_lastCompiledHash; // hash of last compiled frag+vert source to skip redundant recompiles
    bool m_compilePending = false; // true between dispatching new source and receiving first Ready/Error

    int m_zoneLayoutIndex = 1; // default: 2-column split (matches mockup)
    static constexpr int ZoneLayoutCount = 4;
    QStringList m_zoneLayoutNames;
    int m_previewWidth = 800;
    int m_previewHeight = 600;

    bool m_showLabels = true;
    bool m_audioEnabled = false;
    int m_audioBarCount = 64;
    bool m_audioLive = true; // default to real audio when CAVA is available
    CavaService* m_cavaService = nullptr;
    QVariant m_audioSpectrum;
    QPointF m_mousePos;
    std::array<QString, 4> m_userTexturePaths;
    int m_hoveredZoneIndex = -1;
    QTimer m_audioTimer; // generates test audio data when enabled
};

} // namespace PlasmaZones
