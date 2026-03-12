// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KQuickConfigModule>
#include <QString>

namespace PlasmaZones {

class Settings;

/**
 * @brief General sub-KCM — Animations and OSD settings
 */
class KCMGeneral : public KQuickConfigModule
{
    Q_OBJECT

    // Animations
    Q_PROPERTY(bool animationsEnabled READ animationsEnabled WRITE setAnimationsEnabled NOTIFY animationsEnabledChanged)
    Q_PROPERTY(int animationDuration READ animationDuration WRITE setAnimationDuration NOTIFY animationDurationChanged)
    Q_PROPERTY(QString animationEasingCurve READ animationEasingCurve WRITE setAnimationEasingCurve NOTIFY
                   animationEasingCurveChanged)
    Q_PROPERTY(int animationMinDistance READ animationMinDistance WRITE setAnimationMinDistance NOTIFY
                   animationMinDistanceChanged)
    Q_PROPERTY(int animationSequenceMode READ animationSequenceMode WRITE setAnimationSequenceMode NOTIFY
                   animationSequenceModeChanged)
    Q_PROPERTY(int animationStaggerInterval READ animationStaggerInterval WRITE setAnimationStaggerInterval NOTIFY
                   animationStaggerIntervalChanged)
    Q_PROPERTY(int animationStaggerIntervalMax READ animationStaggerIntervalMax CONSTANT)

    // OSD
    Q_PROPERTY(bool showOsdOnLayoutSwitch READ showOsdOnLayoutSwitch WRITE setShowOsdOnLayoutSwitch NOTIFY
                   showOsdOnLayoutSwitchChanged)
    Q_PROPERTY(bool showNavigationOsd READ showNavigationOsd WRITE setShowNavigationOsd NOTIFY showNavigationOsdChanged)
    Q_PROPERTY(int osdStyle READ osdStyle WRITE setOsdStyle NOTIFY osdStyleChanged)
    Q_PROPERTY(
        int overlayDisplayMode READ overlayDisplayMode WRITE setOverlayDisplayMode NOTIFY overlayDisplayModeChanged)

public:
    KCMGeneral(QObject* parent, const KPluginMetaData& data);
    ~KCMGeneral() override = default;

    // Animations
    bool animationsEnabled() const;
    int animationDuration() const;
    QString animationEasingCurve() const;
    int animationMinDistance() const;
    int animationSequenceMode() const;
    int animationStaggerInterval() const;
    int animationStaggerIntervalMax() const;

    void setAnimationsEnabled(bool enabled);
    void setAnimationDuration(int duration);
    void setAnimationEasingCurve(const QString& curve);
    void setAnimationMinDistance(int distance);
    void setAnimationSequenceMode(int mode);
    void setAnimationStaggerInterval(int ms);

    // OSD
    bool showOsdOnLayoutSwitch() const;
    bool showNavigationOsd() const;
    int osdStyle() const;
    int overlayDisplayMode() const;

    void setShowOsdOnLayoutSwitch(bool show);
    void setShowNavigationOsd(bool show);
    void setOsdStyle(int style);
    void setOverlayDisplayMode(int mode);

public Q_SLOTS:
    void load() override;
    void save() override;
    void defaults() override;
    void onExternalSettingsChanged();

Q_SIGNALS:
    void animationsEnabledChanged();
    void animationDurationChanged();
    void animationEasingCurveChanged();
    void animationMinDistanceChanged();
    void animationSequenceModeChanged();
    void animationStaggerIntervalChanged();
    void showOsdOnLayoutSwitchChanged();
    void showNavigationOsdChanged();
    void osdStyleChanged();
    void overlayDisplayModeChanged();

private:
    void emitAllChanged();

    Settings* m_settings = nullptr;
    bool m_saving = false;
};

} // namespace PlasmaZones
