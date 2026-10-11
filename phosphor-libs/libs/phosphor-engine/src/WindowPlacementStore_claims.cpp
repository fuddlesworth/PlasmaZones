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

int WindowPlacementStore::releaseOpenClaimsExcept(const QSet<QString>& aliveInstanceIds)
{
    int released = 0;
    for (auto it = m_openPairing.begin(); it != m_openPairing.end();) {
        if (aliveInstanceIds.contains(it.key())) {
            ++it;
            continue;
        }
        m_claimedBy.remove(it.value());
        it = m_openPairing.erase(it);
        ++released;
    }
    return released;
}

std::optional<WindowPlacement> WindowPlacementStore::claimForOpen(const QString& windowId, const QString& appId)
{
    return claimForOpenImpl(windowId, appId, /*ownRecordFinal=*/false, QString(), {});
}

std::optional<WindowPlacement>
WindowPlacementStore::claimForOpen(const QString& windowId, const QString& appId, const QString& openingScreenId,
                                   const std::function<bool(const WindowPlacement&)>& restorableHere)
{
    return claimForOpenImpl(windowId, appId, /*ownRecordFinal=*/true, openingScreenId, restorableHere);
}

std::optional<WindowPlacement>
WindowPlacementStore::claimForOpenImpl(const QString& windowId, const QString& appId, bool ownRecordFinal,
                                       const QString& openingScreenId,
                                       const std::function<bool(const WindowPlacement&)>& restorableHere)
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

    // 1. The window's own record, when an engine has ever captured it. The
    //    engine-slot test is what keeps the slot-less geometry stub every
    //    open writes under the live uuid from being mistaken for it:
    //    hasRestorableContent alone answers true on geometry, and the stub
    //    carries the spawn frame. Claiming it locked the instance out of
    //    every sibling record through pairingAllows, so a reopen on a
    //    tiling screen, where the stub always precedes the announce,
    //    restored nothing from a closed sibling.
    //    Under the reopen contract (ownRecordFinal) a captured own record is
    //    the answer even when it holds nothing restorable: its slots are the
    //    window's own verdict, and a sibling's record must not stand in.
    for (auto b = m_byApp.constBegin(); b != m_byApp.constEnd(); ++b) {
        for (const WindowPlacement& p : b.value()) {
            if (!sameWindowInstance(p.windowId, windowId) || p.engines.isEmpty()) {
                continue;
            }
            if (p.hasRestorableContent()) {
                // Keep the two maps in lockstep: a claim another instance
                // holds on this record dies here, or its pairing would go on
                // naming a record m_claimedBy now attributes to this one.
                dropClaimsNaming(p.windowId);
                m_openPairing.insert(instance, p.windowId);
                m_claimedBy.insert(p.windowId, instance);
                return p;
            }
            if (ownRecordFinal) {
                return std::nullopt;
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
        // Step 1 owns the same-instance case; the asker's own slot-less stub
        // is the newest record in its bucket and would otherwise win here.
        if (sameWindowInstance(p.windowId, windowId)) {
            continue;
        }
        if (boundToLiveOther(windowId, p)) {
            continue; // belongs to a sibling that is still open
        }
        const auto owner = m_claimedBy.constFind(p.windowId);
        if (owner != m_claimedBy.constEnd() && *owner != instance) {
            continue; // already claimed by a sibling
        }
        // The reopen contract: only a record on the opening output (or with no
        // screen) that the opening engine can restore there. A record on
        // another output stays untouched for an instance that opens there.
        if (!openingScreenId.isEmpty() && !p.screenId.isEmpty()
            && !PhosphorIdentity::VirtualScreenId::samePhysical(p.screenId, openingScreenId)) {
            continue;
        }
        if (restorableHere && !restorableHere(p)) {
            continue;
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
