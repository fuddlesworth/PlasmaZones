// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "backends.h"
#include "shortcutslogging.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QRegularExpression>
#include <QUuid>

namespace Phosphor::Shortcuts {

namespace {

// D-Bus service / interface / path constants. QStringLiteral so the UTF-16
// data lives in rodata and consumers don't pay a runtime conversion at each
// call site.
const QString kPortalService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPortalPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kPortalInterface = QStringLiteral("org.freedesktop.portal.GlobalShortcuts");
const QString kRequestInterface = QStringLiteral("org.freedesktop.portal.Request");
const QString kSessionInterface = QStringLiteral("org.freedesktop.portal.Session");
const QString kResponseSignal = QStringLiteral("Response");

// Portal session handles embed a caller-chosen token. Each app using
// GlobalShortcuts needs its own or sessions collide on-bus. Derive from the
// Qt application name (set by QCoreApplication::setApplicationName()) and
// sanitise to match the portal's path-component grammar. Append a per-run
// UUID so a crashed prior run that left a lingering session at portal side
// doesn't make the next CreateSession fail with a path collision — without
// the UUID, the portal would refuse to register a session handle whose path
// already exists.
QString sessionTokenFromApplication()
{
    QString name = QCoreApplication::applicationName();
    if (name.isEmpty()) {
        name = QStringLiteral("phosphor_shortcuts");
    }
    static const QRegularExpression invalid(QStringLiteral("[^A-Za-z0-9_]"));
    name.replace(invalid, QStringLiteral("_"));
    return name + QStringLiteral("_") + QUuid::createUuid().toString(QUuid::Id128);
}

// Per-Request handle token. Portal constructs the Request object path as
// /org/freedesktop/portal/desktop/request/<sanitised_sender>/<handle_token>,
// so every in-flight Request needs a unique token or Response signals
// collide. QUuid Id128 is 32 hex chars with no dashes — valid D-Bus path
// component.
QString freshHandleToken()
{
    return QStringLiteral("phosphor_") + QUuid::createUuid().toString(QUuid::Id128);
}

QString sanitisedSender(const QDBusConnection& bus)
{
    QString sender = bus.baseService();
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    if (sender.startsWith(QLatin1Char(':'))) {
        sender = sender.mid(1);
    }
    return sender;
}

QString requestPathFor(const QString& sender, const QString& handleToken)
{
    return QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, handleToken);
}

} // namespace

PortalBackend::PortalBackend(QObject* parent)
    : IBackend(parent)
    , m_sessionToken(sessionTokenFromApplication())
{
    qCInfo(lcPhosphorShortcuts) << "PortalBackend: initialising (session token:" << m_sessionToken << ")";

    // Subscribe ONCE to org.freedesktop.portal.Request::Response for any path
    // on the portal service. The slot filters on QDBusMessage::path() to
    // decide whether this Response is ours. This replaces the earlier
    // per-request connect/disconnect dance, which had a spec-allowed race
    // window: the portal may emit Response as soon as the Request object is
    // created, which can happen before our QDBusPendingCallWatcher::finished
    // callback runs — and if the portal chose a path that differed from our
    // pre-computed one, the pre-subscribe couldn't match and the defensive
    // re-subscribe in onSessionCreated would be too late (signal already
    // dispatched). Subscribing with an empty path side-steps that entirely —
    // any Response on the portal lands in onAnyRequestResponse, and path
    // matching happens in-slot where we know the full Request path.
    const bool connected =
        QDBusConnection::sessionBus().connect(kPortalService, QString(), kRequestInterface, kResponseSignal, this,
                                              SLOT(onAnyRequestResponse(uint, QVariantMap, QDBusMessage)));
    if (!connected) {
        qCWarning(lcPhosphorShortcuts) << "Portal: failed to subscribe to org.freedesktop.portal.Request::Response"
                                       << "— CreateSession and BindShortcuts will time out silently";
        m_sessionFailed = true;
        return;
    }

    createSession();
}

PortalBackend::~PortalBackend()
{
    // Tear down the unified Response subscription so a late portal signal
    // doesn't route into a dead slot. We always connected in the ctor (or
    // latched m_sessionFailed on failure); unconditional disconnect is
    // cheap — Qt silently returns false if there was nothing to remove.
    QDBusConnection::sessionBus().disconnect(kPortalService, QString(), kRequestInterface, kResponseSignal, this,
                                             SLOT(onAnyRequestResponse(uint, QVariantMap, QDBusMessage)));

    if (m_sessionHandle.isEmpty()) {
        return;
    }
    QDBusMessage msg =
        QDBusMessage::createMethodCall(kPortalService, m_sessionHandle, kSessionInterface, QStringLiteral("Close"));
    QDBusConnection::sessionBus().asyncCall(msg);
}

void PortalBackend::registerShortcut(const QString& id, const QKeySequence& defaultSeq,
                                     const QKeySequence& /*currentSeq*/, const QString& description)
{
    // Portal: preferred_trigger is advisory and the compositor assigns the
    // actual binding via its own settings UI. Send the compiled-in default
    // as the app's preferred key rather than the consumer's current value —
    // if the user re-binds, they do so compositor-side, not in our config.
    m_pending.insert(id, {defaultSeq, description});
    m_descriptions.insert(id, description);
}

void PortalBackend::updateShortcut(const QString& id, const QKeySequence& defaultSeq,
                                   const QKeySequence& /*newTrigger*/)
{
    // Only allow update for ids the Registry has previously passed to
    // registerShortcut — otherwise we'd silently synthesise a registration
    // with an empty description, which is a spec violation.
    if (!m_descriptions.contains(id)) {
        qCWarning(lcPhosphorShortcuts) << "Portal updateShortcut: unknown id" << id
                                       << "— ignoring (call registerShortcut first)";
        return;
    }

    // preferred_trigger is keyed off the compiled-in default in BOTH
    // registerShortcut and updateShortcut for consistency. The XDG spec
    // treats preferred_trigger as advisory — the compositor exposes its own
    // rebinding UI and we cannot override the user's compositor-side choice
    // from the client side. Sending the user's customised currentSeq as
    // preferred_trigger would falsely advertise it as the app's "factory"
    // value. The user-customised key still has effect through KGlobalAccel /
    // any compositor that surfaces preferred_trigger as a default — but
    // that's a behaviour the consumer chose by binding both seqs.
    auto it = m_pending.find(id);
    if (it != m_pending.end()) {
        // Already queued for next flush — refresh the preferred in-place.
        // No short-circuit here: the pending entry hasn't reached the portal
        // yet, so even a same-value update still needs the flush to happen.
        it->preferred = defaultSeq;
        return;
    }

    // Not in pending. Short-circuit when defaultSeq matches what we last
    // successfully delivered to the compositor: Portal ignores newTrigger,
    // so with defaultSeq unchanged the RPC would carry no new information
    // and only cost a BindShortcuts round-trip. Without this gate every
    // user rebind of currentSeq (which Registry funnels through
    // updateShortcut) would trigger a useless flush.
    const auto lastIt = m_lastSentPreferred.constFind(id);
    if (lastIt != m_lastSentPreferred.constEnd() && *lastIt == defaultSeq) {
        return;
    }

    // defaultSeq has drifted from the last confirmed send (or was never
    // confirmed). Re-queue so the next flush carries the new preferred.
    Pending p;
    p.preferred = defaultSeq;
    p.description = m_descriptions.value(id);
    m_pending.insert(id, p);
}

void PortalBackend::unregisterShortcut(const QString& id)
{
    const bool wasConfirmed = m_confirmedBound.remove(id);
    m_pending.remove(id);
    m_descriptions.remove(id);
    m_lastSentPreferred.remove(id);
    // If an in-flight BindShortcuts batch was carrying this id, drop it from
    // the snapshot so a successful Response doesn't promote a since-released
    // grab back into m_lastSentPreferred.
    m_pendingBindResponse.remove(id);
    // IMPORTANT: the XDG GlobalShortcuts spec has no per-id UnbindShortcuts —
    // BindShortcuts is additive (it "binds new shortcuts", it does not
    // replace the full set), and the only release path is closing the
    // session, which drops every shortcut. Net effect: once a grab reaches
    // the compositor on a Portal backend, the key stays grabbed by our
    // session until the PortalBackend is destroyed. Registry::unbind()
    // removes the callback + local entry so onActivated drops the signal
    // silently, but the compositor still routes the key to us instead of
    // other apps.
    //
    // Consumers that need genuinely transient grabs on Portal-based
    // compositors should bind the shortcut once for the lifetime of the app
    // and gate activation via a flag inside the callback rather than
    // relying on bind/unbind cycles.
    if (wasConfirmed) {
        qCDebug(lcPhosphorShortcuts) << "PortalBackend::unregisterShortcut: id" << id
                                     << "was already bound compositor-side — key stays grabbed by this session"
                                     << "until the portal session closes (XDG GlobalShortcuts has no per-id release).";
    }
}

void PortalBackend::flush()
{
    if (m_sessionFailed) {
        // CreateSession errored earlier. Keep emitting ready() synchronously
        // so consumers that gate UI on ready() don't hang; the warning makes
        // the misconfiguration visible in logs.
        qCWarning(lcPhosphorShortcuts)
            << "Portal flush(): CreateSession failed earlier — grabs will not work on this session";
        Q_EMIT ready();
        return;
    }
    if (m_sessionHandle.isEmpty()) {
        // Defer until the CreateSession Response arrives — ready() fires
        // from there (or from onCreateSessionResponse's error path).
        m_flushRequested = true;
        return;
    }
    if (m_pending.isEmpty()) {
        Q_EMIT ready();
        return;
    }
    sendBindShortcuts();
}

void PortalBackend::createSession()
{
    // Per the XDG Desktop Portal spec, CreateSession returns a Request
    // object handle synchronously; the actual session_handle arrives via
    // the Request::Response signal on that Request's path. The global
    // Response subscription is already wired in the constructor (see
    // onAnyRequestResponse), so we just pre-compute the expected path so
    // the slot can recognise the Response as ours. If the portal ends up
    // choosing a different path format, onSessionCreated will overwrite
    // m_createRequestPath once the async reply lands — but either way,
    // no Response is ever missed because the global subscription has no
    // path filter.
    const QString sender = sanitisedSender(QDBusConnection::sessionBus());
    const QString handleToken = freshHandleToken();
    m_createRequestPath = requestPathFor(sender, handleToken);

    QVariantMap options;
    options.insert(QStringLiteral("session_handle_token"), m_sessionToken);
    options.insert(QStringLiteral("handle_token"), handleToken);

    QDBusMessage msg =
        QDBusMessage::createMethodCall(kPortalService, kPortalPath, kPortalInterface, QStringLiteral("CreateSession"));
    msg.setArguments({QVariant::fromValue(options)});

    QDBusPendingCall pending = QDBusConnection::sessionBus().asyncCall(msg);
    auto* watcher = new QDBusPendingCallWatcher(pending, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, &PortalBackend::onSessionCreated);
}

void PortalBackend::onSessionCreated(QDBusPendingCallWatcher* watcher)
{
    // This callback ONLY confirms the portal accepted the CreateSession RPC.
    // The real session_handle arrives later via onAnyRequestResponse.
    watcher->deleteLater();

    QDBusPendingReply<QDBusObjectPath> reply = *watcher;
    if (reply.isError()) {
        qCWarning(lcPhosphorShortcuts) << "Portal CreateSession failed:" << reply.error().message();
        m_createRequestPath.clear();
        m_sessionFailed = true;
        // Consume any pending flush that was waiting on us — the warning in
        // flush() from now on is the consumer-visible signal that something
        // is wrong.
        m_flushRequested = false;
        Q_EMIT ready();
        return;
    }

    // If the portal chose a different Request path than we predicted (some
    // portal backends use a different handle_token → path mapping than the
    // spec's "example" convention), update our expected path so the unified
    // Response dispatcher recognises the signal as ours. The subscription
    // itself is path-less and needs no changes. In the common case
    // (xdg-desktop-portal-gtk / gnome / kde), the paths match exactly.
    //
    // Race-safety note: if the Response had ALREADY fired before this
    // callback runs (spec-permitted), onAnyRequestResponse would have
    // received it with the actual path, compared it against the stale
    // m_createRequestPath, found no match, and dropped it — we'd hang.
    // But Qt's D-Bus dispatch serialises signals and replies on the same
    // thread in order of arrival on the bus, and the kernel / dbus-daemon
    // delivers the method-reply before any signal that was emitted after
    // the method returned to the bus daemon. In practice the asyncCall
    // reply always arrives before the Response signal for portals that
    // follow the standard flow. If a portal reversed that ordering, it
    // would be a portal bug; the UUID in session_handle_token makes
    // collision retry safe.
    const QString actualRequestPath = reply.value().path();
    if (!actualRequestPath.isEmpty() && actualRequestPath != m_createRequestPath) {
        qCDebug(lcPhosphorShortcuts) << "Portal Request path differs from expected — predicted" << m_createRequestPath
                                     << "got" << actualRequestPath;
        m_createRequestPath = actualRequestPath;
    }
    // Wait for the Response signal; the unified dispatcher will pick it up.
}

void PortalBackend::handleCreateSessionResponse(uint response, const QVariantMap& results)
{
    // Response fires exactly once per Request. Clear the path so the unified
    // dispatcher treats later Responses on the same path as unknown and
    // drops them instead of re-entering this handler.
    m_createRequestPath.clear();

    // response code: 0 = success, 1 = user cancelled, 2 = other failure.
    if (response != 0) {
        qCWarning(lcPhosphorShortcuts) << "Portal CreateSession Response: failed with response code" << response;
        m_sessionFailed = true;
        m_flushRequested = false;
        Q_EMIT ready();
        return;
    }

    m_sessionHandle = results.value(QStringLiteral("session_handle")).toString();
    if (m_sessionHandle.isEmpty()) {
        qCWarning(lcPhosphorShortcuts) << "Portal CreateSession Response: success but no session_handle in results"
                                       << results;
        m_sessionFailed = true;
        m_flushRequested = false;
        Q_EMIT ready();
        return;
    }

    // Subscribe to Activated on the now-confirmed session handle. Signal
    // signature per spec is (o session_handle, s shortcut_id, t timestamp,
    // a{sv} options); all four args must be named in the SLOT() macro or
    // Qt's D-Bus dispatcher silently drops the connection.
    QDBusConnection::sessionBus().connect(kPortalService, m_sessionHandle, kPortalInterface,
                                          QStringLiteral("Activated"), this,
                                          SLOT(onActivated(QDBusObjectPath, QString, qulonglong, QVariantMap)));

    qCInfo(lcPhosphorShortcuts) << "Portal GlobalShortcuts session ready:" << m_sessionHandle;

    // Honour the IBackend contract: registerShortcut queues, and only an
    // explicit flush() (which sets m_flushRequested when the session isn't
    // ready yet) may dispatch to the portal. A non-empty m_pending alone
    // must NOT trigger a send here — consumers queuing ids without flushing
    // should not leak out to the portal just because a session became ready.
    // The only caller today is Registry, which always flushes, so this is a
    // contract-cleanliness fix rather than a visible-behaviour one.
    if (m_flushRequested) {
        m_flushRequested = false;
        if (m_pending.isEmpty()) {
            Q_EMIT ready();
        } else {
            sendBindShortcuts();
        }
    }
}

void PortalBackend::sendBindShortcuts()
{
    // BindShortcuts(o session, a(sa{sv}) shortcuts, s parent_window, a{sv} options)
    //
    // The shortcuts array is a D-Bus struct `(sa{sv})` — id plus options dict
    // whose keys are { "description", "preferred_trigger" }. Qt's QVariant
    // system can't synthesise the struct directly, so we build it via
    // QDBusArgument explicitly.
    QDBusArgument shortcutsArg;
    shortcutsArg.beginArray(qMetaTypeId<QDBusArgument>());
    for (auto it = m_pending.constBegin(); it != m_pending.constEnd(); ++it) {
        shortcutsArg.beginStructure();
        shortcutsArg << it.key();

        QVariantMap shortcutOptions;
        if (!it.value().description.isEmpty()) {
            shortcutOptions.insert(QStringLiteral("description"), it.value().description);
        }
        if (!it.value().preferred.isEmpty()) {
            shortcutOptions.insert(QStringLiteral("preferred_trigger"), it.value().preferred.toString());
        }
        shortcutsArg << shortcutOptions;
        shortcutsArg.endStructure();
    }
    shortcutsArg.endArray();

    // BindShortcuts also returns a Request handle. Like CreateSession, the
    // canonical "done" signal is Response on that handle — that's where the
    // compositor delivers the actually-bound shortcut table. Pre-compute
    // the expected Request path so the unified Response dispatcher
    // (onAnyRequestResponse) can recognise the signal as ours; the
    // subscription itself is the global path-less one wired in the
    // constructor, so no per-call connect is needed. If a prior
    // BindShortcuts is still in-flight (m_bindRequestPath non-empty), its
    // pending Response is discarded when we overwrite the path — the new
    // batch supersedes the old one, matching the pre-refactor semantics.
    const QString sender = sanitisedSender(QDBusConnection::sessionBus());
    const QString handleToken = freshHandleToken();
    m_bindRequestPath = requestPathFor(sender, handleToken);

    QVariantMap callOptions;
    callOptions.insert(QStringLiteral("handle_token"), handleToken);

    QDBusMessage msg =
        QDBusMessage::createMethodCall(kPortalService, kPortalPath, kPortalInterface, QStringLiteral("BindShortcuts"));
    msg.setArguments({QVariant::fromValue(QDBusObjectPath(m_sessionHandle)), QVariant::fromValue(shortcutsArg),
                      QVariant::fromValue(QString()), QVariant::fromValue(callOptions)});

    QDBusPendingCall pending = QDBusConnection::sessionBus().asyncCall(msg);

    // Snapshot the entries we just dispatched so (a) a failure path can
    // restore them into m_pending for the next flush() to retry, and (b) a
    // successful Response can promote their preferred_trigger into
    // m_lastSentPreferred for the updateShortcut short-circuit. Store on
    // the member rather than a lambda capture so handleBindShortcutsResponse
    // (which runs via the unified Response dispatcher, not this watcher) can
    // consume it. A second sendBindShortcuts supersedes any prior snapshot —
    // the superseded Response is dropped at onAnyRequestResponse by path
    // compare, so the snapshot is safe to overwrite.
    m_pendingBindResponse = m_pending;
    m_pending.clear();

    // Capture the path this watcher belongs to. If a second sendBindShortcuts
    // overwrites m_bindRequestPath before this lambda runs, our cleanup must
    // not clear the SUCCESSOR's path — that would orphan its Response and
    // hang the consumer. Compare against m_bindRequestPath before clearing,
    // and only emit ready() if WE were the live request.
    const QString myRequestPath = m_bindRequestPath;
    auto* watcher = new QDBusPendingCallWatcher(pending, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, myRequestPath]() {
        watcher->deleteLater();
        QDBusPendingReply<> reply = *watcher;
        if (!reply.isError()) {
            // Happy path: Response will arrive via onAnyRequestResponse and
            // emit ready() from handleBindShortcutsResponse, which also
            // consumes m_pendingBindResponse.
            return;
        }

        qCWarning(lcPhosphorShortcuts) << "Portal BindShortcuts failed:" << reply.error().message();

        // Superseded request: a newer sendBindShortcuts has replaced our
        // snapshot and owns m_bindRequestPath. Don't touch shared state —
        // the live request's own completion path will fire ready() and
        // consume its own snapshot.
        if (m_bindRequestPath != myRequestPath) {
            return;
        }

        // Restore the grabs we tried to send so a subsequent flush() (or
        // the consumer's own retry) can re-attempt. Only restore ids the
        // consumer hasn't unregistered in the meantime — m_descriptions is
        // the live "still wanted" set. Do NOT promote into
        // m_lastSentPreferred: these ids never reached the portal.
        for (auto it = m_pendingBindResponse.constBegin(); it != m_pendingBindResponse.constEnd(); ++it) {
            if (m_descriptions.contains(it.key()) && !m_pending.contains(it.key())) {
                m_pending.insert(it.key(), it.value());
            }
        }
        m_pendingBindResponse.clear();
        m_bindRequestPath.clear();
        Q_EMIT ready();
    });
}

void PortalBackend::handleBindShortcutsResponse(uint response, const QVariantMap& results)
{
    // Clear the path so later (duplicate / replayed) Responses on the same
    // Request path are dropped by the unified dispatcher. Response is
    // documented as once-per-Request, but portals differ and a replay is
    // cheaper to defend against than to debug.
    m_bindRequestPath.clear();

    if (response != 0) {
        qCWarning(lcPhosphorShortcuts) << "Portal BindShortcuts Response: failed with response code" << response;
        // Portal rejected the batch. Drop the snapshot without promoting
        // any id into m_lastSentPreferred — nothing reached the compositor.
        // We don't restore m_pending here (unlike the RPC-failure path):
        // the portal acknowledged receipt of the call but refused the
        // contents, and blindly re-queuing would loop. Consumers can
        // re-bind explicitly if they want to retry.
        m_pendingBindResponse.clear();
        Q_EMIT ready();
        return;
    }

    // results contains "shortcuts" (a(sa{sv})) — the compositor-confirmed
    // binding set including any trigger the user/compositor substituted for
    // our preferred_trigger. We don't surface the triggers today — consumers
    // get activation events by id and don't need to know which physical key
    // was assigned — but the id list is captured in m_confirmedBound so
    // unregisterShortcut can tell consumers when a release is a no-op
    // versus when it leaves a grab dangling (spec limitation — see
    // unregisterShortcut).
    const QVariant shortcutsVar = results.value(QStringLiteral("shortcuts"));
    if (shortcutsVar.canConvert<QDBusArgument>()) {
        QDBusArgument arg = shortcutsVar.value<QDBusArgument>();
        arg.beginArray();
        while (!arg.atEnd()) {
            arg.beginStructure();
            QString id;
            QVariantMap opts;
            arg >> id >> opts;
            arg.endStructure();
            // Filter against m_descriptions: if the id was unregisterShortcut'd
            // between the BindShortcuts call and this Response, it's gone from
            // m_descriptions. Reinserting into m_confirmedBound would leak a
            // stale id that no consumer can clean up (only cosmetic — affects
            // the "still grabbed compositor-side" log in unregisterShortcut —
            // but cheap to avoid).
            if (!id.isEmpty() && m_descriptions.contains(id)) {
                m_confirmedBound.insert(id);
                // Promote into m_lastSentPreferred using the preferred we
                // actually sent (from the dispatch snapshot). If the id
                // wasn't in the snapshot (Response carries ids from prior
                // batches too — BindShortcuts is additive), leave
                // m_lastSentPreferred unchanged: we never claimed to have
                // refreshed it in this batch.
                const auto snapIt = m_pendingBindResponse.constFind(id);
                if (snapIt != m_pendingBindResponse.constEnd()) {
                    m_lastSentPreferred.insert(id, snapIt->preferred);
                }
            }
        }
        arg.endArray();
    }
    m_pendingBindResponse.clear();
    qCDebug(lcPhosphorShortcuts) << "Portal BindShortcuts Response: success, results=" << results;
    Q_EMIT ready();
}

void PortalBackend::onActivated(const QDBusObjectPath& /*sessionHandle*/, const QString& shortcutId,
                                qulonglong /*timestamp*/, const QVariantMap& /*options*/)
{
    if (!m_descriptions.contains(shortcutId)) {
        qCDebug(lcPhosphorShortcuts) << "Portal Activated for unknown id" << shortcutId;
        return;
    }
    qCDebug(lcPhosphorShortcuts) << "Portal Activated:" << shortcutId;
    Q_EMIT activated(shortcutId);
}

void PortalBackend::onAnyRequestResponse(uint response, const QVariantMap& results, const QDBusMessage& msg)
{
    // The unified Response dispatcher. One subscription, one slot —
    // de-multiplex by Request path. This replaces the earlier per-request
    // connect/disconnect dance and is robust against the spec-allowed race
    // where the portal emits Response before our async-call reply lands
    // (the subscription is always live, so no Response ever slips past).
    const QString path = msg.path();
    if (!m_createRequestPath.isEmpty() && path == m_createRequestPath) {
        handleCreateSessionResponse(response, results);
        return;
    }
    if (!m_bindRequestPath.isEmpty() && path == m_bindRequestPath) {
        handleBindShortcutsResponse(response, results);
        return;
    }
    // Not a Request we're tracking. Dropping this is the safe default —
    // a stale Response from a superseded BindShortcuts is the most likely
    // cause, but even a portal that replays Responses or a different
    // sender's Request sharing the same path prefix would end up here.
    qCDebug(lcPhosphorShortcuts) << "Portal Response: untracked path" << path << "— dropping";
}

} // namespace Phosphor::Shortcuts
