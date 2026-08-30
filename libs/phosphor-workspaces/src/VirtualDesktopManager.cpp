// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWorkspaces/VirtualDesktopManager.h>

#include <PhosphorIdentity/VirtualScreenId.h>

#include <algorithm>
#include <utility>

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcVirtualDesktops, "plasmazones.workspaces.vdm", QtWarningMsg)

namespace PhosphorWorkspaces {

namespace {
const QString& kwinService()
{
    static const QString service = QStringLiteral("org.kde.KWin");
    return service;
}
const QString& kwinPath()
{
    static const QString path = QStringLiteral("/VirtualDesktopManager");
    return path;
}
const QString& kwinInterface()
{
    static const QString iface = QStringLiteral("org.kde.KWin.VirtualDesktopManager");
    return iface;
}
} // namespace

VirtualDesktopManager::VirtualDesktopManager(QObject* parent)
    : QObject(parent)
{
    m_refreshRetryTimer.setSingleShot(true);
    m_refreshRetryTimer.setInterval(RefreshRetryMs);
    connect(&m_refreshRetryTimer, &QTimer::timeout, this, &VirtualDesktopManager::refreshFromKWin);
}

VirtualDesktopManager::~VirtualDesktopManager()
{
    stop();
}

bool VirtualDesktopManager::init()
{
    initKWinDBus();
    if (m_useKWinDBus) {
        // Bind-time seed, blocking and bounded: init() runs before anything is
        // driving an event loop, and every caller of desktopIds()/desktopCount()
        // between here and start() would otherwise read an empty cache.
        refreshFromKWin();
    }

    // KWin absent right now is not a permanent verdict: the daemon can start
    // before the compositor, and KWin can restart under a running daemon.
    // Watch for the name and bind on registration instead of latching off.
    if (!m_kwinWatcher) {
        m_kwinWatcher = new QDBusServiceWatcher(kwinService(), QDBusConnection::sessionBus(),
                                                QDBusServiceWatcher::WatchForRegistration, this);
        connect(m_kwinWatcher, &QDBusServiceWatcher::serviceRegistered, this, [this](const QString&) {
            qCWarning(lcVirtualDesktops) << "KWin appeared on the bus; (re)binding the virtual-desktop interface";
            // Bind only. A re-bind drops the old proxy's subscriptions, so a
            // RUNNING manager re-subscribes and re-reads here; a STOPPED one
            // must not, or stop()'s contract ("takes no further KWin events")
            // would quietly lapse on the next KWin restart and every event
            // would then take the blocking refresh path.
            initKWinDBus();
            if (m_useKWinDBus && m_running) {
                subscribeKWinSignals(true);
                refreshFromKWin();
            }
        });
    }

    return m_useKWinDBus;
}

void VirtualDesktopManager::initKWinDBus()
{
    // Re-bind is idempotent: a KWin restart re-registers the name and the old
    // proxy is stale, so drop it (with its subscriptions) before probing again.
    if (m_kwinVDInterface) {
        subscribeKWinSignals(false);
        // deleteLater, never delete: the proxy is a QObject parented to this,
        // and this can be reached from a slot the proxy itself is delivering.
        m_kwinVDInterface->deleteLater();
        m_kwinVDInterface = nullptr;
        m_useKWinDBus = false;
    }

    m_kwinVDInterface =
        new QDBusInterface(kwinService(), kwinPath(), kwinInterface(), QDBusConnection::sessionBus(), this);

    if (!m_kwinVDInterface->isValid()) {
        m_kwinVDInterface->deleteLater();
        m_kwinVDInterface = nullptr;
        return;
    }

    m_useKWinDBus = true;
    refreshRowsFromKWin();
    // Subscribing and refreshing are NOT part of binding: this is also the
    // KWin-restart path, and a stopped manager must come out of it bound but
    // silent. init() seeds the cache and start() subscribes.
}

void VirtualDesktopManager::subscribeKWinSignals(bool subscribe)
{
    if (subscribe == m_subscribed) {
        return;
    }
    m_subscribed = subscribe;

    QDBusConnection bus = QDBusConnection::sessionBus();
    const auto wire = [&](const char* signal, const char* slot) {
        if (subscribe) {
            bus.connect(kwinService(), kwinPath(), kwinInterface(), QString::fromLatin1(signal), this, slot);
        } else {
            bus.disconnect(kwinService(), kwinPath(), kwinInterface(), QString::fromLatin1(signal), this, slot);
        }
    };

    wire("currentChanged", SLOT(onKWinCurrentChanged(QString)));
    wire("countChanged", SLOT(onNumberOfDesktopsChanged(int)));
    wire("desktopCreated", SLOT(onKWinDesktopCreated(QString)));
    wire("desktopRemoved", SLOT(onKWinDesktopRemoved(QString)));
    // A live grid reshape (e.g. 1×4 → 2×2) changes `rows` WITHOUT changing
    // the desktop count, so it fires neither countChanged nor created/removed
    // — without this the cached row count goes stale and cross-desktop
    // directional navigation computes neighbours against the wrong grid shape.
    wire("rowsChanged", SLOT(onKWinDesktopRowsChanged()));
}

void VirtualDesktopManager::applyDesktopListReply(const QDBusMessage& reply)
{
    struct DesktopInfo
    {
        int position;
        QString id;
        QString name;
    };
    QList<DesktopInfo> desktops;

    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        QVariant outerVariant = reply.arguments().at(0);
        QDBusVariant dbusVariant = outerVariant.value<QDBusVariant>();
        QVariant innerVariant = dbusVariant.variant();

        if (innerVariant.userType() == qMetaTypeId<QDBusArgument>()) {
            const QDBusArgument& arg = *static_cast<const QDBusArgument*>(innerVariant.constData());

            arg.beginArray();
            while (!arg.atEnd()) {
                DesktopInfo info;
                arg.beginStructure();
                arg >> info.position >> info.id >> info.name;
                arg.endStructure();
                desktops.append(info);
            }
            arg.endArray();

            std::sort(desktops.begin(), desktops.end(), [](const DesktopInfo& a, const DesktopInfo& b) {
                return a.position < b.position;
            });
        }
    }

    QStringList ids;
    QStringList names;
    ids.reserve(desktops.size());
    names.reserve(desktops.size());
    for (const auto& desktop : desktops) {
        ids.append(desktop.id);
        // RAW, exactly as KWin reports it — an empty entry means "unnamed".
        // Placeholder filling happens in desktopNames(), never here: the
        // named-workspace claim compares declared names against these, and a
        // placeholder would let a workspace named "Desktop 3" claim an unnamed
        // desktop as its own.
        names.append(desktop.name);
    }

    // A D-Bus error, a KWin caught mid-restart, or a reply whose payload did
    // not demarshal all land here as an empty list. KWin never has zero
    // desktops, so an empty list while we already knew of some is a FAILED
    // refresh — publishing it would make the reconciler drop every slice and
    // the engines reap all per-desktop state. Keep what we have and re-ask.
    //
    // An empty list while we know of NONE is the same failure, not a quieter
    // one: init()'s blocking seed can land while KWin is still coming up, and
    // with no previous list to protect this path used to arm nothing, emit
    // nothing and simply leave the cache permanently empty. Both cases retry.
    if (ids.isEmpty()) {
        if (m_refreshRetries < MaxRefreshRetries) {
            ++m_refreshRetries;
            qCWarning(lcVirtualDesktops) << "desktop list refresh returned nothing;" << m_desktopIds.size()
                                         << "desktops are known and are being kept, retry" << m_refreshRetries << "of"
                                         << MaxRefreshRetries;
            m_refreshRetryTimer.start();
        } else {
            qCWarning(lcVirtualDesktops) << "desktop list refresh kept failing; giving up until the next KWin event";
            // Hand the budget back. The next KWin event is a different episode
            // and deserves its own retries; leaving the counter at the ceiling
            // gave every later transient failure zero attempts.
            m_refreshRetries = 0;
            // desktopCreated / desktopRemoved announce nothing themselves — the
            // count and the per-screen clamp ride the settled list, which just
            // failed. Re-announce the count we still hold so consumers re-diff
            // against KWin rather than sitting on a state this refresh was
            // supposed to correct. The VALUE is unchanged on purpose; it is the
            // notification that was lost, not the number.
            //
            // Consumers do more than re-diff on this: the daemon's handler
            // also cancels any in-flight drag-insert previews, so a refresh
            // that gives up during a live drag ends that drag's previews. That
            // is ACCEPTED rather than routed around with a separate failure
            // signal — the previews are resolved against desktop state this
            // refresh has just failed, several times over, to confirm, and
            // dropping them is the conservative answer. It costs the user one
            // re-drag in a session where KWin is already not answering.
            Q_EMIT desktopCountChanged(m_desktopCount);
        }
        return;
    }
    m_refreshRetries = 0;

    const QStringList previousIds = m_desktopIds;
    m_desktopIds = ids;
    m_desktopNames = names;

    // The settled list is announced FIRST, before the count and the per-screen
    // clamp it drives. setDesktopCount -> clampScreenDesktopsToCount emits
    // screenDesktopChanged, and the reconciler resolves those 1-based numbers
    // against the id list it last settled: announcing the count first left it
    // resolving every clamp report against the PREVIOUS list, so an externally
    // removed desktop made each report name the wrong id, which reads as a
    // foreign switch and drives a real snap-back onto a desktop the screen was
    // never on. The daemon's own ordering requirement is clamp-before-count,
    // and both of those still happen inside setDesktopCount, in that order.
    if (m_desktopIds != previousIds) {
        Q_EMIT desktopListChanged(m_desktopIds);
    }

    // The parsed list is the authority on the count — no separate blocking
    // `count` property read, and no phantom padded names from a count that
    // came from a different instant than the list.
    if (!m_desktopIds.isEmpty()) {
        setDesktopCount(m_desktopIds.size());
    }

    resolveCurrentFromId();
}

void VirtualDesktopManager::resolveCurrentFromId()
{
    if (m_currentDesktopId.isEmpty() || m_desktopIds.isEmpty()) {
        return;
    }
    const int idx = m_desktopIds.indexOf(m_currentDesktopId);
    if (idx < 0) {
        return; // the current desktop vanished; countChanged clamps the index
    }
    if (m_currentDesktop == idx + 1) {
        return;
    }
    // The id did not move but its INDEX did (a renumber from a mid-list
    // removal or reorder). KWin sends no currentChanged for that, so without
    // this every consumer of the 1-based number stays on the old slot.
    m_currentDesktop = idx + 1;
    Q_EMIT currentDesktopChanged(m_currentDesktop);
}

void VirtualDesktopManager::setDesktopCount(int count)
{
    const int next = qMax(1, count);
    if (m_desktopCount == next) {
        return;
    }
    m_desktopCount = next;

    if (m_currentDesktop > m_desktopCount) {
        m_currentDesktop = m_desktopCount;
        Q_EMIT currentDesktopChanged(m_currentDesktop);
    }

    // Per-screen clamp BEFORE the count announcement: the daemon's
    // desktopCountChanged handler relies on every screen whose number this
    // change moved having already been re-diffed through screenDesktopChanged.
    clampScreenDesktopsToCount();

    Q_EMIT desktopCountChanged(m_desktopCount);
}

void VirtualDesktopManager::refreshRowsFromKWin()
{
    if (!m_kwinVDInterface || !m_kwinVDInterface->isValid()) {
        return;
    }
    const QVariant rowsVar = m_kwinVDInterface->property("rows");
    if (rowsVar.isValid()) {
        // Clamp to >= 1 so a missing / zero property can't divide the grid
        // arithmetic by zero.
        m_desktopRows = qMax(1, rowsVar.toInt());
    }
}

void VirtualDesktopManager::refreshFromKWin()
{
    if (!m_kwinVDInterface || !m_kwinVDInterface->isValid()) {
        return;
    }

    QDBusMessage getDesktopsMsg = QDBusMessage::createMethodCall(
        kwinService(), kwinPath(), QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    getDesktopsMsg << kwinInterface() << QStringLiteral("desktops");

    if (!m_running) {
        // Bind-time / stopped path only: one blocking read to seed the cache
        // before any event loop is driving us.
        const QVariant currentVar = m_kwinVDInterface->property("current");
        if (currentVar.isValid()) {
            m_currentDesktopId = currentVar.toString();
        }
        QDBusMessage reply = QDBusConnection::sessionBus().call(getDesktopsMsg, QDBus::Block, 1000);
        applyDesktopListReply(reply);
        return;
    }

    ++m_refreshGeneration;
    const uint thisGeneration = m_refreshGeneration;

    QDBusPendingCall pendingCall = QDBusConnection::sessionBus().asyncCall(getDesktopsMsg);
    QDBusPendingCallWatcher* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, thisGeneration](QDBusPendingCallWatcher* w) {
        if (thisGeneration != m_refreshGeneration) {
            w->deleteLater();
            return;
        }

        QDBusPendingReply<QDBusVariant> reply = *w;
        applyDesktopListReply(reply.reply());

        w->deleteLater();
    });
}

void VirtualDesktopManager::onKWinCurrentChanged(const QString& desktopId)
{
    if (desktopId.isEmpty()) {
        return;
    }
    // The id is the identity; the index is derived. Record it even when the
    // list has not caught up yet, so the in-flight refresh resolves the index
    // on arrival instead of this handler guessing desktop 1 and firing a
    // spurious switch.
    m_currentDesktopId = desktopId;

    const int idx = m_desktopIds.indexOf(desktopId);
    if (idx < 0) {
        return; // unknown mid-refresh; resolveCurrentFromId() finishes the job
    }

    if (m_currentDesktop == idx + 1) {
        return;
    }

    m_currentDesktop = idx + 1;
    Q_EMIT currentDesktopChanged(m_currentDesktop);
}

void VirtualDesktopManager::onKWinDesktopCreated(const QString& desktopId)
{
    Q_EMIT kwinDesktopCreated(desktopId);
    // The count and the per-screen clamp follow from the settled list, which
    // the async refresh applies through setDesktopCount — emitting here would
    // announce the PRE-event count.
    refreshFromKWin();
}

void VirtualDesktopManager::onKWinDesktopRemoved(const QString& desktopId)
{
    Q_EMIT kwinDesktopRemoved(desktopId);
    refreshFromKWin();
}

void VirtualDesktopManager::onKWinDesktopRowsChanged()
{
    // Only the grid shape moved; the desktop list is untouched, so this is the
    // one property read and nothing else.
    refreshRowsFromKWin();
}

void VirtualDesktopManager::start()
{
    if (m_running) {
        return;
    }

    m_running = true;

    if (m_useKWinDBus) {
        subscribeKWinSignals(true);
        refreshFromKWin();
    }
}

void VirtualDesktopManager::stop()
{
    if (!m_running && !m_subscribed) {
        return;
    }
    m_running = false;
    // Drop the subscriptions: a stopped manager that stays subscribed keeps
    // taking KWin events, and with m_running false every one of them takes the
    // BLOCKING refresh path.
    subscribeKWinSignals(false);
    m_refreshRetryTimer.stop();
    m_refreshRetries = 0;
    // Invalidate any in-flight async reply so it cannot land after the stop.
    ++m_refreshGeneration;
}

int VirtualDesktopManager::currentDesktop() const
{
    return m_useKWinDBus ? m_currentDesktop : 1;
}

int VirtualDesktopManager::currentDesktopForScreen(const QString& screenId) const
{
    const auto it = m_screenDesktops.constFind(screenId);
    if (it != m_screenDesktops.constEnd()) {
        return it.value();
    }

    // The map is keyed by PHYSICAL output id (updateScreenDesktop is fed from
    // the effect's per-output report), but most callers ask with an EFFECTIVE
    // id, and a subdivided output only ever produces its `/vs:N` children. An
    // exact-key miss on a virtual id is therefore the normal case there, not an
    // unknown screen — resolve it against the parent output before falling back
    // to the global desktop, which would otherwise be wrong on every virtual
    // screen of a monitor running its own desktop.
    if (PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        const auto physical =
            m_screenDesktops.constFind(PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId));
        if (physical != m_screenDesktops.constEnd()) {
            return physical.value();
        }
    }

    return currentDesktop();
}

bool VirtualDesktopManager::hasScreenDesktopReport(const QString& screenId) const
{
    if (m_screenDesktops.contains(screenId)) {
        return true;
    }
    if (PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        return m_screenDesktops.contains(PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId));
    }
    return false;
}

bool VirtualDesktopManager::perScreenModeActive() const
{
    if (m_screenDesktops.size() < 2) {
        return false;
    }
    auto it = m_screenDesktops.constBegin();
    const int first = it.value();
    for (++it; it != m_screenDesktops.constEnd(); ++it) {
        if (it.value() != first) {
            return true;
        }
    }
    return false;
}

void VirtualDesktopManager::updateScreenDesktop(const QString& screenId, int desktop)
{
    if (screenId.isEmpty() || desktop < 1) {
        return;
    }
    if (m_screenDesktops.value(screenId, -1) == desktop) {
        return;
    }
    m_screenDesktops.insert(screenId, desktop);
    Q_EMIT screenDesktopChanged(screenId, desktop);
}

void VirtualDesktopManager::removeScreenDesktop(const QString& screenId)
{
    m_screenDesktops.remove(screenId);
}

void VirtualDesktopManager::renameScreen(const QString& oldId, const QString& newId)
{
    if (oldId.isEmpty() || newId.isEmpty() || oldId == newId) {
        return;
    }
    const auto from = m_screenDesktops.constFind(oldId);
    if (from == m_screenDesktops.constEnd()) {
        return; // nothing recorded under the dead id
    }
    const int desktop = from.value();
    m_screenDesktops.remove(oldId);
    // A live-id row already exists: it is a fresher report than the one we
    // would carry over, so it stands and the carried value is discarded. The
    // value under newId did not change, so nothing is emitted.
    if (m_screenDesktops.contains(newId)) {
        return;
    }
    m_screenDesktops.insert(newId, desktop);
    Q_EMIT screenDesktopChanged(newId, desktop);
}

void VirtualDesktopManager::clampScreenDesktopsToCount()
{
    // Clamp only entries above the live count: KWin renumbers on desktop
    // removal, so a screen pinned past the new count is pulled down to it here.
    // This does NOT re-identify a surviving entry whose desktop was renumbered
    // by a mid-list removal — the effect re-reports each output's true desktop
    // shortly after via updateScreenDesktop, which is authoritative for that.
    //
    // Mutate first, then emit the captured value — emitting mid-iteration could
    // re-enter updateScreenDesktop and invalidate the hash iterator, and a
    // re-entrant write must not change the value we report for this clamp.
    QList<std::pair<QString, int>> clamped;
    for (auto it = m_screenDesktops.begin(); it != m_screenDesktops.end(); ++it) {
        if (it.value() > m_desktopCount) {
            it.value() = m_desktopCount;
            clamped.append({it.key(), m_desktopCount});
        }
    }
    for (const auto& [screenId, desktop] : clamped) {
        Q_EMIT screenDesktopChanged(screenId, desktop);
    }
}

void VirtualDesktopManager::setCurrentDesktop(int desktop)
{
    if (!m_useKWinDBus || !m_kwinVDInterface) {
        return;
    }
    // Bound against the ID LIST, not the cached count: the count can be a
    // KWin countChanged ahead of the settled list, and an index past the list
    // would silently do nothing here anyway.
    if (desktop < 1 || desktop > m_desktopIds.size()) {
        return;
    }

    // Async: this sits on the shortcut/verb hot path and a blocking property
    // write would stall the daemon for a compositor round trip. The result
    // arrives as KWin's own currentChanged.
    QDBusMessage setCurrentMsg = QDBusMessage::createMethodCall(
        kwinService(), kwinPath(), QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Set"));
    setCurrentMsg << kwinInterface() << QStringLiteral("current")
                  << QVariant::fromValue(QDBusVariant(m_desktopIds.at(desktop - 1)));
    QDBusConnection::sessionBus().asyncCall(setCurrentMsg);
}

void VirtualDesktopManager::createDesktop(uint position, const QString& name)
{
    if (!m_useKWinDBus || !m_kwinVDInterface) {
        return;
    }
    m_kwinVDInterface->asyncCall(QStringLiteral("createDesktop"), position, name);
}

void VirtualDesktopManager::removeDesktop(const QString& desktopId)
{
    if (!m_useKWinDBus || !m_kwinVDInterface || desktopId.isEmpty()) {
        return;
    }
    m_kwinVDInterface->asyncCall(QStringLiteral("removeDesktop"), desktopId);
}

void VirtualDesktopManager::setDesktopName(const QString& desktopId, const QString& name)
{
    if (!m_useKWinDBus || !m_kwinVDInterface || desktopId.isEmpty()) {
        return;
    }
    m_kwinVDInterface->asyncCall(QStringLiteral("setDesktopName"), desktopId, name);
}

QStringList VirtualDesktopManager::desktopIds() const
{
    return m_desktopIds;
}

QString VirtualDesktopManager::desktopIdAt(int desktop) const
{
    if (desktop < 1 || desktop > m_desktopIds.size()) {
        return QString();
    }
    return m_desktopIds.at(desktop - 1);
}

int VirtualDesktopManager::desktopIndexOf(const QString& desktopId) const
{
    return m_desktopIds.indexOf(desktopId) + 1;
}

void VirtualDesktopManager::onNumberOfDesktopsChanged(int count)
{
    if (m_desktopCount == count) {
        return;
    }

    // setDesktopCount does the clamping and the announcement, in that order,
    // and reports the value we actually hold rather than the signal argument
    // (the settled list refresh may correct it moments later).
    setDesktopCount(count);

    if (m_useKWinDBus) {
        refreshFromKWin();
    }
}

int VirtualDesktopManager::desktopCount() const
{
    return m_useKWinDBus ? m_desktopCount : 1;
}

int VirtualDesktopManager::desktopRows() const
{
    return m_useKWinDBus ? qMax(1, m_desktopRows) : 1;
}

QStringList VirtualDesktopManager::rawDesktopNames() const
{
    return m_useKWinDBus ? m_desktopNames : QStringList();
}

QStringList VirtualDesktopManager::desktopNames() const
{
    const int count = desktopCount();
    QStringList names = rawDesktopNames();
    // Fill every unnamed slot, and pad to the count, with a positional
    // placeholder. Not localized on purpose — see the header: this library
    // links no i18n, so a user-facing caller reads rawDesktopNames() and
    // supplies its own translated fallback for the empty entries.
    for (int i = 0; i < names.size(); ++i) {
        if (names.at(i).isEmpty()) {
            names[i] = QStringLiteral("Desktop %1").arg(i + 1);
        }
    }
    while (names.size() < count) {
        names.append(QStringLiteral("Desktop %1").arg(names.size() + 1));
    }
    return names;
}

} // namespace PhosphorWorkspaces
