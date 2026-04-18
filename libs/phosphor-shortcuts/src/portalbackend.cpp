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

namespace Phosphor::Shortcuts {

namespace {

constexpr auto kPortalService = "org.freedesktop.portal.Desktop";
constexpr auto kPortalPath = "/org/freedesktop/portal/desktop";
constexpr auto kPortalInterface = "org.freedesktop.portal.GlobalShortcuts";
constexpr auto kSessionInterface = "org.freedesktop.portal.Session";

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
    if (m_sessionHandle.isEmpty()) {
        return;
    }
    QDBusMessage msg = QDBusMessage::createMethodCall(QString::fromLatin1(kPortalService), m_sessionHandle,
                                                      QString::fromLatin1(kSessionInterface), QStringLiteral("Close"));
    QDBusConnection::sessionBus().asyncCall(msg);
}

void PortalBackend::registerShortcut(const QString& id, const QKeySequence& preferredTrigger,
                                     const QString& description)
{
    m_pending.insert(id, {preferredTrigger, description});
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
        // Defer until onSessionCreated() runs — ready() fires from there.
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
    QVariantMap options;
    options.insert(QStringLiteral("session_handle_token"), m_sessionToken);
    options.insert(QStringLiteral("handle_token"), QVariant(m_sessionToken + QStringLiteral("_request")));

    QDBusMessage msg =
        QDBusMessage::createMethodCall(QString::fromLatin1(kPortalService), QString::fromLatin1(kPortalPath),
                                       QString::fromLatin1(kPortalInterface), QStringLiteral("CreateSession"));
    msg.setArguments({QVariant::fromValue(options)});

    QDBusPendingCall pending = QDBusConnection::sessionBus().asyncCall(msg);
    auto* watcher = new QDBusPendingCallWatcher(pending, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, &PortalBackend::onSessionCreated);
}

void PortalBackend::onSessionCreated(QDBusPendingCallWatcher* watcher)
{
    watcher->deleteLater();

    QDBusPendingReply<QDBusObjectPath> reply = *watcher;
    if (reply.isError()) {
        qCWarning(lcPhosphorShortcuts) << "Portal CreateSession failed:" << reply.error().message();
        m_sessionFailed = true;
        // Consume any pending flush that was waiting on us — the warning in
        // flush() from now on is the consumer-visible signal that something
        // is wrong.
        m_flushRequested = false;
        Q_EMIT ready();
        return;
    }

    // Portal convention: session path is
    //   /org/freedesktop/portal/desktop/session/<sanitised_sender>/<token>
    QString sender = QDBusConnection::sessionBus().baseService();
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    if (sender.startsWith(QLatin1Char(':'))) {
        sender = sender.mid(1);
    }
    m_sessionHandle = QStringLiteral("/org/freedesktop/portal/desktop/session/%1/%2").arg(sender, m_sessionToken);

    // Subscribe to Activated on this session before sending any binds — the
    // portal may fire an Activated immediately after BindShortcuts in some
    // compositor implementations if the key is already held.
    QDBusConnection::sessionBus().connect(QString::fromLatin1(kPortalService), m_sessionHandle,
                                          QString::fromLatin1(kPortalInterface), QStringLiteral("Activated"), this,
                                          SLOT(onActivated(QDBusObjectPath, QString, QVariantMap)));

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

        QVariantMap options;
        if (!it.value().description.isEmpty()) {
            options.insert(QStringLiteral("description"), it.value().description);
        }
        if (!it.value().preferred.isEmpty()) {
            options.insert(QStringLiteral("preferred_trigger"), it.value().preferred.toString());
        }
        shortcutsArg << options;
        shortcutsArg.endStructure();
    }
    shortcutsArg.endArray();

    QDBusMessage msg =
        QDBusMessage::createMethodCall(QString::fromLatin1(kPortalService), QString::fromLatin1(kPortalPath),
                                       QString::fromLatin1(kPortalInterface), QStringLiteral("BindShortcuts"));
    msg.setArguments({QVariant::fromValue(QDBusObjectPath(m_sessionHandle)), QVariant::fromValue(shortcutsArg),
                      QVariant::fromValue(QString()), QVariant::fromValue(QVariantMap{})});

    QDBusPendingCall pending = QDBusConnection::sessionBus().asyncCall(msg);
    m_pending.clear();

    // ready() now means "portal has acknowledged the BindShortcuts batch" —
    // fire it from the watcher's finished callback rather than immediately
    // after asyncCall, so consumers that un-grey UI on ready() don't do it
    // before grabs are actually live.
    auto* watcher = new QDBusPendingCallWatcher(pending, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        QDBusPendingReply<> reply = *watcher;
        if (reply.isError()) {
            qCWarning(lcPhosphorShortcuts) << "Portal BindShortcuts failed:" << reply.error().message();
            // Still emit ready() — the consumer needs to progress. The log
            // is the signal that grabs didn't go through.
        }
        Q_EMIT ready();
    });
}

void PortalBackend::onActivated(const QDBusObjectPath& /*sessionHandle*/, const QString& shortcutId,
                                const QVariantMap& /*options*/)
{
    if (!m_descriptions.contains(shortcutId)) {
        qCDebug(lcPhosphorShortcuts) << "Portal Activated for unknown id" << shortcutId;
        return;
    }
    qCDebug(lcPhosphorShortcuts) << "Portal Activated:" << shortcutId;
    Q_EMIT activated(shortcutId);
}

} // namespace Phosphor::Shortcuts
