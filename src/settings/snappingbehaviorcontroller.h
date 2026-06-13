// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorControl/PageController.h>
#include <QObject>
#include <QVariantList>

namespace PlasmaZones {

class ISettings;

/// Q_PROPERTY surface for the "Snapping → Behavior" settings page.
///
/// Exposed as a child Q_PROPERTY on SettingsController; QML reads
/// `settingsController.snappingBehaviorPage.alwaysActivateOnDrag` etc.
/// Covers the three trigger lists (drag activation, zone span, snap
/// assist) plus the "always active" booleans derived from them, and the
/// adjacent-threshold slider bounds.
///
/// Trigger lists round-trip between the on-disk `DragModifier` enum form
/// and the Qt-bitmask form QML widgets expect; conversion lives in
/// `PlasmaZones::TriggerUtils`.
///
/// Dirty tracking: the underlying trigger properties ARE Q_PROPERTY on the
/// Settings class, so the meta-object-loop in SettingsController's ctor
/// already wires them to `onSettingsPropertyChanged()`. The sub-controller
/// therefore only needs to forward NOTIFY signals to QML — no additional
/// `changed()` bridging (the editor-page case needed it because its
/// properties are NOT Q_PROPERTY on Settings).
///
/// `alwaysActivateOnDrag` is DERIVED from the trigger list (the
/// AlwaysActive sentinel modifier is the master-toggle proxy). The
/// WRITE setter routes through `m_settings->setDragActivationTriggers`
/// — a Q_PROPERTY — so the meta-loop still catches the dirty flip
/// even though `alwaysActivateOnDrag` itself isn't a Q_PROPERTY on
/// Settings. Same trick for TilingBehaviorController::alwaysReinsertIntoStack.
class SnappingBehaviorController : public PhosphorControl::PageController
{
    Q_OBJECT

    Q_PROPERTY(bool alwaysActivateOnDrag READ alwaysActivateOnDrag WRITE setAlwaysActivateOnDrag NOTIFY
                   alwaysActivateOnDragChanged)
    Q_PROPERTY(QVariantList dragActivationTriggers READ dragActivationTriggers WRITE setDragActivationTriggers NOTIFY
                   dragActivationTriggersChanged)
    Q_PROPERTY(QVariantList defaultDragActivationTriggers READ defaultDragActivationTriggers CONSTANT)
    Q_PROPERTY(
        QVariantList zoneSpanTriggers READ zoneSpanTriggers WRITE setZoneSpanTriggers NOTIFY zoneSpanTriggersChanged)
    Q_PROPERTY(QVariantList defaultZoneSpanTriggers READ defaultZoneSpanTriggers CONSTANT)
    Q_PROPERTY(QVariantList snapAssistTriggers READ snapAssistTriggers WRITE setSnapAssistTriggers NOTIFY
                   snapAssistTriggersChanged)
    Q_PROPERTY(QVariantList defaultSnapAssistTriggers READ defaultSnapAssistTriggers CONSTANT)
    Q_PROPERTY(int adjacentThresholdMin READ adjacentThresholdMin CONSTANT)
    Q_PROPERTY(int adjacentThresholdMax READ adjacentThresholdMax CONSTANT)

public:
    explicit SnappingBehaviorController(ISettings& settings, QObject* parent = nullptr);

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

    bool alwaysActivateOnDrag() const;
    QVariantList dragActivationTriggers() const;
    QVariantList defaultDragActivationTriggers() const;
    QVariantList zoneSpanTriggers() const;
    QVariantList defaultZoneSpanTriggers() const;
    QVariantList snapAssistTriggers() const;
    QVariantList defaultSnapAssistTriggers() const;

    void setAlwaysActivateOnDrag(bool enabled);
    void setDragActivationTriggers(const QVariantList& triggers);
    void setZoneSpanTriggers(const QVariantList& triggers);
    void setSnapAssistTriggers(const QVariantList& triggers);

    int adjacentThresholdMin() const;
    int adjacentThresholdMax() const;

Q_SIGNALS:
    void alwaysActivateOnDragChanged();
    void dragActivationTriggersChanged();
    void zoneSpanTriggersChanged();
    void snapAssistTriggersChanged();

private:
    ISettings* m_settings = nullptr;
    /// Cached alwaysActivateOnDrag state, so the
    /// `dragActivationTriggersChanged → alwaysActivateOnDragChanged` forwarder
    /// only fires when the derived boolean actually flips (CLAUDE.md:
    /// "Only emit signals when value actually changes").
    bool m_lastAlwaysActiveOnDrag = false;
    /// Cached AlwaysActive-stripped trigger list. Same rationale: toggling
    /// only the master `alwaysActivateOnDrag` flag flips the sentinel
    /// modifier but leaves the QML-facing stripped list identical, so we
    /// only emit `dragActivationTriggersChanged` when the stripped form
    /// actually differs.
    QVariantList m_lastDragActivationTriggers;
};

} // namespace PlasmaZones
