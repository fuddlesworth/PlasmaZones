// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

namespace KTextEditor {
class Document;
}

namespace PlasmaZones {

class PreviewController : public QObject
{
    Q_OBJECT

    // Properties exposed to QML
    Q_PROPERTY(QUrl shaderSource READ shaderSource NOTIFY shaderSourceChanged)
    Q_PROPERTY(QVariantList zones READ zones NOTIFY zonesChanged)
    Q_PROPERTY(QVariantMap shaderParams READ shaderParams NOTIFY shaderParamsChanged)
    Q_PROPERTY(QImage labelsTexture READ labelsTexture NOTIFY labelsTextureChanged)
    Q_PROPERTY(qreal iTime READ iTime NOTIFY iTimeChanged)
    Q_PROPERTY(qreal iTimeDelta READ iTimeDelta NOTIFY iTimeDeltaChanged)
    Q_PROPERTY(int iFrame READ iFrame NOTIFY iFrameChanged)
    Q_PROPERTY(bool animating READ animating WRITE setAnimating NOTIFY animatingChanged)
    Q_PROPERTY(QString errorLog READ errorLog NOTIFY errorLogChanged)
    Q_PROPERTY(int status READ status NOTIFY statusChanged)
    Q_PROPERTY(int fps READ fps NOTIFY fpsChanged)
    Q_PROPERTY(QString zoneLayoutName READ zoneLayoutName NOTIFY zoneLayoutNameChanged)

public:
    explicit PreviewController(QObject* parent = nullptr);
    ~PreviewController() override;

    void setFragmentDocument(KTextEditor::Document* doc);
    void setVertexDocument(KTextEditor::Document* doc);
    void setShaderDirectory(const QString& dir);

    QUrl shaderSource() const;
    QVariantList zones() const;
    QVariantMap shaderParams() const;
    QImage labelsTexture() const;
    qreal iTime() const;
    qreal iTimeDelta() const;
    int iFrame() const;
    bool animating() const;
    QString errorLog() const;
    int status() const;
    int fps() const;
    QString zoneLayoutName() const;

    void setAnimating(bool animating);

    Q_INVOKABLE void cycleZoneLayout();
    Q_INVOKABLE void resetTime();
    Q_INVOKABLE void recompile();
    Q_INVOKABLE void setPreviewSize(int width, int height);
    void loadDefaultParamsFromMetadata(const QString& metadataJson);

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

private Q_SLOTS:
    void onDocumentTextChanged();
    void onRecompileTimerFired();
    void onAnimationTimerFired();

private:
    void buildZoneLayout();
    void buildLabelsTexture();
    void writeExpandedShader();

    KTextEditor::Document* m_fragDoc = nullptr;
    KTextEditor::Document* m_vertDoc = nullptr;
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
    int m_iFrame = 0;
    bool m_animating = false;
    QString m_errorLog;
    int m_status = 0; // ZoneShaderItem::Status::Null
    int m_fps = 0;
    int m_fpsCounter = 0;
    QTimer m_fpsTimer;

    QTemporaryDir m_tempDir; // temp dir for expanded shader files
    QString m_expandedFragPath; // path in m_tempDir
    QString m_expandedVertPath;

    int m_zoneLayoutIndex = 0;
    static constexpr int ZoneLayoutCount = 4;
    QStringList m_zoneLayoutNames;
    int m_previewWidth = 800;
    int m_previewHeight = 600;
};

} // namespace PlasmaZones
