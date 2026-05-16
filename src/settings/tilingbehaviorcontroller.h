// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QVariantList>

namespace PlasmaZones {

class Settings;

/// Q_PROPERTY surface for the "Tiling → Behavior" settings page.
///
/// Exposed as a child Q_PROPERTY on SettingsController; QML reads
/// `settingsController.tilingBehaviorPage.autotileDragInsertTriggers` etc.
/// Covers the autotile drag-insert trigger list plus its derived
/// `alwaysReinsertIntoStack` boolean. Trigger-list conversion lives in
/// `PlasmaZones::TriggerUtils`, shared with SnappingBehaviorController.
///
/// Dirty tracking: the underlying `autotileDragInsertTriggers` property
/// IS Q_PROPERTY on Settings, so SettingsController's meta-object-loop
/// already wires the NOTIFY to `onSettingsPropertyChanged()`. This class
/// just forwards the NOTIFY to QML and caches the derived boolean so it
/// only fires when it actually flips.
class TilingBehaviorController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool alwaysReinsertIntoStack READ alwaysReinsertIntoStack WRITE setAlwaysReinsertIntoStack NOTIFY
                   alwaysReinsertIntoStackChanged)
    Q_PROPERTY(QVariantList autotileDragInsertTriggers READ autotileDragInsertTriggers WRITE
                   setAutotileDragInsertTriggers NOTIFY autotileDragInsertTriggersChanged)
    Q_PROPERTY(QVariantList defaultAutotileDragInsertTriggers READ defaultAutotileDragInsertTriggers CONSTANT)

public:
    explicit TilingBehaviorController(Settings* settings, QObject* parent = nullptr);

    bool alwaysReinsertIntoStack() const;
    QVariantList autotileDragInsertTriggers() const;
    QVariantList defaultAutotileDragInsertTriggers() const;

    void setAlwaysReinsertIntoStack(bool enabled);
    void setAutotileDragInsertTriggers(const QVariantList& triggers);

Q_SIGNALS:
    void alwaysReinsertIntoStackChanged();
    void autotileDragInsertTriggersChanged();

private:
    Settings* m_settings = nullptr;
    /// Cached alwaysReinsertIntoStack state so the
    /// `autotileDragInsertTriggersChanged → alwaysReinsertIntoStackChanged`
    /// forwarder only fires when the derived boolean actually flips.
    bool m_lastAlwaysReinsertIntoStack = false;
};

} // namespace PlasmaZones
