// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorProtocol/WireTypes.h>

#include <QObject>
#include <QSet>
#include <QString>

namespace KWin {
class EffectWindow;
}

namespace PlasmaZones {

class PlasmaZonesEffect;

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

private:
    PhosphorProtocol::SnapAssistCandidateList buildCandidates(const QString& excludeWindowId, const QString& screenId,
                                                              const QSet<QString>& snappedWindowIds) const;

    PlasmaZonesEffect* m_effect;
    bool m_snapAssistEnabled = false;
};

} // namespace PlasmaZones
