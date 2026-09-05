// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PanePopoutTransport.h"

#include "ControlCenterController.h"

#include <PhosphorPopout/PopoutRequest.h>
#include <PhosphorShell/FloatingWindow.h>

#include <QLoggingCategory>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QRect>
#include <QScreen>
#include <QTimer>

#include <utility>

namespace {
Q_LOGGING_CATEGORY(lcPaneTransport, "phosphorshell.popout.pane")

// Handle prefix, disjoint from the layer ("popout-") and socket ("socket-")
// transports': RoutingPopoutTransport keys close-routing on the handle.
constexpr QLatin1String kPaneHandlePrefix("pane-");
constexpr QLatin1String kPaneAppIdPrefix("org.phosphor.shell.pane.");
// A pane that is not placed by an engine is not a pane. PlacementMapScreen
// reports -1 for "no daemon / screen not listed"; 0..2 are the modes.
constexpr int kNoMode = -1;
} // namespace

namespace PhosphorShellApp {

PanePopoutTransport::PanePopoutTransport(ControlCenterController* controller,
                                         PhosphorPopout::IPopoutTransport* fallback, QObject* parent)
    : QObject(parent)
    , m_controller(controller)
    , m_fallback(fallback)
    , m_resolveScreenName([this](QScreen* requested) {
        const QScreen* const target =
            requested ? requested : (m_controller ? m_controller->screenOf(nullptr) : nullptr);
        return target ? target->name() : QString();
    })
{
    // The fallback's own self-dismissals (its output going away) surface
    // through us: forget the handle and pass it on.
    if (m_fallback) {
        m_fallback->setSurfaceDismissedCallback([this](const QString& handle) {
            m_fallbackHandles.remove(handle);
            if (m_dismissed) {
                m_dismissed(handle);
            }
        });
    }
}

PanePopoutTransport::~PanePopoutTransport()
{
    if (m_fallback) {
        m_fallback->setSurfaceDismissedCallback({});
    }
    if (!m_entries.isEmpty()) {
        qCWarning(lcPaneTransport) << "destroyed with" << m_entries.size()
                                   << "live pane(s); drain() should have run first";
        drain();
    }
}

QString PanePopoutTransport::appIdFor(const QString& popoutId)
{
    return kPaneAppIdPrefix + popoutId;
}

void PanePopoutTransport::setEngine(QQmlEngine* engine)
{
    m_engine = engine;
}

void PanePopoutTransport::setScreenNameResolver(ScreenNameResolver resolver)
{
    m_resolveScreenName = std::move(resolver);
}

void PanePopoutTransport::setPaneSize(int width, int height)
{
    m_paneWidth = qMax(1, width);
    m_paneHeight = qMax(1, height);
}

void PanePopoutTransport::drain()
{
    const auto entries = std::exchange(m_entries, {});
    for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
        destroyEntry(it.key(), it.value());
    }
    publishOpenState({});
}

PhosphorShell::FloatingWindow* PanePopoutTransport::windowFor(const QString& handle) const
{
    const auto it = m_entries.constFind(handle);
    if (it == m_entries.cend() || it->closing) {
        return nullptr;
    }
    return it->window.data();
}

void PanePopoutTransport::publishOpenState(const QString& screenName)
{
    if (!m_controller) {
        return;
    }
    // External FIRST on open, so a screen write never lands while
    // `paneExternal` still says false (the bar would flash its inline pane
    // for a frame); on close the order is harmless either way.
    m_controller->setPaneExternal(!screenName.isEmpty());
    m_controller->setOpenScreen(screenName);
}

QString PanePopoutTransport::openSurface(const PhosphorPopout::PopoutRequest& request)
{
    const bool fromRequest = request.targetScreen != nullptr;
    const QString screenName = m_resolveScreenName ? m_resolveScreenName(request.targetScreen) : QString();
    const int mode = (m_controller && !screenName.isEmpty()) ? m_controller->modeForScreen(screenName) : kNoMode;

    // Everything that sends the open to the floating fallback is decided up
    // front, so a refused pane never half-creates a window.
    QString fallbackReason;
    if (mode == kNoMode) {
        fallbackReason = QStringLiteral("no placement engine on %1")
                             .arg(screenName.isEmpty() ? QStringLiteral("(unnamed output)") : screenName);
    } else if (!m_engine) {
        fallbackReason = QStringLiteral("no QML engine yet");
    } else if (!request.content) {
        fallbackReason = QStringLiteral("request carries no content component");
    }
    if (!fallbackReason.isEmpty()) {
        return openFallback(request, fallbackReason);
    }

    // Retire a pane of the same id that is still releasing, so a fast
    // re-toggle does not stack a second toplevel on the first.
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (it->closing && it->popoutId == request.popoutId) {
            const QString stale = it.key();
            const Entry entry = it.value();
            it = m_entries.erase(it);
            qCDebug(lcPaneTransport) << "retiring still-releasing pane" << stale << "before reopening"
                                     << request.popoutId;
            destroyEntry(stale, entry);
        } else {
            ++it;
        }
    }

    QQmlComponent* component = request.content;
    QObject* contentObject = component->beginCreate(m_engine->rootContext());
    auto* contentItem = qobject_cast<QQuickItem*>(contentObject);
    if (!contentItem) {
        component->completeCreate();
        if (contentObject) {
            contentObject->deleteLater();
        }
        return openFallback(request, QStringLiteral("content is not a QQuickItem: ") + component->errorString());
    }
    if (!request.props.isEmpty()) {
        component->setInitialProperties(contentItem, request.props);
    }
    component->completeCreate();

    QQmlComponent hostComponent(m_engine, QStringLiteral("Phosphor.Popout"), QStringLiteral("PaneHost"));
    QObject* hostObject = hostComponent.isError() ? nullptr : hostComponent.beginCreate(m_engine->rootContext());
    auto* hostItem = qobject_cast<QQuickItem*>(hostObject);
    if (!hostItem) {
        if (hostObject) {
            hostComponent.completeCreate();
            hostObject->deleteLater();
        }
        contentItem->deleteLater();
        return openFallback(request, QStringLiteral("PaneHost failed to build: ") + hostComponent.errorString());
    }
    if (!hostItem->setProperty("contentItem", QVariant::fromValue(contentItem))) {
        qCWarning(lcPaneTransport) << request.popoutId << "— PaneHost rejected the contentItem write";
    }
    contentItem->setParent(hostItem);
    QScreen* screen =
        request.targetScreen ? request.targetScreen : (m_controller ? m_controller->screenOf(nullptr) : nullptr);
    if (screen) {
        hostItem->setProperty("railWidth", screen->geometry().width());
    }
    hostComponent.completeCreate();

    const QString handle = kPaneHandlePrefix + QString::number(++m_counter);
    if (!QObject::connect(hostItem, SIGNAL(dismissed()), this, SLOT(onHostDismissed()))
        || !QObject::connect(hostItem, SIGNAL(released()), this, SLOT(onHostReleased()))) {
        hostItem->deleteLater();
        return openFallback(request, QStringLiteral("PaneHost lacks its dismissed/released signals"));
    }

    // The toplevel. Parented to the transport (Qt ownership) and torn down
    // through deleteLater, which takes the host and content with it. The
    // pane's size is also its minimum, which is how a tiling or scrolling
    // engine learns the smallest slot it may hand it.
    auto* window = new PhosphorShell::FloatingWindow();
    window->setParent(this);
    window->setTitle(request.popoutId);
    window->setAppId(appIdFor(request.popoutId));
    // A pane is a tile, not a titled window: no server-side decoration.
    window->setFrameless(true);
    window->setWindowWidth(m_paneWidth);
    window->setWindowHeight(m_paneHeight);
    window->setMinimumWidth(m_paneWidth);
    window->setMinimumHeight(m_paneHeight);
    hostItem->setParentItem(window);
    // The compositor closing the toplevel (the engine's close verb, a
    // window-close shortcut) is a self-dismissal with no release to run:
    // the surface is already gone.
    QObject::connect(window, &PhosphorShell::FloatingWindow::windowClosedByCompositor, this, [this, handle] {
        auto it = m_entries.find(handle);
        if (it == m_entries.end()) {
            return;
        }
        const bool wasClosing = it->closing;
        const Entry entry = it.value();
        m_entries.erase(it);
        qCDebug(lcPaneTransport) << "pane" << handle << "was closed by the compositor";
        publishOpenState({});
        destroyEntry(handle, entry);
        if (!wasClosing) {
            reportDismissed(handle);
        }
    });
    // The bar locates the pane on the placement map and reports its frame;
    // the pane's top band samples the rail over that x-range.
    if (m_controller) {
        QPointer<QQuickItem> guardedHost(hostItem);
        const auto feedRect = [guardedHost, this] {
            if (!guardedHost || !m_controller) {
                return;
            }
            const QRect rect = m_controller->paneRect();
            if (rect.width() > 0) {
                guardedHost->setProperty("railOffset", rect.x());
            }
        };
        QObject::connect(m_controller, &ControlCenterController::paneRectChanged, hostItem, feedRect);
        feedRect();
    }

    qCDebug(lcPaneTransport) << "opening" << request.popoutId << "as toplevel" << appIdFor(request.popoutId) << "for"
                             << screenName << (fromRequest ? "(from request)" : "(primary fallback)") << "mode" << mode;
    window->setWindowVisible(true);
    m_entries.insert(handle, Entry{window, hostItem, false, request.popoutId, screenName});
    publishOpenState(screenName);
    if (!hostItem->setProperty("open", true)) {
        qCWarning(lcPaneTransport) << request.popoutId << "— PaneHost rejected the open write; it will never appear";
    }
    return handle;
}

QString PanePopoutTransport::openFallback(const PhosphorPopout::PopoutRequest& request, const QString& reason)
{
    if (!m_fallback) {
        qCWarning(lcPaneTransport) << "refusing" << request.popoutId << "—" << reason << "and no floating fallback";
        return {};
    }
    qCInfo(lcPaneTransport) << request.popoutId << "goes to the floating fallback:" << reason;
    const QString handle = m_fallback->openSurface(request);
    if (!handle.isEmpty()) {
        m_fallbackHandles.insert(handle);
    }
    return handle;
}

void PanePopoutTransport::closeSurface(const QString& handle)
{
    if (m_fallbackHandles.remove(handle)) {
        if (m_fallback) {
            m_fallback->closeSurface(handle);
        }
        return;
    }
    auto it = m_entries.find(handle);
    if (it == m_entries.end() || it->closing) {
        return;
    }
    beginRelease(it);
}

void PanePopoutTransport::beginRelease(QHash<QString, Entry>::iterator it)
{
    it->closing = true;
    // The bar's tether retracts as soon as the close starts, before the
    // window unmaps (A2 §4.4, close column).
    publishOpenState({});
    if (!it->host || !it->host->setProperty("open", false)) {
        const QString handle = it.key();
        const Entry entry = it.value();
        m_entries.erase(it);
        destroyEntry(handle, entry);
    }
}

void PanePopoutTransport::setSurfaceDismissedCallback(std::function<void(const QString&)> callback)
{
    m_dismissed = std::move(callback);
}

void PanePopoutTransport::onHostDismissed()
{
    // Escape inside the pane: the same release as a controller close, but
    // reported upward since the controller did not ask for it.
    const QString handle = handleForHost(sender());
    auto it = m_entries.find(handle);
    if (it == m_entries.end() || it->closing) {
        return;
    }
    qCDebug(lcPaneTransport) << "pane" << handle << "dismissed itself";
    beginRelease(it);
    reportDismissed(handle);
}

void PanePopoutTransport::onHostReleased()
{
    // The release choreography finished: unmap now.
    const QString handle = handleForHost(sender());
    auto it = m_entries.find(handle);
    if (it == m_entries.end() || !it->closing) {
        return;
    }
    const Entry entry = it.value();
    m_entries.erase(it);
    destroyEntry(handle, entry);
}

void PanePopoutTransport::reportDismissed(const QString& handle)
{
    // Deferred: the contract forbids firing from inside open/close, and a
    // compositor close arrives from inside a QWindow event.
    QTimer::singleShot(0, this, [this, handle] {
        if (m_dismissed) {
            m_dismissed(handle);
        }
    });
}

QString PanePopoutTransport::handleForHost(const QObject* host) const
{
    if (!host) {
        return {};
    }
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        if (it.value().host == host) {
            return it.key();
        }
    }
    return {};
}

void PanePopoutTransport::destroyEntry(const QString& handle, const Entry& entry)
{
    qCDebug(lcPaneTransport) << "tearing down pane" << handle;
    if (entry.host) {
        QObject::disconnect(entry.host, nullptr, this, nullptr);
    }
    if (entry.window) {
        QObject::disconnect(entry.window, nullptr, this, nullptr);
        entry.window->setWindowVisible(false);
        // The host and its content live in the window's scene; the
        // FloatingWindow destructor takes the QQuickWindow with them.
        entry.window->deleteLater();
    } else if (entry.host) {
        entry.host->deleteLater();
    }
}

} // namespace PhosphorShellApp
