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
 * @brief Autotiling sub-KCM -- Tiling algorithms, gaps, and behavior settings
 */
class KCMAutotiling : public KQuickConfigModule
{
    Q_OBJECT

    // Enable
    Q_PROPERTY(bool autotileEnabled READ autotileEnabled WRITE setAutotileEnabled NOTIFY autotileEnabledChanged)

    // Algorithm
    Q_PROPERTY(
        QString autotileAlgorithm READ autotileAlgorithm WRITE setAutotileAlgorithm NOTIFY autotileAlgorithmChanged)
    Q_PROPERTY(
        qreal autotileSplitRatio READ autotileSplitRatio WRITE setAutotileSplitRatio NOTIFY autotileSplitRatioChanged)
    Q_PROPERTY(
        int autotileMasterCount READ autotileMasterCount WRITE setAutotileMasterCount NOTIFY autotileMasterCountChanged)
    Q_PROPERTY(qreal autotileCenteredMasterSplitRatio READ autotileCenteredMasterSplitRatio WRITE
                   setAutotileCenteredMasterSplitRatio NOTIFY autotileCenteredMasterSplitRatioChanged)
    Q_PROPERTY(int autotileCenteredMasterMasterCount READ autotileCenteredMasterMasterCount WRITE
                   setAutotileCenteredMasterMasterCount NOTIFY autotileCenteredMasterMasterCountChanged)

    // Gaps
    Q_PROPERTY(int autotileInnerGap READ autotileInnerGap WRITE setAutotileInnerGap NOTIFY autotileInnerGapChanged)
    Q_PROPERTY(int autotileOuterGap READ autotileOuterGap WRITE setAutotileOuterGap NOTIFY autotileOuterGapChanged)
    Q_PROPERTY(bool autotileSmartGaps READ autotileSmartGaps WRITE setAutotileSmartGaps NOTIFY autotileSmartGapsChanged)
    Q_PROPERTY(bool autotileUsePerSideOuterGap READ autotileUsePerSideOuterGap WRITE setAutotileUsePerSideOuterGap
                   NOTIFY autotileUsePerSideOuterGapChanged)
    Q_PROPERTY(
        int autotileOuterGapTop READ autotileOuterGapTop WRITE setAutotileOuterGapTop NOTIFY autotileOuterGapTopChanged)
    Q_PROPERTY(int autotileOuterGapBottom READ autotileOuterGapBottom WRITE setAutotileOuterGapBottom NOTIFY
                   autotileOuterGapBottomChanged)
    Q_PROPERTY(int autotileOuterGapLeft READ autotileOuterGapLeft WRITE setAutotileOuterGapLeft NOTIFY
                   autotileOuterGapLeftChanged)
    Q_PROPERTY(int autotileOuterGapRight READ autotileOuterGapRight WRITE setAutotileOuterGapRight NOTIFY
                   autotileOuterGapRightChanged)

    // Behavior
    Q_PROPERTY(bool autotileFocusNewWindows READ autotileFocusNewWindows WRITE setAutotileFocusNewWindows NOTIFY
                   autotileFocusNewWindowsChanged)
    Q_PROPERTY(
        int autotileMaxWindows READ autotileMaxWindows WRITE setAutotileMaxWindows NOTIFY autotileMaxWindowsChanged)
    Q_PROPERTY(int autotileInsertPosition READ autotileInsertPosition WRITE setAutotileInsertPosition NOTIFY
                   autotileInsertPositionChanged)
    Q_PROPERTY(bool autotileFocusFollowsMouse READ autotileFocusFollowsMouse WRITE setAutotileFocusFollowsMouse NOTIFY
                   autotileFocusFollowsMouseChanged)
    Q_PROPERTY(bool autotileRespectMinimumSize READ autotileRespectMinimumSize WRITE setAutotileRespectMinimumSize
                   NOTIFY autotileRespectMinimumSizeChanged)

    // Decorations / Borders
    Q_PROPERTY(bool autotileHideTitleBars READ autotileHideTitleBars WRITE setAutotileHideTitleBars NOTIFY
                   autotileHideTitleBarsChanged)
    Q_PROPERTY(
        bool autotileShowBorder READ autotileShowBorder WRITE setAutotileShowBorder NOTIFY autotileShowBorderChanged)
    Q_PROPERTY(
        int autotileBorderWidth READ autotileBorderWidth WRITE setAutotileBorderWidth NOTIFY autotileBorderWidthChanged)
    Q_PROPERTY(int autotileBorderRadius READ autotileBorderRadius WRITE setAutotileBorderRadius NOTIFY
                   autotileBorderRadiusChanged)
    Q_PROPERTY(QColor autotileBorderColor READ autotileBorderColor WRITE setAutotileBorderColor NOTIFY
                   autotileBorderColorChanged)
    Q_PROPERTY(QColor autotileInactiveBorderColor READ autotileInactiveBorderColor WRITE setAutotileInactiveBorderColor
                   NOTIFY autotileInactiveBorderColorChanged)
    Q_PROPERTY(bool autotileUseSystemBorderColors READ autotileUseSystemBorderColors WRITE
                   setAutotileUseSystemBorderColors NOTIFY autotileUseSystemBorderColorsChanged)

    // Screens
    Q_PROPERTY(QVariantList screens READ screens NOTIFY screensChanged)

public:
    KCMAutotiling(QObject* parent, const KPluginMetaData& data);
    ~KCMAutotiling() override;

    // Enable
    bool autotileEnabled() const;
    void setAutotileEnabled(bool enabled);

    // Algorithm
    QString autotileAlgorithm() const;
    void setAutotileAlgorithm(const QString& algorithm);
    qreal autotileSplitRatio() const;
    void setAutotileSplitRatio(qreal ratio);
    int autotileMasterCount() const;
    void setAutotileMasterCount(int count);
    qreal autotileCenteredMasterSplitRatio() const;
    void setAutotileCenteredMasterSplitRatio(qreal ratio);
    int autotileCenteredMasterMasterCount() const;
    void setAutotileCenteredMasterMasterCount(int count);

    // Gaps
    int autotileInnerGap() const;
    void setAutotileInnerGap(int gap);
    int autotileOuterGap() const;
    void setAutotileOuterGap(int gap);
    bool autotileSmartGaps() const;
    void setAutotileSmartGaps(bool smart);
    bool autotileUsePerSideOuterGap() const;
    void setAutotileUsePerSideOuterGap(bool enabled);
    int autotileOuterGapTop() const;
    void setAutotileOuterGapTop(int gap);
    int autotileOuterGapBottom() const;
    void setAutotileOuterGapBottom(int gap);
    int autotileOuterGapLeft() const;
    void setAutotileOuterGapLeft(int gap);
    int autotileOuterGapRight() const;
    void setAutotileOuterGapRight(int gap);

    // Behavior
    bool autotileFocusNewWindows() const;
    void setAutotileFocusNewWindows(bool focus);
    int autotileMaxWindows() const;
    void setAutotileMaxWindows(int max);
    int autotileInsertPosition() const;
    void setAutotileInsertPosition(int position);
    bool autotileFocusFollowsMouse() const;
    void setAutotileFocusFollowsMouse(bool follows);
    bool autotileRespectMinimumSize() const;
    void setAutotileRespectMinimumSize(bool respect);

    // Decorations / Borders
    bool autotileHideTitleBars() const;
    void setAutotileHideTitleBars(bool hide);
    bool autotileShowBorder() const;
    void setAutotileShowBorder(bool show);
    int autotileBorderWidth() const;
    void setAutotileBorderWidth(int width);
    int autotileBorderRadius() const;
    void setAutotileBorderRadius(int radius);
    QColor autotileBorderColor() const;
    void setAutotileBorderColor(const QColor& color);
    QColor autotileInactiveBorderColor() const;
    void setAutotileInactiveBorderColor(const QColor& color);
    bool autotileUseSystemBorderColors() const;
    void setAutotileUseSystemBorderColors(bool use);

    // Screens
    QVariantList screens() const;

    // Q_INVOKABLE methods
    Q_INVOKABLE QVariantList availableAlgorithms() const;
    Q_INVOKABLE QVariantList generateAlgorithmPreview(const QString& algorithmId, int windowCount, double splitRatio,
                                                      int masterCount) const;

    // Per-screen autotiling
    Q_INVOKABLE QVariantMap getPerScreenAutotileSettings(const QString& screenName) const;
    Q_INVOKABLE void setPerScreenAutotileSetting(const QString& screenName, const QString& key, const QVariant& value);
    Q_INVOKABLE void clearPerScreenAutotileSettings(const QString& screenName);
    Q_INVOKABLE bool hasPerScreenAutotileSettings(const QString& screenName) const;

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
    // Enable
    void autotileEnabledChanged();

    // Algorithm
    void autotileAlgorithmChanged();
    void autotileSplitRatioChanged();
    void autotileMasterCountChanged();
    void autotileCenteredMasterSplitRatioChanged();
    void autotileCenteredMasterMasterCountChanged();

    // Gaps
    void autotileInnerGapChanged();
    void autotileOuterGapChanged();
    void autotileSmartGapsChanged();
    void autotileUsePerSideOuterGapChanged();
    void autotileOuterGapTopChanged();
    void autotileOuterGapBottomChanged();
    void autotileOuterGapLeftChanged();
    void autotileOuterGapRightChanged();

    // Behavior
    void autotileFocusNewWindowsChanged();
    void autotileMaxWindowsChanged();
    void autotileInsertPositionChanged();
    void autotileFocusFollowsMouseChanged();
    void autotileRespectMinimumSizeChanged();

    // Decorations / Borders
    void autotileHideTitleBarsChanged();
    void autotileShowBorderChanged();
    void autotileBorderWidthChanged();
    void autotileBorderRadiusChanged();
    void autotileBorderColorChanged();
    void autotileInactiveBorderColorChanged();
    void autotileUseSystemBorderColorsChanged();

    // Screens
    void screensChanged();

private:
    void emitAllChanged();

    Settings* m_settings = nullptr;
    std::unique_ptr<ScreenHelper> m_screenHelper;
    bool m_saving = false;
};

} // namespace PlasmaZones
