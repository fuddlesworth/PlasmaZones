// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorContext/IContextResolver.h>

#include <QSet>
#include <QString>

namespace PlasmaZones::TestHelpers {

/// Answers MonitorDisabled for the screens in m_disabled, NotDisabled for every
/// other context, and locks nothing. The handles carry only what was asked for.
class ScreenDisablingResolver : public PhosphorContext::IContextResolver
{
public:
    QSet<QString> m_disabled;

    PhosphorContext::ContextHandle handleFor(const QString& screenId) const override
    {
        PhosphorContext::ContextHandle h;
        h.screenId = screenId;
        return h;
    }
    PhosphorContext::ContextHandle globalHandle() const override
    {
        return {};
    }
    PhosphorContext::ContextHandle handleForMode(const QString& screenId,
                                                 PhosphorZones::AssignmentEntry::Mode mode) const override
    {
        PhosphorContext::ContextHandle h;
        h.screenId = screenId;
        h.mode = mode;
        return h;
    }
    PhosphorContext::ContextHandle handleForPersisted(const QString& screenId, int virtualDesktop,
                                                      const QString& activity) const override
    {
        PhosphorContext::ContextHandle h;
        h.screenId = screenId;
        h.virtualDesktop = virtualDesktop;
        h.activity = activity;
        return h;
    }
    int currentVirtualDesktop() const override
    {
        return 1;
    }
    QString currentActivity() const override
    {
        return QString();
    }
    PhosphorContext::DisabledReason disabledReason(const PhosphorContext::ContextHandle& h) const override
    {
        return m_disabled.contains(h.screenId) ? PhosphorContext::DisabledReason::MonitorDisabled
                                               : PhosphorContext::DisabledReason::NotDisabled;
    }
    bool isLocked(const PhosphorContext::ContextHandle&) const override
    {
        return false;
    }
};

} // namespace PlasmaZones::TestHelpers
