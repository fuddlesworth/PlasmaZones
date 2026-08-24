// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWorkspaces/VirtualDesktopManager.h>

#include <PhosphorIdentity/VirtualScreenId.h>

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QDBusVariant>

#include <algorithm>
#include <array>
#include <utility>

namespace PhosphorWorkspaces {

namespace {

/// Placeholder label for a desktop KWin gave no name to.
///
/// Deliberately untranslated. `PhosphorI18n::tr()` — the project's only
/// sanctioned translation entry point for C++ — lives in `src/phosphor_i18n.h`,
/// inside the GPL-3.0 application tree. This library is LGPL-2.1 precisely so
/// third-party tools can link it without inheriting GPL, so reaching into that
/// header would defeat the licence split the project maintains on purpose.
/// The shell tier that renders these labels has no translation wiring either,
/// so the string localises when that story lands, together with the rest of
/// the shell's text. Same reasoning as the bar's widget display names.
QString fallbackDesktopName(int oneBasedPosition)
{
    return QStringLiteral("Desktop %1").arg(oneBasedPosition);
}

} // namespace

VirtualDesktopManager::VirtualDesktopManager(QObject* parent)
    : QObject(parent)
{
}

VirtualDesktopManager::~VirtualDesktopManager()
{
    stop();
}

bool VirtualDesktopManager::init()
{
    initKWinDBus();
    return true;
}

void VirtualDesktopManager::initKWinDBus()
{
    m_kwinVDInterface =
        new QDBusInterface(QStringLiteral("org.kde.KWin"), QStringLiteral("/VirtualDesktopManager"),
                           QStringLiteral("org.kde.KWin.VirtualDesktopManager"), QDBusConnection::sessionBus(), this);

    if (m_kwinVDInterface->isValid()) {
        m_useKWinDBus = true;
        // The `rows` / `current` property reads in refreshFromKWin are
        // synchronous in both running and non-running modes, and
        // QDBusInterface defaults to a 25-second timeout. A
        // wedged-but-registered KWin would stall whichever thread refreshes
        // for that long, which for the shell is the GUI thread. One second
        // is far beyond any healthy round trip and bounds the damage.
        //
        // This does NOT bound everything: QDBusInterface's constructor above
        // already performed a blocking Introspect call at the 25-second
        // default before isValid() could be consulted, and setTimeout cannot
        // reach backwards to it. Capping that too would mean probing with a
        // bounded message before constructing the interface, or building it
        // asynchronously — worth doing if a wedged KWin ever shows up in
        // practice, but not something this line achieves.
        m_kwinVDInterface->setTimeout(1000);

        refreshFromKWin();

        QDBusConnection::sessionBus().connect(QStringLiteral("org.kde.KWin"), QStringLiteral("/VirtualDesktopManager"),
                                              QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
                                              QStringLiteral("currentChanged"), this,
                                              SLOT(onKWinCurrentChanged(QString)));

        QDBusConnection::sessionBus().connect(QStringLiteral("org.kde.KWin"), QStringLiteral("/VirtualDesktopManager"),
                                              QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
                                              QStringLiteral("countChanged"), this,
                                              SLOT(onNumberOfDesktopsChanged(uint)));

        QDBusConnection::sessionBus().connect(QStringLiteral("org.kde.KWin"), QStringLiteral("/VirtualDesktopManager"),
                                              QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
                                              QStringLiteral("desktopCreated"), this, SLOT(onKWinDesktopCreated()));

        // Renames and position moves. KWin leaves `count` untouched for
        // these, so without this subscription a stale desktop name would
        // survive until an unrelated create/remove forced a refresh.
        QDBusConnection::sessionBus().connect(QStringLiteral("org.kde.KWin"), QStringLiteral("/VirtualDesktopManager"),
                                              QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
                                              QStringLiteral("desktopDataChanged"), this,
                                              SLOT(onKWinDesktopDataChanged()));

        QDBusConnection::sessionBus().connect(QStringLiteral("org.kde.KWin"), QStringLiteral("/VirtualDesktopManager"),
                                              QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
                                              QStringLiteral("desktopRemoved"), this, SLOT(onKWinDesktopRemoved()));

        // A live grid reshape (e.g. 1×4 → 2×2) changes `rows` WITHOUT changing
        // the desktop count, so it fires neither countChanged nor created/removed
        // — without this the cached row count goes stale and cross-desktop
        // directional navigation computes neighbours against the wrong grid shape.
        QDBusConnection::sessionBus().connect(QStringLiteral("org.kde.KWin"), QStringLiteral("/VirtualDesktopManager"),
                                              QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
                                              QStringLiteral("rowsChanged"), this, SLOT(onKWinDesktopRowsChanged()));
    } else {
        delete m_kwinVDInterface;
        m_kwinVDInterface = nullptr;
    }
}

void VirtualDesktopManager::applyDesktopListReply(const QDBusMessage& reply, const QString& currentId, int rows)
{
    struct DesktopInfo
    {
        int position;
        QString id;
        QString name;
    };
    QList<DesktopInfo> desktops;

    // Parse into LOCALS and commit only on success. The members must not be
    // cleared up front: a failed reply (KWin restarting, a congested bus, an
    // async call that errored) would then leave ids empty while the count
    // kept its previous value, the pad loop below would refill names to that
    // count, and the "ids and names are index-aligned" contract this class
    // publishes would be broken with no way back until some unrelated signal
    // forced another refresh. Every id-keyed operation fails silently in that
    // state: the pager empties, and both setCurrentDesktopById and
    // setCurrentDesktop drop every switch because they validate against the
    // empty id list.
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return;
    }
    const QDBusVariant dbusVariant = reply.arguments().at(0).value<QDBusVariant>();
    const QVariant innerVariant = dbusVariant.variant();
    if (innerVariant.userType() != qMetaTypeId<QDBusArgument>()) {
        return;
    }

    // `position` is read as int even though the WIRE type is a(uss).
    // KWin's introspection XML advertises a(iss), but `busctl get-property`
    // shows the marshalled value is actually a(uss). Reading it into an int
    // is bit-compatible for the non-negative positions KWin emits, and
    // QDBusDemarshaller's union read yields the right value — verified
    // against the live interface rather than inferred. Flagged here because
    // the XML and the wire disagree, which is exactly the kind of thing a
    // future reader would "fix" in the wrong direction.
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

    // An empty array is a well-formed reply, but KWin always has at least one
    // desktop, so it means the demarshal produced nothing usable. Treat it
    // like a failed reply rather than publishing an empty compositor.
    if (desktops.isEmpty()) {
        return;
    }

    std::sort(desktops.begin(), desktops.end(), [](const DesktopInfo& a, const DesktopInfo& b) {
        return a.position < b.position;
    });

    QStringList newIds;
    QStringList newNames;
    newIds.reserve(desktops.size());
    newNames.reserve(desktops.size());
    for (const auto& desktop : desktops) {
        newIds.append(desktop.id);
        newNames.append(desktop.name.isEmpty() ? fallbackDesktopName(desktop.position + 1) : desktop.name);
    }

    // The count IS the list length. Reading it from the separate `count`
    // property would reintroduce the possibility of the two disagreeing, and
    // the pad loop that used to follow could never run anyway: names and ids
    // are built together above and are the same length by construction.
    const int newCount = newIds.size();

    int newCurrent = m_currentDesktop;
    if (!currentId.isEmpty()) {
        const int idx = newIds.indexOf(currentId);
        if (idx >= 0) {
            newCurrent = idx + 1;
        }
    }
    // Clamp here, not only in onNumberOfDesktopsChanged. Removing a desktop
    // BEFORE the current one renumbers it downward without KWin sending
    // currentChanged (the id did not move), and that handler early-returns
    // when the count already matches — so this is the only place the
    // out-of-range case is guaranteed to be caught. Publishing a position
    // greater than the count would make activeIndex read past the end.
    newCurrent = qBound(1, newCurrent, qMax(1, newCount));

    const bool listChanged = (newIds != m_desktopIds || newNames != m_desktopNames);
    const bool currentChanged = (newCurrent != m_currentDesktop);
    const bool countChanged = (newCount != m_desktopCount);

    m_desktopIds = newIds;
    m_desktopNames = newNames;
    m_desktopCount = newCount;
    m_desktopRows = rows;
    m_currentDesktop = newCurrent;

    // Change-gated: a refresh runs on create, remove, and every rename, and
    // most of those leave this data untouched. An unconditional emit would
    // reset a pager's model on unrelated events.
    if (listChanged) {
        Q_EMIT desktopsChanged();
    }
    // A desktop added or removed BEFORE the current one renumbers the current
    // position while its id stays put, so KWin sends no currentChanged and
    // this is the only place the move is observable. Consumers key placement
    // state on the 1-based number and re-read only on this signal.
    if (currentChanged) {
        Q_EMIT currentDesktopChanged(m_currentDesktop);
    }
    // The count notification belongs HERE, where the value is committed.
    // The create/remove handlers used to emit it themselves, which worked
    // only while refreshFromKWin also read `count` synchronously. Now that
    // the whole snapshot lands with the async reply, emitting from those
    // handlers would carry the OLD count and clamp against it.
    if (countChanged) {
        clampScreenDesktopsToCount();
        Q_EMIT desktopCountChanged(m_desktopCount);
    }
}

void VirtualDesktopManager::refreshFromKWin()
{
    if (!m_kwinVDInterface || !m_kwinVDInterface->isValid()) {
        return;
    }

    // Read the scalar properties into LOCALS and hand them to
    // applyDesktopListReply, which commits them together with the desktop
    // list or commits nothing at all.
    //
    // Committing them here instead would recreate, mirrored, the very
    // inconsistency the list-side rewrite removed: a dropped or errored list
    // reply would leave a fresh `count` beside a stale `ids`, so
    // desktopCount() and desktopIds().size() would disagree permanently and
    // the published "ids are index-aligned with names" contract would break
    // with no path back until an unrelated signal forced another refresh.
    // The snapshot is all-or-nothing.
    //
    // Rows clamps to >= 1 so a missing or zero property cannot divide the
    // grid arithmetic by zero. It has no NOTIFY: nothing subscribes to
    // row-shape changes, the value is re-read per navigation.
    const QVariant rowsVar = m_kwinVDInterface->property("rows");
    const int pendingRows = rowsVar.isValid() ? qMax(1, rowsVar.toInt()) : m_desktopRows;

    const QVariant currentVar = m_kwinVDInterface->property("current");
    QString currentId;
    if (currentVar.isValid()) {
        currentId = currentVar.toString();
    }

    QDBusMessage getDesktopsMsg =
        QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/VirtualDesktopManager"),
                                       QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    getDesktopsMsg << QStringLiteral("org.kde.KWin.VirtualDesktopManager") << QStringLiteral("desktops");

    // Bump BEFORE branching, not only on the async path. stop() clears
    // m_running while leaving every D-Bus subscription live, so a later
    // signal can re-enter here on the blocking path while an earlier async
    // watcher is still outstanding; without bumping, that watcher's
    // generation still matches and it would apply its older snapshot on top
    // of the newer blocking result.
    ++m_refreshGeneration;
    const uint thisGeneration = m_refreshGeneration;

    if (!m_running) {
        QDBusMessage reply = QDBusConnection::sessionBus().call(getDesktopsMsg, QDBus::Block, 1000);
        applyDesktopListReply(reply, currentId, pendingRows);
        return;
    }

    QDBusPendingCall pendingCall = QDBusConnection::sessionBus().asyncCall(getDesktopsMsg);
    QDBusPendingCallWatcher* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, currentId, pendingRows, thisGeneration](QDBusPendingCallWatcher* w) {
                if (thisGeneration != m_refreshGeneration) {
                    w->deleteLater();
                    return;
                }

                QDBusPendingReply<QDBusVariant> reply = *w;
                applyDesktopListReply(reply.reply(), currentId, pendingRows);

                w->deleteLater();
            });
}

void VirtualDesktopManager::onKWinCurrentChanged(const QString& desktopId)
{
    int idx = m_desktopIds.indexOf(desktopId);
    int newDesktop = (idx >= 0) ? idx + 1 : 1;

    if (m_currentDesktop == newDesktop) {
        return;
    }

    m_currentDesktop = newDesktop;
    Q_EMIT currentDesktopChanged(m_currentDesktop);
}

void VirtualDesktopManager::onKWinDesktopCreated()
{
    // No emit here: refreshFromKWin is asynchronous once started, so the
    // count is still the old one at this point. applyDesktopListReply
    // notifies when it actually commits the new snapshot.
    refreshFromKWin();
}

void VirtualDesktopManager::onKWinDesktopRemoved()
{
    // Neither the clamp nor the emit belongs here: refreshFromKWin is
    // asynchronous once started, so both would run against the OLD count
    // and the clamp would be a no-op. applyDesktopListReply does both when
    // it commits the new snapshot.
    refreshFromKWin();
}

void VirtualDesktopManager::onKWinDesktopRowsChanged()
{
    // Re-read the grid shape so the on-demand desktopRows() pull stays fresh
    // after a live grid reshape (the desktop count is unaffected here).
    refreshFromKWin();
}

void VirtualDesktopManager::start()
{
    if (m_running) {
        return;
    }

    m_running = true;

    if (m_useKWinDBus) {
        refreshFromKWin();
    }
}

void VirtualDesktopManager::stop()
{
    m_running = false;
    // Drop the session-bus subscriptions too. Clearing the flag alone left a
    // "stopped" manager still servicing currentChanged / countChanged /
    // desktopCreated / desktopRemoved / desktopDataChanged, and each of
    // those re-enters refreshFromKWin and issues blocking D-Bus calls — so
    // stop() did not stop anything, it only changed which code path the
    // work took. It matters most during destruction: ~VirtualDesktopManager
    // calls stop(), and a signal dispatched between there and ~QObject would
    // re-enter a half-destroyed object.
    //
    // Each (signal, slot) pair must be named EXPLICITLY. QDBusConnection
    // registers hooks keyed on signal name plus path, and its disconnect()
    // returns false for a null slot or an empty signal name — a wildcard
    // "drop everything for this interface" call compiles, runs, and removes
    // nothing at all. Mirrors the six connect() calls in initKWinDBus.
    // Not constexpr: SLOT() expands to a qFlagLocation() call.
    const std::array<std::pair<const char*, const char*>, 6> subscriptions{{
        {"currentChanged", SLOT(onKWinCurrentChanged(QString))},
        {"countChanged", SLOT(onNumberOfDesktopsChanged(uint))},
        {"desktopCreated", SLOT(onKWinDesktopCreated())},
        {"desktopDataChanged", SLOT(onKWinDesktopDataChanged())},
        {"desktopRemoved", SLOT(onKWinDesktopRemoved())},
        {"rowsChanged", SLOT(onKWinDesktopRowsChanged())},
    }};
    for (const auto& [signalName, slotSpec] : subscriptions) {
        QDBusConnection::sessionBus().disconnect(
            QStringLiteral("org.kde.KWin"), QStringLiteral("/VirtualDesktopManager"),
            QStringLiteral("org.kde.KWin.VirtualDesktopManager"), QLatin1String(signalName), this, slotSpec);
    }
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
    if (desktop < 1 || desktop > m_desktopCount) {
        return;
    }

    if (m_useKWinDBus && m_kwinVDInterface) {
        int idx = desktop - 1;
        if (idx >= 0 && idx < m_desktopIds.size()) {
            m_kwinVDInterface->setProperty("current", m_desktopIds.at(idx));
        }
    }
}

void VirtualDesktopManager::setCurrentDesktopById(const QString& desktopId)
{
    if (desktopId.isEmpty() || !m_useKWinDBus || !m_kwinVDInterface) {
        return;
    }
    // Only ids we actually know about. Writing an unknown UUID would
    // either be rejected by KWin or, worse, switch to a desktop this
    // manager has no record of, leaving m_currentDesktop wrong until the
    // next refresh.
    if (!m_desktopIds.contains(desktopId)) {
        return;
    }
    m_kwinVDInterface->setProperty("current", desktopId);
}

void VirtualDesktopManager::onKWinDesktopDataChanged()
{
    // No count change to report; refreshFromKWin re-reads the list and
    // applyDesktopListReply emits desktopsChanged if anything moved.
    refreshFromKWin();
}

void VirtualDesktopManager::onNumberOfDesktopsChanged(uint count)
{
    // The parameter must stay `uint`: KWin's numberOfDesktopsChanged carries
    // `u` on the wire, and QDBusConnection::connect matches on the signature,
    // so an `int` slot here returns true at connect time and then never fires.
    // `uint` is therefore a wire type only — narrow once at this boundary and
    // let everything below stay int, matching m_currentDesktop, the 1-based
    // desktop math, and desktopCountChanged(int).
    const int newCount = static_cast<int>(count);
    if (m_desktopCount == newCount) {
        return;
    }

    m_desktopCount = newCount;

    if (m_useKWinDBus) {
        refreshFromKWin();
    }

    // Clamp against the (possibly refreshed) live count, not the signal arg:
    // refreshFromKWin() may have re-read m_desktopCount from KWin's property,
    // and the current desktop must stay within whatever count is now authoritative.
    if (m_currentDesktop > m_desktopCount) {
        m_currentDesktop = m_desktopCount;
        Q_EMIT currentDesktopChanged(m_currentDesktop);
    }

    clampScreenDesktopsToCount();

    // Report the live member, not the signal argument, for the same reason the
    // clamp above reads it: refreshFromKWin() may have re-read a different
    // count from KWin's property, and that is the authoritative one. Emitting
    // `newCount` here would announce a value this object no longer holds, so a
    // consumer's cached count would disagree with desktopCount(). Matches the
    // emit in applyDesktopListReply, which already sends m_desktopCount.
    Q_EMIT desktopCountChanged(m_desktopCount);
}

int VirtualDesktopManager::desktopCount() const
{
    return m_useKWinDBus ? m_desktopCount : 1;
}

int VirtualDesktopManager::desktopRows() const
{
    return m_useKWinDBus ? qMax(1, m_desktopRows) : 1;
}

QStringList VirtualDesktopManager::desktopNames() const
{
    if (m_useKWinDBus && !m_desktopNames.isEmpty()) {
        return m_desktopNames;
    }

    QStringList names;
    int count = desktopCount();
    for (int i = 1; i <= count; ++i) {
        names.append(fallbackDesktopName(i));
    }
    return names;
}

bool VirtualDesktopManager::isAvailable() const
{
    return m_useKWinDBus;
}

QStringList VirtualDesktopManager::desktopIds() const
{
    // No synthesised fallback, unlike desktopNames(): a made-up name is a
    // harmless placeholder, but a made-up id would be a key that matches
    // nothing in KWin, and setCurrentDesktopById would silently drop every
    // switch made against it. An empty list says "no stable ids here",
    // which a caller can act on.
    return m_desktopIds;
}

} // namespace PhosphorWorkspaces
