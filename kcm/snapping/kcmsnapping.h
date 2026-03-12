// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KQuickConfigModule>
#include <QColor>
#include <functional>
#include <memory>

namespace PlasmaZones {

class ScreenHelper;
class Settings;

/**
 * @brief Snapping sub-KCM -- Zone snapping appearance, behavior, and zone selector settings
 */
class KCMSnapping : public KQuickConfigModule
{
    Q_OBJECT

    // Activation
    Q_PROPERTY(QVariantList dragActivationTriggers READ dragActivationTriggers WRITE setDragActivationTriggers NOTIFY
                   dragActivationTriggersChanged)
    Q_PROPERTY(QVariantList defaultDragActivationTriggers READ defaultDragActivationTriggers CONSTANT)
    Q_PROPERTY(bool alwaysActivateOnDrag READ alwaysActivateOnDrag WRITE setAlwaysActivateOnDrag NOTIFY
                   alwaysActivateOnDragChanged)
    Q_PROPERTY(bool toggleActivation READ toggleActivation WRITE setToggleActivation NOTIFY toggleActivationChanged)
    Q_PROPERTY(bool snappingEnabled READ snappingEnabled WRITE setSnappingEnabled NOTIFY snappingEnabledChanged)
    Q_PROPERTY(bool zoneSpanEnabled READ zoneSpanEnabled WRITE setZoneSpanEnabled NOTIFY zoneSpanEnabledChanged)
    Q_PROPERTY(
        QVariantList zoneSpanTriggers READ zoneSpanTriggers WRITE setZoneSpanTriggers NOTIFY zoneSpanTriggersChanged)
    Q_PROPERTY(QVariantList defaultZoneSpanTriggers READ defaultZoneSpanTriggers CONSTANT)

    // Snap Assist
    Q_PROPERTY(bool snapAssistFeatureEnabled READ snapAssistFeatureEnabled WRITE setSnapAssistFeatureEnabled NOTIFY
                   snapAssistFeatureEnabledChanged)
    Q_PROPERTY(bool snapAssistEnabled READ snapAssistEnabled WRITE setSnapAssistEnabled NOTIFY snapAssistEnabledChanged)
    Q_PROPERTY(QVariantList snapAssistTriggers READ snapAssistTriggers WRITE setSnapAssistTriggers NOTIFY
                   snapAssistTriggersChanged)
    Q_PROPERTY(QVariantList defaultSnapAssistTriggers READ defaultSnapAssistTriggers CONSTANT)

    // Display / Behavior
    Q_PROPERTY(bool showZonesOnAllMonitors READ showZonesOnAllMonitors WRITE setShowZonesOnAllMonitors NOTIFY
                   showZonesOnAllMonitorsChanged)
    Q_PROPERTY(bool showZoneNumbers READ showZoneNumbers WRITE setShowZoneNumbers NOTIFY showZoneNumbersChanged)
    Q_PROPERTY(
        bool flashZonesOnSwitch READ flashZonesOnSwitch WRITE setFlashZonesOnSwitch NOTIFY flashZonesOnSwitchChanged)
    Q_PROPERTY(bool keepWindowsInZonesOnResolutionChange READ keepWindowsInZonesOnResolutionChange WRITE
                   setKeepWindowsInZonesOnResolutionChange NOTIFY keepWindowsInZonesOnResolutionChangeChanged)
    Q_PROPERTY(bool moveNewWindowsToLastZone READ moveNewWindowsToLastZone WRITE setMoveNewWindowsToLastZone NOTIFY
                   moveNewWindowsToLastZoneChanged)
    Q_PROPERTY(bool restoreOriginalSizeOnUnsnap READ restoreOriginalSizeOnUnsnap WRITE setRestoreOriginalSizeOnUnsnap
                   NOTIFY restoreOriginalSizeOnUnsnapChanged)
    Q_PROPERTY(int stickyWindowHandling READ stickyWindowHandling WRITE setStickyWindowHandling NOTIFY
                   stickyWindowHandlingChanged)
    Q_PROPERTY(bool restoreWindowsToZonesOnLogin READ restoreWindowsToZonesOnLogin WRITE setRestoreWindowsToZonesOnLogin
                   NOTIFY restoreWindowsToZonesOnLoginChanged)

    // Appearance
    Q_PROPERTY(bool useSystemColors READ useSystemColors WRITE setUseSystemColors NOTIFY useSystemColorsChanged)
    Q_PROPERTY(QColor highlightColor READ highlightColor WRITE setHighlightColor NOTIFY highlightColorChanged)
    Q_PROPERTY(QColor inactiveColor READ inactiveColor WRITE setInactiveColor NOTIFY inactiveColorChanged)
    Q_PROPERTY(QColor borderColor READ borderColor WRITE setBorderColor NOTIFY borderColorChanged)
    Q_PROPERTY(QColor labelFontColor READ labelFontColor WRITE setLabelFontColor NOTIFY labelFontColorChanged)
    Q_PROPERTY(qreal activeOpacity READ activeOpacity WRITE setActiveOpacity NOTIFY activeOpacityChanged)
    Q_PROPERTY(qreal inactiveOpacity READ inactiveOpacity WRITE setInactiveOpacity NOTIFY inactiveOpacityChanged)
    Q_PROPERTY(int borderWidth READ borderWidth WRITE setBorderWidth NOTIFY borderWidthChanged)
    Q_PROPERTY(int borderRadius READ borderRadius WRITE setBorderRadius NOTIFY borderRadiusChanged)
    Q_PROPERTY(bool enableBlur READ enableBlur WRITE setEnableBlur NOTIFY enableBlurChanged)
    Q_PROPERTY(QString labelFontFamily READ labelFontFamily WRITE setLabelFontFamily NOTIFY labelFontFamilyChanged)
    Q_PROPERTY(
        qreal labelFontSizeScale READ labelFontSizeScale WRITE setLabelFontSizeScale NOTIFY labelFontSizeScaleChanged)
    Q_PROPERTY(int labelFontWeight READ labelFontWeight WRITE setLabelFontWeight NOTIFY labelFontWeightChanged)
    Q_PROPERTY(bool labelFontItalic READ labelFontItalic WRITE setLabelFontItalic NOTIFY labelFontItalicChanged)
    Q_PROPERTY(
        bool labelFontUnderline READ labelFontUnderline WRITE setLabelFontUnderline NOTIFY labelFontUnderlineChanged)
    Q_PROPERTY(
        bool labelFontStrikeout READ labelFontStrikeout WRITE setLabelFontStrikeout NOTIFY labelFontStrikeoutChanged)

    // Shader Effects
    Q_PROPERTY(bool enableShaderEffects READ enableShaderEffects WRITE setEnableShaderEffects NOTIFY
                   enableShaderEffectsChanged)
    Q_PROPERTY(int shaderFrameRate READ shaderFrameRate WRITE setShaderFrameRate NOTIFY shaderFrameRateChanged)
    Q_PROPERTY(bool enableAudioVisualizer READ enableAudioVisualizer WRITE setEnableAudioVisualizer NOTIFY
                   enableAudioVisualizerChanged)
    Q_PROPERTY(bool cavaAvailable READ cavaAvailable CONSTANT)
    Q_PROPERTY(int audioSpectrumBarCount READ audioSpectrumBarCount WRITE setAudioSpectrumBarCount NOTIFY
                   audioSpectrumBarCountChanged)

    // Zones / Gaps
    Q_PROPERTY(int zonePadding READ zonePadding WRITE setZonePadding NOTIFY zonePaddingChanged)
    Q_PROPERTY(int outerGap READ outerGap WRITE setOuterGap NOTIFY outerGapChanged)
    Q_PROPERTY(
        bool usePerSideOuterGap READ usePerSideOuterGap WRITE setUsePerSideOuterGap NOTIFY usePerSideOuterGapChanged)
    Q_PROPERTY(int outerGapTop READ outerGapTop WRITE setOuterGapTop NOTIFY outerGapTopChanged)
    Q_PROPERTY(int outerGapBottom READ outerGapBottom WRITE setOuterGapBottom NOTIFY outerGapBottomChanged)
    Q_PROPERTY(int outerGapLeft READ outerGapLeft WRITE setOuterGapLeft NOTIFY outerGapLeftChanged)
    Q_PROPERTY(int outerGapRight READ outerGapRight WRITE setOuterGapRight NOTIFY outerGapRightChanged)
    Q_PROPERTY(int adjacentThreshold READ adjacentThreshold WRITE setAdjacentThreshold NOTIFY adjacentThresholdChanged)

    // Zone Selector
    Q_PROPERTY(bool zoneSelectorEnabled READ zoneSelectorEnabled WRITE setZoneSelectorEnabled NOTIFY
                   zoneSelectorEnabledChanged)
    Q_PROPERTY(int zoneSelectorTriggerDistance READ zoneSelectorTriggerDistance WRITE setZoneSelectorTriggerDistance
                   NOTIFY zoneSelectorTriggerDistanceChanged)
    Q_PROPERTY(int zoneSelectorPosition READ zoneSelectorPosition WRITE setZoneSelectorPosition NOTIFY
                   zoneSelectorPositionChanged)
    Q_PROPERTY(int zoneSelectorLayoutMode READ zoneSelectorLayoutMode WRITE setZoneSelectorLayoutMode NOTIFY
                   zoneSelectorLayoutModeChanged)
    Q_PROPERTY(int zoneSelectorPreviewWidth READ zoneSelectorPreviewWidth WRITE setZoneSelectorPreviewWidth NOTIFY
                   zoneSelectorPreviewWidthChanged)
    Q_PROPERTY(int zoneSelectorPreviewHeight READ zoneSelectorPreviewHeight WRITE setZoneSelectorPreviewHeight NOTIFY
                   zoneSelectorPreviewHeightChanged)
    Q_PROPERTY(bool zoneSelectorPreviewLockAspect READ zoneSelectorPreviewLockAspect WRITE
                   setZoneSelectorPreviewLockAspect NOTIFY zoneSelectorPreviewLockAspectChanged)
    Q_PROPERTY(int zoneSelectorGridColumns READ zoneSelectorGridColumns WRITE setZoneSelectorGridColumns NOTIFY
                   zoneSelectorGridColumnsChanged)
    Q_PROPERTY(int zoneSelectorSizeMode READ zoneSelectorSizeMode WRITE setZoneSelectorSizeMode NOTIFY
                   zoneSelectorSizeModeChanged)
    Q_PROPERTY(
        int zoneSelectorMaxRows READ zoneSelectorMaxRows WRITE setZoneSelectorMaxRows NOTIFY zoneSelectorMaxRowsChanged)

    // Screens
    Q_PROPERTY(QVariantList screens READ screens NOTIFY screensChanged)

public:
    KCMSnapping(QObject* parent, const KPluginMetaData& data);
    ~KCMSnapping() override = default;

    // Activation
    QVariantList dragActivationTriggers() const;
    QVariantList defaultDragActivationTriggers() const;
    bool alwaysActivateOnDrag() const;
    bool toggleActivation() const;
    bool snappingEnabled() const;
    bool zoneSpanEnabled() const;
    QVariantList zoneSpanTriggers() const;
    QVariantList defaultZoneSpanTriggers() const;

    void setDragActivationTriggers(const QVariantList& triggers);
    void setAlwaysActivateOnDrag(bool enabled);
    void setToggleActivation(bool enable);
    void setSnappingEnabled(bool enabled);
    void setZoneSpanEnabled(bool enabled);
    void setZoneSpanTriggers(const QVariantList& triggers);

    // Snap Assist
    bool snapAssistFeatureEnabled() const;
    bool snapAssistEnabled() const;
    QVariantList snapAssistTriggers() const;
    QVariantList defaultSnapAssistTriggers() const;

    void setSnapAssistFeatureEnabled(bool enabled);
    void setSnapAssistEnabled(bool enabled);
    void setSnapAssistTriggers(const QVariantList& triggers);

    // Display / Behavior
    bool showZonesOnAllMonitors() const;
    bool showZoneNumbers() const;
    bool flashZonesOnSwitch() const;
    bool keepWindowsInZonesOnResolutionChange() const;
    bool moveNewWindowsToLastZone() const;
    bool restoreOriginalSizeOnUnsnap() const;
    int stickyWindowHandling() const;
    bool restoreWindowsToZonesOnLogin() const;

    void setShowZonesOnAllMonitors(bool show);
    void setShowZoneNumbers(bool show);
    void setFlashZonesOnSwitch(bool flash);
    void setKeepWindowsInZonesOnResolutionChange(bool keep);
    void setMoveNewWindowsToLastZone(bool move);
    void setRestoreOriginalSizeOnUnsnap(bool restore);
    void setStickyWindowHandling(int handling);
    void setRestoreWindowsToZonesOnLogin(bool restore);

    // Appearance
    bool useSystemColors() const;
    QColor highlightColor() const;
    QColor inactiveColor() const;
    QColor borderColor() const;
    QColor labelFontColor() const;
    qreal activeOpacity() const;
    qreal inactiveOpacity() const;
    int borderWidth() const;
    int borderRadius() const;
    bool enableBlur() const;
    QString labelFontFamily() const;
    qreal labelFontSizeScale() const;
    int labelFontWeight() const;
    bool labelFontItalic() const;
    bool labelFontUnderline() const;
    bool labelFontStrikeout() const;

    void setUseSystemColors(bool use);
    void setHighlightColor(const QColor& color);
    void setInactiveColor(const QColor& color);
    void setBorderColor(const QColor& color);
    void setLabelFontColor(const QColor& color);
    void setActiveOpacity(qreal opacity);
    void setInactiveOpacity(qreal opacity);
    void setBorderWidth(int width);
    void setBorderRadius(int radius);
    void setEnableBlur(bool enable);
    void setLabelFontFamily(const QString& family);
    void setLabelFontSizeScale(qreal scale);
    void setLabelFontWeight(int weight);
    void setLabelFontItalic(bool italic);
    void setLabelFontUnderline(bool underline);
    void setLabelFontStrikeout(bool strikeout);

    // Shader Effects
    bool enableShaderEffects() const;
    int shaderFrameRate() const;
    bool enableAudioVisualizer() const;
    bool cavaAvailable() const;
    int audioSpectrumBarCount() const;

    void setEnableShaderEffects(bool enable);
    void setShaderFrameRate(int fps);
    void setEnableAudioVisualizer(bool enable);
    void setAudioSpectrumBarCount(int count);

    // Zones / Gaps
    int zonePadding() const;
    int outerGap() const;
    bool usePerSideOuterGap() const;
    int outerGapTop() const;
    int outerGapBottom() const;
    int outerGapLeft() const;
    int outerGapRight() const;
    int adjacentThreshold() const;

    void setZonePadding(int padding);
    void setOuterGap(int gap);
    void setUsePerSideOuterGap(bool enabled);
    void setOuterGapTop(int gap);
    void setOuterGapBottom(int gap);
    void setOuterGapLeft(int gap);
    void setOuterGapRight(int gap);
    void setAdjacentThreshold(int threshold);

    // Zone Selector
    bool zoneSelectorEnabled() const;
    int zoneSelectorTriggerDistance() const;
    int zoneSelectorPosition() const;
    int zoneSelectorLayoutMode() const;
    int zoneSelectorPreviewWidth() const;
    int zoneSelectorPreviewHeight() const;
    bool zoneSelectorPreviewLockAspect() const;
    int zoneSelectorGridColumns() const;
    int zoneSelectorSizeMode() const;
    int zoneSelectorMaxRows() const;

    void setZoneSelectorEnabled(bool enabled);
    void setZoneSelectorTriggerDistance(int distance);
    void setZoneSelectorPosition(int position);
    void setZoneSelectorLayoutMode(int mode);
    void setZoneSelectorPreviewWidth(int width);
    void setZoneSelectorPreviewHeight(int height);
    void setZoneSelectorPreviewLockAspect(bool locked);
    void setZoneSelectorGridColumns(int columns);
    void setZoneSelectorSizeMode(int mode);
    void setZoneSelectorMaxRows(int rows);

    // Screens
    QVariantList screens() const;

    // Q_INVOKABLE methods
    Q_INVOKABLE QStringList fontStylesForFamily(const QString& family) const;
    Q_INVOKABLE int fontStyleWeight(const QString& family, const QString& style) const;
    Q_INVOKABLE bool fontStyleItalic(const QString& family, const QString& style) const;
    Q_INVOKABLE void loadColorsFromPywal();
    Q_INVOKABLE void loadColorsFromFile(const QString& filePath);

    // Per-screen snapping
    Q_INVOKABLE QVariantMap getPerScreenSnappingSettings(const QString& screenName) const;
    Q_INVOKABLE void setPerScreenSnappingSetting(const QString& screenName, const QString& key, const QVariant& value);
    Q_INVOKABLE void clearPerScreenSnappingSettings(const QString& screenName);
    Q_INVOKABLE bool hasPerScreenSnappingSettings(const QString& screenName) const;

    // Per-screen zone selector
    Q_INVOKABLE QVariantMap getPerScreenZoneSelectorSettings(const QString& screenName) const;
    Q_INVOKABLE void setPerScreenZoneSelectorSetting(const QString& screenName, const QString& key,
                                                     const QVariant& value);
    Q_INVOKABLE void clearPerScreenZoneSelectorSettings(const QString& screenName);
    Q_INVOKABLE bool hasPerScreenZoneSelectorSettings(const QString& screenName) const;

    // Monitor disable
    Q_INVOKABLE bool isMonitorDisabled(const QString& screenName) const;
    Q_INVOKABLE void setMonitorDisabled(const QString& screenName, bool disabled);

public Q_SLOTS:
    void load() override;
    void save() override;
    void defaults() override;
    void refreshScreens();
    void onExternalSettingsChanged();

Q_SIGNALS:
    // Activation
    void dragActivationTriggersChanged();
    void alwaysActivateOnDragChanged();
    void toggleActivationChanged();
    void snappingEnabledChanged();
    void zoneSpanEnabledChanged();
    void zoneSpanTriggersChanged();

    // Snap Assist
    void snapAssistFeatureEnabledChanged();
    void snapAssistEnabledChanged();
    void snapAssistTriggersChanged();

    // Display / Behavior
    void showZonesOnAllMonitorsChanged();
    void showZoneNumbersChanged();
    void flashZonesOnSwitchChanged();
    void keepWindowsInZonesOnResolutionChangeChanged();
    void moveNewWindowsToLastZoneChanged();
    void restoreOriginalSizeOnUnsnapChanged();
    void stickyWindowHandlingChanged();
    void restoreWindowsToZonesOnLoginChanged();

    // Appearance
    void useSystemColorsChanged();
    void highlightColorChanged();
    void inactiveColorChanged();
    void borderColorChanged();
    void labelFontColorChanged();
    void activeOpacityChanged();
    void inactiveOpacityChanged();
    void borderWidthChanged();
    void borderRadiusChanged();
    void enableBlurChanged();
    void labelFontFamilyChanged();
    void labelFontSizeScaleChanged();
    void labelFontWeightChanged();
    void labelFontItalicChanged();
    void labelFontUnderlineChanged();
    void labelFontStrikeoutChanged();

    // Shader Effects
    void enableShaderEffectsChanged();
    void shaderFrameRateChanged();
    void enableAudioVisualizerChanged();
    void audioSpectrumBarCountChanged();

    // Zones / Gaps
    void zonePaddingChanged();
    void outerGapChanged();
    void usePerSideOuterGapChanged();
    void outerGapTopChanged();
    void outerGapBottomChanged();
    void outerGapLeftChanged();
    void outerGapRightChanged();
    void adjacentThresholdChanged();

    // Zone Selector
    void zoneSelectorEnabledChanged();
    void zoneSelectorTriggerDistanceChanged();
    void zoneSelectorPositionChanged();
    void zoneSelectorLayoutModeChanged();
    void zoneSelectorPreviewWidthChanged();
    void zoneSelectorPreviewHeightChanged();
    void zoneSelectorPreviewLockAspectChanged();
    void zoneSelectorGridColumnsChanged();
    void zoneSelectorSizeModeChanged();
    void zoneSelectorMaxRowsChanged();

    // Screens
    void screensChanged();

    // Color import
    void colorImportError(const QString& error);
    void colorImportSuccess();

private:
    void emitAllChanged();
    void emitColorChanged();

    static QVariantList convertTriggersForQml(const QVariantList& triggers);
    static QVariantList convertTriggersForStorage(const QVariantList& triggers);

    Settings* m_settings = nullptr;
    std::unique_ptr<ScreenHelper> m_screenHelper;
    bool m_saving = false;
};

} // namespace PlasmaZones
