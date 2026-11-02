// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snapgaptracker.h"

#include <utility>

namespace PlasmaZones {

namespace {

bool sameContext(const SnapGapTracker::Verdict& a, const SnapGapTracker::Verdict& b)
{
    return a.desktop == b.desktop && a.activity == b.activity;
}

} // namespace

void SnapGapTracker::rebase(const QStringList& screenIds, const Resolver& resolve)
{
    QHash<QString, Verdict> next;
    next.reserve(screenIds.size());
    for (const QString& screenId : screenIds) {
        Verdict current = resolve(screenId);
        const auto previous = m_verdicts.constFind(screenId);
        if (previous != m_verdicts.constEnd() && sameContext(*previous, current)) {
            next.insert(screenId, *previous);
        } else {
            next.insert(screenId, std::move(current));
        }
    }
    m_verdicts = std::move(next);
}

QStringList SnapGapTracker::takeChanges(const QStringList& screenIds, const Resolver& resolve)
{
    QStringList moved;
    QHash<QString, Verdict> next;
    next.reserve(screenIds.size());
    for (const QString& screenId : screenIds) {
        Verdict current = resolve(screenId);
        const auto previous = m_verdicts.constFind(screenId);
        if (previous != m_verdicts.constEnd() && sameContext(*previous, current) && previous->gaps != current.gaps) {
            moved.append(screenId);
        }
        next.insert(screenId, std::move(current));
    }
    m_verdicts = std::move(next);
    return moved;
}

void SnapGapTracker::refresh(const QStringList& screenIds, const Resolver& resolve)
{
    for (const QString& screenId : screenIds) {
        m_verdicts.insert(screenId, resolve(screenId));
    }
}

std::optional<SnapGapTracker::Verdict> SnapGapTracker::recorded(const QString& screenId) const
{
    const auto it = m_verdicts.constFind(screenId);
    if (it == m_verdicts.constEnd()) {
        return std::nullopt;
    }
    return *it;
}

void SnapGapTracker::clear()
{
    m_verdicts.clear();
}

} // namespace PlasmaZones
