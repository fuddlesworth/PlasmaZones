// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// D-Bus subscription wiring for SettingsController.
//
// Centralises every daemon-broadcast subscription the settings UI relies
// on for live updates:
//   * Settings reload + async running-windows reply (Settings interface)
//   * Layout list / property / assignment mutations + quick-slot updates
//     (LayoutRegistry interface)
//   * Virtual-desktop + activity changes (LayoutRegistry interface)
//   * Window-rules `rulesChanged` broadcast — reloads the settings-side
//     mirror store (`m_localRuleStore`) so the in-process LayoutRegistry's
//     assignment cascade sees daemon-driven rule edits without a process
//     restart.
//
// The previous monolithic block in `settingscontroller.cpp` pushed that
// TU past the project's 800-line cap (CLAUDE.md). Extracting the D-Bus
// wire-up is the natural seam: the helper lambda + subscriptions are
// cohesive, only call into D-Bus session-bus APIs and the controller's
// own slot table, and don't touch the page-controller construction
// graph. Same class, separate translation unit, no API change.

#include "settingscontroller.h"

#include "../core/logging.h"
#include "dbusutils.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorWindowRules/WindowRuleStore.h>

#include <QDBusConnection>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace PlasmaZones {

QVariantMap SettingsController::gapProvenance(const QString& screenName) const
{
    QVariantMap result;
    const QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::Control),
                                                      QStringLiteral("getGapProvenance"), {screenName});
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return result; // daemon unreachable / error reply
    }
    const QString json = reply.arguments().constFirst().toString();
    if (json.isEmpty()) {
        return result;
    }
    const QJsonObject root = QJsonDocument::fromJson(json.toUtf8()).object();
    const auto unpack = [&root, &result](QLatin1String facetKey, const QString& valueKey, const QString& layerKey) {
        const QJsonObject facet = root.value(facetKey).toObject();
        result.insert(valueKey, facet.value(QLatin1String("value")).toInt());
        result.insert(layerKey, facet.value(QLatin1String("layer")).toString());
    };
    unpack(QLatin1String("innerGap"), QStringLiteral("innerValue"), QStringLiteral("innerLayer"));
    unpack(QLatin1String("outerGap"), QStringLiteral("outerValue"), QStringLiteral("outerLayer"));
    return result;
}

void SettingsController::wireDaemonSubscriptions(QStringList& failedSubscriptions)
{
    // QDBusConnection::connect's API is fundamentally string-based (signal
    // name + SLOT() signature) — it can't use the modern member-function-
    // pointer connect syntax because D-Bus signals are dynamically named.
    // The lambda just factors the repeated `service + objectPath` tuple
    // out of every call site so each subscription is one line, and
    // centralises failure logging in one place.
    //
    // The const-char* SLOT signature is normalised through
    // QMetaObject::normalizedSignature inside QDBusConnection, so spacing
    // variations between call sites are harmless — but the helper takes
    // the un-normalised string so call sites can be grep'd consistently.
    //
    // Failed subscriptions are accumulated in @p failedSubscriptions so
    // the constructor can surface ALL missing routes in one post-loop
    // warning line rather than scattering per-call warnings across the
    // boot log. Helps diagnose the "daemon not up yet at construct time"
    // case where many signals miss together.
    const auto subscribeDaemonSignal = [this, &failedSubscriptions](const QString& interfaceName,
                                                                    const QString& signalName, const char* slot) {
        const bool ok = QDBusConnection::sessionBus().connect(QString(PhosphorProtocol::Service::Name),
                                                              QString(PhosphorProtocol::Service::ObjectPath),
                                                              interfaceName, signalName, this, slot);
        if (!ok) {
            qCWarning(PlasmaZones::lcCore)
                << "SettingsController: failed to connect D-Bus signal" << signalName << "on" << interfaceName;
            failedSubscriptions.append(interfaceName + QStringLiteral(".") + signalName);
        }
    };

    // Listen for external settings changes from the daemon.
    const QString settingsIface = QString(PhosphorProtocol::Service::Interface::Settings);
    subscribeDaemonSignal(settingsIface, QStringLiteral("settingsChanged"), SLOT(onExternalSettingsChanged()));

    // Async window picker reply channel. Emitted by SettingsAdaptor whenever
    // the KWin effect answers a runningWindowsRequested call via
    // provideRunningWindows(). The signal carries the JSON payload directly
    // so clients don't need a follow-up blocking fetch.
    subscribeDaemonSignal(settingsIface, QStringLiteral("runningWindowsAvailable"),
                          SLOT(onRunningWindowsAvailable(QString)));

    // Connect layout D-Bus signals for live updates — route through the 50 ms
    // scheduleLayoutLoad() debounce slot so a burst of signals (e.g. editor
    // save → layoutChanged + layoutListChanged together, or KCM property
    // tweak → layoutPropertyChanged + layoutListChanged) coalesces into
    // one loadLayoutsAsync() call instead of recomputing the full preview
    // list + D-Bus round-trip for every hit.
    const QString layoutIface = QString(PhosphorProtocol::Service::Interface::LayoutRegistry);
    subscribeDaemonSignal(layoutIface, QStringLiteral("layoutCreated"), SLOT(scheduleLayoutLoad()));
    subscribeDaemonSignal(layoutIface, QStringLiteral("layoutDeleted"), SLOT(scheduleLayoutLoad()));
    // layoutChanged fires when a layout is modified (editor saves, zone changes, rename)
    subscribeDaemonSignal(layoutIface, QStringLiteral("layoutChanged"), SLOT(scheduleLayoutLoad()));
    // layoutPropertyChanged fires on compact property mutations (hidden, autoAssign,
    // aspectRatioClass) — Phase 4 of refactor/dbus-performance. The settings UI still
    // triggers a full reload so the layout list view refreshes, but the daemon side
    // saved a full JSON serialization per mutation by not emitting layoutChanged.
    subscribeDaemonSignal(layoutIface, QStringLiteral("layoutPropertyChanged"), SLOT(scheduleLayoutLoad()));
    // layoutListChanged fires when the layout list changes (editor, import, system layout reload)
    subscribeDaemonSignal(layoutIface, QStringLiteral("layoutListChanged"), SLOT(scheduleLayoutLoad()));
    // screenLayoutChanged(QString,QString,int) fires when assignments change (hotkeys, scripts, toggle)
    subscribeDaemonSignal(layoutIface, QStringLiteral("screenLayoutChanged"),
                          SLOT(onScreenLayoutChanged(QString, QString, int)));
    // quickLayoutSlotsChanged fires when quick layout slots are modified externally
    subscribeDaemonSignal(layoutIface, QStringLiteral("quickLayoutSlotsChanged"), SIGNAL(quickLayoutSlotsChanged()));

    // Connect virtual desktop / activity D-Bus signals for reactive updates
    subscribeDaemonSignal(layoutIface, QStringLiteral("virtualDesktopCountChanged"), SLOT(onVirtualDesktopsChanged()));
    subscribeDaemonSignal(layoutIface, QStringLiteral("virtualDesktopNamesChanged"), SLOT(onVirtualDesktopsChanged()));
    subscribeDaemonSignal(layoutIface, QStringLiteral("activitiesChanged"), SLOT(onActivitiesChanged()));
    subscribeDaemonSignal(layoutIface, QStringLiteral("currentActivityChanged"), SLOT(onActivitiesChanged()));

    // Window-rules → settings-side mirror store. The daemon owns
    // windowrules.json; when it persists a change via setAllRules() the
    // adaptor emits `rulesChanged(persisted)`. Without this hook the
    // settings-app's `m_localRuleStore` (which backs the in-process
    // LayoutRegistry's assignment cascade) keeps serving the snapshot
    // it scanned at process start, so daemon-driven rule edits don't
    // affect the settings-side layout preview computation until the
    // next launch. WindowRuleStore::load() is idempotent — a no-change
    // reload doesn't re-emit rulesChanged (per its documented contract).
    //
    // The WindowRuleController also subscribes to this same broadcast
    // on its own connection to drive its model reload; the two
    // subscriptions are independent and the daemon delivers each
    // sessionBus().connect target separately.
    const QString rulesIface = QString(PhosphorProtocol::Service::Interface::WindowRules);
    const bool rulesOk = QDBusConnection::sessionBus().connect(
        QString(PhosphorProtocol::Service::Name), QString(PhosphorProtocol::Service::ObjectPath), rulesIface,
        QStringLiteral("rulesChanged"), this, SLOT(reloadLocalRuleStore(bool)));
    if (!rulesOk) {
        qCWarning(PlasmaZones::lcCore) << "SettingsController: failed to connect D-Bus signal rulesChanged on"
                                       << rulesIface;
        failedSubscriptions.append(rulesIface + QStringLiteral(".rulesChanged"));
    }
}

void SettingsController::reloadLocalRuleStore(bool persisted)
{
    Q_UNUSED(persisted)
    // Reload the on-disk windowrules.json mirror. Idempotent — the store's
    // load() compares hashes and only emits rulesChanged when the file
    // content actually differs from the in-memory set, so a same-process
    // round-trip (the unlikely case of our own apply triggering this) is
    // free of feedback amplification.
    if (m_localRuleStore) {
        m_localRuleStore->load();
    }
}

} // namespace PlasmaZones
