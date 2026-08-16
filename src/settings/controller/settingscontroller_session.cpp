// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Session-state methods for SettingsController:
//   * Font enumeration helpers (Q_INVOKABLE getters consumed by QML).
//   * The QUrl → local-path helper QML hands file-dialog results through.
//   * Stage-assignment setters and quick-layout-slot accessors, plus the
//     screen-state / staged-assignment queries the assignments page reads back.
//   * Virtual-desktop + activity tracking (D-Bus signals from KWin /
//     ActivityManager and the daemon's screen-layout broadcast).
//   * Running-windows queries (used by the assignments page).
//   * The live scrolling-strip preview the Monitors page renders as a
//     layout thumbnail.
//   * Window-geometry persistence (QSettings entry under the
//     organization config file, NOT the main JSON).
//
// Config export/import and the KZones import wrappers live in
// settingscontroller_transfer.cpp; per-screen overrides in
// settingscontroller_perscreen.cpp and the ordering helpers in
// settingscontroller_ordering.cpp.
//
// All methods here are members of PlasmaZones::SettingsController and use its
// private state. Same class as settingscontroller.cpp, separate translation
// unit, no API change.

#include "settingscontroller.h"

#include "settingscontroller_pagekeys.h"

#include "config/configdefaults.h"
#include "core/platform/logging.h"
#include "core/interfaces/settings_interfaces.h"
#include "settings/utils/dbusutils.h"
#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/ZoneJsonKeys.h>

#include <QDBusMessage>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRect>
#include <QScreen>
#include <QSettings>

namespace PlasmaZones {

QStringList SettingsController::fontStylesForFamily(const QString& family) const
{
    return QFontDatabase::styles(family);
}

int SettingsController::fontStyleWeight(const QString& family, const QString& style) const
{
    return QFontDatabase::weight(family, style);
}

bool SettingsController::fontStyleItalic(const QString& family, const QString& style) const
{
    return QFontDatabase::italic(family, style);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Assignment staging — the QML-callable atomic mode+layout staging entry point
// forwards to StagingService and flips the dirty flag; all state + flush logic
// lives in the service.
// ═══════════════════════════════════════════════════════════════════════════════

void SettingsController::stageAssignmentEntry(const QString& screenName, int virtualDesktop, const QString& activityId,
                                              int mode, const QString& snappingLayoutId,
                                              const QString& tilingAlgorithmId)
{
    // QML hands both of these across as bare ints and they reach the wire
    // unchanged, so validate here. A malformed mode is REFUSED, never clamped.
    if (mode < static_cast<int>(PhosphorZones::AssignmentEntry::Snapping)
        || mode > static_cast<int>(PhosphorZones::AssignmentEntry::Scrolling)) {
        qCWarning(lcCore) << "stageAssignmentEntry: refusing unknown mode" << mode << "for screen" << screenName;
        return;
    }
    // A negative desktop is malformed on any reading, so it is refused
    // unconditionally.
    if (virtualDesktop < 0) {
        qCWarning(lcCore) << "stageAssignmentEntry: refusing negative virtual desktop" << virtualDesktop << "for screen"
                          << screenName;
        return;
    }
    // The UPPER bound is only enforced against a count the daemon actually
    // answered with. m_virtualDesktopCount falls back to 1 when the read fails,
    // and that fallback is a DISPLAY value — the same contract the
    // disabled-desktop pruner honours by gating on refreshVirtualDesktops()'s
    // return. Enforcing the bound against it would refuse every legitimate
    // stage onto desktop 2 and up for as long as the daemon was unreachable,
    // and the refusal is silent (void, no QML feedback), so the user would see
    // their pick simply not take.
    if (m_virtualDesktopCountFromDaemon && virtualDesktop > m_virtualDesktopCount) {
        qCWarning(lcCore) << "stageAssignmentEntry: refusing virtual desktop" << virtualDesktop << "outside 0.."
                          << m_virtualDesktopCount << "for screen" << screenName;
        return;
    }
    m_staging.stageAssignmentEntry(screenName, virtualDesktop, activityId, mode, snappingLayoutId, tilingAlgorithmId);
    setNeedsSave(true);
}

void SettingsController::stageScrollingTemplate(const QString& screenName, int virtualDesktop,
                                                const QString& activityId, const QString& templateId)
{
    // Same desktop validation as stageAssignmentEntry, for the same reason:
    // QML hands the number across as a bare int and it reaches the wire
    // unchanged. There is no mode argument to validate here — the flush
    // decides whether to issue this slot's call from the STAGED mode, so a
    // template staged against a context that ends up non-scrolling is simply
    // never written.
    if (virtualDesktop < 0) {
        qCWarning(lcCore) << "stageScrollingTemplate: refusing negative virtual desktop" << virtualDesktop
                          << "for screen" << screenName;
        return;
    }
    if (m_virtualDesktopCountFromDaemon && virtualDesktop > m_virtualDesktopCount) {
        qCWarning(lcCore) << "stageScrollingTemplate: refusing virtual desktop" << virtualDesktop << "outside 0.."
                          << m_virtualDesktopCount << "for screen" << screenName;
        return;
    }
    m_staging.stageScrollingTemplate(screenName, virtualDesktop, activityId, templateId);
    setNeedsSave(true);
}

// Removes any staged entry for the (screen × desktop × activity) assignment
// context — a true unstage, so on Apply the context's daemon-side assignment is
// left untouched.
//
// No QML page calls this today. It is kept as the staging surface's inverse:
// stageAssignmentEntry is the only way in, and a page that stages a pick and
// then wants to take it back (rather than stage the opposite) has no other
// route. Deleting it would leave the surface one-way.
void SettingsController::removeStagedAssignment(const QString& screenName, int virtualDesktop,
                                                const QString& activityId)
{
    m_staging.removeStagedAssignment(screenName, virtualDesktop, activityId);
    // Same bookkeeping as stageAssignmentEntry. Removing a staged entry can
    // leave the staging map empty, but dirtiness here is page-level, not a count
    // of staged entries: the user interacted with the page, and Apply flushes
    // whatever remains, an empty map being a no-op. Nothing in this controller
    // flips needsSave back to false short of save()/load(), so setNeedsSave(true)
    // is the coherent mirror of the other staging mutators.
    setNeedsSave(true);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Path helpers exposed to QML
// ═══════════════════════════════════════════════════════════════════════════════

QString SettingsController::urlToLocalFile(const QUrl& url) const
{
    // QUrl::toLocalFile handles percent-decoding, embedded query/fragment,
    // and uses the canonical scheme-strip path that QtDBus/QFile interop
    // already relies on. Replaces the ad-hoc regex pattern previously
    // duplicated in QML file-picker callbacks.
    return url.toLocalFile();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Quick layout slots (D-Bus to daemon)
// ═══════════════════════════════════════════════════════════════════════════════

QString SettingsController::getQuickLayoutSlot(int slotNumber) const
{
    if (slotNumber < 1 || slotNumber > QUICK_LAYOUT_SLOT_COUNT)
        return {};
    QString staged;
    if (m_staging.stagedSnappingQuickSlot(slotNumber, staged))
        return staged;
    QDBusMessage reply =
        DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                               QStringLiteral("getQuickLayoutSlot"), {QuickSlotModeSnapping, slotNumber});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        return reply.arguments().first().toString();
    return {};
}

void SettingsController::setQuickLayoutSlot(int slotNumber, const QString& layoutId)
{
    if (slotNumber < 1 || slotNumber > QUICK_LAYOUT_SLOT_COUNT)
        return;
    m_staging.stageSnappingQuickSlot(slotNumber, layoutId);
    setNeedsSave(true);
    // The staged value has no NOTIFY of its own, so without this the slot cards
    // never re-read and the pick reverted on screen at the next model rebuild
    // while Apply still wrote it. getQuickLayoutSlot is staging-aware, so the
    // re-read this triggers shows the staged pick rather than the daemon's
    // saved slot. Same emit the per-page Reset path uses.
    Q_EMIT quickLayoutSlotsChanged();
}

QString SettingsController::getQuickLayoutShortcut(int slotNumber) const
{
    if (slotNumber < 1 || slotNumber > QUICK_LAYOUT_SLOT_COUNT)
        return {};
    // The quick-layout shortcuts are config-backed, so the live sequence comes
    // from the store the user's rebind writes to — not from a hardcoded
    // "Meta+Alt+N", which kept showing the factory binding on every slot card
    // after a rebind. The schema registers the factory sequence as the key's
    // default, so an untouched slot still reads back Meta+Alt+N. The getter is
    // 0-based (it indexes the same slot array ShortcutManager walks), while
    // slots are 1-based at every UI and D-Bus surface.
    return m_settings.quickLayoutShortcut(slotNumber - 1);
}

QString SettingsController::getTilingQuickLayoutSlot(int slotNumber) const
{
    if (slotNumber < 1 || slotNumber > QUICK_LAYOUT_SLOT_COUNT)
        return {};
    QString staged;
    if (m_staging.stagedTilingQuickSlot(slotNumber, staged))
        return staged;
    // Tiling quick slots are daemon-backed (mode-keyed LayoutRegistry), same as
    // snapping slots.
    QDBusMessage reply =
        DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                               QStringLiteral("getQuickLayoutSlot"), {QuickSlotModeTiling, slotNumber});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        return reply.arguments().first().toString();
    return {};
}

void SettingsController::setTilingQuickLayoutSlot(int slotNumber, const QString& layoutId)
{
    if (slotNumber < 1 || slotNumber > QUICK_LAYOUT_SLOT_COUNT)
        return;
    m_staging.stageTilingQuickSlot(slotNumber, layoutId);
    setNeedsSave(true);
    // See setQuickLayoutSlot above — the staged value has no NOTIFY of its own,
    // and getTilingQuickLayoutSlot is staging-aware, so this re-read is what
    // keeps the pick on screen until Apply.
    Q_EMIT quickLayoutSlotsChanged();
}

QString SettingsController::getScrollingQuickLayoutSlot(int slotNumber) const
{
    if (slotNumber < 1 || slotNumber > QUICK_LAYOUT_SLOT_COUNT)
        return {};
    QString staged;
    if (m_staging.stagedScrollingQuickSlot(slotNumber, staged))
        return staged;
    // Scrolling quick slots hold native template ids.
    QDBusMessage reply =
        DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                               QStringLiteral("getQuickLayoutSlot"), {QuickSlotModeScrolling, slotNumber});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        return reply.arguments().first().toString();
    return {};
}

void SettingsController::setScrollingQuickLayoutSlot(int slotNumber, const QString& templateId)
{
    if (slotNumber < 1 || slotNumber > QUICK_LAYOUT_SLOT_COUNT)
        return;
    m_staging.stageScrollingQuickSlot(slotNumber, templateId);
    setNeedsSave(true);
    // See setQuickLayoutSlot above — the staged value has no NOTIFY of its
    // own, and the getter is staging-aware, so this re-read is what keeps
    // the pick on screen until Apply.
    Q_EMIT quickLayoutSlotsChanged();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Virtual desktops / activities (D-Bus queries to daemon)
// ═══════════════════════════════════════════════════════════════════════════════

bool SettingsController::refreshVirtualDesktops()
{
    bool countOk = false;
    QDBusMessage countReply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                     QStringLiteral("getVirtualDesktopCount"));
    if (countReply.type() == QDBusMessage::ReplyMessage && !countReply.arguments().isEmpty()) {
        m_virtualDesktopCount = countReply.arguments().first().toInt();
        countOk = true;
    } else {
        // Plain `else`, deliberately: an argument-less ReplyMessage is a
        // successful call that answered nothing, which is no more usable than a
        // transport error and used to match NEITHER branch — leaving the stale
        // count in place while the return said the refresh had failed.
        qCWarning(lcCore) << "refreshVirtualDesktops: getVirtualDesktopCount D-Bus call failed:"
                          << (countReply.type() == QDBusMessage::ReplyMessage
                                  ? QStringLiteral("reply carried no arguments")
                                  : countReply.errorMessage());
        // Mirror the refreshActivities pattern: reset to the single-
        // desktop default on error so QML doesn't render desktop indices
        // the daemon no longer enumerates. A DISPLAY fallback only: the false
        // return is what stops a caller reading it as "the user really is down
        // to one desktop" and pruning every list that names desktop 2 and up.
        m_virtualDesktopCount = 1;
    }
    // Whether the count in hand came from the daemon or is the display
    // fallback. Every caller that REFUSES or DESTROYS on the strength of the
    // count reads this rather than the count itself.
    m_virtualDesktopCountFromDaemon = countOk;

    bool namesOk = false;
    QDBusMessage namesReply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                     QStringLiteral("getVirtualDesktopNames"));
    if (namesReply.type() == QDBusMessage::ReplyMessage && !namesReply.arguments().isEmpty()) {
        m_virtualDesktopNames = namesReply.arguments().first().toStringList();
        namesOk = true;
    } else {
        // Same reasoning as the count branch above.
        qCWarning(lcCore) << "refreshVirtualDesktops: getVirtualDesktopNames D-Bus call failed:"
                          << (namesReply.type() == QDBusMessage::ReplyMessage
                                  ? QStringLiteral("reply carried no arguments")
                                  : namesReply.errorMessage());
        m_virtualDesktopNames.clear();
    }
    // Both halves have to have landed. The two calls fail independently, and a
    // count without names (or names without a count) is a half-refreshed view
    // no destructive caller should act on.
    // Deliberately BOTH: the prune only consumes the count, but a names
    // failure with a good count still means the D-Bus session is degraded,
    // and skipping one prune cycle is the cheap, non-destructive posture
    // (stale disabled entries just wait for the next change event).
    return countOk && namesOk;
}

void SettingsController::refreshActivities()
{
    QDBusMessage availReply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                     QStringLiteral("isActivitiesAvailable"));
    if (availReply.type() == QDBusMessage::ReplyMessage && !availReply.arguments().isEmpty()) {
        m_activitiesAvailable = availReply.arguments().first().toBool();
    } else if (availReply.type() == QDBusMessage::ErrorMessage) {
        qCWarning(lcCore) << "refreshActivities: isActivitiesAvailable D-Bus call failed:" << availReply.errorMessage();
        // Treat a D-Bus error the same as an explicit "false" reply —
        // without this, m_activitiesAvailable kept its previous (likely
        // true) value, the function then entered the `if (true)` branch
        // below, each sub-call also errored, and m_activities /
        // m_currentActivity stayed stale. QML rendered activities the
        // daemon could no longer enumerate.
        m_activitiesAvailable = false;
    }

    if (m_activitiesAvailable) {
        QDBusMessage infoReply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                        QStringLiteral("getAllActivitiesInfo"));
        if (infoReply.type() != QDBusMessage::ErrorMessage) {
            // A malformed or non-array payload comes back as an empty array
            // (the helper warns), and rebuilding from an empty array clears —
            // which is the posture the explicit branches here used to spell
            // out: stop rendering an activity set we can no longer trust to
            // match the daemon's view rather than leaving it stale forever.
            const QJsonArray arr =
                DaemonDBus::replyJsonArray(infoReply, QLatin1String("refreshActivities: getAllActivitiesInfo"));
            m_activities.clear();
            m_activities.reserve(arr.size());
            for (const auto& val : arr) {
                m_activities.append(val.toObject().toVariantMap());
            }
        } else {
            qCWarning(lcCore) << "refreshActivities: getAllActivitiesInfo D-Bus call failed:"
                              << infoReply.errorMessage();
            // Same rationale as the isActivitiesAvailable branch above:
            // a D-Bus failure leaves m_activities stale and QML
            // continues to render activities the daemon can no longer
            // enumerate. Clear so the view honestly reflects "we
            // couldn't talk to the daemon."
            m_activities.clear();
        }

        QDBusMessage currentReply = DaemonDBus::callDaemon(
            QString(PhosphorProtocol::Service::Interface::LayoutRegistry), QStringLiteral("getCurrentActivity"));
        if (currentReply.type() == QDBusMessage::ReplyMessage && !currentReply.arguments().isEmpty()) {
            m_currentActivity = currentReply.arguments().first().toString();
        } else if (currentReply.type() == QDBusMessage::ErrorMessage) {
            qCWarning(lcCore) << "refreshActivities: getCurrentActivity D-Bus call failed:"
                              << currentReply.errorMessage();
            // Same clear-on-error posture as the two branches above and as the
            // activities-unavailable arm below. Keeping the previous id would
            // also defeat the restart change-detection that compares the
            // current activity against the one we last saw.
            m_currentActivity.clear();
        }
    } else {
        // Activities subsystem went away (kactivities not running, plugin
        // disabled, etc.) — clear stale state so QML stops rendering
        // activities that no longer exist.
        m_activities.clear();
        m_currentActivity.clear();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Virtual desktop / activity D-Bus signal handlers
// ═══════════════════════════════════════════════════════════════════════════════

void SettingsController::onVirtualDesktopsChanged()
{
    const bool desktopsRefreshed = refreshVirtualDesktops();

    // Prune both per-mode disabled-desktop lists. This is driven by a
    // KDE virtual-desktop count change (external system event), not by
    // the user clicking on the active page — target the "overview" page
    // (where MonitorStatePage.qml owns the per-mode disabled lists) so
    // dirty state attaches to the right tab regardless of where the user
    // is currently looking. Pattern mirrors the rules handler in
    // settingscontroller.cpp.
    //
    // Wrap the SETTER calls (not just the trailing setNeedsSave) so the
    // synchronous NOTIFY emit from setDisabledDesktops() — which routes
    // through onSettingsPropertyChanged → setNeedsSave — finds the
    // external-edit target set to "overview" instead of whatever leaf
    // the user is currently viewing. Without this wrap, the dirty bit
    // attaches to the active page and the explicit setNeedsSave below
    // becomes a no-op for the intended target.
    bool prunedAny = false;
    {
        ExternalEditScope scope(*this, QStringLiteral("overview"));
        // Only prune against a count that actually came from the daemon. A
        // failed read leaves the member at its display fallback of 1, and
        // pruning against that deletes every disabled-desktop entry for desktop
        // 2 and up, persisted on the next Save.
        //
        // Iterate every mode the (Mode, Family) table knows about so a
        // future mode (e.g. Scrolling) is automatically pruned when the
        // user removes a virtual desktop it referenced. The hand-maintained
        // {Snapping, Autotile} list here used to silently skip Scrolling.
        if (desktopsRefreshed && m_virtualDesktopCount >= 1) {
            for (const auto mode : PhosphorZones::allModes()) {
                QStringList disabled = m_settings.disabledDesktops(mode);
                if (pruneDisabledDesktopEntries(disabled, m_virtualDesktopCount)) {
                    m_settings.setDisabledDesktops(mode, disabled);
                    prunedAny = true;
                }
            }
        }
        if (prunedAny) {
            setNeedsSave(true);
        }
    }

    Q_EMIT virtualDesktopsChanged();
}

void SettingsController::onActivitiesChanged()
{
    refreshActivities();

    // Prune disabled-activity entries that reference removed activities.
    // External event (KDE activity change) — see onVirtualDesktopsChanged
    // above for the same wrap-the-setters rationale (the synchronous
    // NOTIFY emit re-routes dirty bookkeeping through the meta-object
    // loop, which needs m_externalEditStack already top-of-stack set to
    // "overview").
    bool prunedAny = false;
    {
        ExternalEditScope scope(*this, QStringLiteral("overview"));
        QSet<QString> validIds;
        for (const QVariant& v : std::as_const(m_activities)) {
            const QVariantMap map = v.toMap();
            const QString id = map.value(QStringLiteral("id")).toString();
            if (!id.isEmpty()) {
                validIds.insert(id);
            }
        }
        // Gate on the ids the prune actually consumes, not on m_activities: a
        // non-empty payload whose entries carry no usable id passes an
        // m_activities check with an empty validIds, and pruning against an
        // empty set wipes every disabled-activity entry the user has.
        //
        // Iterate every mode the (Mode, Family) table knows about so a
        // future mode (e.g. Scrolling) is automatically pruned when the
        // user removes a KDE activity it referenced. Symmetric with the
        // desktop-prune loop in onVirtualDesktopsChanged above.
        if (!validIds.isEmpty()) {
            for (const auto mode : PhosphorZones::allModes()) {
                QStringList disabledActs = m_settings.disabledActivities(mode);
                if (pruneDisabledActivityEntries(disabledActs, validIds)) {
                    m_settings.setDisabledActivities(mode, disabledActs);
                    prunedAny = true;
                }
            }
        }
        if (prunedAny) {
            setNeedsSave(true);
        }
    }

    Q_EMIT activitiesChanged();
}

void SettingsController::onScreenLayoutChanged(const QString& screenId, const QString& layoutId, int virtualDesktop)
{
    Q_UNUSED(screenId)
    Q_UNUSED(layoutId)
    Q_UNUSED(virtualDesktop)
    // External assignment change (hotkey, script, toggle) — refresh overview.
    // The daemon-side broadcast carries (screenId, layoutId, virtualDesktop)
    // but every current QML consumer reacts by re-querying full state, so
    // the no-arg signal is sufficient and keeps the public Q_SIGNAL surface
    // small. If a future consumer needs per-screen targeting, widen the
    // signal in lockstep with the QML side (single-signal source of truth).
    Q_EMIT screenLayoutChanged();
}

namespace {

// Parses the daemon's running-windows JSON payload into a QVariantList of
// {windowClass, appName, caption, desktopFile} maps ready for QML consumption. The
// synchronous getRunningWindows() predecessor was removed in Phase 6 of
// refactor/dbus-performance; only onRunningWindowsAvailable calls this now.
// Anonymous namespace keeps this TU-local — same convention as the
// settingscontroller_ordering.cpp helpers so unity-build batching can't
// produce a duplicate-symbol surprise.
QVariantList parseRunningWindowsJson(const QString& json)
{
    if (json.isEmpty()) {
        return {};
    }
    // A signal payload rather than a method reply, so DaemonDBus::replyJsonArray
    // does not fit — but it warns for the same reason: the caller sees an empty
    // list either way, and without a log line "no windows are open" and "the
    // effect sent something unreadable" are the same state.
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcCore) << "parseRunningWindowsJson: payload is not valid JSON:" << parseError.errorString();
        return {};
    }
    if (!doc.isArray()) {
        qCWarning(lcCore) << "parseRunningWindowsJson: payload JSON is not an array";
        return {};
    }

    QVariantList result;
    const QJsonArray array = doc.array();
    result.reserve(array.size());
    for (const QJsonValue& value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject obj = value.toObject();
        QVariantMap item;
        item[QStringLiteral("windowClass")] = obj[QLatin1String("windowClass")].toString();
        item[QStringLiteral("appName")] = obj[QLatin1String("appName")].toString();
        item[QStringLiteral("caption")] = obj[QLatin1String("caption")].toString();
        // `desktopFile` is forwarded verbatim — older effect builds that
        // don't include the field will produce an empty string, which the
        // WindowPickerDialog hides from its DesktopFile mode.
        item[QStringLiteral("desktopFile")] = obj[QLatin1String("desktopFile")].toString();
        result.append(item);
    }
    return result;
}

} // namespace

// ── Running window picker (async flow) ──────────────────────────────────────
//
// The QML picker dialog calls this and binds to runningWindowsAvailable(list)
// — no blocking D-Bus round-trip. The controller caches the most recent list in
// m_cachedRunningWindows so QML dialogs can read it directly between calls. The
// old synchronous getRunningWindows() was removed in Phase 6 of
// refactor/dbus-performance.
//
// This invalidates the cache before issuing the call, so QML readers binding to
// cachedRunningWindows() during a refresh see an empty list (intentional —
// distinguishes "loading" from "stale-but-cached").
//
// A client-side timeout guards against the KWin effect being unloaded: if no
// reply arrives within RunningWindowsTimeoutMs, we emit runningWindowsTimedOut()
// so the QML dialog can show a "no response" state instead of hanging on a
// spinner forever.
void SettingsController::requestRunningWindows()
{
    // Fire-and-forget: the daemon emits runningWindowsRequested to the
    // KWin effect, which answers via provideRunningWindows, which the
    // daemon fans out on runningWindowsAvailable — caught by our
    // onRunningWindowsAvailable slot. The UI thread never blocks.
    //
    // If the daemon isn't running, dispatching the call would still go
    // through D-Bus auto-start machinery (or fail outright) and the user
    // would see an empty list for the full timeout window with no
    // indication the request never made it out. Drop the cached list so
    // a subsequent cachedRunningWindows() read reflects the daemon-down
    // state (rather than a stale snapshot), then surface the empty-reply
    // via an immediate timeout so the QML page renders the requestTimedOut
    // placeholder state instead of a generic "no windows" message.
    if (!m_daemonController.isRunning()) {
        m_cachedRunningWindows.clear();
        m_runningWindowsTimeout.stop();
        Q_EMIT runningWindowsTimedOut();
        return;
    }
    // Start (or restart) the client-side timeout guard. Repeated calls
    // coalesce — the most recent deadline wins, matching the fire-and-
    // forget semantics on the daemon side.
    //
    // Invalidate the cached list so a subsequent timeout truly reflects
    // "no fresh data" — without this, QML readers would continue to see
    // the stale list and never realise the refresh attempt failed.
    m_cachedRunningWindows.clear();
    m_runningWindowsTimeout.start();
    DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::Settings),
                           QStringLiteral("requestRunningWindows"));
}

void SettingsController::onRunningWindowsAvailable(const QString& json)
{
    // Reply arrived — stop the timeout timer so a stale runningWindowsTimedOut()
    // doesn't fire after we've already served fresh data.
    m_runningWindowsTimeout.stop();
    m_cachedRunningWindows = parseRunningWindowsJson(json);
    Q_EMIT runningWindowsAvailable(m_cachedRunningWindows);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Screen state query
// ═══════════════════════════════════════════════════════════════════════════════

QVariantList SettingsController::getScreenStates() const
{
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                QStringLiteral("getScreenStates"));
    const QJsonArray arr = DaemonDBus::replyJsonArray(reply, QLatin1String("getScreenStates"));
    QVariantList result;
    result.reserve(arr.size());
    for (const QJsonValue& value : arr) {
        if (value.isObject())
            result.append(value.toObject().toVariantMap());
    }
    return result;
}

QVariantMap SettingsController::getScrollingBlueprintProgress(const QString& screenId) const
{
    if (screenId.isEmpty()) {
        return {};
    }
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::Scrolling),
                                                QStringLiteral("blueprintProgressJson"), {screenId});
    const QJsonObject obj = DaemonDBus::replyJsonObject(reply, QLatin1String("getScrollingBlueprintProgress"));
    // A screen the daemon gated out answers "{}", which parses to an empty
    // object and reaches QML as an empty map. Passed through as-is rather
    // than defaulted to zeroes: the caller distinguishes "no blueprint to
    // describe" from "a blueprint with nothing spent", and zeroes would
    // collapse the two.
    if (obj.isEmpty()) {
        return {};
    }
    return obj.toVariantMap();
}

QVariantList SettingsController::getScrollingStripPreview(const QString& screenId) const
{
    if (screenId.isEmpty()) {
        return {};
    }
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::Scrolling),
                                                QStringLiteral("visibleStripJson"), {screenId});
    const QJsonArray arr = DaemonDBus::replyJsonArray(reply, QLatin1String("getScrollingStripPreview"));
    // Shape the rects into the zone maps LayoutThumbnail/LayoutCard render,
    // mirroring the daemon's OSD strip preview (numbers ascending in strip
    // order; no custom colors).
    QVariantList zones;
    zones.reserve(arr.size());
    // Counts the tiles actually EMITTED, which is what the fallback numbering
    // and the synthetic ids below are keyed on. The raw array index leaves a
    // hole wherever an element is skipped: a four-element payload whose second
    // element is malformed numbered its three tiles 1, 3, 4, so the thumbnail
    // labelled slots the Snap-to-Zone digits do not address.
    int emitted = 0;
    for (int i = 0; i < arr.size(); ++i) {
        if (!arr.at(i).isObject()) {
            // A non-object element reads back as an all-default rect and renders
            // as a zero-size zone at the origin. Skip the phantom tile.
            continue;
        }
        ++emitted;
        const QJsonObject rect = arr.at(i).toObject();
        QVariantMap relGeo;
        relGeo[PhosphorZones::ZoneJsonKeys::X] = rect.value(PhosphorZones::ZoneJsonKeys::X).toDouble();
        relGeo[PhosphorZones::ZoneJsonKeys::Y] = rect.value(PhosphorZones::ZoneJsonKeys::Y).toDouble();
        relGeo[PhosphorZones::ZoneJsonKeys::Width] = rect.value(PhosphorZones::ZoneJsonKeys::Width).toDouble();
        relGeo[PhosphorZones::ZoneJsonKeys::Height] = rect.value(PhosphorZones::ZoneJsonKeys::Height).toDouble();
        QVariantMap zone;
        // The wire's zoneNumber is the tile's 1-based visible slot in strip
        // order — the same sequential space the Snap-to-Zone digits target,
        // so the thumbnail labels exactly what the digits do. The fallback is
        // for a payload from an older daemon that carries no zoneNumber, and it
        // counts EMITTED tiles so a skipped malformed element does not leave a
        // hole in the numbering.
        zone[PhosphorZones::ZoneJsonKeys::ZoneNumber] =
            rect.value(PhosphorZones::ZoneJsonKeys::ZoneNumber).toInt(emitted);
        zone[PhosphorZones::ZoneJsonKeys::RelativeGeometry] = relGeo;
        // Namespaced, never a bare index. These are render-only synthetic
        // zones with no persisted identity, so nothing resolves them today —
        // but a bare "0"/"1"/"2" is indistinguishable from a real zone id, and
        // any consumer that starts keying on zone.id (a delegate reuse key,
        // selection state) would collide across screens. CLAUDE.md: zone IDs
        // everywhere, never indices.
        zone[PhosphorZones::ZoneJsonKeys::Id] = QStringLiteral("strip:%1:%2").arg(screenId).arg(emitted);
        // Nothing reads these two. They keep a synthetic strip zone the same
        // shape as a real one, so the thumbnail delegate takes one path.
        zone[PhosphorZones::ZoneJsonKeys::Name] = QString();
        zone[PhosphorZones::ZoneJsonKeys::UseCustomColors] = false;
        zones.append(zone);
    }
    return zones;
}

QVariantMap SettingsController::getStagedAssignment(const QString& screenName, int virtualDesktop,
                                                    const QString& activityId) const
{
    auto* s = m_staging.stagedAssignmentFor(screenName, virtualDesktop, activityId);
    if (!s)
        return {};
    QVariantMap map;
    if (s->snappingLayoutId.has_value())
        map[QStringLiteral("layoutId")] = *s->snappingLayoutId;
    if (s->tilingAlgorithmId.has_value()) {
        const QString& val = *s->tilingAlgorithmId;
        map[QStringLiteral("algorithmId")] =
            PhosphorLayout::LayoutId::isAutotile(val) ? PhosphorLayout::LayoutId::extractAlgorithmId(val) : val;
    }
    // Key PRESENCE is the staged/not-staged answer here, as it is for the two
    // slots above: the staged value is legitimately an empty string when the
    // user picked "inherit the default", so a value test would read that back
    // as untouched and the dropdown would snap away from the user's pick on
    // the next page visit.
    if (s->scrollingTemplateId.has_value())
        map[QStringLiteral("scrollingTemplateId")] = *s->scrollingTemplateId;
    // Explicit mode takes priority (stageAssignmentEntry path)
    if (s->stagedMode.has_value()) {
        map[QStringLiteral("mode")] = *s->stagedMode;
    } else {
        // Infer mode from which fields are staged (per-field path). This only
        // ever answers Snapping or Autotile: the per-field stagers exist for a
        // layout id and an algorithm id, and Scrolling has neither, so a
        // Scrolling pick arrives only through the explicit mode above.
        if (s->tilingAlgorithmId.has_value() && !s->tilingAlgorithmId->isEmpty())
            map[QStringLiteral("mode")] = static_cast<int>(PhosphorZones::AssignmentEntry::Autotile);
        else if (s->snappingLayoutId.has_value() && !s->snappingLayoutId->isEmpty())
            map[QStringLiteral("mode")] = static_cast<int>(PhosphorZones::AssignmentEntry::Snapping);
    }
    return map;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Per-screen autotile / snapping / zone-selector overrides live in
// settingscontroller_perscreen.cpp.
// ═══════════════════════════════════════════════════════════════════════════════

QVariantMap SettingsController::loadWindowGeometry() const
{
    QSettings settings;
    settings.beginGroup(ConfigDefaults::settingsAppWindowGroup());
    QVariantMap geo;
    int w = settings.value(ConfigDefaults::settingsAppWindowWidthKey(), 0).toInt();
    int h = settings.value(ConfigDefaults::settingsAppWindowHeightKey(), 0).toInt();
    int x = settings.value(ConfigDefaults::settingsAppWindowXKey()).toInt();
    int y = settings.value(ConfigDefaults::settingsAppWindowYKey()).toInt();
    // Both coordinates or neither. A file carrying only X restores that X and a
    // silent y=0, putting the window somewhere the user never left it.
    bool hasPosition = settings.contains(ConfigDefaults::settingsAppWindowXKey())
        && settings.contains(ConfigDefaults::settingsAppWindowYKey());

    // Floor for a remembered size. saveWindowGeometry is the only writer, but a
    // hand-edited or truncated file can carry something like 12x8, and QML
    // applies any positive size verbatim, leaving a window too small to grab.
    constexpr int kMinRestoredWindowWidth = 480;
    constexpr int kMinRestoredWindowHeight = 360;

    // Validate against available screen geometry
    QRect virtualGeo;
    for (auto* screen : QGuiApplication::screens())
        virtualGeo = virtualGeo.united(screen->availableGeometry());

    if (w > 0 && h > 0 && !virtualGeo.isEmpty()) {
        w = qMin(w, virtualGeo.width());
        h = qMin(h, virtualGeo.height());
        // Floor after the screen clamp, and never above what the screen can
        // actually hold.
        w = qMax(qMin(kMinRestoredWindowWidth, virtualGeo.width()), w);
        h = qMax(qMin(kMinRestoredWindowHeight, virtualGeo.height()), h);
    }

    // Check if the centre of the saved window is on any screen. Hoisted OUT of
    // the size block: a file carrying a position but no size (a truncated write,
    // a hand edit) took no containment test at all, so a coordinate on a monitor
    // that is now gone was restored verbatim and the window opened off-screen.
    // With w and h at 0 the centre is just the saved corner, which is the right
    // question to ask when there is no size to offset by.
    if (hasPosition && !virtualGeo.isEmpty() && !virtualGeo.contains(QPoint(x + w / 2, y + h / 2))) {
        hasPosition = false; // off-screen, let WM place it
    }

    geo[ConfigDefaults::settingsAppWindowWidthKey()] = w;
    geo[ConfigDefaults::settingsAppWindowHeightKey()] = h;
    geo[ConfigDefaults::settingsAppWindowXKey()] = x;
    geo[ConfigDefaults::settingsAppWindowYKey()] = y;
    // Derived, not persisted: the four keys above are QSettings entries this
    // function reads back, but "hasPosition" is this function's own verdict on
    // them (both coordinates present AND the centre still on a screen). It is a
    // plain map key for QML, deliberately not a ConfigDefaults accessor —
    // nothing ever writes it to a config file.
    geo[QStringLiteral("hasPosition")] = hasPosition;
    settings.endGroup();
    return geo;
}

void SettingsController::saveWindowGeometry(int x, int y, int width, int height)
{
    QSettings settings;
    settings.beginGroup(ConfigDefaults::settingsAppWindowGroup());
    settings.setValue(ConfigDefaults::settingsAppWindowXKey(), x);
    settings.setValue(ConfigDefaults::settingsAppWindowYKey(), y);
    settings.setValue(ConfigDefaults::settingsAppWindowWidthKey(), width);
    settings.setValue(ConfigDefaults::settingsAppWindowHeightKey(), height);
    settings.endGroup();
}

} // namespace PlasmaZones
