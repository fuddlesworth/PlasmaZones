// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "LayerPopoutTransport.h"

#include <PhosphorLayer/IScreenProvider.h>
#include <PhosphorLayer/Role.h>
#include <PhosphorLayer/Surface.h>
#include <PhosphorLayer/SurfaceConfig.h>
#include <PhosphorLayer/SurfaceFactory.h>

#include <QColor>
#include <QLoggingCategory>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QScreen>

#include <utility>

namespace {
Q_LOGGING_CATEGORY(lcPopoutTransport, "phosphorshell.popout.transport")

// The scrim is the only thing that distinguishes a modal popout from a
// cooperative one visually, so the two alphas are a designed pair rather than
// two independent numbers: the modal reads as "the rest is unavailable", the
// cooperative as "this is on top of, not instead of".
constexpr int kModalScrimAlpha = 160;
constexpr int kCooperativeScrimAlpha = 60;

// Handle prefix. RoutingPopoutTransport keys close-routing on the handle
// string and documents these as disjoint by construction, so this must not
// collide with the socket transport's own prefix.
constexpr QLatin1String kLayerHandlePrefix("popout-");
} // namespace

namespace PhosphorShellApp {

LayerPopoutTransport::LayerPopoutTransport(PhosphorLayer::SurfaceFactory* factory,
                                           PhosphorLayer::IScreenProvider* screens, QObject* parent)
    : QObject(parent)
    , m_factory(factory)
    , m_screens(screens)
{
}

LayerPopoutTransport::~LayerPopoutTransport()
{
    // Belt and braces. The owner should have called drain() from
    // aboutToQuit while the engine was alive; reaching here with entries
    // means a surface is being destroyed during static teardown, which is
    // exactly where Qt object graphs misbehave.
    if (!m_entries.isEmpty()) {
        qCWarning(lcPopoutTransport) << "destroyed with" << m_entries.size()
                                     << "live popout(s); drain() should have run first";
        drain();
    }
}

void LayerPopoutTransport::setEngine(QQmlEngine* engine)
{
    m_engine = engine;
}

void LayerPopoutTransport::setReservedMarginsProvider(ReservedMarginsProvider provider)
{
    m_reservedMargins = std::move(provider);
}

void LayerPopoutTransport::drain()
{
    // Note this DEFERS destruction: destroyEntry deleteLater()s each Surface,
    // so the surfaces and their hosts outlive this call by at least one event
    // loop turn, and on the reload path they outlive the QQmlEngine that made
    // them (ShellEngine::teardown resets the engine synchronously right after
    // aboutToReload). What makes that safe is destroyEntry's DISCONNECT, not
    // any synchrony: ~QQmlEngine runs each host's Component.onDestruction, and
    // the disconnect is the only reason that emission reaches nobody. Do not
    // remove it on the assumption that draining first is enough.
    //
    // The callback is deliberately NOT cleared here, for the same reason:
    // nothing below can reach onHostDismissed, so there is no self-dismissal
    // to suppress.
    //
    // `m_engine` is likewise retained rather than nulled: the reload path
    // drains and then hands over a replacement through setEngine, so clearing
    // it here would buy nothing. On the shutdown path the controller's tables
    // are already empty before drain runs, so no further openSurface arrives. Clearing it
    // would be permanent: PopoutController installs its callback once, in its
    // constructor, and never reinstalls, so a transport disarmed by the first
    // hot reload would swallow every click-outside and Escape for the rest of
    // the process while still tearing the surface down. The controller would
    // keep the stale row and report the popout as open forever.
    const auto entries = std::exchange(m_entries, {});
    for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
        destroyEntry(it.key(), it.value());
    }
}

QString LayerPopoutTransport::openSurface(const PhosphorPopout::PopoutRequest& request)
{
    // Every refusal returns an empty string, which the controller reads as
    // "open failed" and leaves its arbitration tables untouched.
    if (!m_factory) {
        qCWarning(lcPopoutTransport) << "refusing" << request.popoutId << "— no surface factory";
        return {};
    }
    if (!m_engine) {
        qCWarning(lcPopoutTransport) << "refusing" << request.popoutId << "— no QML engine yet";
        return {};
    }
    if (!request.content) {
        qCWarning(lcPopoutTransport) << "refusing" << request.popoutId << "— request carries no content component";
        return {};
    }

    // Retire any surface for this popout that is still draining. The
    // controller frees its own row before asking us to close, and our close
    // only sets `open` false and waits for the host's dismiss animation, so
    // a caller toggling faster than that animation would otherwise stack a
    // fresh full-screen surface on top of each one still on its way out,
    // several of them holding an exclusive keyboard grab. Bounded by the
    // animation rather than unbounded, but a local peer can drive it through
    // the IPC toggle verb as fast as it likes.
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (it->closing && it->popoutId == request.popoutId) {
            const QString staleHandle = it.key();
            const Entry stale = *it;
            it = m_entries.erase(it);
            qCDebug(lcPopoutTransport) << "retiring still-closing surface" << staleHandle << "before reopening"
                                       << request.popoutId;
            destroyEntry(staleHandle, stale);
        } else {
            ++it;
        }
    }

    QScreen* screen = request.targetScreen;
    if (!screen && m_screens) {
        // A null targetScreen means "the transport decides". Focused, not
        // primary: a popout the user just triggered belongs on the output
        // they are working on.
        screen = m_screens->focused();
        if (!screen) {
            screen = m_screens->primary();
        }
    }
    if (!screen) {
        qCWarning(lcPopoutTransport) << "refusing" << request.popoutId << "— no screen available";
        return {};
    }

    // Placement. The surface stays full-bleed for every anchor — the scrim,
    // click-outside dismissal and keyboard grab all live on it — and
    // PopoutHost places the content frame within it. The bar anchors hang
    // the frame below the top reserved band: the exclusive zone this
    // shell's own panels advertise on that screen, which the surface cannot
    // ask the compositor for but ShellEngine knows, reached through the
    // provider main.cpp installs.
    //
    // BarCenter is the request's DEFAULT, and with placement implemented it
    // means what it says: a caller that never expressed an opinion gets
    // "below the bar, centred", the natural home for a bar-summoned popout.
    // Anything that wants the middle of the screen (the power menu, the
    // launcher) asks for ScreenCenter explicitly. AtPointer has no pointer
    // position to work from and still centres, with a warning, so a caller
    // asking for it hears that it did not get it.
    QString placement = QStringLiteral("center");
    int reservedTop = 0;
    switch (request.anchor) {
    case PhosphorPopout::Anchor::BarLeft:
        placement = QStringLiteral("barLeft");
        break;
    case PhosphorPopout::Anchor::BarCenter:
        placement = QStringLiteral("barCenter");
        break;
    case PhosphorPopout::Anchor::BarRight:
        placement = QStringLiteral("barRight");
        break;
    case PhosphorPopout::Anchor::Custom:
        placement = QStringLiteral("custom");
        break;
    case PhosphorPopout::Anchor::AtPointer:
        qCWarning(lcPopoutTransport) << "popout" << request.popoutId
                                     << "asked for AtPointer, which this transport cannot place yet; centring";
        break;
    case PhosphorPopout::Anchor::ScreenCenter:
        break;
    }
    if (placement.startsWith(QLatin1String("bar"))) {
        if (m_reservedMargins) {
            reservedTop = m_reservedMargins(screen).top();
        } else {
            // Not fatal: the popout still opens, hanging from the screen's
            // top edge instead of the bar's bottom edge. But that is a
            // wiring omission in the host, so say so.
            qCWarning(lcPopoutTransport) << "popout" << request.popoutId
                                         << "is bar-anchored but no reserved-margins provider is installed;"
                                         << "hanging from the screen edge";
        }
    }

    // Build the content from the SHELL engine so it sees the shell's context
    // properties. Two-phase creation so the request's props are in place
    // before the delegate's bindings settle.
    QQmlComponent* component = request.content;
    QObject* contentObject = component->beginCreate(m_engine->rootContext());
    auto* contentItem = qobject_cast<QQuickItem*>(contentObject);
    if (!contentItem) {
        qCWarning(lcPopoutTransport) << "refusing" << request.popoutId
                                     << "— content is not a QQuickItem:" << component->errorString();
        // completeCreate must still run to balance beginCreate, even on the
        // failure leg.
        component->completeCreate();
        if (contentObject) {
            contentObject->deleteLater();
        }
        return {};
    }
    // setInitialProperties rather than a setProperty loop: it is valid
    // exactly in this beginCreate/completeCreate window, it overrides the
    // delegate's own initial bindings instead of being clobbered by them at
    // completeCreate, and it warns on a name the delegate does not declare.
    // A raw setProperty would silently create a dynamic property that QML
    // cannot see, so a typo'd prop name would be an invisible no-op.
    //
    // The writes to the HOST further down use plain setProperty even though
    // some land inside its own beginCreate window. That is safe only because
    // PopoutHost initializes every one of those properties with a constant
    // rather than a binding, so completeCreate has nothing to re-evaluate over
    // the top of them. A binding introduced there would silently clobber these
    // writes, and they would have to move to setInitialProperties.
    if (!request.props.isEmpty()) {
        component->setInitialProperties(contentItem, request.props);
    }
    component->completeCreate();

    // Wrap it in PopoutHost, which owns the scrim, the click-outside
    // dismissal, Escape, and the open/close animation.
    // Resolve by module URI + type name (Qt 6.5+) rather than hardcoding a
    // qrc path: the path depends on the module's resource-alias pinning, and
    // a wrong guess fails only at runtime. Same form BarController uses for
    // the bar delegates.
    QQmlComponent hostComponent(m_engine, QStringLiteral("Phosphor.Popout"), QStringLiteral("PopoutHost"));
    if (hostComponent.isError()) {
        qCWarning(lcPopoutTransport) << "refusing" << request.popoutId
                                     << "— PopoutHost failed to load:" << hostComponent.errorString();
        contentItem->deleteLater();
        return {};
    }
    QObject* hostObject = hostComponent.beginCreate(m_engine->rootContext());
    auto* hostItem = qobject_cast<QQuickItem*>(hostObject);
    if (!hostItem) {
        qCWarning(lcPopoutTransport) << "refusing" << request.popoutId << "— PopoutHost is not a QQuickItem";
        // Balance beginCreate and delete what it produced, the same way the
        // content leg above does. A non-null non-Item result still needs both.
        hostComponent.completeCreate();
        if (hostObject) {
            hostObject->deleteLater();
        }
        contentItem->deleteLater();
        return {};
    }
    // Checked: these two are what make the popout a popout. A PopoutHost that
    // renamed or re-typed either would otherwise degrade to an empty,
    // never-opening surface with nothing in the log to say why.
    if (!hostItem->setProperty("contentItem", QVariant::fromValue(contentItem))) {
        // Refuse rather than continue. Falling through would build a surface
        // with no content that still takes the Modal legs below: a full-bleed
        // scrim with an exclusive keyboard grab and nothing in it, returned
        // to the caller as a valid handle it cannot distinguish from a real
        // open. Tear down both halves; the content is not yet QObject-parented
        // to the host at this point, so it needs its own deleteLater.
        qCWarning(lcPopoutTransport) << "refusing" << request.popoutId << "— PopoutHost rejected the contentItem write";
        hostComponent.completeCreate();
        hostItem->deleteLater();
        contentItem->deleteLater();
        return {};
    }
    // QObject-parent the content to the host. PopoutHost's `contentItem.parent
    // = contentFrame` sets the VISUAL parent only: QQuickItem::setParentItem
    // does not touch QObject ownership, and beginCreate hands back an object
    // with CppOwnership and no parent. Without this line the content survives
    // every teardown path (close, drain, hot reload, surface-creation failure)
    // and a whole delegate subtree leaks per open.
    contentItem->setParent(hostItem);
    // Checked like the contentItem write above: these are declared PopoutHost
    // properties, so a rejected write means the host renamed or re-typed one
    // and the popout would degrade silently (never dismissing on
    // click-outside, or grabbing keys it should not).
    if (!hostItem->setProperty("dismissOnClickOutside", request.dismissOnFocusLoss)) {
        qCWarning(lcPopoutTransport) << "popout" << request.popoutId
                                     << "— PopoutHost rejected the dismissOnClickOutside write";
    }
    // Keyboard focus is a separate question from the dismiss policy, and the
    // host gates its whole content subtree on this one.
    if (!hostItem->setProperty("keyboardFocus", request.keyboardFocus)) {
        qCWarning(lcPopoutTransport) << "popout" << request.popoutId << "— PopoutHost rejected the keyboardFocus write";
    }
    // The scrim is what makes a Modal read as modal. Cooperative popouts get
    // a light wash, Detached none at all so they never darken the desktop.
    QColor backdrop;
    switch (request.exclusive) {
    case PhosphorPopout::ExclusiveMode::Modal:
        backdrop = QColor(0, 0, 0, kModalScrimAlpha);
        break;
    case PhosphorPopout::ExclusiveMode::Cooperative:
        backdrop = QColor(0, 0, 0, kCooperativeScrimAlpha);
        break;
    case PhosphorPopout::ExclusiveMode::Detached:
        backdrop = QColor(Qt::transparent);
        break;
    }
    if (!hostItem->setProperty("backdropColor", backdrop)) {
        qCWarning(lcPopoutTransport) << "popout" << request.popoutId << "— PopoutHost rejected the backdropColor write";
    }
    // Placement, resolved above. Checked like the other host writes: a
    // rejected write means the host renamed a property and every
    // bar-anchored popout would silently land mid-screen.
    if (!hostItem->setProperty("placement", placement)) {
        qCWarning(lcPopoutTransport) << "popout" << request.popoutId << "— PopoutHost rejected the placement write";
    }
    if (!hostItem->setProperty("reservedTop", reservedTop)) {
        qCWarning(lcPopoutTransport) << "popout" << request.popoutId << "— PopoutHost rejected the reservedTop write";
    }
    // The inset is a one-shot write, so a popout open across an output
    // geometry change kept hanging at the offset the bar had when it opened.
    // Re-push it while this host is alive; the guarded QPointer means a
    // surface torn down in the meantime simply stops being updated. The
    // connection dies with the host item, which is what owns the context.
    if (m_reservedMargins) {
        QPointer<QQuickItem> guardedHost(hostItem);
        const QString popoutId = request.popoutId;
        connect(screen, &QScreen::geometryChanged, hostItem, [this, guardedHost, screen, popoutId] {
            if (!guardedHost || !m_reservedMargins) {
                return;
            }
            if (!guardedHost->setProperty("reservedTop", m_reservedMargins(screen).top())) {
                qCWarning(lcPopoutTransport) << "popout" << popoutId << "— PopoutHost rejected a reservedTop update";
            }
        });
    }
    if (request.anchor == PhosphorPopout::Anchor::Custom) {
        // Both writes always run. Short-circuiting on the first would leave a
        // Custom-anchored popout at a half-applied position, keeping the
        // previous Y with a new X, and the single warning would not say which
        // of the two the host rejected.
        const bool wroteX = hostItem->setProperty("customX", request.customAnchor.x());
        const bool wroteY = hostItem->setProperty("customY", request.customAnchor.y());
        if (!wroteX || !wroteY) {
            qCWarning(lcPopoutTransport) << "popout" << request.popoutId
                                         << "— PopoutHost rejected the customAnchor write; customX ok?" << wroteX
                                         << "customY ok?" << wroteY;
        }
    }
    // Give the content a way back to its host so a delegate can close itself
    // without walking the parent chain.
    contentItem->setProperty("_popoutHost", QVariant::fromValue(hostItem));
    hostComponent.completeCreate();

    // Full-screen overlay. exclusiveZone MUST be -1: Role::isValid rejects
    // an Overlay surface that reserves or respects a zone, so 0 would be
    // refused by the factory outright.
    PhosphorLayer::Role role;
    role.layer = PhosphorLayer::Layer::Overlay;
    role.anchors = PhosphorLayer::AnchorAll;
    role.exclusiveZone = -1;
    // Exclusive is a wholesale keyboard grab on the Overlay layer, so only a
    // Modal earns it: that is the case where the popout genuinely is the only
    // thing the user should be typing at. `keyboardFocus` defaults to true, so
    // granting Exclusive on it alone would give a tray or calendar popout the
    // keyboard away from the focused application. OnDemand gives those focus
    // when clicked and leaves it alone otherwise.
    if (!request.keyboardFocus) {
        role.keyboard = PhosphorLayer::KeyboardInteractivity::None;
    } else if (request.exclusive == PhosphorPopout::ExclusiveMode::Modal) {
        role.keyboard = PhosphorLayer::KeyboardInteractivity::Exclusive;
    } else {
        role.keyboard = PhosphorLayer::KeyboardInteractivity::OnDemand;
    }
    role.scopePrefix = QStringLiteral("phosphor-shell-popout");

    PhosphorLayer::SurfaceConfig cfg;
    cfg.role = role;
    cfg.screen = screen;
    cfg.initialSize = screen->size();
    // Per-popout debug name so phosphorlayer's state-transition logs can tell
    // concurrent surfaces apart.
    cfg.debugName = QStringLiteral("phosphor-shell-popout:%1").arg(request.popoutId);
    // Share the shell's engine rather than letting the Surface build a
    // private one it would never use on the contentItem path.
    cfg.sharedEngine = m_engine;
    cfg.contentItem = std::unique_ptr<QQuickItem>(hostItem);

    // Explicit parent: create(cfg, nullptr) would parent the Surface to the
    // FACTORY, which outlives this transport and would leave surfaces behind
    // after drain().
    auto* surface = m_factory->create(std::move(cfg), this);
    if (!surface) {
        qCWarning(lcPopoutTransport) << "refusing" << request.popoutId << "— surface creation failed";
        // cfg.contentItem's destructor already deleted the host, and the
        // content goes with it because it is QObject-parented to the host
        // above. Nothing to clean up here.
        return {};
    }

    const QString handle = kLayerHandlePrefix + QString::number(++m_counter);
    // String-based connect: `dismissed` is declared in QML and has no
    // compile-time member pointer. Host and transport share the GUI thread, so
    // the default AutoConnection is already direct and the contract's
    // same-thread delivery holds without pinning it.
    // Checked like every other host interaction in this function, and this is
    // the worst one to lose: with no dismissal reaching us the entry never
    // leaves m_entries, the controller keeps believing the popout is open,
    // and the surface is stuck on screen for the process lifetime.
    if (!QObject::connect(hostItem, SIGNAL(dismissed()), this, SLOT(onHostDismissed()))) {
        qCWarning(lcPopoutTransport) << "refusing" << request.popoutId
                                     << "— PopoutHost has no dismissed() signal to connect";
        hostItem->deleteLater();
        return {};
    }

    // A Surface that fails or loses its output does not tear itself down;
    // Surface's own docs put respawn-or-destroy on the consumer. Without these
    // the popout would either be invisible (Failed) or pinned to a dead output
    // (screenLost) while the controller still believed it was open, and the
    // next toggle would route to close() instead of open(). Both signals are
    // documented as safe to destroy the Surface from.
    QObject::connect(surface, &PhosphorLayer::Surface::failed, this, [this, handle](const QString& reason) {
        onSurfaceGone(handle, reason);
    });
    QObject::connect(surface, &PhosphorLayer::Surface::screenLost, this, [this, handle] {
        onSurfaceGone(handle, QStringLiteral("its output was removed"));
    });

    m_entries.insert(handle, Entry{surface, hostItem, false, request.popoutId});

    surface->show();
    // Only now does the open animation run: PopoutHost keys its entrance off
    // `open`, and setting it before the surface was mapped would play the
    // transition into a surface nobody can see yet.
    if (!hostItem->setProperty("open", true)) {
        qCWarning(lcPopoutTransport) << "popout" << request.popoutId
                                     << "— PopoutHost rejected the open write; it will never appear";
    }
    return handle;
}

void LayerPopoutTransport::closeSurface(const QString& handle)
{
    // Idempotent by contract: an unknown or already-closed handle is a no-op.
    auto it = m_entries.find(handle);
    if (it == m_entries.end()) {
        return;
    }
    if (it->closing) {
        return;
    }
    it->closing = true;

    if (!it->hostItem) {
        // The host is already gone (surface failed, screen lost). Nothing to
        // animate; tear the entry down now.
        const Entry entry = *it;
        m_entries.erase(it);
        destroyEntry(handle, entry);
        return;
    }
    // Drive the same close animation a self-dismissal would. The host emits
    // `dismissed` when it settles, onHostDismissed does the teardown, and
    // the `closing` flag keeps the controller from being told about a close
    // it asked for. Deleting synchronously here would instead re-enter this
    // function through PopoutHost's destruction-time `dismissed`.
    //
    // A rejected write means the close animation will never run and
    // `dismissed` will never fire, so the entry would sit latched at
    // closing=true forever and every later closeSurface would no-op at the
    // early-out above: the popout becomes permanently stuck open. Mirror
    // the null-host branch instead and tear the entry down directly.
    if (!it->hostItem->setProperty("open", false)) {
        qCWarning(lcPopoutTransport) << "popout" << handle
                                     << "— PopoutHost rejected the open=false write; tearing down without animation";
        const Entry entry = *it;
        m_entries.erase(it);
        destroyEntry(handle, entry);
    }
}

void LayerPopoutTransport::setSurfaceDismissedCallback(std::function<void(const QString&)> callback)
{
    // Replaces any prior callback, including with an empty function — that
    // is how the controller detaches on destruction.
    m_dismissed = std::move(callback);
}

void LayerPopoutTransport::onHostDismissed()
{
    auto* host = sender();
    if (!host) {
        return;
    }
    // Pointer identity, not a property read. PopoutHost emits `dismissed` from
    // Component.onDestruction and documents that handlers must not dereference
    // the host, so recovering the handle by comparing pointers is the only
    // form that stays safe if that emission ever reaches us.
    const QString handle = handleForHost(host);
    if (handle.isEmpty()) {
        return;
    }
    auto it = m_entries.find(handle);
    if (it == m_entries.end()) {
        return;
    }
    const Entry entry = *it;
    // Erase BEFORE tearing down, so anything re-entrant finds no entry.
    m_entries.erase(it);
    destroyEntry(handle, entry);

    if (!entry.closing && m_dismissed) {
        m_dismissed(handle);
    }
}

QString LayerPopoutTransport::handleForHost(const QObject* host) const
{
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        if (it.value().hostItem == host) {
            return it.key();
        }
    }
    return {};
}

void LayerPopoutTransport::onSurfaceGone(const QString& handle, const QString& reason)
{
    auto it = m_entries.find(handle);
    if (it == m_entries.end()) {
        return;
    }
    qCWarning(lcPopoutTransport) << "popout" << handle << "torn down:" << reason;

    const Entry entry = *it;
    m_entries.erase(it);
    destroyEntry(handle, entry);

    // This IS a self-dismissal: the controller asked for a popout it no longer
    // has, and without the callback its arbitration table would keep the row
    // and report the popout as open forever. Suppressed only when we were
    // already closing the surface ourselves.
    if (!entry.closing && m_dismissed) {
        m_dismissed(handle);
    }
}

void LayerPopoutTransport::destroyEntry(const QString& handle, Entry entry)
{
    qCDebug(lcPopoutTransport) << "tearing down popout" << handle;
    if (entry.hostItem) {
        // Disconnect first: destroying the surface destroys its window,
        // which destroys the host, whose Component.onDestruction emits
        // `dismissed` synchronously from a half-destroyed object.
        QObject::disconnect(entry.hostItem, nullptr, this, nullptr);
    }
    if (entry.surface) {
        // Skip the hide for a Failed surface: requestHide refuses in that
        // state and logs a warning, and this path is reached FROM the failure.
        // The surface's destructor unmaps and deletes the window regardless,
        // so the hide is only ever a courtesy.
        if (entry.surface->state() != PhosphorLayer::Surface::State::Failed) {
            entry.surface->hide();
        }
        // deleteLater, not delete: this can run from inside the host's own
        // dismissed emission, and the surface owns the window that owns the
        // host that is emitting.
        entry.surface->deleteLater();
    }
    // The host and the content are NOT deleted here, and must not be: the
    // Surface owns the wrapper window, the window owns the host, and the host
    // owns the content by QObject parent (set in openSurface). Deleting any of
    // them separately is a double free. The request's QQmlComponent belongs to
    // the caller and is never ours.
}

} // namespace PhosphorShellApp
