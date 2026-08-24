// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorControl/PageController.h>
#include <QObject>
#include <QVariantList>

namespace PlasmaZones {

class ISettings;

/// Q_PROPERTY surface for the scrolling trigger lists on the
/// "Scrolling → Window" page: the drag re-insert triggers (the Triggers
/// card) and the two wheel chords, or "scroll keys" (the Focus and view
/// card). The scrolling twin of TilingBehaviorController.
///
/// Exposed as a child Q_PROPERTY on SettingsController; QML reads
/// `settingsController.scrollingBehaviorPage.scrollingDragInsertTriggers`
/// etc. The drag re-insert list also carries a derived
/// `alwaysReinsertIntoStrip` boolean (the AlwaysActive sentinel stored
/// inside the list); the two wheel lists cannot carry that sentinel at all,
/// because canonicalWheelTriggerList drops it. Trigger-list conversion lives
/// in `PlasmaZones::TriggerUtils`, shared with the snapping and tiling
/// controllers.
///
/// Dirty tracking: all three underlying properties ARE Q_PROPERTY on
/// Settings, so SettingsController's meta-object loop already wires their
/// NOTIFY to `onSettingsPropertyChanged()`. This class just forwards those
/// to QML, and caches the derived boolean so it only fires when it actually
/// flips.
class ScrollingBehaviorController : public PhosphorControl::PageController
{
    Q_OBJECT

    Q_PROPERTY(bool alwaysReinsertIntoStrip READ alwaysReinsertIntoStrip WRITE setAlwaysReinsertIntoStrip NOTIFY
                   alwaysReinsertIntoStripChanged)
    Q_PROPERTY(QVariantList scrollingDragInsertTriggers READ scrollingDragInsertTriggers WRITE
                   setScrollingDragInsertTriggers NOTIFY scrollingDragInsertTriggersChanged)
    Q_PROPERTY(QVariantList defaultScrollingDragInsertTriggers READ defaultScrollingDragInsertTriggers CONSTANT)
    // The two wheel chords. No AlwaysActive sentinel lives in either list —
    // "always" is meaningless for a chord whose whole job is to distinguish
    // one wheel gesture from a plain one — so these need none of the strip /
    // merge dance above and are a straight bitmask conversion each way.
    Q_PROPERTY(QVariantList scrollingWheelFocusTriggers READ scrollingWheelFocusTriggers WRITE
                   setScrollingWheelFocusTriggers NOTIFY scrollingWheelFocusTriggersChanged)
    Q_PROPERTY(QVariantList defaultScrollingWheelFocusTriggers READ defaultScrollingWheelFocusTriggers CONSTANT)
    Q_PROPERTY(QVariantList scrollingWheelViewTriggers READ scrollingWheelViewTriggers WRITE
                   setScrollingWheelViewTriggers NOTIFY scrollingWheelViewTriggersChanged)
    Q_PROPERTY(QVariantList defaultScrollingWheelViewTriggers READ defaultScrollingWheelViewTriggers CONSTANT)

public:
    explicit ScrollingBehaviorController(ISettings& settings, QObject* parent = nullptr);

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

    bool alwaysReinsertIntoStrip() const;
    QVariantList scrollingDragInsertTriggers() const;
    QVariantList defaultScrollingDragInsertTriggers() const;

    QVariantList scrollingWheelFocusTriggers() const;
    QVariantList defaultScrollingWheelFocusTriggers() const;
    QVariantList scrollingWheelViewTriggers() const;
    QVariantList defaultScrollingWheelViewTriggers() const;

    void setAlwaysReinsertIntoStrip(bool enabled);
    void setScrollingDragInsertTriggers(const QVariantList& triggers);
    void setScrollingWheelFocusTriggers(const QVariantList& triggers);
    void setScrollingWheelViewTriggers(const QVariantList& triggers);

Q_SIGNALS:
    void alwaysReinsertIntoStripChanged();
    void scrollingDragInsertTriggersChanged();
    void scrollingWheelFocusTriggersChanged();
    void scrollingWheelViewTriggersChanged();

private:
    ISettings* m_settings = nullptr;
    /// Cached derived boolean; see TilingBehaviorController's twin members
    /// for the emit-on-flip rationale.
    bool m_lastAlwaysReinsertIntoStrip = false;
    /// Cached AlwaysActive-stripped trigger list (master-flag toggles leave
    /// the QML-facing stripped list identical).
    QVariantList m_lastScrollingDragInsertTriggers;
};

} // namespace PlasmaZones
