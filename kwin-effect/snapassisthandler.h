// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorProtocol/ZoneMarshalling.h>

#include <QObject>
#include <QSet>
#include <QString>

namespace KWin {
class EffectWindow;
}

namespace PlasmaZones {

class PlasmaZonesEffect;
class SnapAssistThumbnailCapture;

/**
 * @brief Handles Snap Assist overlay (continuation UI after zone snap).
 *
 * Builds candidate window lists, queries empty zones from the daemon,
 * and sends the overlay show request.
 */
class SnapAssistHandler : public QObject
{
    Q_OBJECT

public:
    explicit SnapAssistHandler(PlasmaZonesEffect* effect, QObject* parent = nullptr);

    /// Show snap assist continuation for a screen (checks enabled, queries empty zones)
    void showContinuationIfNeeded(const QString& screenId);

    /// Full async snap assist: get snapped windows, build candidates, show overlay
    void asyncShow(const QString& excludeWindowId, const QString& screenId,
                   const PhosphorProtocol::EmptyZoneList& emptyZones);

    /// Update the enabled flag (from loadCachedSettings)
    void setEnabled(bool enabled)
    {
        m_snapAssistEnabled = enabled;
    }
    bool isEnabled() const
    {
        return m_snapAssistEnabled;
    }

    /// Forwards to @c SnapAssistThumbnailCapture::resetRecentlyPosted on the
    /// underlying capture instance (no-op if capture wasn't lazily
    /// constructed yet). Called from @c PlasmaZonesEffect's daemon-ready
    /// path so the kwin-effect's view of "the daemon already holds these
    /// thumbnails" is invalidated whenever the daemon's cache might be cold.
    void resetRecentlyPostedThumbnails();

private:
    PhosphorProtocol::SnapAssistCandidateList buildCandidates(const QString& excludeWindowId, const QString& screenId,
                                                              const QSet<QString>& snappedWindowIds) const;

    PlasmaZonesEffect* m_effect;
    bool m_snapAssistEnabled = false;
    /// Lazy — constructed on first asyncShow that produces candidates.
    /// Snap-assist may never trigger in a typical session (autotile-
    /// only setups, users that never drag-snap), so the
    /// OffscreenQuickScene + WindowThumbnail QML compile is deferred
    /// until the capture path is actually exercised. Eager construction
    /// would pay that cost at compositor startup for users who never
    /// hit the path. Owned via QObject parent (this);
    /// ~SnapAssistHandler tears it down.
    SnapAssistThumbnailCapture* m_capture = nullptr;
};

} // namespace PlasmaZones
