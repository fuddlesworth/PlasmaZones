// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorControl/PageController.h>
#include <QObject>
#include <QVariantList>

namespace PlasmaZones {

class ISettings;

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
class TilingBehaviorController : public PhosphorControl::PageController
{
    Q_OBJECT

    Q_PROPERTY(bool alwaysReinsertIntoStack READ alwaysReinsertIntoStack WRITE setAlwaysReinsertIntoStack NOTIFY
                   alwaysReinsertIntoStackChanged)
    Q_PROPERTY(QVariantList autotileDragInsertTriggers READ autotileDragInsertTriggers WRITE
                   setAutotileDragInsertTriggers NOTIFY autotileDragInsertTriggersChanged)
    Q_PROPERTY(QVariantList defaultAutotileDragInsertTriggers READ defaultAutotileDragInsertTriggers CONSTANT)

public:
    explicit TilingBehaviorController(ISettings& settings, QObject* parent = nullptr);

    bool isDirty() const override
    {
        return false;
    }
    void apply() override
    {
    }
    void discard() override
    {
    }

    bool alwaysReinsertIntoStack() const;
    QVariantList autotileDragInsertTriggers() const;
    QVariantList defaultAutotileDragInsertTriggers() const;

    void setAlwaysReinsertIntoStack(bool enabled);
    void setAutotileDragInsertTriggers(const QVariantList& triggers);

Q_SIGNALS:
    void alwaysReinsertIntoStackChanged();
    void autotileDragInsertTriggersChanged();

private:
    ISettings* m_settings = nullptr;
    /// Cached alwaysReinsertIntoStack state so the
    /// `autotileDragInsertTriggersChanged → alwaysReinsertIntoStackChanged`
    /// forwarder only fires when the derived boolean actually flips.
    bool m_lastAlwaysReinsertIntoStack = false;
    /// Cached AlwaysActive-stripped trigger list. Toggling only the
    /// master `alwaysReinsertIntoStack` flag flips the sentinel
    /// modifier but leaves the QML-facing stripped list identical,
    /// so we only emit `autotileDragInsertTriggersChanged` when the
    /// stripped form actually differs.
    QVariantList m_lastAutotileDragInsertTriggers;
};

} // namespace PlasmaZones
