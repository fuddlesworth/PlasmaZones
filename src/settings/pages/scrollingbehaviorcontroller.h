// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorControl/PageController.h>
#include <QObject>
#include <QVariantList>

namespace PlasmaZones {

class ISettings;

/// Q_PROPERTY surface for the scrolling drag re-insert triggers (the
/// Triggers card on the "Scrolling → Window" page) — the scrolling twin of
/// TilingBehaviorController.
///
/// Exposed as a child Q_PROPERTY on SettingsController; QML reads
/// `settingsController.scrollingBehaviorPage.scrollingDragInsertTriggers`
/// etc. Covers the trigger list plus its derived `alwaysReinsertIntoStrip`
/// boolean (the AlwaysActive sentinel stored inside the list). Trigger-list
/// conversion lives in `PlasmaZones::TriggerUtils`, shared with the snapping
/// and tiling controllers.
///
/// Dirty tracking: the underlying `scrollingDragInsertTriggers` property IS
/// Q_PROPERTY on Settings, so SettingsController's meta-object loop already
/// wires the NOTIFY to `onSettingsPropertyChanged()`. This class just
/// forwards the NOTIFY to QML and caches the derived boolean so it only
/// fires when it actually flips.
class ScrollingBehaviorController : public PhosphorControl::PageController
{
    Q_OBJECT

    Q_PROPERTY(bool alwaysReinsertIntoStrip READ alwaysReinsertIntoStrip WRITE setAlwaysReinsertIntoStrip NOTIFY
                   alwaysReinsertIntoStripChanged)
    Q_PROPERTY(QVariantList scrollingDragInsertTriggers READ scrollingDragInsertTriggers WRITE
                   setScrollingDragInsertTriggers NOTIFY scrollingDragInsertTriggersChanged)
    Q_PROPERTY(QVariantList defaultScrollingDragInsertTriggers READ defaultScrollingDragInsertTriggers CONSTANT)

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

    void setAlwaysReinsertIntoStrip(bool enabled);
    void setScrollingDragInsertTriggers(const QVariantList& triggers);

Q_SIGNALS:
    void alwaysReinsertIntoStripChanged();
    void scrollingDragInsertTriggersChanged();

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
