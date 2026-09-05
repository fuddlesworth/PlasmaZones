// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <PhosphorPopout/IPopoutTransport.h>

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QtCore/qtclasshelpermacros.h>

#include <functional>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QQuickItem;
class QScreen;
QT_END_NAMESPACE

namespace PhosphorShell {
class FloatingWindow;
}

namespace PhosphorShellApp {

class ControlCenterController;

// IPopoutTransport for an ENGINE-PLACED PANE (A2 §4): the popout is a real
// xdg toplevel with app id `org.phosphor.shell.pane.<popoutId>`, and the
// placement engine puts it where the screen's mode puts windows. The shell
// ships one window rule per pane (PaneRules) that names the placement:
// snapping snaps it to a zone, tiling inserts it as a tile, scrolling opens
// it as a column after the focused one.
//
// What this transport does per open:
//   1. builds the request's content component against the shell engine's
//      root context (like LayerPopoutTransport) and parents it into a
//      Phosphor.Popout PaneHost, the pane's root item (ground, top band,
//      enter/release choreography, Escape);
//   2. creates a FloatingWindow of the pane's size, with the pane app id
//      and a minimum size the engines honour, and shows it. The compositor
//      places it: a toplevel chooses neither its output nor its position,
//      so the pane lands on the output the compositor considers active,
//      which is the one whose bar was clicked;
//   3. marks the control center EXTERNAL on the controller, so the bar on
//      the pane's screen draws only the tether and not its inline pane.
//
// Floating fallback (A2 §4.2, last row): when the target screen's mode is
// unknown or none (no daemon, no engine on that output), when no engine
// or content is available, or when the toplevel cannot be built, the open
// goes to the FALLBACK transport (the bar-painted SocketPopoutTransport)
// instead, so the popout always appears. Handles from the two are
// disjoint by prefix and closes route back to whichever issued them.
//
// Closing: closeSurface starts the pane's release; when PaneHost reports
// it finished, the window unmaps and the engine reflows. A close the pane
// initiates (Escape, or the compositor closing the window through the
// engine's own close verb) tears down the same way and is reported through
// the dismissed callback, deferred to the next event-loop tick per the
// IPopoutTransport contract.
class PanePopoutTransport : public QObject, public PhosphorPopout::IPopoutTransport
{
    Q_OBJECT

public:
    // `fallback` is not owned and must outlive this transport. Its dismissed
    // callback is taken over here (a fallback pane closing on its own is
    // reported upward as one of ours), so it must not be wired to another
    // owner at the same time.
    PanePopoutTransport(ControlCenterController* controller, PhosphorPopout::IPopoutTransport* fallback,
                        QObject* parent = nullptr);
    ~PanePopoutTransport() override;
    Q_DISABLE_COPY_MOVE(PanePopoutTransport)

    /// The app id a pane popout announces to the compositor and, through
    /// it, to the daemon's window rules. `org.phosphor.shell.pane.<id>`.
    [[nodiscard]] static QString appIdFor(const QString& popoutId);

    /// Adopt the engine every subsequent pane is built from. Called for
    /// the startup engine and again for each hot-reload engine.
    void setEngine(QQmlEngine* engine);

    /// Resolve the output name the request targets (null means "the
    /// transport decides"). Injectable for the same reason the socket
    /// transport's is: headless platforms give screens no name.
    using ScreenNameResolver = std::function<QString(QScreen*)>;
    void setScreenNameResolver(ScreenNameResolver resolver);

    /// The pane's default frame. The engines read the minimum size from the
    /// toplevel, so this is also the smallest slot they will hand it.
    void setPaneSize(int width, int height);

    /// Tear down every live pane synchronously and silently, for shutdown
    /// and for the moment before a hot reload destroys the engine.
    void drain();

    /// The toplevel behind `handle`, or null for an unknown, closing or
    /// fallback handle. For tests.
    [[nodiscard]] PhosphorShell::FloatingWindow* windowFor(const QString& handle) const;

    [[nodiscard]] QString openSurface(const PhosphorPopout::PopoutRequest& request) override;
    void closeSurface(const QString& handle) override;
    void setSurfaceDismissedCallback(std::function<void(const QString&)> callback) override;

private Q_SLOTS:
    /// PaneHost's QML-declared signals, reached by string connect.
    void onHostDismissed();
    void onHostReleased();

private:
    struct Entry
    {
        // Both QPointer: the window is Qt-parented to the transport and
        // torn down through deleteLater; the host dies inside its scene.
        QPointer<PhosphorShell::FloatingWindow> window;
        QPointer<QQuickItem> host;
        // Set when the release started. The controller frees its own row
        // before asking us to close and the release only starts an
        // animation, so a reopen of the same id while it runs must retire
        // the draining pane rather than stack a second toplevel.
        bool closing = false;
        QString popoutId;
        QString screenName;
    };

    [[nodiscard]] QString openFallback(const PhosphorPopout::PopoutRequest& request, const QString& reason);
    void beginRelease(QHash<QString, Entry>::iterator it);
    void reportDismissed(const QString& handle);
    [[nodiscard]] QString handleForHost(const QObject* host) const;
    void destroyEntry(const QString& handle, const Entry& entry);
    /// Mirror the open state onto the controller: which screen's bar
    /// draws the tether, and that the pane is external (no inline pane).
    void publishOpenState(const QString& screenName);

    ControlCenterController* m_controller;
    PhosphorPopout::IPopoutTransport* m_fallback;
    QPointer<QQmlEngine> m_engine;
    ScreenNameResolver m_resolveScreenName;
    QHash<QString, Entry> m_entries;
    QSet<QString> m_fallbackHandles;
    std::function<void(const QString&)> m_dismissed;
    int m_paneWidth = 380;
    int m_paneHeight = 460;
    quint64 m_counter = 0;
};

} // namespace PhosphorShellApp
