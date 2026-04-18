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
// sanitise to match the portal's path-component grammar.
QString sessionTokenFromApplication()
{
    QString name = QCoreApplication::applicationName();
    if (name.isEmpty()) {
        name = QStringLiteral("phosphor_shortcuts");
    }
    static const QRegularExpression invalid(QStringLiteral("[^A-Za-z0-9_]"));
    name.replace(invalid, QStringLiteral("_"));
    return name;
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
    createSession();
}

PortalBackend::~PortalBackend()
{
    // If destruction happens mid-setup (e.g. daemon shutdown before the
    // portal replies), tear down any live Response subscriptions so Qt
    // doesn't route a late signal into a dead slot.
    disconnectCreateSessionResponse();
    disconnectBindShortcutsResponse();

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

void PortalBackend::updateShortcut(const QString& id, const QKeySequence& newTrigger)
{
    // Only allow update for ids the Registry has previously passed to
    // registerShortcut — otherwise we'd silently synthesise a registration
    // with an empty description, which is a spec violation.
    if (!m_descriptions.contains(id)) {
        qCWarning(lcPhosphorShortcuts) << "Portal updateShortcut: unknown id" << id
                                       << "— ignoring (call registerShortcut first)";
        return;
    }

    auto it = m_pending.find(id);
    if (it == m_pending.end()) {
        // Already flushed; re-queue so the next flush re-sends with the new
        // trigger. Description is known because m_descriptions.contains check
        // above passed.
        Pending p;
        p.preferred = newTrigger;
        p.description = m_descriptions.value(id);
        m_pending.insert(id, p);
        return;
    }
    it->preferred = newTrigger;
}

void PortalBackend::unregisterShortcut(const QString& id)
{
    m_pending.remove(id);
    m_descriptions.remove(id);
    // The portal spec has no per-id UnbindShortcuts — shortcuts disappear
    // when the session closes. This matches the pre-library PlasmaZones
    // behaviour.
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
    // the Request::Response signal. Pre-subscribe to Response on the
    // expected Request path BEFORE issuing the RPC — the spec says portals
    // may fire Response as soon as CreateSession returns, so any
    // post-connect is racy.
    const QString sender = sanitisedSender(QDBusConnection::sessionBus());
    const QString handleToken = freshHandleToken();
    m_createRequestPath = requestPathFor(sender, handleToken);

    const bool connected =
        QDBusConnection::sessionBus().connect(kPortalService, m_createRequestPath, kRequestInterface, kResponseSignal,
                                              this, SLOT(onCreateSessionResponse(uint, QVariantMap)));
    if (!connected) {
        qCWarning(lcPhosphorShortcuts) << "Portal CreateSession: failed to subscribe to Response on"
                                       << m_createRequestPath;
        m_createRequestPath.clear();
        m_sessionFailed = true;
        Q_EMIT ready();
        return;
    }

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
    // The real session_handle arrives later via onCreateSessionResponse.
    watcher->deleteLater();

    QDBusPendingReply<QDBusObjectPath> reply = *watcher;
    if (reply.isError()) {
        qCWarning(lcPhosphorShortcuts) << "Portal CreateSession failed:" << reply.error().message();
        disconnectCreateSessionResponse();
        m_sessionFailed = true;
        // Consume any pending flush that was waiting on us — the warning in
        // flush() from now on is the consumer-visible signal that something
        // is wrong.
        m_flushRequested = false;
        Q_EMIT ready();
        return;
    }

    // Defensive check: if the portal returned a Request path that differs
    // from our pre-computed one (some portal backends synthesise a
    // different token format), switch the Response subscription over before
    // the signal fires.
    const QString actualRequestPath = reply.value().path();
    if (!actualRequestPath.isEmpty() && actualRequestPath != m_createRequestPath) {
        qCDebug(lcPhosphorShortcuts) << "Portal Request path differs from expected — subscribed" << m_createRequestPath
                                     << "got" << actualRequestPath;
        disconnectCreateSessionResponse();
        m_createRequestPath = actualRequestPath;
        const bool reconnected = QDBusConnection::sessionBus().connect(
            kPortalService, m_createRequestPath, kRequestInterface, kResponseSignal, this,
            SLOT(onCreateSessionResponse(uint, QVariantMap)));
        if (!reconnected) {
            qCWarning(lcPhosphorShortcuts)
                << "Portal CreateSession: failed to re-subscribe to Response on" << m_createRequestPath;
            m_createRequestPath.clear();
            m_sessionFailed = true;
            m_flushRequested = false;
            Q_EMIT ready();
            return;
        }
    }
    // Otherwise wait for the Response signal.
}

void PortalBackend::onCreateSessionResponse(uint response, const QVariantMap& results)
{
    // Response fires exactly once per Request. Drop the subscription so
    // we're not holding stale connections for the rest of the session.
    disconnectCreateSessionResponse();

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

    if (m_flushRequested || !m_pending.isEmpty()) {
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
    // compositor delivers the actually-bound shortcut table. Subscribe
    // pre-call. If a prior BindShortcuts is still in flight (Response not
    // yet received), drop that subscription — the new batch supersedes it.
    disconnectBindShortcutsResponse();

    const QString sender = sanitisedSender(QDBusConnection::sessionBus());
    const QString handleToken = freshHandleToken();
    m_bindRequestPath = requestPathFor(sender, handleToken);

    const bool connected =
        QDBusConnection::sessionBus().connect(kPortalService, m_bindRequestPath, kRequestInterface, kResponseSignal,
                                              this, SLOT(onBindShortcutsResponse(uint, QVariantMap)));
    if (!connected) {
        qCWarning(lcPhosphorShortcuts) << "Portal BindShortcuts: failed to subscribe to Response on"
                                       << m_bindRequestPath;
        m_bindRequestPath.clear();
        // Still send the RPC — we'll fall back to the direct-reply path and
        // emit ready() from the call watcher below.
    }

    QVariantMap callOptions;
    callOptions.insert(QStringLiteral("handle_token"), handleToken);

    QDBusMessage msg =
        QDBusMessage::createMethodCall(kPortalService, kPortalPath, kPortalInterface, QStringLiteral("BindShortcuts"));
    msg.setArguments({QVariant::fromValue(QDBusObjectPath(m_sessionHandle)), QVariant::fromValue(shortcutsArg),
                      QVariant::fromValue(QString()), QVariant::fromValue(callOptions)});

    QDBusPendingCall pending = QDBusConnection::sessionBus().asyncCall(msg);
    m_pending.clear();

    // If the Response subscription failed, fall back to firing ready() from
    // the direct-reply watcher — better than hanging forever. In the happy
    // path, ready() fires from onBindShortcutsResponse once the compositor
    // confirms the grab.
    auto* watcher = new QDBusPendingCallWatcher(pending, this);
    const bool responseSubscribed = !m_bindRequestPath.isEmpty();
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, responseSubscribed] {
        watcher->deleteLater();
        QDBusPendingReply<> reply = *watcher;
        if (reply.isError()) {
            qCWarning(lcPhosphorShortcuts) << "Portal BindShortcuts failed:" << reply.error().message();
            disconnectBindShortcutsResponse();
            // Still emit ready() — the consumer needs to progress. The log
            // is the signal that grabs didn't go through.
            Q_EMIT ready();
            return;
        }
        if (!responseSubscribed) {
            // Response subscription never connected — we have no way of
            // knowing when the compositor actually finalises the grab, so
            // fire ready() now as a best effort.
            Q_EMIT ready();
        }
        // Otherwise wait for Request::Response — onBindShortcutsResponse
        // emits ready() with the compositor-confirmed shortcut table.
    });
}

void PortalBackend::onBindShortcutsResponse(uint response, const QVariantMap& results)
{
    disconnectBindShortcutsResponse();

    if (response != 0) {
        qCWarning(lcPhosphorShortcuts) << "Portal BindShortcuts Response: failed with response code" << response;
        Q_EMIT ready();
        return;
    }

    // results contains "shortcuts" (a(sa{sv})) — the compositor-confirmed
    // binding set including any trigger the user/compositor substituted for
    // our preferred_trigger. We don't surface this today — consumers get
    // activation events by id and don't need to know which physical key was
    // assigned — but logging it is a nice debugging aid.
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

void PortalBackend::disconnectCreateSessionResponse()
{
    if (m_createRequestPath.isEmpty()) {
        return;
    }
    QDBusConnection::sessionBus().disconnect(kPortalService, m_createRequestPath, kRequestInterface, kResponseSignal,
                                             this, SLOT(onCreateSessionResponse(uint, QVariantMap)));
    m_createRequestPath.clear();
}

void PortalBackend::disconnectBindShortcutsResponse()
{
    if (m_bindRequestPath.isEmpty()) {
        return;
    }
    QDBusConnection::sessionBus().disconnect(kPortalService, m_bindRequestPath, kRequestInterface, kResponseSignal,
                                             this, SLOT(onBindShortcutsResponse(uint, QVariantMap)));
    m_bindRequestPath.clear();
}

} // namespace Phosphor::Shortcuts
