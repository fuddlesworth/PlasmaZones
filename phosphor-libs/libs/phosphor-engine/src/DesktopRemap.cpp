// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorEngine/DesktopRemap.h>

#include <QHash>

namespace PhosphorEngine {

DesktopRemap DesktopRemap::fromIds(const QStringList& oldIds, const QStringList& newIds)
{
    DesktopRemap remap;
    if (oldIds.isEmpty() || newIds.isEmpty()) {
        return remap;
    }
    QHash<QString, int> newPositionById;
    newPositionById.reserve(newIds.size());
    for (int i = 0; i < newIds.size(); ++i) {
        // KWin's ids are unique; should one repeat, the first position wins.
        newPositionById.insert(newIds.at(i), newPositionById.value(newIds.at(i), i + 1));
    }
    remap.m_newCount = newIds.size();
    remap.m_newPositions.resize(oldIds.size());
    bool anyShared = false;
    for (int i = 0; i < oldIds.size(); ++i) {
        const int newPosition = newPositionById.value(oldIds.at(i), 0);
        remap.m_newPositions[i] = newPosition;
        anyShared = anyShared || newPosition > 0;
    }
    if (!anyShared) {
        // No desktop in common: KWin re-created its desktop set (a reset
        // kwinrc), it did not remove every desktop. Reading it as a removal
        // would prune every store; positions are the best match left.
        for (int i = 0; i < oldIds.size(); ++i) {
            remap.m_newPositions[i] = i + 1 <= remap.m_newCount ? i + 1 : 0;
        }
    }
    return remap;
}

DesktopRemap DesktopRemap::removal(int position, int oldCount)
{
    DesktopRemap remap;
    if (oldCount < 1) {
        return remap;
    }
    remap.m_newPositions.resize(oldCount);
    const bool inList = position >= 1 && position <= oldCount;
    remap.m_newCount = inList ? oldCount - 1 : oldCount;
    for (int oldPosition = 1; oldPosition <= oldCount; ++oldPosition) {
        int newPosition = oldPosition;
        if (inList && oldPosition == position) {
            newPosition = 0;
        } else if (inList && oldPosition > position) {
            newPosition = oldPosition - 1;
        }
        remap.m_newPositions[oldPosition - 1] = newPosition;
    }
    return remap;
}

bool DesktopRemap::isIdentity() const
{
    for (int i = 0; i < m_newPositions.size(); ++i) {
        if (m_newPositions.at(i) != i + 1) {
            return false;
        }
    }
    return true;
}

int DesktopRemap::oldCount() const
{
    return m_newPositions.size();
}

int DesktopRemap::newCount() const
{
    return m_newCount;
}

int DesktopRemap::map(int oldPosition) const
{
    if (oldPosition < 1 || oldPosition > m_newPositions.size()) {
        return oldPosition;
    }
    return m_newPositions.at(oldPosition - 1);
}

int DesktopRemap::successor(int oldPosition) const
{
    if (oldPosition < 1 || oldPosition > m_newPositions.size()) {
        return oldPosition;
    }
    if (const int mapped = m_newPositions.at(oldPosition - 1); mapped > 0) {
        return mapped;
    }
    // KWin sends a removed desktop's windows, and any output showing it, to
    // the desktop that takes its number: the one after it, or the new last
    // one when it was the last.
    for (int i = oldPosition; i < m_newPositions.size(); ++i) {
        if (m_newPositions.at(i) > 0) {
            return m_newPositions.at(i);
        }
    }
    for (int i = oldPosition - 2; i >= 0; --i) {
        if (m_newPositions.at(i) > 0) {
            return m_newPositions.at(i);
        }
    }
    return 0;
}

int DesktopRemap::singleRemoval() const
{
    int removed = 0;
    for (int i = 0; i < m_newPositions.size(); ++i) {
        if (m_newPositions.at(i) == 0) {
            if (removed != 0) {
                return 0;
            }
            removed = i + 1;
        }
    }
    if (removed == 0) {
        return 0;
    }
    return *this == removal(removed, oldCount()) ? removed : 0;
}

} // namespace PhosphorEngine
