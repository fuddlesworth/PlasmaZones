// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════════════
// WindowTrackingAdaptor — Phosphor shell surface, per-desktop reads
//
// The dashboard draws every desktop's placement map at once and the polkit
// prompt attaches to the window whose process asked, so two reads that the
// live (current-desktop) surfaces never needed: the window states of one
// desktop, and the window behind a pid. Both answer from a small ledger
// the registry mirror in shellsurface.cpp refreshes on every metadata push
// (desktop, spanned desktops, sticky, pid, push order) and drops on close.
// The ledger is the only extra state; the states themselves come from
// getAllWindowStates, so a row here is exactly a row there, filtered.
// ═══════════════════════════════════════════════════════════════════════════════

#include "windowtrackingadaptor.h"
#include "core/platform/logging.h"
#include <PhosphorEngine/WindowRegistry.h>

namespace PlasmaZones {

void WindowTrackingAdaptor::recordShellWindowFacts(const QString& windowId,
                                                   const PhosphorEngine::WindowMetadata& current)
{
    ShellWindowFacts facts;
    facts.pid = current.pid;
    facts.virtualDesktop = current.virtualDesktop;
    facts.virtualDesktops = current.virtualDesktops;
    // The push's own sticky flag, and the "all desktops" spelling of the
    // desktop field (0), read the same way: the window is on every desktop.
    // NOTE: this is the compositor push feed, where 0 means all desktops. It
    // is NOT WindowRegistry::WindowContext::effectiveDesktop, which reads a 0
    // in the metadata feed as "unknown" and falls back to the current desktop.
    // Same spelling, two feeds, two meanings; do not unify them.
    facts.sticky = current.isSticky.value_or(false) || current.virtualDesktop == 0;
    facts.seq = ++m_shellWindowFactsSeq;
    m_shellWindowFacts.insert(windowId, facts);
}

bool WindowTrackingAdaptor::factsOnDesktop(const ShellWindowFacts& facts, int desktop)
{
    if (facts.sticky) {
        return true;
    }
    if (facts.virtualDesktop == desktop) {
        return true;
    }
    return facts.virtualDesktops.contains(desktop);
}

PhosphorProtocol::WindowStateList WindowTrackingAdaptor::getWindowStatesForDesktop(const QString& screenId,
                                                                                   int virtualDesktop)
{
    PhosphorProtocol::WindowStateList out;
    if (virtualDesktop < 0) {
        qCDebug(lcDbusWindow) << "getWindowStatesForDesktop: ignoring desktop" << virtualDesktop;
        return out;
    }
    // 0 is "current" here (a consumer asking about the desktop it is on
    // without knowing its number), never "all": the screen's resolved
    // desktop stands in. Without a screen there is no current desktop to
    // resolve, so 0 answers nothing rather than guessing.
    int desktop = virtualDesktop;
    if (desktop == 0) {
        if (screenId.isEmpty()) {
            qCDebug(lcDbusWindow) << "getWindowStatesForDesktop: desktop 0 needs a screen";
            return out;
        }
        desktop = currentDesktopForScreen(screenId);
        if (desktop < 1) {
            return out;
        }
    }
    const PhosphorProtocol::WindowStateList all = getAllWindowStates();
    for (const auto& entry : all) {
        if (!screenId.isEmpty() && entry.screenId != screenId) {
            continue;
        }
        const auto facts = m_shellWindowFacts.constFind(entry.windowId);
        if (facts == m_shellWindowFacts.cend()) {
            // No push, no desktop: the window is tracked by the service but
            // the compositor never registered it, or it is already gone.
            continue;
        }
        // The service's sticky flag is the same fact from the other feed
        // (setWindowSticky), so either source lands the window everywhere.
        if (!entry.isSticky && !factsOnDesktop(*facts, desktop)) {
            continue;
        }
        out.append(entry);
    }
    return out;
}

QString WindowTrackingAdaptor::findWindowByPid(int pid)
{
    if (pid <= 0) {
        return QString();
    }
    QString best;
    quint64 bestSeq = 0;
    for (auto it = m_shellWindowFacts.cbegin(); it != m_shellWindowFacts.cend(); ++it) {
        if (it->pid != pid) {
            continue;
        }
        if (best.isEmpty() || it->seq > bestSeq) {
            best = it.key();
            bestSeq = it->seq;
        }
    }
    return best;
}

} // namespace PlasmaZones
