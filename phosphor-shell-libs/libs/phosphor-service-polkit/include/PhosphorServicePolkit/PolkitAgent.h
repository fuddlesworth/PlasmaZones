// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServicePolkit/phosphorservicepolkit_export.h>
// Full AuthRequest definition, not a forward declaration: it sits on this
// header's moc surface — the `activeRequest` Q_PROPERTY and the
// `authenticationRequested` signal both carry AuthRequest*, and Qt6 moc
// auto-registers property/signal-parameter metatypes (QMetaType
// SFINAE-probes completeness). A fwd decl re-fires GCC's -Wsfinae-incomplete
// the moment mocs_compilation aggregation order stops shielding this header
// (same rationale as the phosphor-service-pipewire headers).
#include <PhosphorServicePolkit/AuthRequest.h>

#include <QObject>
#include <QString>

#include <memory>

namespace PhosphorServicePolkit {

class ListenerImpl;

/**
 * @brief A PolicyKit authentication agent for Phosphor-based desktop shells.
 *
 * When an application requests a privileged action, `polkitd` calls into the
 * agent registered for the session; the agent drives the PAM conversation that
 * authenticates the user. This class is that agent. It wraps `polkit-qt6`'s
 * `Agent::Listener` privately, so the public surface stays a clean `QObject`
 * with no polkit-qt types: a consumer binds the request + a `respond` path and
 * never touches polkit-qt directly.
 *
 * Registration is explicit (`registerAgent()`), not done in the constructor:
 * becoming the session's agent intercepts every authentication, so it is an
 * opt-in step a shell or the CLI demo takes deliberately. Exactly one agent
 * serves a session, so when the desktop's agent (KDE / GNOME) already holds it
 * `registerAgent()` fails and the object stays inert (`registered() == false`),
 * mirroring the name-conflict-is-inert shape of `phosphor-service-notifications`.
 *
 * `initiateAuthentication` from polkit is decoded into a typed `AuthRequest`
 * surfaced as `activeRequest`; `authenticate()` then drives the `Agent::Session`
 * PAM conversation (prompt -> `respond()` -> `completed`), with `cancel()` and
 * polkit's withdrawal both resolving polkit's result exactly once.
 */
class PHOSPHORSERVICEPOLKIT_EXPORT PolkitAgent : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool registered READ registered NOTIFY registeredChanged)
    Q_PROPERTY(PhosphorServicePolkit::AuthRequest* activeRequest READ activeRequest NOTIFY activeRequestChanged)

public:
    /// Agent for the current session, at the default object path.
    explicit PolkitAgent(QObject* parent = nullptr);
    /// Dependency-injected: register for an explicit session id at an explicit
    /// object path (tests pass a synthetic session so registration is exercised
    /// without owning the real one). An empty @p sessionId means the current
    /// session, resolved at registration time.
    PolkitAgent(QString sessionId, QString objectPath, QObject* parent = nullptr);
    ~PolkitAgent() override;

    /// True once this process is the registered authentication agent for its
    /// session. False until `registerAgent()` succeeds, or when another agent
    /// already holds the session (see class docs).
    [[nodiscard]] bool registered() const;

    /// The default D-Bus object path the agent exports at.
    [[nodiscard]] static QString defaultObjectPath();

    /// Register with `polkitd` as the session's authentication agent. Returns
    /// `registered()`. A no-op once registered. Q_INVOKABLE so a shell/CLI can
    /// opt in from QML.
    Q_INVOKABLE bool registerAgent();

    /// The authentication request polkit is currently waiting on, or null. A
    /// dialog binds this; at most one is active (polkit serialises).
    [[nodiscard]] AuthRequest* activeRequest() const;

    /// Begin authenticating the active request as its `selectedIdentity`: starts
    /// the PAM conversation, after which `activeRequest`'s `prompt` updates and
    /// the user's answer goes back via `respond()`. A no-op with no active
    /// request or once a conversation is already running. Q_INVOKABLE for a UI.
    Q_INVOKABLE void authenticate();

    /// Answer the active PAM prompt. The response is passed straight to PAM and
    /// is never stored, logged, or echoed by this library. Q_INVOKABLE for a UI.
    Q_INVOKABLE void respond(const QString& response);

    /// Decline the active request: it completes without authorization and clears
    /// (aborting any running PAM conversation). Q_INVOKABLE for a UI / CLI; never
    /// exported on any bus.
    Q_INVOKABLE void cancel();

Q_SIGNALS:
    void registeredChanged();
    void activeRequestChanged();
    /// A new authentication request arrived (also reflected in `activeRequest`).
    void authenticationRequested(PhosphorServicePolkit::AuthRequest* request);
    /// PAM is asking for input: answer it with `respond()`. This is an EVENT, not
    /// a property change, so it fires once per request including a same-text
    /// retry after a wrong answer (which `activeRequest`'s change-guarded `prompt`
    /// would not re-notify). @p echo is false for secrets (do not echo input).
    void promptRequested(const QString& prompt, bool echo);
    /// The PAM conversation finished; @p gainedAuthorization is true when the
    /// user authenticated successfully.
    void authenticationCompleted(bool gainedAuthorization);
    /// PAM reported an error (e.g. "Authentication failure") to show the user.
    void authenticationError(const QString& text);
    /// PAM reported an informational message to show the user.
    void authenticationInfo(const QString& text);
    /// The active request ended with none remaining (declined here, or withdrawn
    /// by polkit).
    void authenticationCancelled();

private:
    Q_DISABLE_COPY_MOVE(PolkitAgent)
    friend class ListenerImpl;

    /// The PAM session finished: settle the active request (completing polkit's
    /// result) and emit authenticationCompleted.
    void onSessionCompleted(bool gainedAuthorization);
    /// Resolve the active request exactly once: capture-and-clear the state first
    /// (so re-entrant calls become no-ops), disconnect + delete the session
    /// (aborting PAM), optionally complete polkit's result, and emit
    /// activeRequestChanged. @p completeResult is false only when polkit itself
    /// withdrew the request and owns the result's teardown.
    void settleActive(bool completeResult);

    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServicePolkit
