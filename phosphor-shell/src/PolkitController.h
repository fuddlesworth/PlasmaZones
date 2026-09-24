// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Full includes, not forward declarations: AuthRequest* is a Q_PROPERTY
// type and QScreen* a Q_INVOKABLE return type. moc registers both
// metatypes from this header and Qt's pointer-to-QObject detection needs
// the complete type (the ControlCenterController::screenOf lesson: with
// a bare `class QScreen;` the returned pointer reached QML unwrapped and
// crashed the shell).
#include <PhosphorServicePolkit/AuthRequest.h>
#include <PhosphorServicePolkit/PolkitAgent.h>

#include <QObject>
#include <QPointer>
#include <QRect>
#include <QScreen>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace PhosphorLayer {
class IScreenProvider;
}

namespace PhosphorShellApp {

// The shell's PolicyKit agent owner, published to QML as the
// PolkitRegistry context property. Owns the one PolkitAgent, registers
// it as the session's agent at startup, and starts the PAM conversation
// for every request as it arrives (a dialog never has to remember to),
// so the prompt in shell.qml only binds `activeRequest`, shows
// `lastError`, and calls respond() / cancel().
//
// It also carries the two facts the prompt's placement needs that live
// outside QML: the requester's pid (from polkit's `polkit.subject-pid` /
// `polkit.caller-pid` details) and its process name, plus the screen
// name and rect shell.qml resolved for the open prompt, which the
// per-screen dim overlays read back. The response text passes straight
// through to the agent and is never stored or logged here.
class PolkitController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool registered READ isRegistered NOTIFY registeredChanged)
    Q_PROPERTY(PhosphorServicePolkit::AuthRequest* activeRequest READ activeRequest NOTIFY activeRequestChanged)
    Q_PROPERTY(qint64 requesterPid READ requesterPid NOTIFY activeRequestChanged)
    Q_PROPERTY(QString requesterName READ requesterName NOTIFY activeRequestChanged)
    Q_PROPERTY(QString requesterProgram READ requesterProgram NOTIFY activeRequestChanged)
    Q_PROPERTY(QString requesterResource READ requesterResource NOTIFY activeRequestChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString info READ info NOTIFY infoChanged)
    Q_PROPERTY(QString phase READ phase NOTIFY stateChanged)
    Q_PROPERTY(bool inputReady READ inputReady NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool canRetry READ canRetry NOTIFY stateChanged)
    Q_PROPERTY(QString promptScreen READ promptScreen NOTIFY placementChanged)
    Q_PROPERTY(QRect anchorRect READ anchorRect NOTIFY placementChanged)

public:
    explicit PolkitController(PhosphorLayer::IScreenProvider* screens, QObject* parent = nullptr);
    ~PolkitController() override;

    /// Become the session's agent. False (and inert) when another agent
    /// already holds the session; see PolkitAgent's class docs.
    bool registerAgent();
    [[nodiscard]] bool isRegistered() const;

    [[nodiscard]] PhosphorServicePolkit::AuthRequest* activeRequest() const;
    [[nodiscard]] qint64 requesterPid() const;
    [[nodiscard]] QString requesterName() const;
    [[nodiscard]] QString requesterProgram() const;
    [[nodiscard]] QString requesterResource() const;
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] QString info() const;
    [[nodiscard]] QString phase() const;
    [[nodiscard]] bool inputReady() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] bool canRetry() const;
    [[nodiscard]] QString promptScreen() const;
    [[nodiscard]] QRect anchorRect() const;

    /// Answer the active PAM prompt. Clears `lastError`.
    Q_INVOKABLE void respond(const QString& response);
    Q_INVOKABLE void selectIdentity(int index);
    Q_INVOKABLE void retry();
    Q_INVOKABLE void clearError();
    /// Decline the active request.
    Q_INVOKABLE void cancel();

    /// Where shell.qml put the prompt: the screen it opened on and, when
    /// the requester has a window there, that window's rect in the
    /// screen's pixels (an empty rect otherwise). Cleared with the request.
    Q_INVOKABLE void setPlacement(const QString& screenName, const QRect& anchorRect);

    /// Every output's name, for the placement-map walk.
    [[nodiscard]] Q_INVOKABLE QStringList screenNames() const;

    /// The QScreen called `name`, or the primary output for an empty or
    /// unknown name. Marked CppOwnership before it crosses into QML so
    /// the JS GC never deletes a live screen.
    [[nodiscard]] Q_INVOKABLE QScreen* screenNamed(const QString& name) const;

    /// The requesting process's pid from polkit's details: the subject
    /// (the application) first, else the caller (the mechanism). 0 when
    /// neither is present.
    [[nodiscard]] static qint64 pidFromDetails(const QVariantMap& details);

    /// `/proc/<pid>/comm`, or empty.
    [[nodiscard]] static QString processName(qint64 pid);

Q_SIGNALS:
    void registeredChanged();
    void activeRequestChanged();
    void lastErrorChanged();
    void infoChanged();
    void stateChanged();
    void promptRequested(const QString& prompt, bool echo);
    void placementChanged();

private:
    void setLastError(const QString& error);
    void setInfo(const QString& info);

    PhosphorServicePolkit::PolkitAgent m_agent;
    PhosphorLayer::IScreenProvider* m_screens;
    QString m_lastError;
    QString m_info;
    QPointer<PhosphorServicePolkit::AuthRequest> m_currentRequest;
    QString m_promptScreen;
    QRect m_anchorRect;
};

} // namespace PhosphorShellApp
