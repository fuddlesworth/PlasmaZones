// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The open-claim bookkeeping and the live-sibling predicate of
// WindowPlacementStore, split out of WindowPlacementStore.cpp by concern: who
// may read which record during one open (claimForOpen / pairingAllows), how a
// claim is released, and the one predicate every live-sibling exclusion in
// the store shares.

#include <PhosphorEngine/WindowPlacementStore.h>
#include <PhosphorIdentity/WindowId.h>

namespace PhosphorEngine {

namespace {
// Same contract as WindowPlacementStore.cpp's: the instance-identity match
// lives in PhosphorIdentity so every library shares one predicate.
using PhosphorIdentity::WindowId::sameWindowInstance;
} // namespace

bool WindowPlacementStore::pairingAllows(const QString& windowId, const WindowPlacement& candidate) const
{
    if (m_openPairing.isEmpty()) {
        return true;
    }
    const QString instance = PhosphorIdentity::WindowId::extractInstanceId(windowId);
    const auto mine = m_openPairing.constFind(instance);
    if (mine != m_openPairing.constEnd()) {
        // Fail open on an inconsistent pair. Every record removal drops the
        // claim naming it, so this should be unreachable; if it ever is not,
        // locking the instance out of every record would restore NOTHING, which
        // is strictly worse than the disagreement the claim exists to fix.
        if (!m_claimedBy.contains(*mine)) {
            return true;
        }
        return candidate.windowId == *mine;
    }
    // No claim of its own: it may read anything no OTHER instance has claimed.
    const auto owner = m_claimedBy.constFind(candidate.windowId);
    return owner == m_claimedBy.constEnd() || *owner == instance;
}

void WindowPlacementStore::dropClaimsNaming(const QString& recordWindowId)
{
    if (m_claimedBy.isEmpty() || recordWindowId.isEmpty()) {
        return;
    }
    const auto owner = m_claimedBy.constFind(recordWindowId);
    if (owner == m_claimedBy.constEnd()) {
        return;
    }
    m_openPairing.remove(*owner);
    m_claimedBy.remove(recordWindowId);
}

bool WindowPlacementStore::boundToLiveOther(const QString& askingWindowId, const WindowPlacement& p) const
{
    return m_liveInstanceProbe && m_liveInstanceProbe(p.windowId) && !sameWindowInstance(p.windowId, askingWindowId);
}

void WindowPlacementStore::releaseOpenClaim(const QString& windowId)
{
    if (windowId.isEmpty() || m_openPairing.isEmpty()) {
        return;
    }
    const QString instance = PhosphorIdentity::WindowId::extractInstanceId(windowId);
    const auto claimed = m_openPairing.constFind(instance);
    if (claimed == m_openPairing.constEnd()) {
        return;
    }
    m_claimedBy.remove(*claimed);
    m_openPairing.remove(instance);
}

std::optional<WindowPlacement> WindowPlacementStore::claimForOpen(const QString& windowId, const QString& appId)
{
    if (windowId.isEmpty()) {
        return std::nullopt;
    }
    const QString instance = PhosphorIdentity::WindowId::extractInstanceId(windowId);

    // Idempotent: an instance that already claimed keeps the same record, so the
    // open channel and every re-drive that follows it cannot disagree.
    const auto existing = m_openPairing.constFind(instance);
    if (existing != m_openPairing.constEnd()) {
        for (auto b = m_byApp.constBegin(); b != m_byApp.constEnd(); ++b) {
            for (const WindowPlacement& p : b.value()) {
                if (p.windowId == *existing) {
                    return p;
                }
            }
        }
        // Unreachable while the removal hooks hold; re-claim rather than trust it.
        m_claimedBy.remove(*existing);
        m_openPairing.remove(instance);
    }

    // 1. The window's own record, when it carries something worth restoring.
    //    hasRestorableContent is what keeps the slot-less geometry stub every
    //    open writes under the live uuid from being mistaken for it.
    for (auto b = m_byApp.constBegin(); b != m_byApp.constEnd(); ++b) {
        for (const WindowPlacement& p : b.value()) {
            if (sameWindowInstance(p.windowId, windowId) && p.hasRestorableContent()) {
                m_openPairing.insert(instance, p.windowId);
                m_claimedBy.insert(p.windowId, instance);
                return p;
            }
        }
    }

    // 2. Newest unclaimed record in the appId bucket that no live sibling owns.
    if (appId.isEmpty()) {
        return std::nullopt;
    }
    const auto bucket = m_byApp.constFind(appId);
    if (bucket == m_byApp.constEnd()) {
        return std::nullopt;
    }
    const WindowPlacement* best = nullptr;
    for (const WindowPlacement& p : bucket.value()) {
        if (!p.hasRestorableContent()) {
            continue;
        }
        if (boundToLiveOther(windowId, p)) {
            continue; // belongs to a sibling that is still open
        }
        const auto owner = m_claimedBy.constFind(p.windowId);
        if (owner != m_claimedBy.constEnd() && *owner != instance) {
            continue; // already claimed by a sibling
        }
        if (!best || p.sequence > best->sequence) {
            best = &p;
        }
    }
    if (!best) {
        return std::nullopt;
    }
    m_openPairing.insert(instance, best->windowId);
    m_claimedBy.insert(best->windowId, instance);
    return *best;
}

} // namespace PhosphorEngine
