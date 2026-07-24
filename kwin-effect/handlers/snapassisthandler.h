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

    /// Show snap assist continuation for a screen (checks enabled, queries empty zones).
    ///
    /// @param requireSnappedWindowId when non-empty, the continuation is gated:
    ///   snap assist is shown only if this window is actually snapped into a
    ///   zone on the daemon side, and the window is also excluded from the
    ///   candidate list (it is already placed). Used by the resnap-completion
    ///   path — a bulk resnap (autotile→snap toggle, rotate, vs-reconfigure)
    ///   is not a per-window snap, so it must not pop snap assist for every
    ///   empty zone when it happened to place nothing. The anchor window
    ///   stands in for "the window the user just snapped"; if it did not land
    ///   in a zone, there is no continuation to offer.
    void showContinuationIfNeeded(const QString& screenId, const QString& requireSnappedWindowId = QString());

    /// Full async snap assist: get snapped windows, build candidates, show overlay.
    ///
    /// @param requireSnappedWindowId when non-empty, abort if this window is
    ///   not among the daemon's snapped windows. @see showContinuationIfNeeded.
    void asyncShow(const QString& excludeWindowId, const QString& screenId,
                   const PhosphorProtocol::EmptyZoneList& emptyZones,
                   const QString& requireSnappedWindowId = QString());

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
    /// only setups, users that never drag-snap), so the capture object
    /// and its bookkeeping are deferred until the capture path is
    /// actually exercised rather than allocated at compositor startup
    /// for users who never hit the path. Owned via QObject parent (this);
    /// ~SnapAssistHandler tears it down.
    SnapAssistThumbnailCapture* m_capture = nullptr;
};

} // namespace PlasmaZones
