// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilingadaptor.h"

#include "core/platform/logging.h"

#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorEngine/WindowRegistry.h>

namespace PlasmaZones {

namespace {
// The window's desktop set as the engines key their states: x11 numbering,
// exactly what the effect stamps into the registry from
// VirtualDesktop::x11DesktopNumber. Empty for a sticky window or one whose
// desktop is unknown (virtualDesktop 0), which the reconcile reads as
// "nothing to check". A multi-desktop span carries the full list in
// virtualDesktops with virtualDesktop equal to its first entry, so the list
// is preferred when present.
QSet<int> desktopSetOf(const PhosphorEngine::WindowMetadata& meta)
{
    QSet<int> desktops;
    if (!meta.virtualDesktops.isEmpty()) {
        for (const int desktop : meta.virtualDesktops) {
            if (desktop > 0) {
                desktops.insert(desktop);
            }
        }
        return desktops;
    }
    if (meta.virtualDesktop > 0) {
        desktops.insert(meta.virtualDesktop);
    }
    return desktops;
}
} // namespace

void TilingAdaptor::setWindowRegistry(PhosphorEngine::WindowRegistry* registry)
{
    QObject::disconnect(m_registryDesktopConnection);
    m_registryDesktopConnection = {};
    if (!registry) {
        return;
    }
    m_registryDesktopConnection =
        QObject::connect(registry, &PhosphorEngine::WindowRegistry::metadataChanged, this,
                         [this, registry](const QString& instanceId, const PhosphorEngine::WindowMetadata& oldMeta,
                                          const PhosphorEngine::WindowMetadata& newMeta) {
                             // Only a change to the desktop set is a move. The registry
                             // re-emits for every metadata edit, title ticks included, and
                             // those must not cost an engine walk each.
                             const QSet<int> desktops = desktopSetOf(newMeta);
                             if (desktops == desktopSetOf(oldMeta)) {
                                 return;
                             }
                             // The engines key by the canonical composite id; the registry
                             // signals with the bare instance id.
                             reconcileWindowDesktops(registry->canonicalizeForLookup(instanceId), desktops);
                         });
}

void TilingAdaptor::reconcileWindowDesktops(const QString& windowId, const QSet<int>& desktops)
{
    if (windowId.isEmpty() || desktops.isEmpty()) {
        return;
    }
    for (PhosphorEngine::IPlacementEngine* engine : std::as_const(m_lifecycleEngines)) {
        const std::optional<PhosphorEngine::PlacementStateKey> held = engine->heldKeyForWindow(windowId);
        if (!held || desktops.contains(held->desktop)) {
            continue;
        }
        qCInfo(lcDbusTiling) << "reconcileWindowDesktops: window" << windowId << "left desktop" << held->desktop
                             << "on screen" << held->screenId << "(now on" << desktops
                             << ") — releasing it from that state";
        // The same live-move release the effect's drag-bypass and desktop
        // arms use: no placement capture (the window is alive, its frame is
        // not a float-back), the move-excuse one-shots armed so the
        // announce on its new desktop is not mistaken for a session restore.
        // releaseWindowTracking dispatches to whichever engine holds the
        // window, which is the one that just answered.
        releaseWindowTracking(windowId);
        return;
    }
}

} // namespace PlasmaZones
