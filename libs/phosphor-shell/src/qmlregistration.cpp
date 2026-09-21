// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/QmlRegistration.h>

#include <PhosphorShell/FileView.h>
#include <PhosphorShell/FloatingWindow.h>
#include <PhosphorShell/LazyLoader.h>
#include <PhosphorShell/PanelWindow.h>
#include <PhosphorShell/PerScreenPanels.h>
#include <PhosphorShell/PersistentProperties.h>
#include <PhosphorShell/PlacementMap.h>
#include <PhosphorShell/PopupWindow.h>
#include <PhosphorShell/Process.h>
#include <PhosphorShell/SystemClock.h>
#include <PhosphorShell/SystemStats.h>
#include <PhosphorShell/SystemUsage.h>
#include <PhosphorShell/Toplevels.h>
#include <PhosphorShell/Variants.h>
#include <PhosphorShell/Workspaces.h>

#include <PhosphorRendering/ShaderEffect.h>
#include <PhosphorWayland/IdleInhibitor.h>

#include <QQmlEngine>
#include <mutex>

namespace PhosphorShell {

void registerQmlTypes()
{
    // QML type registration is process-global (Qt's registry, not per-
    // engine). Guard with std::call_once so multiple ShellEngines in
    // the same process (sequential tests, future multi-shell daemon)
    // don't trip Qt's "type already registered" warning on the second
    // construction. The registrations themselves are unchanged.
    static std::once_flag s_qmlRegistered;
    std::call_once(s_qmlRegistered, [] {
        qmlRegisterType<PanelWindow>("Phosphor.Shell", 1, 0, "PanelWindow");
        qmlRegisterType<PopupWindow>("Phosphor.Shell", 1, 0, "PopupWindow");
        qmlRegisterType<FloatingWindow>("Phosphor.Shell", 1, 0, "FloatingWindow");
        qmlRegisterType<Variants>("Phosphor.Shell", 1, 0, "Variants");
        // One panel per screen. Distinct from Variants and PerScreen
        // because materializePanels() takes ownership of what it finds:
        // instances must be QObject children to be discovered at all, and
        // their creator must never destroy them afterwards. See the class
        // docs for why the stock instantiators cannot satisfy both.
        qmlRegisterType<PerScreenPanels>("Phosphor.Shell", 1, 0, "PerScreenPanels");
        qmlRegisterType<LazyLoader>("Phosphor.Shell", 1, 0, "LazyLoader");
        qmlRegisterType<Process>("Phosphor.Shell", 1, 0, "Process");
        qmlRegisterType<FileView>("Phosphor.Shell", 1, 0, "FileView");
        qmlRegisterType<PersistentProperties>("Phosphor.Shell", 1, 0, "PersistentProperties");
        qmlRegisterType<PhosphorRendering::ShaderEffect>("Phosphor.Shell", 1, 0, "ShaderBackground");
        // ForeignToplevel is uncreatable from QML — it's only ever vended by
        // Toplevels via the toplevelAdded signal / toplevels list. Registering
        // it as uncreatable lets QML resolve `PhosphorWayland.ForeignToplevel`
        // type names in delegates (`required property var modelData` doesn't
        // need the registration, but `as ForeignToplevel` casts do).
        qmlRegisterUncreatableType<PhosphorWayland::ForeignToplevel>(
            "Phosphor.Shell", 1, 0, "ForeignToplevel",
            QStringLiteral("ForeignToplevel is owned by Toplevels and cannot be constructed from QML"));
        qmlRegisterType<SystemClock>("Phosphor.Shell", 1, 0, "SystemClock");
        // CPU / memory sampling. Kept in C++ rather than parsed from
        // /proc in QML: the jiffy-delta arithmetic and the malformed-layout
        // handling are logic, not presentation.
        qmlRegisterType<SystemUsage>("Phosphor.Shell", 1, 0, "SystemUsage");
        qmlRegisterSingletonType<SystemStats>("Phosphor.Shell", 1, 0, "SystemStats",
                                              [](QQmlEngine* engine, QJSEngine*) -> QObject* {
                                                  return new SystemStats(engine);
                                              });
        // Surface-bound idle inhibition (zwp-idle-inhibit-v1): a QML window keeps
        // its own output awake while visible. This stays a foundation primitive.
        // Session-wide idle monitoring (ext-idle-notify-v1) is NOT registered here:
        // it is owned by Phosphor.Service.Idle's IdleService (a multi-stage timeout
        // policy + surface-less inhibition), registered in src/shell/main.cpp, so a
        // single monitor arms each timeout.
        qmlRegisterType<PhosphorWayland::IdleInhibitor>("Phosphor.Shell", 1, 0, "IdleInhibitor");
        qmlRegisterSingletonType<Toplevels>("Phosphor.Shell", 1, 0, "Toplevels", &Toplevels::create);
        // Compositor workspaces (KWin's virtual desktops today). A
        // singleton for the same reason Toplevels is one: the underlying
        // manager holds one D-Bus subscription per process.
        qmlRegisterSingletonType<Workspaces>("Phosphor.Shell", 1, 0, "Workspaces", &Workspaces::create);
        // The placement engine's geometry per screen (the bar's live map).
        // One set of daemon subscriptions per engine, screens vended by
        // forScreen() under C++ ownership.
        qmlRegisterSingletonType<PlacementMap>("Phosphor.Shell", 1, 0, "PlacementMap", &PlacementMap::create);
        qmlRegisterUncreatableType<PlacementMapScreen>("Phosphor.Shell", 1, 0, "PlacementMapScreen",
                                                       QStringLiteral("Vended by PlacementMap.forScreen()"));
    });
}

} // namespace PhosphorShell
