// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorScreens/PlasmaPanelSource.h"

#include "screenslogging.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QScreen>

namespace Phosphor::Screens {

namespace {
constexpr auto kPlasmaShellService = "org.kde.plasmashell";
constexpr auto kPlasmaShellPath = "/PlasmaShell";
constexpr auto kPlasmaShellInterface = "org.kde.PlasmaShell";

/// JS evaluated by `org.kde.plasmashell.evaluateScript` to enumerate panels.
/// Output format per panel (one line each):
///   PANEL:<screenIdx>:<location>:<hiding>:<offset>:<floating>:<x>,<y>,<w>,<h>
/// The trailing geometry is always present (empty sgStr collapses to bare
/// `:` → no digits); the regex below anchors on end-of-line to avoid gluing
/// the next line's `PANEL:` prefix into an optional capture.
const QString& panelScript()
{
    static const QString s = QStringLiteral(R"(
        panels().forEach(function(p,i){
            var floating = p.floating ? 1 : 0;
            var hiding = p.hiding;
            var sg = screenGeometry(p.screen);
            var loc = p.location;
            var pg = p.geometry;
            // Offset defaults to the panel's thickness — overridden below
            // for each edge once we have both panel and screen rects.
            var offset = Math.abs(p.height);
            if (pg && sg) {
                if (loc === "top") {
                    offset = (pg.y + pg.height) - sg.y;
                } else if (loc === "bottom") {
                    offset = (sg.y + sg.height) - pg.y;
                } else if (loc === "left") {
                    offset = (pg.x + pg.width) - sg.x;
                } else if (loc === "right") {
                    offset = (sg.x + sg.width) - pg.x;
                }
            }
            var sgStr = sg ? (sg.x + "," + sg.y + "," + sg.width + "," + sg.height) : "";
            print("PANEL:" + p.screen + ":" + loc + ":" + hiding + ":" + offset + ":" + floating + ":" + sgStr + "\n");
        });
    )");
    return s;
}
} // namespace

PlasmaPanelSource::PlasmaPanelSource(QObject* parent)
    : IPanelSource(parent)
{
    m_requeryTimer.setSingleShot(true);
    connect(&m_requeryTimer, &QTimer::timeout, this, [this]() {
        issueQuery(/*emitRequeryCompleted=*/true);
    });
}

PlasmaPanelSource::~PlasmaPanelSource() = default;

void PlasmaPanelSource::start()
{
    if (m_running) {
        return;
    }
    m_running = true;

    // Watch for plasmashell registration so we re-query as soon as the
    // service appears, instead of guessing with arbitrary timer delays.
    if (!m_plasmaShellWatcher) {
        m_plasmaShellWatcher =
            new QDBusServiceWatcher(QString::fromLatin1(kPlasmaShellService), QDBusConnection::sessionBus(),
                                    QDBusServiceWatcher::WatchForRegistration, this);
        connect(m_plasmaShellWatcher, &QDBusServiceWatcher::serviceRegistered, this, [this]() {
            qCInfo(lcPhosphorScreens) << "Plasmashell: registered, querying panels";
            issueQuery(/*emitRequeryCompleted=*/false);
        });
    }

    issueQuery(/*emitRequeryCompleted=*/false);
}

void PlasmaPanelSource::stop()
{
    if (!m_running) {
        return;
    }
    m_running = false;
    m_requeryTimer.stop();
    if (m_plasmaShellWatcher) {
        // Disconnect BEFORE deleteLater — the watcher lingers on Qt's
        // deferred-delete queue until the next event-loop turn, and a
        // serviceRegistered signal that arrives in that window would
        // otherwise fire our lambda and kick a fresh issueQuery after
        // stop() has cleared our state.
        disconnect(m_plasmaShellWatcher, nullptr, this, nullptr);
        m_plasmaShellWatcher->deleteLater();
        m_plasmaShellWatcher = nullptr;
    }
    // Cancel any in-flight async D-Bus call. The watcher's finished slot
    // early-returns when m_activeWatcher has been cleared, so we avoid
    // mutating m_offsets or emitting signals post-stop. The watcher itself
    // is parented to `this` and deleted with us; deleteLater keeps the
    // Qt event-loop teardown order clean.
    if (m_activeWatcher) {
        m_activeWatcher->deleteLater();
        m_activeWatcher = nullptr;
    }
    m_queryPending = false;
    m_requeryQueued = false;
    m_queuedEmitRequeryCompleted = false;
}

PlasmaPanelSource::Offsets PlasmaPanelSource::currentOffsets(QScreen* screen) const
{
    if (!screen) {
        return {};
    }
    return m_offsets.value(screen->name());
}

bool PlasmaPanelSource::ready() const
{
    return m_ready;
}

void PlasmaPanelSource::requestRequery(int delayMs)
{
    if (delayMs <= 0) {
        issueQuery(/*emitRequeryCompleted=*/true);
        return;
    }
    m_requeryTimer.setInterval(delayMs);
    m_requeryTimer.start();
}

void PlasmaPanelSource::issueQuery(bool emitRequeryCompleted)
{
    // Belt-and-braces: even with the stop() disconnect, a queued-but-
    // not-yet-dispatched slot invocation could still reach here. Bail
    // so we don't spin up a fresh D-Bus call or mutate m_offsets post-stop.
    if (!m_running) {
        return;
    }

    // Coalesce: if a query is already in flight, schedule exactly one
    // follow-up to run when it lands. Additional calls while pending
    // collapse onto that single queued follow-up. The emit-completion
    // request is OR-ed across queued callers so any one that asked for
    // `requeryCompleted` still gets it.
    if (m_queryPending) {
        m_requeryQueued = true;
        m_queuedEmitRequeryCompleted = m_queuedEmitRequeryCompleted || emitRequeryCompleted;
        return;
    }

    auto* plasmaShell =
        new QDBusInterface(QString::fromLatin1(kPlasmaShellService), QString::fromLatin1(kPlasmaShellPath),
                           QString::fromLatin1(kPlasmaShellInterface), QDBusConnection::sessionBus());

    if (!plasmaShell->isValid()) {
        // No Plasma shell — clear m_offsets (so previously-recorded panels
        // disappear after a Plasma shell exit), mark ready, fire per-screen
        // change signals so the manager re-runs availability calculation.
        // This branch is fully synchronous: no async call in flight, so we
        // never flip m_queryPending true.
        delete plasmaShell;

        QHash<QString, Offsets> previous = m_offsets;
        m_offsets.clear();

        for (auto* screen : QGuiApplication::screens()) {
            if (previous.contains(screen->name())) {
                Q_EMIT panelOffsetsChanged(screen);
            }
        }

        if (!m_ready) {
            m_ready = true;
            qCInfo(lcPhosphorScreens) << "Panel geometry: ready, no Plasma shell";
            // First-ready transition — kick a synthetic per-screen
            // change so listeners refresh available-geometry. Otherwise
            // a host that wires panelGeometryReady to "now compute zones"
            // would try to compute against stale (Qt-default) availability.
            for (auto* screen : QGuiApplication::screens()) {
                Q_EMIT panelOffsetsChanged(screen);
            }
        }
        if (emitRequeryCompleted) {
            Q_EMIT requeryCompleted();
        }
        // Service absent: don't silently drain queued requeries; they would
        // all hit the same missing-service path. Clear and move on.
        m_requeryQueued = false;
        m_queuedEmitRequeryCompleted = false;
        return;
    }

    // Only now do we have a genuine async call in flight.
    m_queryPending = true;

    QDBusPendingCall pendingCall = plasmaShell->asyncCall(QStringLiteral("evaluateScript"), panelScript());
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);
    // Parent plasmaShell to the watcher so the interface object goes away
    // with it — covers the two shutdown paths the finished-lambda doesn't:
    // destruction of `this` (QObject deletes the watcher-child, which
    // deletes plasmaShell-grandchild) and a canceled stop() where the
    // reply never lands (deleteLater on the watcher sweeps the interface
    // too). Previously raw `new` here leaked on both.
    plasmaShell->setParent(watcher);
    m_activeWatcher = watcher;

    // plasmaShell intentionally not captured — it's parented to `watcher`
    // (which in turn is parented to `this`), so its lifetime is driven by
    // deleteLater on the watcher. Capturing it would leave a silent
    // dangling-read risk if we forgot to update the capture on future
    // teardown-order refactors.
    connect(
        watcher, &QDBusPendingCallWatcher::finished, this, [this, emitRequeryCompleted](QDBusPendingCallWatcher* w) {
            // stop() cancelled us — don't mutate state or emit signals.
            // The watcher has already been queued for deletion via stop(),
            // so we just return without touching anything. plasmaShell
            // dies with the watcher via the parent relationship above.
            if (m_activeWatcher != w) {
                return;
            }
            QDBusPendingReply<QString> reply = *w;

            QHash<QString, Offsets> newOffsets;
            const bool replyValid = reply.isValid();

            if (replyValid) {
                const QString output = reply.value();
                qCDebug(lcPhosphorScreens) << "queryKdePlasmaPanels D-Bus reply=" << output;

                // Regex capture groups:
                //   1: plasma screen index
                //   2: location (top/bottom/left/right)
                //   3: hiding mode (none/autohide/dodgewindows/windowsgobelow)
                //   4: totalOffset (reserved edge in px)
                //   5: floating flag (0 or 1)
                //   6-9: plasma screen geometry (x,y,w,h)
                //
                // End-of-line anchor (`(?=\n|$)`) so a line whose geometry
                // segment is empty (sg === null in the JS) cannot greedy-match
                // digits from the NEXT line's PANEL: prefix into the optional
                // geometry group. Multiline-enabled so `$` respects intra-reply
                // newlines.
                static const QRegularExpression panelRegex(QStringLiteral("PANEL:(\\d+):(\\w+):(\\w+):(\\d+):(\\d+)"
                                                                          "(?::(\\d+),(\\d+),(\\d+),(\\d+))?(?=\\n|$)"),
                                                           QRegularExpression::MultilineOption);
                const auto qtScreens = QGuiApplication::screens();
                auto it = panelRegex.globalMatch(output);
                while (it.hasNext()) {
                    QRegularExpressionMatch match = it.next();
                    const int plasmaIndex = match.captured(1).toInt();
                    const QString location = match.captured(2);
                    const QString hiding = match.captured(3);
                    int totalOffset = match.captured(4).toInt();
                    const bool floating = (match.captured(5) == QLatin1String("1"));

                    QString connectorName;
                    if (!match.captured(6).isEmpty()) {
                        const QRect plasmaGeom(match.captured(6).toInt(), match.captured(7).toInt(),
                                               match.captured(8).toInt(), match.captured(9).toInt());
                        for (auto* qs : qtScreens) {
                            if (qs->geometry() == plasmaGeom) {
                                connectorName = qs->name();
                                break;
                            }
                        }
                    }

                    if (connectorName.isEmpty()) {
                        qCWarning(lcPhosphorScreens) << "Could not match Plasma screen" << plasmaIndex
                                                     << "to any Qt screen by geometry — skipping panel";
                        continue;
                    }

                    const bool autoHides =
                        (hiding == QLatin1String("autohide") || hiding == QLatin1String("dodgewindows")
                         || hiding == QLatin1String("windowsgobelow"));
                    // Floating panels sit with margins off the screen edge and
                    // do not reserve exclusive area — windows can extend under
                    // them. Treat them like auto-hide for availability accounting;
                    // otherwise calculateAvailableGeometry's qMin(sensor, dbus)
                    // branch would shrink the rect by the floating panel's height.
                    if (autoHides || floating) {
                        totalOffset = 0;
                    }

                    Offsets& offsets = newOffsets[connectorName];
                    if (location == QLatin1String("top")) {
                        offsets.top = qMax(offsets.top, totalOffset);
                    } else if (location == QLatin1String("bottom")) {
                        offsets.bottom = qMax(offsets.bottom, totalOffset);
                    } else if (location == QLatin1String("left")) {
                        offsets.left = qMax(offsets.left, totalOffset);
                    } else if (location == QLatin1String("right")) {
                        offsets.right = qMax(offsets.right, totalOffset);
                    }
                }
            } else {
                qCWarning(lcPhosphorScreens) << "queryKdePlasmaPanels D-Bus query failed:" << reply.error().message();
                // Preserve previous offsets on failure — clearing them would
                // zero out every screen's panel info on a transient error.
                newOffsets = m_offsets;
            }

            // Diff old vs new and fire per-screen change signals only for
            // screens whose offsets actually changed (including transitions
            // to/from "no panels").
            QSet<QString> changedNames;
            for (auto it = newOffsets.constBegin(); it != newOffsets.constEnd(); ++it) {
                auto prev = m_offsets.constFind(it.key());
                if (prev == m_offsets.constEnd() || !(prev.value() == it.value())) {
                    changedNames.insert(it.key());
                }
                qCInfo(lcPhosphorScreens)
                    << "Screen" << it.key() << "panel offsets T=" << it.value().top << "B=" << it.value().bottom
                    << "L=" << it.value().left << "R=" << it.value().right;
            }
            for (auto it = m_offsets.constBegin(); it != m_offsets.constEnd(); ++it) {
                if (!newOffsets.contains(it.key())) {
                    changedNames.insert(it.key());
                }
            }

            m_offsets = std::move(newOffsets);

            const auto qtScreens = QGuiApplication::screens();
            for (const QString& name : std::as_const(changedNames)) {
                for (auto* qs : qtScreens) {
                    if (qs->name() == name) {
                        Q_EMIT panelOffsetsChanged(qs);
                        break;
                    }
                }
            }

            // Only flip to ready on a *successful* reply. A transient D-Bus
            // failure must not leave us stuck in "ready with no panels"
            // forever — the watcher would then never retry first-ready on the
            // next `requestRequery` because we'd already be ready. Callers
            // gate startup work on this flag; giving them a false positive
            // paints stale geometry.
            if (!m_ready && replyValid) {
                m_ready = true;
                qCInfo(lcPhosphorScreens) << "Panel geometry: ready";
                // First-ready transition — synthetic per-screen fan-out so
                // listeners that wire panelGeometryReady to "now compute
                // zones" pick up the offsets even if no per-screen diff
                // fired (e.g. a screen with no panels at all).
                for (auto* qs : qtScreens) {
                    if (!changedNames.contains(qs->name())) {
                        Q_EMIT panelOffsetsChanged(qs);
                    }
                }
            }

            if (emitRequeryCompleted) {
                Q_EMIT requeryCompleted();
            }

            // plasmaShell is parented to w; deleting w deletes it.
            w->deleteLater();
            m_activeWatcher = nullptr;
            m_queryPending = false;

            // Drain any coalesced follow-up. One reissue covers the
            // collapsed stack of requestRequery calls that arrived
            // while the current call was in flight.
            if (m_running && m_requeryQueued) {
                const bool queuedEmit = m_queuedEmitRequeryCompleted;
                m_requeryQueued = false;
                m_queuedEmitRequeryCompleted = false;
                issueQuery(queuedEmit);
            }
        });
}

} // namespace Phosphor::Screens
