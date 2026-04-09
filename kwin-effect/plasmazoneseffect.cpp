// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"

#include <algorithm>
#include <memory>
#include <QBuffer>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QKeyEvent>
#include <QLoggingCategory>
#include <QScreen>
#include <QtMath>
#include <QPixmap>
#include <QPointer>
#include <window.h>
#include <workspace.h>
#include <core/output.h> // For Output::name() for multi-monitor support
#include <scene/windowitem.h>
#include <scene/surfaceitem.h>
#include <scene/outlinedborderitem.h>
#include <scene/borderoutline.h>

#include "autotilehandler.h"
#include "screenchangehandler.h"
#include "snapassisthandler.h"
#include "navigationhandler.h"
#include "windowanimator.h"
#include "dragtracker.h"
#include "dbus_constants.h"
// QGuiApplication::queryKeyboardModifiers() doesn't work in KWin effects on Wayland
// because the effect runs inside the compositor process. We use mouseChanged instead.

namespace PlasmaZones {

Q_LOGGING_CATEGORY(lcEffect, "plasmazones.effect", QtInfoMsg)

namespace {
// Duplicated from daemon's configkeys.h — effect cannot include daemon headers
constexpr QLatin1String TriggerModifierField("modifier");
constexpr QLatin1String TriggerMouseButtonField("mouseButton");
} // namespace

// NavigateDirectivePrefix moved to navigationhandler.cpp to avoid redefinition

// ═══════════════════════════════════════════════════════════════════════════════
// D-Bus helpers (all async — no QDBusInterface to avoid synchronous introspection)
// ═══════════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════════
// Helper Method Implementations
// ═══════════════════════════════════════════════════════════════════════════════

void PlasmaZonesEffect::fireAndForgetDBusCall(const QString& interface, const QString& method, const QVariantList& args,
                                              const QString& logContext)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(DBus::ServiceName, DBus::ObjectPath, interface, method);
    for (const QVariant& arg : args) {
        msg << arg;
    }
    QDBusPendingCall pending = QDBusConnection::sessionBus().asyncCall(msg);
    auto* watcher = new QDBusPendingCallWatcher(pending, this);
    const QString ctx = logContext.isEmpty() ? method : logContext;
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [ctx](QDBusPendingCallWatcher* w) {
        if (w->isError()) {
            qCWarning(lcEffect) << ctx << "D-Bus call failed:" << w->error().message();
        }
        w->deleteLater();
    });
}

QString PlasmaZonesEffect::iconToDataUrl(const QIcon& icon, int size)
{
    if (icon.isNull()) {
        return QString();
    }
    QPixmap pix = icon.pixmap(size, size);
    if (pix.isNull()) {
        return QString();
    }
    QByteArray ba;
    QBuffer buffer(&ba);
    buffer.open(QIODevice::WriteOnly);
    if (!pix.toImage().save(&buffer, "PNG")) {
        return QString();
    }
    return QStringLiteral("data:image/png;base64,") + QString::fromUtf8(ba.toBase64());
}

QRect PlasmaZonesEffect::parseZoneGeometry(const QString& json) const
{
    if (json.isEmpty()) {
        return QRect();
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcEffect) << "Failed to parse zone geometry:" << parseError.errorString();
        return QRect();
    }

    QJsonObject obj = doc.object();
    return QRect(obj[QLatin1String("x")].toInt(), obj[QLatin1String("y")].toInt(), obj[QLatin1String("width")].toInt(),
                 obj[QLatin1String("height")].toInt());
}

void PlasmaZonesEffect::ensurePreSnapGeometryStored(KWin::EffectWindow* w, const QString& windowId,
                                                    const QRectF& preCapturedGeometry)
{
    if (!w || windowId.isEmpty()) {
        return;
    }

    if (!isDaemonReady("ensure pre-snap geometry")) {
        return;
    }

    QPointer<KWin::EffectWindow> safeWindow = w;
    QString capturedWindowId = windowId;
    QRectF capturedGeom = preCapturedGeometry;

    QDBusPendingCall pendingCall =
        asyncMethodCall(DBus::Interface::WindowTracking, QStringLiteral("hasPreTileGeometry"), {windowId});
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, safeWindow, capturedWindowId, capturedGeom](QDBusPendingCallWatcher* watcher) {
                watcher->deleteLater();

                QDBusPendingReply<bool> reply = *watcher;
                bool hasGeometry = reply.isValid() && reply.value();

                if (!hasGeometry && m_daemonServiceRegistered) {
                    // Use pre-captured geometry if provided, otherwise read from window
                    QRectF geom =
                        capturedGeom.isValid() ? capturedGeom : (safeWindow ? safeWindow->frameGeometry() : QRectF());
                    if (geom.width() > 0 && geom.height() > 0) {
                        // Use virtual-screen-aware ID when available.
                        // getWindowScreenId() falls back to the physical ID when
                        // virtual screen defs haven't loaded yet, so it is safe
                        // to call unconditionally.  Using it here ensures the
                        // stored screen ID always matches the ID used by later
                        // lookups (which also call getWindowScreenId).
                        QString screenId;
                        if (safeWindow) {
                            screenId = getWindowScreenId(safeWindow);
                        }
                        fireAndForgetDBusCall(DBus::Interface::WindowTracking, QStringLiteral("storePreTileGeometry"),
                                              {capturedWindowId, static_cast<int>(geom.x()), static_cast<int>(geom.y()),
                                               static_cast<int>(geom.width()), static_cast<int>(geom.height()),
                                               screenId, false},
                                              QStringLiteral("storePreTileGeometry"));
                        qCInfo(lcEffect) << "Stored pre-tile geometry for window" << capturedWindowId;
                    }
                }
            });
}

QHash<QString, KWin::EffectWindow*> PlasmaZonesEffect::buildWindowMap(bool filterHandleable) const
{
    const auto windows = KWin::effects->stackingOrder();
    QHash<QString, KWin::EffectWindow*> windowMap;
    windowMap.reserve(windows.size());
    for (KWin::EffectWindow* w : windows) {
        if (w && (!filterHandleable || shouldHandleWindow(w))) {
            windowMap[getWindowId(w)] = w;
        }
    }
    return windowMap;
}

KWin::EffectWindow* PlasmaZonesEffect::getValidActiveWindowOrFail(const QString& action)
{
    KWin::EffectWindow* activeWindow = getActiveWindow();
    if (!activeWindow || !shouldHandleWindow(activeWindow)) {
        qCDebug(lcEffect) << "No valid active window for" << action;
        emitNavigationFeedback(false, action, QStringLiteral("no_window"));
        return nullptr;
    }
    return activeWindow;
}

bool PlasmaZonesEffect::isWindowFloating(const QString& windowId) const
{
    return m_navigationHandler->isWindowFloating(windowId);
}

PlasmaZonesEffect::PlasmaZonesEffect()
    : Effect()
    , m_autotileHandler(std::make_unique<AutotileHandler>(this))
    , m_navigationHandler(std::make_unique<NavigationHandler>(this))
    , m_screenChangeHandler(std::make_unique<ScreenChangeHandler>(this))
    , m_snapAssistHandler(std::make_unique<SnapAssistHandler>(this))
    , m_windowAnimator(std::make_unique<WindowAnimator>(this))
    , m_dragTracker(std::make_unique<DragTracker>(this))
{
    // Connect DragTracker signals
    //
    // Performance optimization: keyboard grab and D-Bus dragMoved calls are deferred
    // until an activation trigger is detected. This eliminates 60Hz D-Bus traffic and
    // keyboard grab/ungrab overhead for non-zone window drags (discussion #167).
    connect(m_dragTracker.get(), &DragTracker::dragStarted, this,
            [this](KWin::EffectWindow* w, const QString& windowId, const QRectF& geometry) {
                qCDebug(lcEffect) << "Window move started -" << w->windowClass()
                                  << "current modifiers:" << static_cast<int>(m_currentModifiers);
                // On autotile screens, don't show manual zone overlay or grab keyboard.
                // The drag proceeds freely; floatWindow is called on drag end.
                // Capture this decision so dragStopped uses the same state — prevents
                // a race where m_autotileScreens changes mid-drag (async D-Bus signal)
                // and leaves the popup visible with no snap.
                if (m_autotileHandler->isAutotileScreen(getWindowScreenId(w))) {
                    m_dragBypassedForAutotile = true;
                    m_dragBypassScreenId = getWindowScreenId(w);
                    return;
                }
                m_dragBypassedForAutotile = false;
                m_dragActivationDetected = false;
                m_dragStartedSent = false;
                m_pendingDragWindowId = windowId;
                m_pendingDragGeometry = geometry;
                m_snapDragStartScreenId = getWindowScreenId(w);

                // Check if zones are needed right now. If so, send dragStarted
                // immediately and grab keyboard. Otherwise defer until activation
                // is detected mid-drag (or skip entirely if user never activates).
                //
                // When triggers haven't loaded yet (!m_triggersLoaded), stay
                // permissive — send immediately to avoid masking trigger issues (#175).
                if (detectActivationAndGrab() || m_cachedZoneSelectorEnabled || !m_triggersLoaded) {
                    sendDeferredDragStarted();
                }
                // Grab keyboard to intercept Escape before KWin's MoveResizeFilter.
                // Without this, Escape cancels the interactive move AND the overlay.
                // With the grab, Escape only dismisses the overlay while the drag continues.
                if (!m_keyboardGrabbed) {
                    KWin::effects->grabKeyboard(this);
                    m_keyboardGrabbed = true;
                }
            });
    connect(m_dragTracker.get(), &DragTracker::dragMoved, this,
            [this](const QString& windowId, const QPointF& cursorPos) {
                // Cross-VS drag mode transitions: when the cursor crosses between
                // autotile and snap screens mid-drag, switch the drag handling mode
                // so zone overlay and snap activation match the screen under the cursor.
                if (!m_virtualScreenDefs.isEmpty()) {
                    QPoint cursorPt(qRound(cursorPos.x()), qRound(cursorPos.y()));
                    const KWin::LogicalOutput* cursorOutput = nullptr;
                    for (const auto* output : KWin::effects->screens()) {
                        if (output->geometry().contains(cursorPt)) {
                            cursorOutput = output;
                            break;
                        }
                    }
                    if (cursorOutput) {
                        QString cursorScreenId = resolveEffectiveScreenId(cursorPt, cursorOutput);
                        bool cursorOnAutotile = m_autotileHandler->isAutotileScreen(cursorScreenId);

                        if (m_dragBypassedForAutotile && !cursorOnAutotile) {
                            // Autotile→snap: exit bypass mode and initialize snap-drag
                            // state as if the drag started on this snap screen.
                            m_dragBypassedForAutotile = false;
                            m_dragActivationDetected = false;
                            m_dragStartedSent = false;
                            m_pendingDragWindowId = windowId;
                            KWin::EffectWindow* dragW = m_dragTracker->draggedWindow();
                            m_pendingDragGeometry = dragW ? dragW->frameGeometry() : QRectF();
                            m_snapDragStartScreenId = cursorScreenId;
                            qCInfo(lcEffect) << "Drag crossed from autotile" << m_dragBypassScreenId << "to snap screen"
                                             << cursorScreenId << "- activating zones";
                            if (!m_keyboardGrabbed) {
                                KWin::effects->grabKeyboard(this);
                                m_keyboardGrabbed = true;
                            }
                        } else if (!m_dragBypassedForAutotile && cursorOnAutotile && m_dragStartedSent) {
                            // Snap→autotile: re-enter bypass mode. Cancel the active
                            // snap overlay so zones disappear while cursor is on the
                            // autotile screen. If the user drags back to snap, the
                            // autotile→snap path above re-initializes snap state.
                            callCancelSnap();
                            m_dragBypassedForAutotile = true;
                            m_dragBypassScreenId = cursorScreenId; // Track the autotile VS we entered
                            m_dragStartedSent = false;
                            m_pendingDragWindowId.clear();
                            m_pendingDragGeometry = QRectF();
                            m_snapDragStartScreenId.clear();
                            qCInfo(lcEffect)
                                << "Drag crossed from snap to autotile" << cursorScreenId << "- deactivating zones";
                        }
                    }
                }

                // In autotile bypass — skip snap zone processing
                if (m_dragBypassedForAutotile) {
                    return;
                }

                // Gate D-Bus calls: if no activation trigger is held, toggle mode is off,
                // and zone selector is disabled, skip the D-Bus call entirely. This
                // eliminates 60Hz D-Bus traffic during non-zone drags.
                //
                // When triggers haven't loaded yet, stay permissive (#175).
                if (!detectActivationAndGrab() && !m_cachedZoneSelectorEnabled && m_triggersLoaded) {
                    return;
                }
                // Ensure dragStarted was sent before any dragMoved
                sendDeferredDragStarted();
                callDragMoved(windowId, cursorPos, m_currentModifiers, static_cast<int>(m_currentMouseButtons));
            });
    connect(m_dragTracker.get(), &DragTracker::dragStopped, this,
            [this](KWin::EffectWindow* w, const QString& windowId, bool cancelled) {
                // Release keyboard grab before handling drag end
                if (m_keyboardGrabbed) {
                    KWin::effects->ungrabKeyboard();
                    m_keyboardGrabbed = false;
                }
                // Use the captured autotile state from drag start (not live m_autotileScreens)
                // to ensure consistent behavior even if autotile screens changed mid-drag.
                if (m_dragBypassedForAutotile) {
                    if (!cancelled) {
                        // Re-resolve the window's virtual screen at drop time.
                        // KWin's outputChanged only fires on physical monitor changes,
                        // so moving between virtual screens on the same monitor
                        // (e.g., A/vs:0 → A/vs:1) is invisible to the outputChanged
                        // handler. Detect the change here and trigger a transfer.
                        const QString dropScreenId = w ? getWindowScreenId(w) : m_dragBypassScreenId;
                        if (dropScreenId != m_dragBypassScreenId) {
                            qCInfo(lcEffect) << "Autotile drag: virtual screen changed" << m_dragBypassScreenId << "->"
                                             << dropScreenId;
                            // Preserve pre-autotile geometry across virtual screen transfer
                            // (mirrors handleWindowOutputChanged in AutotileHandler).
                            // Must happen BEFORE onWindowClosed which clears the source map.
                            const bool dropIsAutotile = m_autotileHandler->isAutotileScreen(dropScreenId);
                            if (dropIsAutotile) {
                                m_autotileHandler->transferPreAutotileGeometry(windowId, m_dragBypassScreenId,
                                                                               dropScreenId);
                            }

                            // Transfer: remove from old virtual screen, add to new one.
                            // handleWindowOutputChanged won't fire (same physical monitor),
                            // so manually perform the close/open cycle.
                            // Restore border and pre-autotile size BEFORE onWindowClosed
                            // clears the tiling/borderless tracking state.
                            if (!dropIsAutotile) {
                                m_autotileHandler->handleDragToFloat(w, windowId, m_dragBypassScreenId);
                            }

                            m_autotileHandler->onWindowClosed(windowId, m_dragBypassScreenId);
                            if (w && dropIsAutotile) {
                                m_autotileHandler->notifyWindowAdded(w);
                                // The window was floating (being dragged) — keep it floating
                                // on the destination VS. notifyWindowAdded tiles the window,
                                // so immediately restore its floating state and size.
                                m_autotileHandler->handleDragToFloat(w, windowId, dropScreenId);
                                m_dragFloatedWindowIds.insert(windowId);
                                fireAndForgetDBusCall(DBus::Interface::WindowTracking,
                                                      QStringLiteral("setWindowFloatingForScreen"),
                                                      {windowId, dropScreenId, true},
                                                      QStringLiteral("setWindowFloatingForScreen - cross-VS drag"));
                                qCInfo(lcEffect) << "Autotile cross-VS drag-to-float:" << windowId
                                                 << m_dragBypassScreenId << "->" << dropScreenId;
                            }
                            // Non-autotile drop: handleDragToFloat already restored
                            // border and size locally. onWindowClosed sent D-Bus
                            // windowClosed which removes daemon tracking — a subsequent
                            // setWindowFloatingForScreen would be a no-op (window not
                            // found in autotile engine). Don't insert into
                            // m_dragFloatedWindowIds either: the stale entry would
                            // incorrectly skip geometry restore if the window is later
                            // re-snapped on the drop screen and then float-toggled.
                        } else {
                            // Same virtual screen — normal drag-to-float behavior.
                            // Restore border and pre-autotile size synchronously (don't
                            // wait for the daemon's async windowFloatingChanged signal).
                            m_autotileHandler->handleDragToFloat(w, windowId, m_dragBypassScreenId);
                            // Mark as drag-floated so slotApplyGeometryRequested skips the
                            // daemon's pre-autotile geometry restore — the window should stay
                            // where the user dropped it, not snap back to its original position.
                            m_dragFloatedWindowIds.insert(windowId);
                            fireAndForgetDBusCall(
                                DBus::Interface::WindowTracking, QStringLiteral("setWindowFloatingForScreen"),
                                {windowId, m_dragBypassScreenId, true}, QStringLiteral("setWindowFloatingForScreen"));
                            qCInfo(lcEffect) << "Autotile drag-to-float:" << windowId;
                        }
                    }
                    m_snapDragStartScreenId.clear();
                    m_dragBypassedForAutotile = false;
                    m_dragBypassScreenId.clear();
                    return;
                }
                m_dragActivationDetected = false;

                // Virtual screen crossing for snap-mode: KWin's outputChanged
                // doesn't fire when moving between virtual screens on the same
                // physical monitor, and the frameGeometryChanged handler skips
                // during drag. Re-resolve now and notify the daemon if changed.
                if (!cancelled && w && !m_snapDragStartScreenId.isEmpty()) {
                    const QString dropScreenId = getWindowScreenId(w);
                    if (dropScreenId != m_snapDragStartScreenId && !dropScreenId.isEmpty()
                        && VirtualScreenId::samePhysical(dropScreenId, m_snapDragStartScreenId)
                        && !m_autotileHandler->isAutotileScreen(dropScreenId)) {
                        // Only send windowScreenChanged for snap-to-snap VS crossings.
                        // Snap-to-autotile crossings are handled by callDragStopped's callback
                        // which does notifyWindowAdded + setWindowFloatingForScreen.
                        qCInfo(lcEffect) << "Snap drag: virtual screen changed" << m_snapDragStartScreenId << "->"
                                         << dropScreenId;
                        fireAndForgetDBusCall(DBus::Interface::WindowTracking, QStringLiteral("windowScreenChanged"),
                                              {windowId, dropScreenId}, QStringLiteral("snap drag VS crossing"));
                    }
                }
                // Capture snap-mode start screen ID BEFORE clearing — callDragStopped's
                // async callback needs it for cross-VS autotile transfer detection.
                // m_dragBypassScreenId is only set for autotile-bypass drags, not snap drags,
                // so without this capture the cross-VS path in callDragStopped is dead code.
                const QString snapDragStartScreenId = m_snapDragStartScreenId;
                m_snapDragStartScreenId.clear();

                if (!m_dragStartedSent) {
                    // Drag ended without ever activating zones — no D-Bus state to clean up.
                    // BUT: if the window was snapped, notify the daemon so it can handle
                    // unsnap (restore original size, clear zone assignment, mark floating).
                    // Without this, dragging a snapped window without the activation trigger
                    // leaves stale zone state that persists across close/reopen.
                    if (!cancelled && !m_pendingDragWindowId.isEmpty()) {
                        fireAndForgetDBusCall(DBus::Interface::WindowTracking, QStringLiteral("notifyDragOutUnsnap"),
                                              {m_pendingDragWindowId}, QStringLiteral("notifyDragOutUnsnap"));
                    }
                    m_pendingDragWindowId.clear();
                    m_pendingDragGeometry = QRectF();
                    return;
                }
                m_dragStartedSent = false;
                m_pendingDragWindowId.clear();
                m_pendingDragGeometry = QRectF();

                if (cancelled) {
                    // Drag was cancelled externally (e.g. window went fullscreen).
                    // Tell the daemon to cancel rather than snap to the hovered zone.
                    callCancelSnap();
                } else {
                    callDragStopped(w, windowId, snapDragStartScreenId);
                }
            });

    // Connect to window lifecycle signals
    connect(KWin::effects, &KWin::EffectsHandler::windowAdded, this, &PlasmaZonesEffect::slotWindowAdded);
    connect(KWin::effects, &KWin::EffectsHandler::windowClosed, this, &PlasmaZonesEffect::slotWindowClosed);

    // Belt-and-suspenders: windowClosed removes animations, but if a deferred
    // timer re-adds one between windowClosed and windowDeleted, the Item tree
    // will be torn down while an animation entry still references the window.
    // Purge here to prevent SIGSEGV in animationBounds → expandedGeometry.
    // Also clean up caches that slotWindowClosed may have already cleared —
    // QHash::take/remove on missing keys is a no-op, so this is safe.
    connect(KWin::effects, &KWin::EffectsHandler::windowDeleted, this, [this](KWin::EffectWindow* w) {
        m_windowAnimator->removeAnimation(w);
        if (m_windowIdCache.contains(w)) {
            const QString cachedId = m_windowIdCache.take(w);
            m_windowIdReverse.remove(cachedId);
        }
        m_trackedScreenPerWindow.remove(w);
    });

    connect(KWin::effects, &KWin::EffectsHandler::windowActivated, this, &PlasmaZonesEffect::slotWindowActivated);

    // Update the daemon's primary screen override when KDE Display Settings change
    if (auto* ws = KWin::Workspace::self()) {
        connect(ws, &KWin::Workspace::outputOrderChanged, this, [this]() {
            auto* workspace = KWin::Workspace::self();
            if (workspace && m_daemonServiceRegistered) {
                const auto outputs = workspace->outputOrder();
                if (!outputs.isEmpty()) {
                    fireAndForgetDBusCall(DBus::Interface::Screen, QStringLiteral("setPrimaryScreenFromKWin"),
                                          {outputs.first()->name()}, QStringLiteral("setPrimaryScreenFromKWin"));
                }
            }
        });
    }

    // mouseChanged is the only reliable way to get modifier state in a KWin effect on Wayland;
    // QGuiApplication::queryKeyboardModifiers() doesn't work since effects run in the compositor.
    connect(KWin::effects, &KWin::EffectsHandler::mouseChanged, this, &PlasmaZonesEffect::slotMouseChanged);

    // Connect to screen geometry changes for keepWindowsInZonesOnResolutionChange feature
    // In KWin 6, use virtualScreenGeometryChanged (not per-screen signal)
    connect(KWin::effects, &KWin::EffectsHandler::virtualScreenGeometryChanged, m_screenChangeHandler.get(),
            &ScreenChangeHandler::slotScreenGeometryChanged);
    // Invalidate screen ID cache and refresh virtual screen definitions on screen changes
    // (connector names may be reassigned, physical screen geometry changes invalidate
    // virtual screen absolute geometry)
    connect(KWin::effects, &KWin::EffectsHandler::virtualScreenGeometryChanged, this, [this]() {
        m_screenIdCache.clear();
        m_lastEffectiveScreenId.clear();
    });

    // Connect to daemon's settingsChanged D-Bus signal
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Settings,
                                          QStringLiteral("settingsChanged"), this, SLOT(slotSettingsChanged()));
    qCInfo(lcEffect) << "Connected to daemon settingsChanged D-Bus signal";

    // Connect to virtual screen changes — daemon emits this when a physical screen's
    // virtual subdivisions are added, removed, or modified.
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Screen,
                                          QStringLiteral("virtualScreensChanged"), this,
                                          SLOT(onVirtualScreensChanged(QString)));

    // Connect to keyboard navigation D-Bus signals
    connectNavigationSignals();

    // Connect to autotile D-Bus signals
    m_autotileHandler->connectSignals();
    m_autotileHandler->loadSettings();

    // Verify daemon availability asynchronously to avoid blocking the compositor.
    // CRITICAL: Do NOT use synchronous isServiceRegistered() here. The daemon
    // registers its D-Bus service name in init() BEFORE start() runs heavy
    // initialization and BEFORE the event loop begins (main.cpp:88→94→102).
    // During that window, isServiceRegistered() returns true but the daemon
    // can't process messages. Any synchronous QDBusInterface creation would
    // trigger Introspect, blocking KWin for up to the D-Bus timeout (~25s).
    //
    // Instead, send an async Introspect — if the daemon responds, it's fully
    // operational and we trigger slotDaemonReady(). If it can't respond (still
    // initializing), the call times out harmlessly and we wait for the
    // daemonReady D-Bus signal instead.
    {
        QDBusMessage introspect = QDBusMessage::createMethodCall(DBus::ServiceName, DBus::ObjectPath,
                                                                 QStringLiteral("org.freedesktop.DBus.Introspectable"),
                                                                 QStringLiteral("Introspect"));
        auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(introspect, 3000), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            QDBusPendingReply<QString> reply = *w;
            if (reply.isValid() && !m_daemonServiceRegistered) {
                // Daemon responded — it's fully operational.
                // Trigger the same ready flow as the daemonReady signal.
                slotDaemonReady();
            }
        });
    }

    // Connect to daemon's daemonReady signal — emitted at the end of Daemon::start()
    // after all initialization is complete and the daemon can process D-Bus messages.
    // This is the safe point to set m_daemonServiceRegistered and create QDBusInterfaces.
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::LayoutManager,
                                          QStringLiteral("daemonReady"), this, SLOT(slotDaemonReady()));

    // Watch for daemon D-Bus service registration and unregistration.
    // After a daemon restart, m_lastCursorOutput is still valid in the effect
    // but the daemon's lastCursorScreenName/lastActiveScreenName are empty.
    // Without this, keyboard shortcuts (rotate, etc.) operate on all screens
    // because resolveShortcutScreen returns nullptr.
    //
    // On Wayland, this watcher uses D-Bus monitoring (not X11 selection),
    // which works reliably across both sessions.
    auto* serviceWatcher = new QDBusServiceWatcher(
        DBus::ServiceName, QDBusConnection::sessionBus(),
        QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this);
    connect(serviceWatcher, &QDBusServiceWatcher::serviceUnregistered, this, [this]() {
        qCInfo(lcEffect) << "Daemon service unregistered";
        m_daemonServiceRegistered = false;
        m_daemonReadyRestoresDone = false;
        m_daemonReadyWindowStateProcessed = false;
        m_snapRestoreCache.clear();

        // Restore borderless and monocle-maximized windows — daemon state is gone
        m_autotileHandler->restoreAllBorderless();
        m_autotileHandler->restoreAllMonocleMaximized();
        clearAllBorders();
    });
    connect(serviceWatcher, &QDBusServiceWatcher::serviceRegistered, this, [this]() {
        qCInfo(lcEffect) << "Daemon registered: waiting for daemonReady signal";

        // DO NOT set m_daemonServiceRegistered = true here.
        // The daemon registers its D-Bus service name in init(), BEFORE start()
        // runs heavy initialization and BEFORE the event loop begins. Keep the
        // flag false until the daemon's own daemonReady signal fires (end of
        // Daemon::start()), confirming it can handle D-Bus requests.

        // Reconnect daemonReady signal — Qt may cache the old daemon's unique bus
        // name in match rules, so refresh for the new daemon instance.
        // Disconnect first to prevent duplicate match rules (Qt doesn't deduplicate),
        // which would cause slotDaemonReady to fire twice on the same signal.
        QDBusConnection::sessionBus().disconnect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::LayoutManager,
                                                 QStringLiteral("daemonReady"), this, SLOT(slotDaemonReady()));
        QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::LayoutManager,
                                              QStringLiteral("daemonReady"), this, SLOT(slotDaemonReady()));
    });

    // NOTE: syncFloatingWindowsFromDaemon() and loadCachedSettings() are NOT
    // called here. m_daemonServiceRegistered is false at this point (set only by
    // slotDaemonReady), so any ensureInterface() call would bail out immediately.
    // All daemon state sync is deferred to slotDaemonReady().

    // Connect to existing windows
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        setupWindowConnections(w);
    }

    // The daemon disables KWin's Quick Tile via kwriteconfig6. We don't reserve electric borders
    // here because that would turn on the edge effect visually; the daemon's config approach
    // is the right way to prevent Quick Tile from activating.

    // Seed m_lastCursorOutput with the compositor's active screen. This ensures
    // the daemon has a valid cursor screen even if no mouse movement occurs after login.
    // slotMouseChanged will overwrite this as soon as the cursor moves.
    //
    // The actual D-Bus push to the daemon happens in slotDaemonReady(), which fires
    // either from the async Introspect callback above (daemon already running) or
    // from the daemonReady D-Bus signal (daemon starts later). We do NOT push here
    // to avoid synchronous QDBusInterface creation on the compositor thread.
    auto* initialScreen = KWin::effects->activeScreen();
    if (initialScreen) {
        m_lastCursorOutput = initialScreen->name();
    }

    qCInfo(lcEffect) << "initialized: C++ effect with D-Bus support and mouseChanged connection";
}

PlasmaZonesEffect::~PlasmaZonesEffect()
{
    // Restore borderless and monocle-maximized windows so they recover properly.
    // Guard against compositor teardown — effects may outlive the stacking order.
    if (KWin::effects) {
        m_autotileHandler->restoreAllBorderless();
        m_autotileHandler->restoreAllMonocleMaximized();
        clearAllBorders();
    }

    if (m_keyboardGrabbed) {
        KWin::effects->ungrabKeyboard();
        m_keyboardGrabbed = false;
    }
    m_screenChangeHandler->stop();
    // We no longer reserve/unreserve edges; the daemon disables KWin snap via config.
}

bool PlasmaZonesEffect::supported()
{
    // This effect is a compositor plugin that works in KWin on Wayland
    // Note: PlasmaZones daemon requires Wayland with layer-shell support
    return true;
}

bool PlasmaZonesEffect::enabledByDefault()
{
    return true;
}

void PlasmaZonesEffect::reconfigure(ReconfigureFlags flags)
{
    Q_UNUSED(flags)
    // Called when KWin wants effects to reload or when daemon notifies of settings change
    qCDebug(lcEffect) << "reconfigure() called";
}

bool PlasmaZonesEffect::isActive() const
{
    return m_dragTracker->isDragging() || m_windowAnimator->hasActiveAnimations();
}

void PlasmaZonesEffect::grabbedKeyboardEvent(QKeyEvent* e)
{
    if (e->type() == QEvent::KeyPress && e->key() == Qt::Key_Escape && m_dragTracker->isDragging()) {
        // The keyboard grab ensures this runs before KWin's MoveResizeFilter,
        // so Escape never reaches the interactive move handler. The daemon
        // hides the overlay and sets snapCancelled; the drag continues as
        // a plain window move without zone snapping.
        qCInfo(lcEffect) << "Drag escape: overlay hidden, drag continues";
        callCancelSnap();
    }
    // All other keys are silently consumed by the grab. Modifier state is
    // unaffected because mouseChanged reads xkb state directly.
}

void PlasmaZonesEffect::slotWindowAdded(KWin::EffectWindow* w)
{
    setupWindowConnections(w);
    updateWindowStickyState(w);

    // Sync floating state for this window from daemon
    // This ensures windows that were floating when closed remain floating when reopened
    // Use full windowId so daemon can do per-instance lookup with appId fallback
    QString windowId = getWindowId(w);
    m_navigationHandler->syncFloatingStateForWindow(windowId);

    bool onAutotileScreen = m_autotileHandler->isAutotileScreen(getWindowScreenId(w));

    // Check if this window is a candidate for snap restore
    // Use stricter filter - only normal application windows, NOT dialogs/utilities
    bool canSnapRestore =
        shouldHandleWindow(w) && isTileableWindow(w) && !w->isMinimized() && !hasOtherWindowOfClassWithDifferentPid(w);

    // Instant snap restore: if we have a cached zone geometry for this app,
    // teleport the window immediately — no D-Bus round-trip, no visible flash.
    // The async callResolveWindowRestore still runs to register the zone assignment
    // in the daemon; this just eliminates the visual lag.
    if (canSnapRestore && !m_snapRestoreCache.isEmpty()) {
        QString appId = extractAppId(windowId);
        auto cacheIt = m_snapRestoreCache.find(appId);
        if (cacheIt != m_snapRestoreCache.end()) {
            QRect cachedGeo = cacheIt.value();
            if (cachedGeo.isValid()) {
                qCInfo(lcEffect) << "Instant snap restore for" << appId << "to:" << cachedGeo;
                applySnapGeometry(w, cachedGeo, false, /*skipAnimation=*/true);
                m_snapRestoreCache.erase(cacheIt);
                // Re-evaluate screen after teleport — the window may have moved
                // off the autotile screen, so update for the autotile decision below.
                onAutotileScreen = m_autotileHandler->isAutotileScreen(getWindowScreenId(w));
            }
        }
    }

    if (onAutotileScreen && canSnapRestore) {
        // Window landed on an autotile screen, but may have a pending snap restore
        // to a non-autotile screen. KWin's session restore places windows at their
        // saved geometry, which may be a pre-snap floating position in the autotile
        // screen's area — even though the window was snapped in the snap screen
        // before logout. Try snap restore FIRST: if it moves the window off the
        // autotile screen, we avoid the autotile add→float→remove→resnap dance
        // that causes visible flickering and repeated resizing.
        QPointer<KWin::EffectWindow> safeW = w;
        callResolveWindowRestore(w, [this, safeW]() {
            if (!safeW || safeW->isDeleted()) {
                return;
            }
            // Snap restore either moved the window to a snap screen (no-op for
            // autotile) or didn't apply (window genuinely belongs on autotile).
            m_autotileHandler->notifyWindowAdded(safeW);
        });
        return;
    }

    // Standard path: notify autotile first, then try snap restore
    m_autotileHandler->notifyWindowAdded(w);

    if (!onAutotileScreen && canSnapRestore) {
        callResolveWindowRestore(w);
    }
}

void PlasmaZonesEffect::slotWindowClosed(KWin::EffectWindow* w)
{
    // Release keyboard grab if the dragged window was closed
    if (m_keyboardGrabbed && m_dragTracker->draggedWindow() == w) {
        KWin::effects->ungrabKeyboard();
        m_keyboardGrabbed = false;
    }

    // Delegate to helpers
    m_dragTracker->handleWindowClosed(w);

    // Clear floating state — floating is runtime-only and resets on window close.
    // The daemon clears its side in windowClosed().
    m_navigationHandler->setWindowFloating(getWindowId(w), false);

    m_windowAnimator->removeAnimation(w);

    const QString closedWindowId = getWindowId(w);
    const QString closedScreenId = getWindowScreenId(w);

    // Clean up snap-mode minimize tracking
    m_minimizeFloatedWindows.remove(closedWindowId);
    m_dragFloatedWindowIds.remove(closedWindowId);

    // Notify autotile handler for cleanup (tracking sets + autotile D-Bus)
    m_autotileHandler->onWindowClosed(closedWindowId, closedScreenId);

    // Remove the window's border item (parent WindowItem is being destroyed anyway,
    // but clean up our tracking hash to avoid stale entries).
    removeWindowBorder(closedWindowId);

    // Notify general daemon for cleanup
    notifyWindowClosed(w);

    // Clean up caches AFTER all consumers that call getWindowId(w).
    // The windowDeleted handler does final cleanup, but removing here
    // prevents re-insertion by any late calls.
    m_windowIdCache.remove(w);
    m_windowIdReverse.remove(closedWindowId);
    m_trackedScreenPerWindow.remove(w);
}

void PlasmaZonesEffect::slotWindowActivated(KWin::EffectWindow* w)
{
    // Filtering (e.g. shouldHandleWindow) is done inside notifyWindowActivated
    notifyWindowActivated(w);

    // Recreate all borders so the active window gets the active color
    // and inactive windows get the inactive color.  A full recreate is
    // used instead of in-place setOutline() because the latter may not
    // trigger a scene-graph repaint in all KWin versions.
    updateAllBorders();
}

void PlasmaZonesEffect::setupWindowConnections(KWin::EffectWindow* w)
{
    if (!w)
        return;

    connect(w, &KWin::EffectWindow::windowDesktopsChanged, this, [this](KWin::EffectWindow* window) {
        updateWindowStickyState(window);

        // When a window is moved to a different desktop (e.g., "Move to Desktop 2"),
        // treat it as removed from the current desktop's tiling. The normal desktop-
        // switch flow will pick it up when the user switches to the target desktop.
        if (window && !window->isOnCurrentDesktop() && !window->isOnAllDesktops()) {
            const QString windowId = getWindowId(window);
            const QString screenId = getWindowScreenId(window);
            if (m_autotileHandler->isAutotileScreen(screenId)) {
                // Save pre-autotile geometry before onWindowClosed clears it.
                // When the window is re-added on the target desktop, this preserved
                // geometry is used instead of the current (tiled) frame position.
                m_autotileHandler->savePreAutotileForDesktopMove(windowId, screenId);

                // Restore title bar before removing from tiling — onWindowClosed
                // only clears tracking, it doesn't call setNoBorder(false) since
                // it's also used for truly closing windows.
                if (m_autotileHandler->isBorderlessWindow(windowId)) {
                    KWin::Window* kw = window->window();
                    if (kw) {
                        kw->setNoBorder(false);
                    }
                }
                m_autotileHandler->onWindowClosed(windowId, screenId);
                removeWindowBorder(windowId);
                qCInfo(lcEffect) << "Window moved off current desktop, removed from autotile:" << windowId;
            }
        }
    });

    // Detect when a window moves between monitors (e.g., "Move to Screen Right").
    // KWin::Window::outputChanged fires once when the window's output property changes.
    // Transfer the window from the old screen's autotile state to the new screen's state,
    // and unsnap any snapped window that crosses screens.
    KWin::Window* kw = w->window();
    if (kw) {
        QPointer<KWin::EffectWindow> safeW = w;
        // Track the window's screen ID so we can detect cross-screen moves for snapping windows
        // (not tracked by the autotile handler's m_notifiedWindowScreens).
        m_trackedScreenPerWindow[w] = getWindowScreenId(w);
        connect(kw, &KWin::Window::outputChanged, this, [this, safeW]() {
            if (!safeW || safeW->isDeleted()) {
                return;
            }
            const QString newScreenId = getWindowScreenId(safeW);
            const QString oldScreenId = m_trackedScreenPerWindow.value(safeW);
            m_trackedScreenPerWindow[safeW] = newScreenId;

            // Delegate autotile handling (autotile→autotile, autotile→snapping, etc.)
            // This must run even during drag so the autotile engine removes the
            // window from the old screen's tiling state immediately.
            m_autotileHandler->handleWindowOutputChanged(safeW);

            // For snapping→snapping cross-screen moves: notify the daemon which
            // decides whether to unsnap based on its own state. If the daemon just
            // assigned this window to the new screen (restore/resnap/snap assist),
            // the stored screen matches and no unsnap occurs. If the user moved
            // the window via "Move to Screen" shortcut, the stored screen differs
            // and the daemon unsnaps.
            // Skip during drag: the drag system owns snap state transitions
            // (float, unsnap, size restore, pre-tile cleanup) and handles them
            // in dragStopped() with richer context.
            // Skip when the old screen disappeared (monitor standby/disconnect):
            // KWin reassigns orphaned windows to remaining outputs, firing
            // outputChanged even though the window didn't actually move. The
            // ScreenChangeHandler will resnap windows after the debounce settles.
            // Also skip during an active screen geometry change (debounce in flight).
            bool oldScreenStillConnected = false;
            for (const auto* output : KWin::effects->screens()) {
                if (outputScreenId(output) == oldScreenId) {
                    oldScreenStillConnected = true;
                    break;
                }
            }
            if (!oldScreenId.isEmpty() && oldScreenId != newScreenId
                && !m_autotileHandler->isAutotileScreen(oldScreenId)
                && !m_autotileHandler->isAutotileScreen(newScreenId) && !m_dragTracker->isDragging()
                && oldScreenStillConnected && !m_screenChangeHandler->isScreenChangeInProgress()) {
                const QString windowId = getWindowId(safeW);
                fireAndForgetDBusCall(DBus::Interface::WindowTracking, QStringLiteral("windowScreenChanged"),
                                      {windowId, newScreenId}, QStringLiteral("cross-screen move"));
            }
        });
        // Virtual screen boundary detection: KWin's outputChanged only fires when
        // the physical monitor changes. Moving a window between virtual screens on the
        // same physical monitor (e.g., A/vs:0 → A/vs:1) is invisible to outputChanged.
        // Detect these crossings via frameGeometryChanged, using the same trackedScreen
        // state as the outputChanged handler above.
        // (The autotile handler has its own detection in slotWindowFrameGeometryChanged;
        // this covers snapping-mode windows which autotile doesn't track.)
        //
        // VS crossing detection uses VirtualScreenId::isVirtualScreenCrossing()
        // (shared/virtualscreenid.h) — the same predicate used by autotilehandler/tiling.cpp.
        connect(safeW, &KWin::EffectWindow::windowFrameGeometryChanged, this, [this, safeW]() {
            if (!safeW || safeW->isDeleted() || m_virtualScreenDefs.isEmpty() || !m_virtualScreensReady) {
                return;
            }
            const QString newScreenId = getWindowScreenId(safeW);
            const QString oldScreenId = m_trackedScreenPerWindow.value(safeW);
            if (!VirtualScreenId::isVirtualScreenCrossing(oldScreenId, newScreenId)) {
                return;
            }
            m_trackedScreenPerWindow[safeW] = newScreenId;

            // Skip during drag — the drag system owns state transitions.
            // Autotile drag handles VS transfers in dragStopped (line 262-285).
            // Snapping drag handles cross-screen unsnap in dragStopped via daemon.
            if (m_dragTracker->isDragging()) {
                return;
            }

            // Skip VS detection for autotile-tracked windows — the autotile
            // handler's slotWindowFrameGeometryChanged owns VS crossing for
            // windows it already tracks (m_notifiedWindows). Only untracked
            // windows (snapping-mode entering an autotile VS) need delegation.
            const QString windowId = getWindowId(safeW);
            if (m_autotileHandler->isTrackedWindow(windowId)) {
                return;
            }

            // Delegate autotile handling for untracked cross-VS transitions
            // (snapping→autotile). The autotile handler's own detection only
            // covers windows it already tracks.
            m_autotileHandler->handleWindowOutputChanged(safeW);

            // For snapping→snapping cross-VS moves: notify the daemon
            if (!m_autotileHandler->isAutotileScreen(oldScreenId) && !m_autotileHandler->isAutotileScreen(newScreenId)
                && !m_screenChangeHandler->isScreenChangeInProgress()) {
                fireAndForgetDBusCall(DBus::Interface::WindowTracking, QStringLiteral("windowScreenChanged"),
                                      {windowId, newScreenId}, QStringLiteral("virtual screen crossing"));
            }
        });

        // Clean up the tracked screen entry when the window is destroyed
        connect(safeW, &QObject::destroyed, this, [this, safeW]() {
            m_trackedScreenPerWindow.remove(safeW);
        });
    }

    // Detect drag start/end via KWin's per-window signals instead of polling.
    // windowStartUserMovedResized fires once when an interactive move (or resize) begins;
    // windowFinishUserMovedResized fires once when it ends (button release, Escape, etc.).
    // This eliminates the poll timer that previously scanned the full stacking order at
    // 32ms intervals during drag — a significant source of compositor-thread overhead.
    //
    // NOTE: windowFrameGeometryChanged / windowStepUserMovedResized are intentionally NOT
    // connected for drag tracking. They fire on every pixel of movement, which would flood
    // D-Bus. Cursor position updates are handled event-driven via slotMouseChanged →
    // DragTracker::updateCursorPosition(), throttled to ~30Hz.
    connect(w, &KWin::EffectWindow::windowStartUserMovedResized, this, [this](KWin::EffectWindow* window) {
        m_dragTracker->handleWindowStartMoveResize(window);
    });
    connect(w, &KWin::EffectWindow::windowFinishUserMovedResized, this, [this](KWin::EffectWindow* window) {
        m_dragTracker->handleWindowFinishMoveResize(window);
    });

    // Track when user manually unmaximizes a monocle-maximized window
    connect(w, &KWin::EffectWindow::windowMaximizedStateChanged, m_autotileHandler.get(),
            &AutotileHandler::slotWindowMaximizedStateChanged);

    // Track when a monocle-maximized window goes fullscreen
    connect(w, &KWin::EffectWindow::windowFullScreenChanged, m_autotileHandler.get(),
            &AutotileHandler::slotWindowFullScreenChanged);

    // Autotile: center undersized Wayland windows as soon as they commit constrained size
    connect(w, &KWin::EffectWindow::windowFrameGeometryChanged, m_autotileHandler.get(),
            &AutotileHandler::slotWindowFrameGeometryChanged);

    // Autotile: track minimize/unminimize to remove/re-add windows from tiling
    connect(w, &KWin::EffectWindow::minimizedChanged, m_autotileHandler.get(),
            &AutotileHandler::slotWindowMinimizedChanged);

    // Snap mode: track minimize/unminimize to float/unfloat snapped windows
    connect(w, &KWin::EffectWindow::minimizedChanged, this, &PlasmaZonesEffect::slotWindowMinimizedChanged);
}

void PlasmaZonesEffect::slotMouseChanged(const QPointF& pos, const QPointF& oldpos, Qt::MouseButtons buttons,
                                         Qt::MouseButtons oldbuttons, Qt::KeyboardModifiers modifiers,
                                         Qt::KeyboardModifiers oldmodifiers)
{
    Q_UNUSED(oldpos)
    Q_UNUSED(oldmodifiers)

    const bool modifiersChanged = (m_currentModifiers != modifiers);
    const bool buttonsChanged = (oldbuttons != buttons);

    if (buttonsChanged && m_dragTracker->isDragging()) {
        qCInfo(lcEffect) << "mouseChanged buttons:" << static_cast<int>(oldbuttons) << "->"
                         << static_cast<int>(buttons);
    }

    if (modifiersChanged) {
        m_currentModifiers = modifiers;
        qCDebug(lcEffect) << "Modifiers changed to" << static_cast<int>(modifiers);
    }
    m_currentMouseButtons = buttons;

    if (m_dragTracker->isDragging()) {
        if ((oldbuttons & Qt::LeftButton) && !(buttons & Qt::LeftButton)) {
            // Primary button released = drag is over. Force-end regardless of whether
            // other buttons (e.g. right-click for zone activation) are still held.
            //
            // KWin keeps isUserMove() true while any button is held, so
            // windowFinishUserMovedResized wouldn't fire until ALL buttons are
            // released. forceEnd() gives immediate snap response on LMB release.
            //
            // After forceEnd, applySnapGeometry will defer (retry every 100 ms)
            // until isUserMove() clears when the remaining buttons are released.
            m_dragTracker->forceEnd(pos);
        } else if (modifiersChanged || buttonsChanged) {
            // Push modifier/button changes to daemon during drag immediately.
            // This includes activation button press/release — the daemon shows/hides
            // the overlay based on whether the activation trigger is currently held,
            // matching keyboard modifier behavior (hold to show, release to hide,
            // re-press to show again).
            //
            // Skip on autotile screens — no zone overlay to update, and calling
            // detectActivationAndGrab() would wastefully grab the keyboard and
            // sendDeferredDragStarted() would send a D-Bus call the daemon can't use.
            //
            // Gating: same logic as dragMoved lambda — skip if no activation
            // detected and no reason to send (avoids D-Bus traffic for non-zone drags).
            if (!m_dragBypassedForAutotile) {
                // When triggers haven't loaded yet, stay permissive (#175).
                if (detectActivationAndGrab() || m_cachedZoneSelectorEnabled || !m_triggersLoaded) {
                    sendDeferredDragStarted();
                    callDragMoved(m_dragTracker->draggedWindowId(), pos, m_currentModifiers,
                                  static_cast<int>(m_currentMouseButtons));
                }
            }
        } else {
            // Position-only change: drive cursor tracking through DragTracker's
            // event-driven path. This eliminates QTimer jitter from the compositor
            // frame path — updates arrive at input-device cadence (throttled to
            // ~30Hz inside DragTracker to avoid D-Bus flooding).
            m_dragTracker->updateCursorPosition(pos);
        }
    }

    // Track which screen the cursor is on for shortcut screen detection.
    // Only send a D-Bus call when the cursor actually crosses to a different monitor
    // (or virtual screen), not on every pixel move. This gives the daemon accurate
    // cursor-based screen info on Wayland where QCursor::pos() is unreliable for
    // background processes.
    const QPoint roundedPos(qRound(pos.x()), qRound(pos.y()));
    auto* output = KWin::effects->screenAt(roundedPos);
    QString connectorName;
    QString effectiveScreenId;
    if (output) {
        connectorName = output->name();
        // Resolve to virtual screen ID if subdivisions exist
        effectiveScreenId = resolveEffectiveScreenId(roundedPos, output);
        if (effectiveScreenId != m_lastEffectiveScreenId) {
            m_lastEffectiveScreenId = effectiveScreenId;
            m_lastCursorOutput = connectorName;
            if (m_daemonServiceRegistered) {
                fireAndForgetDBusCall(DBus::Interface::WindowTracking, QStringLiteral("cursorScreenChanged"),
                                      {effectiveScreenId});
            }
        }
    }

    // Focus follows mouse: activate autotile window under cursor when not dragging.
    // Reuse effectiveScreenId computed above to avoid redundant resolveEffectiveScreenId call.
    if (!m_dragTracker->isDragging() && output) {
        m_autotileHandler->handleCursorMoved(pos, effectiveScreenId);
    }
}

void PlasmaZonesEffect::applyStaggeredOrImmediate(int count, const std::function<void(int)>& applyFn,
                                                  const std::function<void()>& onComplete)
{
    if (count <= 0) {
        if (onComplete) {
            onComplete();
        }
        return;
    }

    const int staggerMs = m_cachedAnimationStaggerInterval;
    const bool stagger = (m_cachedAnimationSequenceMode == 1) && (count > 1) && (staggerMs > 0);

    if (stagger) {
        // Apply first item immediately (no event loop delay), schedule rest.
        // Stagger branch requires count > 1, so the loop always runs.
        applyFn(0);
        for (int i = 1; i < count; ++i) {
            const int delay = i * staggerMs;
            const bool isLast = (i == count - 1);
            QTimer::singleShot(delay, this, [applyFn, onComplete, i, isLast]() {
                applyFn(i);
                if (isLast && onComplete) {
                    onComplete();
                }
            });
        }
    } else {
        for (int i = 0; i < count; ++i) {
            applyFn(i);
        }
        if (onComplete) {
            onComplete();
        }
    }
}

void PlasmaZonesEffect::slotDaemonReady()
{
    if (m_daemonServiceRegistered) {
        return; // Already ready — idempotent guard
    }

    m_daemonServiceRegistered = true;
    qCInfo(lcEffect) << "daemon ready: re-pushing state";

    // All D-Bus calls use QDBusMessage::createMethodCall + asyncCall (no QDBusInterface)
    // to avoid synchronous D-Bus introspection that blocks the compositor thread.

    // Push KWin's output-order primary screen to the daemon so getPrimaryScreen()
    // reflects KDE Display Settings rather than QGuiApplication::primaryScreen().
    auto* ws = KWin::Workspace::self();
    if (ws) {
        const auto outputs = ws->outputOrder();
        if (!outputs.isEmpty()) {
            fireAndForgetDBusCall(DBus::Interface::Screen, QStringLiteral("setPrimaryScreenFromKWin"),
                                  {outputs.first()->name()}, QStringLiteral("setPrimaryScreenFromKWin"));
        }
    }

    // Re-push cursor screen — use the cached effective screen ID (which includes
    // virtual screen IDs like "A/vs:0") so the daemon's shortcut handler resolves
    // to the correct virtual screen, not the physical monitor.
    // m_lastEffectiveScreenId was set during the last processCursorPosition() call
    // via resolveEffectiveScreenId(), so it already has the correct virtual ID.
    if (!m_lastEffectiveScreenId.isEmpty()) {
        fireAndForgetDBusCall(DBus::Interface::WindowTracking, QStringLiteral("cursorScreenChanged"),
                              {m_lastEffectiveScreenId}, QStringLiteral("cursorScreenChanged"));
        qCDebug(lcEffect) << "Re-sent cursor screen:" << m_lastEffectiveScreenId;
    } else if (!m_lastCursorOutput.isEmpty()) {
        // Fallback: no effective ID cached yet (cursor hasn't moved since startup).
        // Resolve physical ID from connector name.
        QString cursorScreenId;
        for (const auto* output : KWin::effects->screens()) {
            if (output->name() == m_lastCursorOutput) {
                cursorScreenId = outputScreenId(output);
                break;
            }
        }
        if (cursorScreenId.isEmpty()) {
            cursorScreenId = m_lastCursorOutput;
        }
        fireAndForgetDBusCall(DBus::Interface::WindowTracking, QStringLiteral("cursorScreenChanged"), {cursorScreenId},
                              QStringLiteral("cursorScreenChanged"));
        qCDebug(lcEffect) << "Re-sent cursor screen (physical fallback):" << cursorScreenId;
    }

    // Re-notify active window (gives daemon lastActiveScreenName).
    // Use notifyWindowActivated which bypasses user exclusion lists — the daemon
    // must always know which window is active for correct shortcut handling.
    KWin::EffectWindow* activeWindow = getActiveWindow();
    if (activeWindow) {
        notifyWindowActivated(activeWindow);
    }

    // Fetch virtual screen definitions from daemon — needed before any screen ID
    // resolution so that getWindowScreenId() and cursor tracking return virtual
    // screen IDs when subdivisions are configured.
    // Clear ready flag immediately to close the race window where stale virtual
    // screen state from the previous daemon cycle is used before the new fetch
    // completes.
    m_virtualScreensReady = false;
    fetchAllVirtualScreenConfigs();

    // Re-sync floating windows (async, no QDBusInterface needed).
    // MUST clear the local set first — after daemon restart, the daemon's float state
    // is empty (ephemeral). Without clearing, stale entries from the previous daemon
    // session would persist in the effect, causing isWindowFloating() to return true
    // for windows that are no longer floating.
    {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking, QStringLiteral("getFloatingWindows"));
        auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            QDBusPendingReply<QStringList> reply = *w;
            if (reply.isValid()) {
                m_navigationHandler->clearAllFloatingState();
                QStringList floatingIds = reply.value();
                for (const QString& id : floatingIds) {
                    m_navigationHandler->setWindowFloating(id, true);
                }
                qCDebug(lcEffect) << "Synced" << floatingIds.size() << "floating windows from daemon";
            }
        });
    }

    // These already use QDBusMessage::createMethodCall (no QDBusInterface)
    loadCachedSettings();
    // Note: connectNavigationSignals() is NOT called here — it's already called
    // once in the constructor. D-Bus signal subscriptions persist across daemon
    // restarts. Calling it again would create duplicate connections, causing
    // handlers (e.g., toggleWindowFloat) to fire twice per signal.

    // Window state processing (autotile init, snap restore, etc.) depends on
    // virtual screen definitions being loaded for correct screen ID resolution.
    // Deferred to processDaemonReadyWindowState(), called by fetchAllVirtualScreenConfigs
    // once all async D-Bus replies have arrived.
}

void PlasmaZonesEffect::processDaemonReadyWindowState()
{
    if (m_daemonReadyWindowStateProcessed) {
        return;
    }
    m_daemonReadyWindowStateProcessed = true;

    // Delegate autotile re-initialization to handler.
    // Snapshot the active window so the autotile raise loop can re-activate it
    // after putting all tiled windows on top (which would bury non-tiled windows
    // like the KCM settings panel). Only set if the active window is NOT on an
    // autotile screen — autotile screens handle their own focus via
    // m_pendingAutotileFocusWindowId in the onComplete callback.
    KWin::EffectWindow* activeWin = KWin::effects->activeWindow();
    if (activeWin && !m_autotileHandler->isAutotileScreen(getWindowScreenId(activeWin))) {
        m_autotileHandler->setPendingReactivateWindow(activeWin);
    }
    m_autotileHandler->onDaemonReady();

    // Re-announce all existing windows on autotile screens in one batch D-Bus
    // call instead of per-window windowOpened round-trips.
    const auto windows = KWin::effects->stackingOrder();
    m_autotileHandler->notifyWindowsAddedBatch(windows);

    // Report all live window IDs to the daemon so it can prune stale
    // entries from KConfig (windows that were snapped but no longer exist).
    {
        QStringList aliveWindowIds;
        for (KWin::EffectWindow* w : windows) {
            if (w && shouldHandleWindow(w)) {
                aliveWindowIds.append(getWindowId(w));
            }
        }
        fireAndForgetDBusCall(DBus::Interface::WindowTracking, QStringLiteral("pruneStaleWindows"),
                              {QVariant::fromValue(aliveWindowIds)}, QStringLiteral("pruneStaleWindows"));
    }

    // Fetch pre-computed pending restore geometries so slotWindowAdded can
    // teleport windows to their zone immediately (no D-Bus round-trip flash).
    // Fire-and-forget: the cache is populated asynchronously. Windows that open
    // before the reply arrives fall back to the normal async restore path.
    {
        QDBusMessage geoMsg =
            QDBusMessage::createMethodCall(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                           QStringLiteral("getPendingRestoreGeometries"));
        auto* geoWatcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(geoMsg), this);
        connect(geoWatcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            QDBusPendingReply<QString> reply = *w;
            if (!reply.isValid()) {
                return;
            }
            QJsonDocument doc = QJsonDocument::fromJson(reply.value().toUtf8());
            if (!doc.isObject()) {
                return;
            }
            QJsonObject obj = doc.object();
            m_snapRestoreCache.clear();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                QJsonObject geo = it.value().toObject();
                int x = geo[QLatin1String("x")].toInt();
                int y = geo[QLatin1String("y")].toInt();
                int w = geo[QLatin1String("width")].toInt();
                int h = geo[QLatin1String("height")].toInt();
                if (w > 0 && h > 0) {
                    m_snapRestoreCache.insert(it.key(), QRect(x, y, w, h));
                }
            }
            qCDebug(lcEffect) << "Cached" << m_snapRestoreCache.size() << "pending restore geometries";
        });
    }

    // Restore snap state for all untracked windows.
    // pendingRestoresAvailable may have fired BEFORE daemonReady, causing
    // slotPendingRestoresAvailable to bail out (m_daemonServiceRegistered was false).
    // Now that the daemon is confirmed ready, retry the restore flow using raw
    // QDBusMessage (no QDBusInterface) to avoid synchronous introspection.
    {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking, QStringLiteral("getSnappedWindows"));
        auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
            w->deleteLater();

            // Guard: prevent slotPendingRestoresAvailable from double-processing
            // the same windows. Set inside the callback so that if this D-Bus call
            // fails, the flag stays false and slotPendingRestoresAvailable can
            // still function as a fallback.
            m_daemonReadyRestoresDone = true;

            QDBusPendingReply<QStringList> reply = *w;
            QSet<QString> trackedAppIds;
            if (reply.isValid()) {
                const QStringList trackedWindows = reply.value();
                for (const QString& windowId : trackedWindows) {
                    QString appId = extractAppId(windowId);
                    if (!appId.isEmpty()) {
                        trackedAppIds.insert(appId);
                    }
                }
            }

            // Snapshot the current stacking order before snap restores.
            // moveResize() on KWin 6 / Wayland implicitly raises the target
            // window. After all restores complete, we re-raise windows in
            // their original order — same pattern as the autotile handler's
            // onComplete raise loop in tiling.cpp.
            const auto allWindows = KWin::effects->stackingOrder();
            QVector<QPointer<KWin::EffectWindow>> savedStackingOrder;
            for (KWin::EffectWindow* w : allWindows) {
                savedStackingOrder.append(QPointer<KWin::EffectWindow>(w));
            }

            // Collect windows that need snap restoration (untracked).
            // Don't skip windows on autotile screens: KWin session restore may
            // place a window in the autotile screen's area even though it was
            // snapped in the snap screen before logout. The daemon's pending
            // restore entry knows the correct screen; if it returns a snap
            // geometry, the window moves off the autotile screen and the
            // autotile handler detects the departure via VS crossing detection.
            // Use QPointer for lifetime safety in case a window is destroyed
            // between collection and the dispatch loop below.
            QVector<QPointer<KWin::EffectWindow>> toRestore;
            for (KWin::EffectWindow* window : allWindows) {
                if (!window || !shouldHandleWindow(window)) {
                    continue;
                }
                if (window->isMinimized()) {
                    continue;
                }
                QString appId = extractAppId(getWindowId(window));
                if (trackedAppIds.contains(appId)) {
                    continue;
                }
                toRestore.append(QPointer<KWin::EffectWindow>(window));
            }

            if (toRestore.isEmpty()) {
                qCDebug(lcEffect) << "No untracked windows need snap restore after daemon ready";
                return;
            }

            qCInfo(lcEffect) << "Triggered snap restore for" << toRestore.size()
                             << "untracked windows after daemon ready";

            // Track how many windows actually moved (moveResize was called).
            // If none moved, skip the stacking restoration — no disruption occurred.
            auto pending = std::make_shared<int>(toRestore.size());
            auto movedCount = std::make_shared<int>(0);

            for (const auto& safeWindow : toRestore) {
                if (!safeWindow || safeWindow->isDeleted()) {
                    // Window destroyed between collection and dispatch — count
                    // it as done so the pending counter still reaches zero.
                    if (--(*pending) == 0) {
                        qCDebug(lcEffect) << "Stacking restore: all targets gone, skipping";
                    }
                    continue;
                }
                // Snapshot geometry before the async call; if it changes after
                // applySnapGeometry, we know a moveResize happened.
                QRectF geoBefore = safeWindow->frameGeometry();

                callResolveWindowRestore(
                    safeWindow.data(), [pending, movedCount, safeWindow, geoBefore, savedStackingOrder]() {
                        // Detect whether moveResize actually fired by comparing geometry.
                        if (safeWindow && !safeWindow->isDeleted() && safeWindow->frameGeometry() != geoBefore) {
                            ++(*movedCount);
                        }

                        if (--(*pending) > 0) {
                            return;
                        }

                        // All snap restores done.
                        if (*movedCount == 0) {
                            qCDebug(lcEffect) << "Stacking restore: all windows at target geometry, skipping";
                            return;
                        }

                        // Re-raise windows in original order (bottom-to-top).
                        auto* ws = KWin::Workspace::self();
                        if (!ws) {
                            return;
                        }
                        for (const auto& wPtr : savedStackingOrder) {
                            if (wPtr && !wPtr->isDeleted()) {
                                KWin::Window* kw = wPtr->window();
                                if (kw) {
                                    ws->raiseWindow(kw);
                                }
                            }
                        }
                    });
            }
        });
    }
}

void PlasmaZonesEffect::slotSettingsChanged()
{
    qCInfo(lcEffect) << "settingsChanged: reloading settings";
    loadCachedSettings();
    // Note: loadAutotileSettings() is intentionally NOT called here.
    // Autotile screen changes are tracked via the dedicated autotileScreensChanged
    // D-Bus signal (→ slotAutotileScreensChanged), which is authoritative.
    // Calling loadAutotileSettings on every settingsChanged causes redundant
    // full window re-notification (N D-Bus windowOpened calls + retile round)
    // on every algorithm/gap/setting change — the daemon already retiles and
    // emits windowsTiled directly for those changes.
}

QString PlasmaZonesEffect::getWindowId(KWin::EffectWindow* w) const
{
    if (!w) {
        return QString();
    }

    // Cache hit: window IDs never change during a window's lifetime
    auto cacheIt = m_windowIdCache.constFind(w);
    if (cacheIt != m_windowIdCache.constEnd()) {
        return cacheIt.value();
    }

    KWin::Window* window = w->window();
    if (!window) {
        return QString();
    }

    // App identity: prefer desktopFileName (most stable cross-session identifier)
    QString appId = window->desktopFileName();
    if (appId.isEmpty()) {
        // Fallback: normalize windowClass
        // X11: "resourceName resourceClass" -> extract resourceClass
        // Wayland: app_id as-is
        QString wc = w->windowClass();
        int spaceIdx = wc.indexOf(QLatin1Char(' '));
        appId = (spaceIdx > 0) ? wc.mid(spaceIdx + 1) : wc;
    }
    appId = appId.toLower();

    // Instance identity: KWin's internal UUID (unique within KWin session)
    QString instanceId = window->internalId().toString(QUuid::WithoutBraces);

    const QString result = appId + QLatin1Char('|') + instanceId;
    m_windowIdCache.insert(w, result);
    m_windowIdReverse.insert(result, w);
    return result;
}

bool PlasmaZonesEffect::shouldHandleWindow(KWin::EffectWindow* w) const
{
    if (!w) {
        return false;
    }

    // Never snap our own overlay/editor windows (but allow the settings app)
    const QString windowClass = w->windowClass();
    if (windowClass.contains(QLatin1String("plasmazonesd"), Qt::CaseInsensitive)
        || windowClass.contains(QLatin1String("plasmazones-editor"), Qt::CaseInsensitive)) {
        return false;
    }

    // Exclude XDG desktop portal windows (file dialogs, color pickers, etc.)
    if (windowClass.contains(QLatin1String("xdg-desktop-portal"), Qt::CaseInsensitive)) {
        return false;
    }

    // Check user-configured exclusion lists (needed for drag gating — daemon also enforces
    // for keyboard nav, but the effect must filter for drag operations and lifecycle reporting)
    if (!m_excludedApplications.isEmpty() || !m_excludedWindowClasses.isEmpty()) {
        KWin::Window* kw = w->window();
        const QString appName = kw ? kw->desktopFileName() : QString();
        for (const QString& excluded : m_excludedApplications) {
            if (!excluded.isEmpty() && appName.contains(excluded, Qt::CaseInsensitive)) {
                return false;
            }
        }
        for (const QString& excluded : m_excludedWindowClasses) {
            if (!excluded.isEmpty() && windowClass.contains(excluded, Qt::CaseInsensitive)) {
                return false;
            }
        }
    }

    // Skip special / non-manageable window types (inherently effect-side — KWin metadata)
    if (w->isSpecialWindow() || w->isDesktop() || w->isDock() || w->isFullScreen() || w->isSkipSwitcher()) {
        return false;
    }

    // Skip transient/dialog windows unconditionally. Dialogs, utilities, tooltips,
    // notifications, etc. should never be zone-managed. User-configured exclusion
    // lists and minimum size checks are handled by the daemon.
    if (w->isDialog() || w->isUtility() || w->isSplash() || w->isNotification() || w->isOnScreenDisplay()
        || w->isModal() || w->isPopupWindow()) {
        return false;
    }

    return true;
}

bool PlasmaZonesEffect::isTileableWindow(KWin::EffectWindow* w) const
{
    // Reject menus, popups, tooltips, modals, and transient children.
    // Electron apps (Vesktop, VS Code, Discord) create separate windows
    // for context menus and dropdowns that pass shouldHandleWindow() but
    // must never enter the autotile tree.
    if (!w->isNormalWindow() || w->isModal() || w->isPopupWindow() || w->isDropdownMenu() || w->isPopupMenu()
        || w->isTooltip() || w->isMenu() || w->transientFor()) {
        return false;
    }
    // Reject keep-above windows — overlay/utility tools (Spectacle, color
    // pickers, screen rulers, etc.) set keep-above and should not enter the
    // autotile tree or receive auto-focus. Without this guard, opening
    // Spectacle while focusNewWindows is enabled disrupts the tiled layout.
    if (w->keepAbove()) {
        return false;
    }
    return true;
}

// shouldAutoSnapWindow removed — equivalent to shouldHandleWindow + isTileableWindow.
// Call sites use isTileableWindow directly (stricter than shouldHandleWindow alone).

bool PlasmaZonesEffect::hasOtherWindowOfClassWithDifferentPid(KWin::EffectWindow* w) const
{
    if (!w) {
        return false;
    }

    QString windowClass = w->windowClass();
    pid_t windowPid = w->pid();

    // Check all existing windows for same class but different PID
    // This detects when another app (e.g., Cachy Update) spawns a window
    // of a class that the user has previously snapped (e.g., Ghostty)
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* other : windows) {
        if (other == w) {
            continue; // Skip self
        }
        if (!shouldHandleWindow(other)) {
            continue; // Skip non-managed windows
        }
        if (other->windowClass() == windowClass && other->pid() != windowPid) {
            // Found another window of the same class with different PID
            // This means the new window was likely spawned by a different app
            return true;
        }
    }

    return false;
}

bool PlasmaZonesEffect::isDaemonReady(const char* methodName) const
{
    if (!m_daemonServiceRegistered) {
        qCDebug(lcEffect) << "Cannot" << methodName << "- daemon not ready";
        return false;
    }
    return true;
}

QDBusPendingCall PlasmaZonesEffect::asyncMethodCall(const QString& interface, const QString& method,
                                                    const QVariantList& args)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(DBus::ServiceName, DBus::ObjectPath, interface, method);
    for (const QVariant& arg : args) {
        msg << arg;
    }
    return QDBusConnection::sessionBus().asyncCall(msg);
}

void PlasmaZonesEffect::syncFloatingWindowsFromDaemon()
{
    // Delegate to NavigationHandler
    m_navigationHandler->syncFloatingWindowsFromDaemon();
}

// Template implementation for loadSettingAsync — must be in .cpp since
// PlasmaZonesEffect is not a header-only class (all callers are in this TU).
template<typename Fn>
void PlasmaZonesEffect::loadSettingAsync(const QString& name, Fn&& onValue)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Settings,
                                                      QStringLiteral("getSetting"));
    msg << name;
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [name, onValue](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QVariant> reply = *w;
        if (reply.isValid()) {
            QVariant value = reply.value();
            if (value.canConvert<QDBusVariant>()) {
                value = value.value<QDBusVariant>().variant();
            }
            onValue(value);
            qCDebug(lcEffect) << "Loaded" << name;
        }
    });
}

void PlasmaZonesEffect::loadCachedSettings()
{
    // Uses raw QDBusMessage (not QDBusInterface) to avoid synchronous introspection
    // that would block the compositor during login (see discussion #158).
    //
    // Transient exclusion and min-size are handled by the daemon. Exclusion lists are
    // cached here for drag-operation gating (shouldHandleWindow).
    m_triggersLoaded = false; // Permissive until new triggers arrive (#175)

    loadSettingAsync(QStringLiteral("excludedApplications"), [this](const QVariant& v) {
        m_excludedApplications = v.toStringList();
    });
    loadSettingAsync(QStringLiteral("excludedWindowClasses"), [this](const QVariant& v) {
        m_excludedWindowClasses = v.toStringList();
    });
    loadSettingAsync(QStringLiteral("minimumWindowWidth"), [this](const QVariant& v) {
        m_cachedMinWindowWidth = v.toInt();
    });
    loadSettingAsync(QStringLiteral("minimumWindowHeight"), [this](const QVariant& v) {
        m_cachedMinWindowHeight = v.toInt();
    });
    loadSettingAsync(QStringLiteral("snapAssistEnabled"), [this](const QVariant& v) {
        m_snapAssistHandler->setEnabled(v.toBool());
    });
    loadSettingAsync(QStringLiteral("animationsEnabled"), [this](const QVariant& v) {
        m_windowAnimator->setEnabled(v.toBool());
    });
    loadSettingAsync(QStringLiteral("animationDuration"), [this](const QVariant& v) {
        const int d = qBound(50, v.toInt(), 500);
        m_windowAnimator->setDuration(d);
        m_cachedAnimationDuration = d;
    });
    loadSettingAsync(QStringLiteral("animationEasingCurve"), [this](const QVariant& v) {
        m_windowAnimator->setEasingCurve(EasingCurve::fromString(v.toString()));
    });
    loadSettingAsync(QStringLiteral("animationMinDistance"), [this](const QVariant& v) {
        m_windowAnimator->setMinDistance(qBound(0, v.toInt(), 200));
    });
    loadSettingAsync(QStringLiteral("animationSequenceMode"), [this](const QVariant& v) {
        m_cachedAnimationSequenceMode = qBound(0, v.toInt(), 1);
    });
    loadSettingAsync(QStringLiteral("animationStaggerInterval"), [this](const QVariant& v) {
        m_cachedAnimationStaggerInterval = qBound(10, v.toInt(), 200);
    });
    loadSettingAsync(QStringLiteral("toggleActivation"), [this](const QVariant& v) {
        m_cachedToggleActivation = v.toBool();
    });
    loadSettingAsync(QStringLiteral("zoneSelectorEnabled"), [this](const QVariant& v) {
        m_cachedZoneSelectorEnabled = v.toBool();
    });

    // autotileHideTitleBars needs extra logic when toggled off — delegate to handler
    loadSettingAsync(QStringLiteral("autotileHideTitleBars"), [this](const QVariant& v) {
        m_autotileHandler->updateHideTitleBarsSetting(v.toBool());
        updateAllBorders();
    });

    loadSettingAsync(QStringLiteral("autotileShowBorder"), [this](const QVariant& v) {
        m_autotileHandler->updateShowBorderSetting(v.toBool());
        updateAllBorders();
    });

    loadSettingAsync(QStringLiteral("autotileBorderWidth"), [this](const QVariant& v) {
        int bw = qBound(0, v.toInt(), 10);
        if (m_autotileHandler->borderWidth() != bw) {
            m_autotileHandler->setBorderWidth(bw);
            // Invalidate pending stagger timers that would use the old border width
            m_autotileHandler->invalidateStaggerGeneration();
            fireAndForgetDBusCall(DBus::Interface::Autotile, QStringLiteral("retileAllScreens"), {},
                                  QStringLiteral("border width change retile"));
            updateAllBorders();
        }
    });

    loadSettingAsync(QStringLiteral("autotileBorderRadius"), [this](const QVariant& v) {
        int br = qBound(0, v.toInt(), 20);
        if (m_autotileHandler->borderRadius() != br) {
            m_autotileHandler->setBorderRadius(br);
            updateAllBorders();
        }
    });

    loadSettingAsync(QStringLiteral("autotileBorderColor"), [this](const QVariant& v) {
        m_autotileHandler->setBorderColor(QColor(v.toString()));
        updateAllBorders();
    });

    loadSettingAsync(QStringLiteral("autotileInactiveBorderColor"), [this](const QVariant& v) {
        m_autotileHandler->setInactiveBorderColor(QColor(v.toString()));
        updateAllBorders();
    });

    loadSettingAsync(QStringLiteral("autotileFocusFollowsMouse"), [this](const QVariant& v) {
        m_autotileHandler->setFocusFollowsMouse(v.toBool());
    });

    // dragActivationTriggers has complex QDBusArgument deserialization — keep as special case
    {
        QDBusMessage msg = QDBusMessage::createMethodCall(DBus::ServiceName, DBus::ObjectPath,
                                                          DBus::Interface::Settings, QStringLiteral("getSetting"));
        msg << QStringLiteral("dragActivationTriggers");
        auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            QDBusPendingReply<QVariant> reply = *w;
            if (!reply.isValid()) {
                qCWarning(lcEffect) << "dragActivationTriggers: load failed, gating remains permissive";
                return;
            }
            QVariant triggerVariant = reply.value();
            if (triggerVariant.canConvert<QDBusVariant>()) {
                triggerVariant = triggerVariant.value<QDBusVariant>().variant();
            }

            // D-Bus may deliver QVariantList-of-QVariantMap as QDBusArgument (#175)
            QVariantList triggerList;
            if (triggerVariant.canConvert<QDBusArgument>()) {
                const QDBusArgument arg = triggerVariant.value<QDBusArgument>();
                arg.beginArray();
                while (!arg.atEnd()) {
                    QVariant element;
                    arg >> element;
                    triggerList.append(element);
                }
                arg.endArray();
            } else {
                triggerList = triggerVariant.toList();
            }
            m_cachedDragActivationTriggers = triggerList;

            // Pre-parse to POD structs (avoids QVariant unboxing at ~30Hz)
            m_parsedTriggers.clear();
            m_parsedTriggers.reserve(triggerList.size());
            for (const auto& t : std::as_const(triggerList)) {
                QVariantMap map;
                if (t.canConvert<QDBusArgument>()) {
                    const QDBusArgument elemArg = t.value<QDBusArgument>();
                    elemArg >> map;
                } else {
                    map = t.toMap();
                }
                ParsedTrigger pt;
                pt.modifier = map.value(TriggerModifierField, 0).toInt();
                pt.mouseButton = map.value(TriggerMouseButtonField, 0).toInt();
                m_parsedTriggers.append(pt);
            }

            qCDebug(lcEffect) << "Loaded dragActivationTriggers:" << m_parsedTriggers.size() << "triggers";
            bool anyValid =
                std::any_of(m_parsedTriggers.cbegin(), m_parsedTriggers.cend(), [](const ParsedTrigger& pt) {
                    return pt.modifier != 0 || pt.mouseButton != 0;
                });
            if (!m_parsedTriggers.isEmpty() && !anyValid) {
                qCWarning(lcEffect) << "All triggers have modifier=0 mouseButton=0"
                                    << "- possible deserialization issue";
            }
            m_triggersLoaded = true;
        });
    }

    qCDebug(lcEffect) << "Loading cached settings asynchronously, using defaults until loaded";
}

bool PlasmaZonesEffect::checkLocalModifier(int modifierSetting, Qt::KeyboardModifiers mods)
{
    const bool shiftHeld = mods.testFlag(Qt::ShiftModifier);
    const bool ctrlHeld = mods.testFlag(Qt::ControlModifier);
    const bool altHeld = mods.testFlag(Qt::AltModifier);
    const bool metaHeld = mods.testFlag(Qt::MetaModifier);

    switch (modifierSetting) {
    case 0:
        return false; // Disabled
    case 1:
        return shiftHeld; // Shift
    case 2:
        return ctrlHeld; // Ctrl
    case 3:
        return altHeld; // Alt
    case 4:
        return metaHeld; // Meta
    case 5:
        return ctrlHeld && altHeld; // CtrlAlt
    case 6:
        return ctrlHeld && shiftHeld; // CtrlShift
    case 7:
        return altHeld && shiftHeld; // AltShift
    case 8:
        return true; // AlwaysActive
    case 9:
        return altHeld && metaHeld; // AltMeta
    case 10:
        return ctrlHeld && altHeld && metaHeld; // CtrlAltMeta
    default:
        return false;
    }
}

bool PlasmaZonesEffect::anyLocalTriggerHeld() const
{
    // Use pre-parsed triggers to avoid QVariant unboxing (~30x/sec during drag)
    for (const auto& t : m_parsedTriggers) {
        const bool modMatch = (t.modifier == 0) || checkLocalModifier(t.modifier, m_currentModifiers);
        const bool btnMatch = (t.mouseButton == 0) || (static_cast<int>(m_currentMouseButtons) & t.mouseButton) != 0;
        if (modMatch && btnMatch && (t.modifier != 0 || t.mouseButton != 0))
            return true;
    }
    return false;
}

bool PlasmaZonesEffect::detectActivationAndGrab()
{
    if (m_dragActivationDetected) {
        return true;
    }
    if (anyLocalTriggerHeld() || m_cachedToggleActivation) {
        m_dragActivationDetected = true;
        if (!m_keyboardGrabbed) {
            KWin::effects->grabKeyboard(this);
            m_keyboardGrabbed = true;
        }
        return true;
    }
    return false;
}

void PlasmaZonesEffect::sendDeferredDragStarted()
{
    if (m_dragStartedSent) {
        return;
    }
    m_dragStartedSent = true;
    callDragStarted(m_pendingDragWindowId, m_pendingDragGeometry);
}

void PlasmaZonesEffect::connectNavigationSignals()
{
    // Daemon-driven navigation: daemon computes geometry and emits applyGeometryRequested directly
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("applyGeometryRequested"), this,
                                          SLOT(slotApplyGeometryRequested(QString, QString, QString, QString)));

    // Daemon-driven focus/cycle: daemon resolves target window and emits activateWindowRequested
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("activateWindowRequested"), this,
                                          SLOT(slotActivateWindowRequested(QString)));

    // Float toggle (daemon handles full flow, emits applyGeometryRequested for geometry)
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("toggleWindowFloatRequested"), this,
                                          SLOT(slotToggleWindowFloatRequested(bool)));

    // Daemon-driven batch operations (rotate, resnap emit applyGeometriesBatch)
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("applyGeometriesBatch"), this,
                                          SLOT(slotApplyGeometriesBatch(QString, QString)));

    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("raiseWindowsRequested"), this,
                                          SLOT(slotRaiseWindowsRequested(QStringList)));

    // Snap-all: daemon triggers effect to collect candidates
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("snapAllWindowsRequested"), this,
                                          SLOT(slotSnapAllWindowsRequested(QString)));

    // Move specific window (Snap Assist selection)
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("moveSpecificWindowToZoneRequested"), this,
                                          SLOT(slotMoveSpecificWindowToZoneRequested(QString, QString, QString)));

    // Pending restores on daemon startup
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("pendingRestoresAvailable"), this,
                                          SLOT(slotPendingRestoresAvailable()));

    // Screen geometry reapply
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("reapplyWindowGeometriesRequested"),
                                          m_screenChangeHandler.get(), SLOT(slotReapplyWindowGeometriesRequested()));

    // Floating state sync
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowTracking,
                                          QStringLiteral("windowFloatingChanged"), this,
                                          SLOT(slotWindowFloatingChanged(QString, bool, QString)));

    // Settings: window picker for KCM exclusion list
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Settings,
                                          QStringLiteral("runningWindowsRequested"), this,
                                          SLOT(slotRunningWindowsRequested()));

    // WindowDrag: during-drag size restore
    QDBusConnection::sessionBus().connect(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowDrag,
                                          QStringLiteral("restoreSizeDuringDragChanged"), this,
                                          SLOT(slotRestoreSizeDuringDrag(QString, int, int)));

    qCInfo(lcEffect) << "Connected to navigation D-Bus signals";
}

KWin::EffectWindow* PlasmaZonesEffect::getActiveWindow() const
{
    // Prefer KWin's active (focused) window when it is manageable and on current desktop
    KWin::EffectWindow* active = KWin::effects->activeWindow();
    if (active && active->isOnCurrentActivity() && active->isOnCurrentDesktop() && !active->isMinimized()
        && shouldHandleWindow(active)) {
        return active;
    }
    // Fallback: topmost manageable window on current desktop (e.g. when activeWindow() is
    // null or refers to a dialog/utility we don't handle)
    const auto windows = KWin::effects->stackingOrder();
    for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
        KWin::EffectWindow* w = *it;
        if (w && w->isOnCurrentActivity() && w->isOnCurrentDesktop() && !w->isMinimized() && shouldHandleWindow(w)) {
            return w;
        }
    }
    return nullptr;
}

QString PlasmaZonesEffect::outputScreenId(const KWin::LogicalOutput* output) const
{
    if (!output) {
        return QString();
    }
    const QString connectorName = output->name();

    // Cache: screen IDs are stable for the lifetime of an output. Caching avoids
    // repeated QGuiApplication::screens() iteration and sysfs reads (~30Hz during drag).
    // Invalidated on screen add/remove (m_screenIdCache cleared by screen change handler).
    auto it = m_screenIdCache.constFind(connectorName);
    if (it != m_screenIdCache.constEnd()) {
        return *it;
    }

    // Build a screen ID that exactly matches the daemon's Utils::screenIdentifier().
    // The daemon uses QScreen::serialNumber() with a sysfs EDID fallback. KWin's
    // Output::serialNumber() may return a different EDID field (text serial descriptor
    // vs header serial). Mirror the daemon's resolution order to produce identical IDs.
    //
    // Note: duplicates daemon's readEdidHeaderSerial() for the sysfs fallback because
    // the effect plugin can't link plasmazones_core. The EDID header format (bytes 0-15)
    // is a hardware standard that won't change.
    const QString manufacturer = output->manufacturer();
    const QString model = output->model();
    QString serial;

    // Try QScreen::serialNumber() (same source as daemon)
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->name() == connectorName) {
            serial = screen->serialNumber();
            break;
        }
    }

    // Normalize hex header serial to decimal (same as daemon)
    if (!serial.isEmpty() && serial.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
        bool ok = false;
        uint32_t numericSerial = serial.toUInt(&ok, 16);
        if (ok && numericSerial != 0) {
            serial = QString::number(numericSerial);
        }
    }

    // Fallback: sysfs EDID header serial (bytes 12-15, little-endian uint32)
    if (serial.isEmpty()) {
        QDir drmDir(QStringLiteral("/sys/class/drm"));
        if (drmDir.exists()) {
            for (const QString& entry : drmDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                int dashPos = entry.indexOf(QLatin1Char('-'));
                if (dashPos < 0 || entry.mid(dashPos + 1) != connectorName) {
                    continue;
                }
                QFile edidFile(drmDir.filePath(entry) + QStringLiteral("/edid"));
                if (!edidFile.open(QIODevice::ReadOnly)) {
                    continue;
                }
                QByteArray header = edidFile.read(16);
                if (header.size() < 16) {
                    continue;
                }
                const auto* data = reinterpret_cast<const uint8_t*>(header.constData());
                if (data[0] != 0x00 || data[1] != 0xFF || data[2] != 0xFF || data[3] != 0xFF || data[4] != 0xFF
                    || data[5] != 0xFF || data[6] != 0xFF || data[7] != 0x00) {
                    continue;
                }
                uint32_t headerSerial = data[12] | (static_cast<uint32_t>(data[13]) << 8)
                    | (static_cast<uint32_t>(data[14]) << 16) | (static_cast<uint32_t>(data[15]) << 24);
                if (headerSerial != 0) {
                    serial = QString::number(headerSerial);
                    break;
                }
            }
        }
    }

    QString baseId;
    if (!serial.isEmpty()) {
        baseId = manufacturer + QLatin1Char(':') + model + QLatin1Char(':') + serial;
    } else if (!manufacturer.isEmpty() || !model.isEmpty()) {
        baseId = manufacturer + QLatin1Char(':') + model;
    } else {
        baseId = connectorName;
    }

    // Disambiguate identical monitors: if another screen produces the same base ID,
    // append "/ConnectorName" to make each unique. Mirrors daemon's screenIdentifier().
    // Build base IDs for other screens using the full logic (including sysfs EDID fallback)
    // to ensure parity with the daemon's screenBaseIdentifier().
    auto buildBaseId = [](QScreen* s) -> QString {
        const QString mfr = s->manufacturer();
        const QString mdl = s->model();
        QString ser = s->serialNumber();
        if (!ser.isEmpty() && ser.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
            bool ok = false;
            uint32_t num = ser.toUInt(&ok, 16);
            if (ok && num != 0) {
                ser = QString::number(num);
            }
        }
        // sysfs EDID header serial fallback (same as daemon's readEdidHeaderSerial)
        if (ser.isEmpty()) {
            QDir drmDir(QStringLiteral("/sys/class/drm"));
            if (drmDir.exists()) {
                const QString cName = s->name();
                for (const QString& entry : drmDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                    int dashPos = entry.indexOf(QLatin1Char('-'));
                    if (dashPos < 0 || entry.mid(dashPos + 1) != cName) {
                        continue;
                    }
                    QFile edidFile(drmDir.filePath(entry) + QStringLiteral("/edid"));
                    if (!edidFile.open(QIODevice::ReadOnly)) {
                        continue;
                    }
                    QByteArray hdr = edidFile.read(16);
                    if (hdr.size() < 16) {
                        continue;
                    }
                    const auto* d = reinterpret_cast<const uint8_t*>(hdr.constData());
                    if (d[0] != 0x00 || d[1] != 0xFF || d[2] != 0xFF || d[3] != 0xFF || d[4] != 0xFF || d[5] != 0xFF
                        || d[6] != 0xFF || d[7] != 0x00) {
                        continue;
                    }
                    uint32_t headerSerial =
                        d[12] | (uint32_t(d[13]) << 8) | (uint32_t(d[14]) << 16) | (uint32_t(d[15]) << 24);
                    if (headerSerial != 0) {
                        ser = QString::number(headerSerial);
                        break;
                    }
                }
            }
        }
        if (!ser.isEmpty()) {
            return mfr + QLatin1Char(':') + mdl + QLatin1Char(':') + ser;
        }
        if (!mfr.isEmpty() || !mdl.isEmpty()) {
            return mfr + QLatin1Char(':') + mdl;
        }
        return s->name();
    };

    bool hasDuplicate = false;
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->name() != connectorName && buildBaseId(screen) == baseId) {
            hasDuplicate = true;
            break;
        }
    }

    QString result = hasDuplicate ? baseId + QLatin1Char('/') + connectorName : baseId;
    m_screenIdCache.insert(connectorName, result);
    return result;
}

QString PlasmaZonesEffect::getWindowScreenId(KWin::EffectWindow* w) const
{
    if (!w) {
        return QString();
    }
    const QPointF c = w->frameGeometry().center();
    return resolveEffectiveScreenId(QPoint(qRound(c.x()), qRound(c.y())), w->screen());
}

// ═══════════════════════════════════════════════════════════════════════════════
// Virtual Screen Support
// ═══════════════════════════════════════════════════════════════════════════════

QString PlasmaZonesEffect::resolveEffectiveScreenId(const QPoint& pos, const KWin::LogicalOutput* output) const
{
    const QString physId = outputScreenId(output);
    if (physId.isEmpty()) {
        return physId;
    }

    // Check if this physical screen has virtual subdivisions
    auto it = m_virtualScreenDefs.constFind(physId);
    if (it == m_virtualScreenDefs.constEnd() || it->isEmpty()) {
        return physId; // No subdivisions, return physical ID
    }

    // Find which virtual screen contains the point.
    // Use exclusive-right/bottom semantics to match the daemon's containment check.
    // QRect::contains() uses inclusive-right, which causes boundary-pixel mismatches
    // between effect and daemon for abutting virtual screens.
    for (const auto& vs : *it) {
        const QRect& r = vs.geometry;
        if (pos.x() >= r.x() && pos.x() < r.x() + r.width() && pos.y() >= r.y() && pos.y() < r.y() + r.height()) {
            return vs.id;
        }
    }

    // Fallback: pick nearest virtual screen (covers rounding gaps)
    QString nearestVsId;
    int minDist = INT_MAX;
    for (const auto& vs : *it) {
        // Manhattan distance from point to nearest edge of the rect
        int dx = 0;
        int dy = 0;
        // Use exclusive-right/bottom (x + width, y + height) to match the
        // primary containment check above.  QRect::right()/bottom() return
        // inclusive values (x + width - 1), which would be off by 1px.
        const int exRight = vs.geometry.x() + vs.geometry.width();
        const int exBottom = vs.geometry.y() + vs.geometry.height();
        if (pos.x() < vs.geometry.left()) {
            dx = vs.geometry.left() - pos.x();
        } else if (pos.x() >= exRight) {
            dx = pos.x() - exRight;
        }
        if (pos.y() < vs.geometry.top()) {
            dy = vs.geometry.top() - pos.y();
        } else if (pos.y() >= exBottom) {
            dy = pos.y() - exBottom;
        }
        int dist = dx + dy;
        if (dist < minDist) {
            minDist = dist;
            nearestVsId = vs.id;
        }
    }
    if (!nearestVsId.isEmpty()) {
        return nearestVsId;
    }
    // Ultimate fallback (should never reach here)
    qCWarning(lcEffect) << "resolveEffectiveScreenId: no virtual screens found for" << physId;
    return physId;
}

void PlasmaZonesEffect::fetchVirtualScreenConfig(const QString& physicalScreenId, uint64_t generation)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Screen,
                                                      QStringLiteral("getVirtualScreenConfig"));
    msg << physicalScreenId;

    QDBusPendingCall call = QDBusConnection::sessionBus().asyncCall(msg);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    QPointer<PlasmaZonesEffect> self(this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [self, physicalScreenId, generation](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                if (!self)
                    return;
                // Helper lambda: decrement pending counter and fire deferred processing when all done.
                // Only participates in the startup gate if generation != 0 (issued by fetchAllVirtualScreenConfigs)
                // and the generation matches the current one (not stale from a prior fetch cycle).
                // Captures self by value (QPointer copy) to avoid dangling reference.
                auto countdownVsGate = [self, generation]() {
                    if (generation == 0 || !self || self->m_vsConfigGeneration != generation) {
                        return;
                    }
                    if (self->m_pendingVsConfigReplies > 0 && --self->m_pendingVsConfigReplies == 0) {
                        self->m_virtualScreensReady = true;
                        if (self->m_daemonServiceRegistered) {
                            self->processDaemonReadyWindowState();
                        }
                    }
                };

                QDBusPendingReply<QString> reply = *w;
                if (reply.isError()) {
                    qCDebug(lcEffect) << "fetchVirtualScreenConfig: no virtual screens for" << physicalScreenId
                                      << reply.error().message();
                    self->m_virtualScreenDefs.remove(physicalScreenId);
                    countdownVsGate();
                    return;
                }

                const QString json = reply.value();
                QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
                if (!doc.isObject()) {
                    self->m_virtualScreenDefs.remove(physicalScreenId);
                    countdownVsGate();
                    return;
                }

                QJsonArray screens = doc.object().value(QLatin1String("screens")).toArray();

                // Look up the physical output geometry ONCE rather than per VS definition (O(N) vs O(N*M))
                QRect physGeom;
                const auto outputs = KWin::effects->screens();
                for (const auto* out : outputs) {
                    if (self->outputScreenId(out) == physicalScreenId) {
                        physGeom = out->geometry();
                        break;
                    }
                }

                if (!physGeom.isValid()) {
                    qCWarning(lcEffect) << "Physical output" << physicalScreenId
                                        << "not found (hot-unplug?) — skipping VS config update;"
                                        << "will re-fetch on reconnect";
                }

                QVector<EffectVirtualScreenDef> defs;
                for (const QJsonValue& val : screens) {
                    QJsonObject obj = val.toObject();
                    QJsonObject region = obj.value(QLatin1String("region")).toObject();

                    EffectVirtualScreenDef def;
                    def.id = obj.value(QLatin1String("id")).toString();

                    // Compute absolute geometry from fractional region within physical screen
                    if (physGeom.isValid()) {
                        qreal rx = region.value(QLatin1String("x")).toDouble();
                        qreal ry = region.value(QLatin1String("y")).toDouble();
                        qreal rw = region.value(QLatin1String("width")).toDouble();
                        qreal rh = region.value(QLatin1String("height")).toDouble();
                        // Edge-consistent rounding: compute edges then derive width/height
                        // to avoid 1px gaps between abutting virtual screens
                        int left = physGeom.x() + qRound(rx * physGeom.width());
                        int top = physGeom.y() + qRound(ry * physGeom.height());
                        int right = physGeom.x() + qRound((rx + rw) * physGeom.width());
                        int bottom = physGeom.y() + qRound((ry + rh) * physGeom.height());
                        def.geometry = QRect(left, top, right - left, bottom - top);
                    }

                    if (def.geometry.isValid() && !def.id.isEmpty()) {
                        defs.append(def);
                    }
                }

                if (defs.isEmpty()) {
                    self->m_virtualScreenDefs.remove(physicalScreenId);
                } else {
                    qCInfo(lcEffect) << "Loaded" << defs.size() << "virtual screens for" << physicalScreenId;
                    self->m_virtualScreenDefs.insert(physicalScreenId, defs);
                }

                // Re-resolve tracked screen IDs so stale virtual screen IDs
                // are replaced with IDs from the updated boundaries.
                for (auto it = self->m_trackedScreenPerWindow.begin(); it != self->m_trackedScreenPerWindow.end();
                     ++it) {
                    auto* window = it.key();
                    if (!window || window->isDeleted()) {
                        continue;
                    }
                    {
                        const QPointF cf = window->frameGeometry().center();
                        const QPoint center(qRound(cf.x()), qRound(cf.y()));
                        const QString newScreenId = self->resolveEffectiveScreenId(center, window->screen());
                        if (!newScreenId.isEmpty()) {
                            it.value() = newScreenId;
                            // Also update the autotile handler's notified screen map
                            // so slotWindowFrameGeometryChanged does not compare against
                            // the stale pre-config-change screen ID.
                            const QString windowId = self->getWindowId(window);
                            self->m_autotileHandler->updateNotifiedScreen(windowId, newScreenId);
                        }
                    }
                }

                countdownVsGate();

                // For live VS config changes (generation=0), re-enable VS crossing
                // detection now that boundary definitions are updated.
                // countdownVsGate skips for generation=0, so m_virtualScreensReady
                // must be restored here. For startup fetches (generation>0),
                // countdownVsGate already sets it when all screens are processed.
                if (generation == 0) {
                    self->m_virtualScreensReady = true;
                }
            });
}

void PlasmaZonesEffect::fetchAllVirtualScreenConfigs()
{
    const auto outputs = KWin::effects->screens();

    // Collect physical screen IDs in a single pass to avoid count/iterate race
    // (a screen removed between two loops would cause count and calls to diverge)
    QStringList physIds;
    for (const auto* output : outputs) {
        const QString physId = outputScreenId(output);
        if (!physId.isEmpty()) {
            physIds.append(physId);
        }
    }

    physIds.removeDuplicates();

    // Prune stale m_virtualScreenDefs entries for physical screens that are no
    // longer connected. Without this, resolveEffectiveScreenId could match against
    // geometry from a disconnected monitor.
    const QSet<QString> currentPhysIds(physIds.begin(), physIds.end());
    for (auto it = m_virtualScreenDefs.begin(); it != m_virtualScreenDefs.end();) {
        if (!currentPhysIds.contains(it.key()))
            it = m_virtualScreenDefs.erase(it);
        else
            ++it;
    }

    if (physIds.isEmpty()) {
        // No physical screens to query — gate opens immediately
        m_virtualScreensReady = true;
        m_pendingVsConfigReplies = 0;
        if (m_daemonServiceRegistered) {
            processDaemonReadyWindowState();
        }
        return;
    }

    // Bump generation so stale callbacks from prior fetches are ignored
    const uint64_t generation = ++m_vsConfigGeneration;
    m_pendingVsConfigReplies = physIds.size();
    m_virtualScreensReady = false;

    for (const QString& physId : physIds) {
        fetchVirtualScreenConfig(physId, generation);
    }
}

void PlasmaZonesEffect::onVirtualScreensChanged(const QString& physicalScreenId)
{
    qCInfo(lcEffect) << "Virtual screens changed for" << physicalScreenId;
    m_screenIdCache.clear();
    m_lastEffectiveScreenId.clear();
    // Temporarily disable VS-aware crossing detection while the async fetch is in-flight.
    // Without this, slotWindowFrameGeometryChanged uses stale boundary definitions from the
    // old config, potentially causing spurious VS crossing events during the D-Bus round-trip.
    m_virtualScreensReady = false;
    fetchVirtualScreenConfig(physicalScreenId); // generation=0, won't participate in startup gate
}

void PlasmaZonesEffect::emitNavigationFeedback(bool success, const QString& action, const QString& reason,
                                               const QString& sourceZoneId, const QString& targetZoneId,
                                               const QString& screenId)
{
    // Call D-Bus method on daemon to report navigation feedback (can't emit signals on another service's interface)
    if (!isDaemonReady("report navigation feedback")) {
        return;
    }
    fireAndForgetDBusCall(DBus::Interface::WindowTracking, QStringLiteral("reportNavigationFeedback"),
                          {success, action, reason, sourceZoneId, targetZoneId, screenId});
}

void PlasmaZonesEffect::slotActivateWindowRequested(const QString& windowId)
{
    KWin::EffectWindow* w = findWindowById(windowId);
    if (w) {
        KWin::effects->activateWindow(w);
    } else {
        qCDebug(lcEffect) << "slotActivateWindowRequested: window not found" << windowId;
    }
}

void PlasmaZonesEffect::slotMoveSpecificWindowToZoneRequested(const QString& windowId, const QString& zoneId,
                                                              const QString& geometryJson)
{
    QRect geometry = parseZoneGeometry(geometryJson);
    if (!geometry.isValid()) {
        qCWarning(lcEffect) << "slotMoveSpecificWindowToZoneRequested: invalid geometry" << geometryJson;
        return;
    }

    // Match by exact full window ID (appId|uuid) to distinguish
    // multiple windows of the same application. Fall back to appId only if
    // the exact match fails (e.g. window was recreated between candidate build
    // and selection).
    KWin::EffectWindow* targetWindow = nullptr;
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        if (w && shouldHandleWindow(w) && getWindowId(w) == windowId) {
            targetWindow = w;
            break;
        }
    }
    if (!targetWindow) {
        QString appId = extractAppId(windowId);
        for (KWin::EffectWindow* w : windows) {
            if (w && shouldHandleWindow(w) && extractAppId(getWindowId(w)) == appId) {
                targetWindow = w;
                break;
            }
        }
    }

    if (!targetWindow) {
        qCWarning(lcEffect) << "slotMoveSpecificWindowToZoneRequested: window not found" << windowId;
        emitNavigationFeedback(false, QStringLiteral("snap_assist"), QStringLiteral("window_not_found"));
        return;
    }

    // Capture geometry BEFORE applySnapGeometry resizes the window. The async D-Bus
    // callback in ensurePreSnapGeometryStored would read frameGeometry() after the
    // resize, corrupting the pre-tile entry with zone dimensions.
    ensurePreSnapGeometryStored(targetWindow, getWindowId(targetWindow), targetWindow->frameGeometry());
    applySnapGeometry(targetWindow, geometry);

    // Derive screen from the applied geometry center. Use resolveEffectiveScreenId
    // to get the virtual screen ID (not just the physical output).
    QPoint geoCenter = geometry.center();
    const auto* output = KWin::effects->screenAt(geoCenter);
    QString screenId = output ? resolveEffectiveScreenId(geoCenter, output) : getWindowScreenId(targetWindow);

    if (isDaemonReady("snap assist windowSnapped")) {
        fireAndForgetDBusCall(DBus::Interface::WindowTracking, QStringLiteral("windowSnapped"),
                              {getWindowId(targetWindow), zoneId, screenId});
        fireAndForgetDBusCall(DBus::Interface::WindowTracking, QStringLiteral("recordSnapIntent"),
                              {getWindowId(targetWindow), true});

        // Snap Assist continuation: only for manual-mode screens.
        // Autotile screens manage their own window placement; showing snap assist
        // after an autotile resnap is incorrect (the daemon silently ignores the
        // selection anyway via the isAutotileScreen guard in signals.cpp).
        if (!m_autotileHandler->isAutotileScreen(screenId)) {
            m_snapAssistHandler->showContinuationIfNeeded(screenId);
        }
    }
}

void PlasmaZonesEffect::slotToggleWindowFloatRequested(bool shouldFloat)
{
    Q_UNUSED(shouldFloat)
    KWin::EffectWindow* activeWindow = getValidActiveWindowOrFail(QStringLiteral("float"));
    if (!activeWindow) {
        return;
    }
    QString windowId = getWindowId(activeWindow);
    QString screenId = getWindowScreenId(activeWindow);

    // Store current geometry before the daemon processes the toggle.
    // D-Bus calls on the same connection are processed in order.
    //   - Floating → unfloat: capture floating position (overwrite=true)
    //   - Snapped/tiled → float: first-only (overwrite=false)
    QRectF frameGeo = activeWindow->frameGeometry();
    const bool floating = isWindowFloating(windowId);

    // Daemon handles the full flow: pre-snap geometry, zone bookkeeping,
    // emits applyGeometryRequested for the geometry change.
    fireAndForgetDBusCall(DBus::Interface::WindowTracking, QStringLiteral("storePreTileGeometry"),
                          {windowId, static_cast<int>(frameGeo.x()), static_cast<int>(frameGeo.y()),
                           static_cast<int>(frameGeo.width()), static_cast<int>(frameGeo.height()), screenId, floating},
                          QStringLiteral("storePreTileGeometry"));
    fireAndForgetDBusCall(DBus::Interface::WindowTracking, QStringLiteral("toggleFloatForWindow"), {windowId, screenId},
                          QStringLiteral("toggleFloatForWindow"));
}

void PlasmaZonesEffect::slotApplyGeometryRequested(const QString& windowId, const QString& geometryJson,
                                                   const QString& zoneId, const QString& screenId)
{
    KWin::EffectWindow* w = findWindowById(windowId);
    if (!w) {
        qCDebug(lcEffect) << "slotApplyGeometryRequested: window not found" << windowId;
        return;
    }

    // Check for size-only restore (drag-out unsnap without activation trigger).
    // The daemon sets sizeOnly=true to restore pre-snap width/height while keeping
    // the window at its current drop position.
    QJsonDocument doc = QJsonDocument::fromJson(geometryJson.toUtf8());
    QJsonObject geoObj = doc.object();
    if (geoObj.value(QLatin1String("sizeOnly")).toBool(false)) {
        int newWidth = geoObj.value(QLatin1String("width")).toInt();
        int newHeight = geoObj.value(QLatin1String("height")).toInt();
        if (newWidth > 0 && newHeight > 0) {
            QRectF currentFrame = w->frameGeometry();
            QRect sizeOnlyGeo(qRound(currentFrame.x()), qRound(currentFrame.y()), newWidth, newHeight);
            qCInfo(lcEffect) << "slotApplyGeometryRequested: size-only restore for" << windowId << newWidth << "x"
                             << newHeight;
            applySnapGeometry(w, sizeOnlyGeo);
        }
        return;
    }

    QRect geometry = parseZoneGeometry(geometryJson);
    if (!geometry.isValid()) {
        qCWarning(lcEffect) << "slotApplyGeometryRequested: invalid geometry" << geometryJson;
        return;
    }
    // Skip float-restore geometry on minimized windows: when a snapped window is minimized
    // we float it (to free the zone slot), but applying the pre-tile geometry while minimized
    // would poison what KWin restores to on unminimize, causing a visible flash of the
    // pre-snap geometry before the unfloat re-snaps to the zone.
    if (w->isMinimized() && zoneId.isEmpty()) {
        qCDebug(lcEffect) << "slotApplyGeometryRequested: skipping float-restore geometry on minimized window:"
                          << windowId;
        return;
    }
    // Skip float-restore geometry for drag-to-float: when the user drags a window
    // off the autotile layout, the daemon restores pre-autotile geometry. But the
    // user expects the window to stay where they dropped it, not snap back.
    if (zoneId.isEmpty() && m_dragFloatedWindowIds.remove(windowId)) {
        qCInfo(lcEffect) << "slotApplyGeometryRequested: skipping float-restore for drag-floated window:" << windowId;
        return;
    }
    qCInfo(lcEffect) << "slotApplyGeometryRequested:" << windowId << "geo:" << geometry << "zoneId:" << zoneId
                     << "screen:" << screenId << "floating:" << isWindowFloating(windowId)
                     << "currentFrame:" << w->frameGeometry();
    // Store pre-snap geometry before first snap (idempotent — skips if already stored).
    // The daemon handles windowSnapped/recordSnapIntent internally, but only the effect
    // knows the window's current frame geometry for pre-tile storage.
    if (!zoneId.isEmpty()) {
        // Capture frame geometry synchronously BEFORE applySnapGeometry moves the window.
        // ensurePreSnapGeometryStored is async (D-Bus hasPreTileGeometry check) — without
        // pre-capturing, the callback would read the post-move geometry instead of the
        // original free-floating position.
        ensurePreSnapGeometryStored(w, getWindowId(w), w->frameGeometry());
    }

    applySnapGeometry(w, geometry);
    // Note: windowSnapped/recordSnapIntent are NOT called here. For daemon-driven
    // navigation, the daemon handles zone bookkeeping internally before emitting
    // applyGeometryRequested. For legacy callers (autotile float restore via
    // applyGeometryForFloat), zoneId is empty so no snap confirmation is needed.
}

void PlasmaZonesEffect::slotApplyGeometriesBatch(const QString& batchJson, const QString& action)
{
    qCInfo(lcEffect) << "applyGeometriesBatch:" << action;

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(batchJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        qCWarning(lcEffect) << "applyGeometriesBatch: invalid JSON:" << parseError.errorString();
        return;
    }

    QJsonArray entries = doc.array();
    if (entries.isEmpty()) {
        return;
    }

    QHash<QString, KWin::EffectWindow*> windowMap = buildWindowMap();

    struct PendingApply
    {
        QPointer<KWin::EffectWindow> window;
        QRect geometry;
    };
    QVector<PendingApply> pending;

    for (const QJsonValue& value : entries) {
        if (!value.isObject()) {
            continue;
        }
        QJsonObject obj = value.toObject();
        QString windowId = obj[QLatin1String("windowId")].toString();
        int x = obj[QLatin1String("x")].toInt();
        int y = obj[QLatin1String("y")].toInt();
        int width = obj[QLatin1String("width")].toInt();
        int height = obj[QLatin1String("height")].toInt();

        if (windowId.isEmpty() || width <= 0 || height <= 0) {
            continue;
        }

        // Exact match first, appId fallback for single-instance apps
        KWin::EffectWindow* window = windowMap.value(windowId);
        if (!window) {
            QString appId = extractAppId(windowId);
            KWin::EffectWindow* candidate = nullptr;
            int matchCount = 0;
            for (auto it = windowMap.constBegin(); it != windowMap.constEnd(); ++it) {
                if (extractAppId(it.key()) == appId) {
                    candidate = it.value();
                    if (++matchCount > 1)
                        break;
                }
            }
            if (matchCount == 1) {
                window = candidate;
            }
        }

        if (!window) {
            continue;
        }

        PendingApply p;
        p.window = QPointer<KWin::EffectWindow>(window);
        p.geometry = QRect(x, y, width, height);
        pending.append(p);
    }

    if (pending.isEmpty()) {
        return;
    }

    // Note: ensurePreSnapGeometryStored is NOT called here. Batch operations (rotate, resnap)
    // move windows between zones — their pre-tile geometry is already stored from the original
    // snap. The daemon's processBatchEntries calls clearPreTileGeometry only for __restore__
    // entries (overflow windows). Calling ensurePreSnapGeometryStored here would race with
    // the daemon's clearPreTileGeometry and store the zone geometry as pre-tile, corrupting
    // the restore path on subsequent mode transitions.

    // Capture stacking order before applying geometries (moveResize raises on Wayland)
    const auto allWindows = KWin::effects->stackingOrder();
    QVector<QPointer<KWin::EffectWindow>> savedStack;
    for (KWin::EffectWindow* w : allWindows) {
        savedStack.append(QPointer<KWin::EffectWindow>(w));
    }

    applyStaggeredOrImmediate(
        pending.size(),
        [this, pending](int i) {
            const auto& p = pending[i];
            if (p.window) {
                applySnapGeometry(p.window, p.geometry);
            }
        },
        [this, savedStack, action]() {
            // Restore z-order after all geometries applied
            auto* ws = KWin::Workspace::self();
            if (ws) {
                for (const auto& wPtr : savedStack) {
                    if (wPtr && !wPtr->isDeleted()) {
                        KWin::Window* kw = wPtr->window();
                        if (kw) {
                            ws->raiseWindow(kw);
                        }
                    }
                }
            }
            // Show snap assist after resnap if applicable
            if (action == QLatin1String("resnap") && m_snapAssistHandler->isEnabled()) {
                KWin::EffectWindow* activeWin = getActiveWindow();
                QString activeScreenId = activeWin ? getWindowScreenId(activeWin) : QString();
                if (!activeScreenId.isEmpty() && !m_autotileHandler->isAutotileScreen(activeScreenId)) {
                    m_snapAssistHandler->showContinuationIfNeeded(activeScreenId);
                }
            }
        });
}

void PlasmaZonesEffect::slotRaiseWindowsRequested(const QStringList& windowIds)
{
    auto* ws = KWin::Workspace::self();
    if (!ws) {
        return;
    }

    for (const QString& windowId : windowIds) {
        KWin::EffectWindow* w = findWindowById(windowId);
        if (w && !w->isDeleted()) {
            KWin::Window* kw = w->window();
            if (kw) {
                ws->raiseWindow(kw);
            }
        }
    }
}

void PlasmaZonesEffect::slotSnapAllWindowsRequested(const QString& screenId)
{
    qCInfo(lcEffect) << "Snap all windows requested for screen:" << screenId;

    if (!isDaemonReady("snap all windows")) {
        return;
    }

    // Async fetch all snapped windows to filter already-snapped ones locally
    QDBusPendingCall snapCall = asyncMethodCall(DBus::Interface::WindowTracking, QStringLiteral("getSnappedWindows"));
    auto* snapWatcher = new QDBusPendingCallWatcher(snapCall, this);

    connect(snapWatcher, &QDBusPendingCallWatcher::finished, this, [this, screenId](QDBusPendingCallWatcher* sw) {
        sw->deleteLater();

        QDBusPendingReply<QStringList> snapReply = *sw;
        QSet<QString> snappedFullIds;
        QSet<QString> snappedAppIds;
        if (snapReply.isValid()) {
            for (const QString& id : snapReply.value()) {
                snappedFullIds.insert(id);
                snappedAppIds.insert(extractAppId(id));
            }
        }

        // Collect unsnapped, non-floating windows on this screen in stacking order
        // (bottom-to-top) so lower windows get lower-numbered zones deterministically
        QStringList unsnappedWindowIds;
        const auto windows = KWin::effects->stackingOrder();
        for (KWin::EffectWindow* w : windows) {
            if (!w || !shouldHandleWindow(w)) {
                continue;
            }

            QString windowId = getWindowId(w);
            QString appId = extractAppId(windowId);

            // User-initiated snap commands override floating state.
            // windowSnapped() on the daemon will clear floating via clearFloatingStateForSnap().

            // Always use EDID-based screen ID for comparison
            QString winScreen = getWindowScreenId(w);
            if (winScreen != screenId) {
                qCDebug(lcEffect) << "snap-all: skipping window on different screen" << appId;
                continue;
            }

            if (w->isMinimized() || !w->isOnCurrentDesktop() || !w->isOnCurrentActivity()) {
                qCDebug(lcEffect) << "snap-all: skipping minimized/other-desktop window" << appId;
                continue;
            }

            // Full ID match first (distinguishes multi-instance apps),
            // appId fallback for single-instance apps
            if (snappedFullIds.contains(windowId)) {
                qCDebug(lcEffect) << "snap-all: skipping already-snapped window" << appId;
                continue;
            }
            if (!hasOtherWindowOfClassWithDifferentPid(w) && snappedAppIds.contains(appId)) {
                qCDebug(lcEffect) << "snap-all: skipping already-snapped window (appId match)" << appId;
                continue;
            }

            unsnappedWindowIds.append(windowId);
        }

        qCDebug(lcEffect) << "snap-all: found" << unsnappedWindowIds.size() << "unsnapped windows to snap";

        if (unsnappedWindowIds.isEmpty()) {
            qCDebug(lcEffect) << "No unsnapped windows to snap on screen" << screenId;
            emitNavigationFeedback(false, QStringLiteral("snap_all"), QStringLiteral("no_unsnapped_windows"), QString(),
                                   QString(), screenId);
            return;
        }

        if (!isDaemonReady("snap all windows calculation")) {
            return;
        }

        // Ask daemon to calculate zone assignments
        QDBusPendingCall calcCall =
            asyncMethodCall(DBus::Interface::WindowTracking, QStringLiteral("calculateSnapAllWindows"),
                            {QVariant::fromValue(unsnappedWindowIds), screenId});
        auto* calcWatcher = new QDBusPendingCallWatcher(calcCall, this);

        connect(calcWatcher, &QDBusPendingCallWatcher::finished, this, [this, screenId](QDBusPendingCallWatcher* cw) {
            cw->deleteLater();

            QDBusPendingReply<QString> calcReply = *cw;
            if (calcReply.isError()) {
                qCWarning(lcEffect) << "calculateSnapAllWindows failed:" << calcReply.error().message();
                emitNavigationFeedback(false, QStringLiteral("snap_all"), QStringLiteral("calculation_error"),
                                       QString(), QString(), screenId);
                return;
            }

            QString snapData = calcReply.value();
            // Apply batch geometries using the daemon-driven batch path
            slotApplyGeometriesBatch(snapData, QStringLiteral("snap_all"));

            // Confirm snap assignments with daemon. calculateSnapAllWindows returns
            // serialized ZoneAssignmentEntry format ({windowId, targetZoneId, ...}), but
            // windowsSnappedBatch expects {windowId, zoneId, screenId}. Transform:
            if (isDaemonReady("snap-all confirmation")) {
                QJsonDocument snapDoc = QJsonDocument::fromJson(snapData.toUtf8());
                QJsonArray batchArr;
                for (const QJsonValue& val : snapDoc.array()) {
                    QJsonObject entry = val.toObject();
                    QJsonObject batchEntry;
                    batchEntry[QLatin1String("windowId")] = entry.value(QLatin1String("windowId"));
                    batchEntry[QLatin1String("zoneId")] = entry.value(QLatin1String("targetZoneId"));
                    batchEntry[QLatin1String("screenId")] = screenId;
                    batchEntry[QLatin1String("isRestore")] = false;
                    batchArr.append(batchEntry);
                }
                if (!batchArr.isEmpty()) {
                    QString batchJson = QString::fromUtf8(QJsonDocument(batchArr).toJson(QJsonDocument::Compact));
                    fireAndForgetDBusCall(DBus::Interface::WindowTracking, QStringLiteral("windowsSnappedBatch"),
                                          {batchJson});
                }
            }
        });
    });
}

void PlasmaZonesEffect::slotPendingRestoresAvailable()
{
    // If slotDaemonReady already dispatched snap restores for this daemon
    // session, skip — both signals fire during restart, and the second round
    // of moveResize() calls would disrupt the stacking order that the first
    // round carefully preserves via activateWindow(previouslyActive).
    if (m_daemonReadyRestoresDone) {
        qCInfo(lcEffect) << "Pending restores: already handled by slotDaemonReady, skipping";
        return;
    }

    qCInfo(lcEffect) << "Pending restores: retrying restoration for all visible windows";

    if (!isDaemonReady("pending restores")) {
        return;
    }

    // Use ASYNC batch call to get all tracked windows at once
    QDBusPendingCall pendingCall =
        asyncMethodCall(DBus::Interface::WindowTracking, QStringLiteral("getSnappedWindows"));
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();

        QDBusPendingReply<QStringList> reply = *w;
        QSet<QString> trackedAppIds;

        if (reply.isValid()) {
            // Extract app IDs from tracked windows for comparison
            const QStringList trackedWindows = reply.value();
            for (const QString& windowId : trackedWindows) {
                QString appId = extractAppId(windowId);
                if (!appId.isEmpty()) {
                    trackedAppIds.insert(appId);
                }
            }
            qCDebug(lcEffect) << "Got" << trackedAppIds.size() << "tracked windows from daemon";
        } else {
            qCWarning(lcEffect) << "Failed to get tracked windows:" << reply.error().message();
            // Continue anyway - will try to restore all windows (daemon will handle duplicates)
        }

        // Now iterate through all visible windows and restore untracked ones
        const auto windows = KWin::effects->stackingOrder();
        for (KWin::EffectWindow* window : windows) {
            if (!window || !shouldHandleWindow(window)) {
                continue;
            }

            // Skip minimized or invisible windows
            if (window->isMinimized() || !window->isOnCurrentDesktop() || !window->isOnCurrentActivity()) {
                continue;
            }

            // Check if this window is already tracked using local set lookup (O(1))
            QString windowId = getWindowId(window);
            QString appId = extractAppId(windowId);
            if (trackedAppIds.contains(appId)) {
                continue; // Already tracked
            }

            // Window is not tracked - try to restore it
            qCDebug(lcEffect) << "Retrying restoration for untracked window:" << windowId;
            callResolveWindowRestore(window);
        }
    });
}

void PlasmaZonesEffect::slotWindowFloatingChanged(const QString& windowId, bool isFloating, const QString& screenId)
{
    Q_UNUSED(screenId)
    // Update local floating cache when daemon notifies us of state changes
    // This keeps the effect's cache in sync with the daemon, preventing
    // inverted toggle behavior when a floating window is drag-snapped.
    // Uses full windowId for per-instance tracking (appId fallback in isWindowFloating).
    qCInfo(lcEffect) << "Floating state changed for" << windowId << "- isFloating:" << isFloating;
    m_navigationHandler->setWindowFloating(windowId, isFloating);
    // When a window is unfloated (tiled/snapped), clear the drag-float skip flag.
    // Without this, a subsequent float toggle's geometry restore would be skipped
    // because m_dragFloatedWindowIds still has the entry from the original drag.
    if (!isFloating) {
        m_dragFloatedWindowIds.remove(windowId);
    }
}

void PlasmaZonesEffect::slotWindowMinimizedChanged(KWin::EffectWindow* w)
{
    if (!w || !shouldHandleWindow(w) || !isTileableWindow(w)) {
        return;
    }
    const QString windowId = getWindowId(w);
    const QString screenId = getWindowScreenId(w);

    // Autotile handler handles its own screens — only handle snap-mode here
    if (m_autotileHandler->isAutotileScreen(screenId)) {
        return;
    }

    const bool minimized = w->isMinimized();

    if (minimized) {
        if (isWindowFloating(windowId)) {
            qCDebug(lcEffect) << "Snap: minimized already-floating window, skipping float:" << windowId;
            return;
        }
        m_minimizeFloatedWindows.insert(windowId);
    } else {
        if (!m_minimizeFloatedWindows.remove(windowId)) {
            qCDebug(lcEffect) << "Snap: unminimized window was not minimize-floated, skipping unfloat:" << windowId;
            return;
        }
    }

    qCInfo(lcEffect) << "Snap: window" << (minimized ? "minimized, floating:" : "unminimized, unfloating:") << windowId
                     << "on" << screenId;

    if (m_daemonServiceRegistered) {
        fireAndForgetDBusCall(DBus::Interface::WindowTracking, QStringLiteral("setWindowFloatingForScreen"),
                              {windowId, screenId, minimized}, QStringLiteral("setWindowFloatingForScreen"));
    }
}

void PlasmaZonesEffect::slotRunningWindowsRequested()
{
    qCInfo(lcEffect) << "Running windows requested by KCM";

    QJsonArray windowArray;
    QSet<QString> seenClasses;

    // Iterate in reverse (top-to-bottom) so deduplication keeps the topmost
    // window's caption per class, which is more useful to the user
    const auto windows = KWin::effects->stackingOrder();
    for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
        KWin::EffectWindow* w = *it;
        if (!w) {
            continue;
        }

        // Include all normal, non-special windows (relaxed filter for the picker)
        if (w->isSpecialWindow() || w->isDesktop() || w->isDock() || w->isSkipSwitcher() || w->isNotification()
            || w->isOnScreenDisplay() || w->isPopupWindow()) {
            continue;
        }

        QString windowClass = w->windowClass();
        if (windowClass.isEmpty()) {
            continue;
        }

        // Normalize X11 "resourceName resourceClass" to just resourceClass,
        // matching the format used by getWindowId() for app rule matching.
        int spaceIdx = windowClass.indexOf(QLatin1Char(' '));
        if (spaceIdx > 0) {
            windowClass = windowClass.mid(spaceIdx + 1);
        }

        // Deduplicate by windowClass (first seen = topmost due to reverse iteration)
        if (seenClasses.contains(windowClass)) {
            continue;
        }
        seenClasses.insert(windowClass);

        QString appName = deriveShortNameFromWindowClass(windowClass);
        if (appName.isEmpty()) {
            appName = windowClass;
        }

        QJsonObject obj;
        obj[QLatin1String("windowClass")] = windowClass;
        obj[QLatin1String("appName")] = appName;
        obj[QLatin1String("caption")] = w->caption();
        windowArray.append(obj);
    }

    QString jsonString = QString::fromUtf8(QJsonDocument(windowArray).toJson(QJsonDocument::Compact));
    qCDebug(lcEffect) << "Providing" << windowArray.size() << "running windows to daemon";

    // Send result back to daemon via D-Bus
    if (m_daemonServiceRegistered) {
        fireAndForgetDBusCall(DBus::Interface::Settings, QStringLiteral("provideRunningWindows"), {jsonString},
                              QStringLiteral("provideRunningWindows"));
    } else {
        qCWarning(lcEffect) << "provideRunningWindows: daemon not ready";
    }
}

bool PlasmaZonesEffect::borderActivated(KWin::ElectricBorder border)
{
    Q_UNUSED(border)
    // We no longer reserve edges, so this callback won't be triggered by our effect.
    // The daemon handles disabling Quick Tile via KWin config.
    return false;
}

void PlasmaZonesEffect::callResolveWindowRestore(KWin::EffectWindow* window, std::function<void()> onComplete)
{
    if (!window) {
        if (onComplete)
            onComplete();
        return;
    }

    if (!isDaemonReady("resolve window restore")) {
        if (onComplete)
            onComplete();
        return;
    }

    QString windowId = getWindowId(window);
    QString screenId = getWindowScreenId(window);
    bool sticky = isWindowSticky(window);

    QPointer<KWin::EffectWindow> safeWindow = window;

    // Single D-Bus call — daemon runs the full appRule → persisted → emptyZone → lastZone chain
    // skipAnimation=true: window is being restored to its snap position on startup/reopen,
    // so teleport directly instead of sliding from KWin's saved position.
    // storePreSnap=false: the window is already at its snap/zone position (from before
    // daemon restart or from KWin session restore), so its current frameGeometry is the
    // zone geometry — NOT the free-floating geometry. Storing it as pre-tile would cause
    // float toggle to restore to the zone geometry instead of the original free-floating position.
    tryAsyncSnapCall(DBus::Interface::WindowTracking, QStringLiteral("resolveWindowRestore"),
                     {windowId, screenId, sticky}, safeWindow, windowId, false, nullptr, nullptr,
                     /*skipAnimation=*/true, onComplete);
}

void PlasmaZonesEffect::callDragStarted(const QString& windowId, const QRectF& geometry)
{
    updateWindowStickyState(m_dragTracker->draggedWindow());

    // Use QDBusMessage::createMethodCall instead of QDBusInterface to avoid
    // synchronous D-Bus introspection. QDBusInterface's constructor blocks the
    // compositor thread (~25s timeout) if the daemon is registered but not yet
    // processing messages. QDBusMessage is purely local — no D-Bus communication
    // until asyncCall, which returns immediately.
    QDBusMessage msg = QDBusMessage::createMethodCall(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowDrag,
                                                      QStringLiteral("dragStarted"));
    msg << windowId << geometry.x() << geometry.y() << geometry.width() << geometry.height()
        << static_cast<int>(m_currentMouseButtons);
    QDBusConnection::sessionBus().asyncCall(msg);
}
bool PlasmaZonesEffect::isWindowSticky(KWin::EffectWindow* w) const
{
    return w && w->isOnAllDesktops();
}

void PlasmaZonesEffect::updateWindowStickyState(KWin::EffectWindow* w)
{
    if (!w || !m_daemonServiceRegistered) {
        return;
    }

    QString windowId = getWindowId(w);
    if (windowId.isEmpty()) {
        return;
    }

    bool sticky = isWindowSticky(w);
    // Use fire-and-forget instead of QDBusInterface to avoid synchronous D-Bus
    // introspection. slotWindowAdded → updateWindowStickyState fires for every
    // window during login; QDBusInterface creation blocks the compositor thread
    // for ~25s if the daemon hasn't entered app.exec() yet (daemonReady is
    // emitted before the event loop starts).
    fireAndForgetDBusCall(DBus::Interface::WindowTracking, QStringLiteral("setWindowSticky"), {windowId, sticky},
                          QStringLiteral("setWindowSticky"));
}

void PlasmaZonesEffect::callDragMoved(const QString& windowId, const QPointF& cursorPos, Qt::KeyboardModifiers mods,
                                      int mouseButtons)
{
    // Don't send manual zone drag updates when drag was started on an autotile screen.
    // Use captured flag (not live m_autotileScreens) for consistency with drag start/stop.
    if (m_dragBypassedForAutotile) {
        return;
    }

    // QDBusMessage::createMethodCall — purely local, no D-Bus introspection.
    // See callDragStarted() comment for rationale.
    QDBusMessage msg = QDBusMessage::createMethodCall(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowDrag,
                                                      QStringLiteral("dragMoved"));
    msg << windowId << static_cast<int>(cursorPos.x()) << static_cast<int>(cursorPos.y()) << static_cast<int>(mods)
        << mouseButtons;
    QDBusConnection::sessionBus().asyncCall(msg);
}

void PlasmaZonesEffect::callDragStopped(KWin::EffectWindow* window, const QString& windowId,
                                        const QString& snapDragStartScreenId)
{
    // Cursor position at release (from last poll during drag) - daemon uses this for release screen
    QPointF cursorAtRelease = m_dragTracker->lastCursorPos();

    // Modifiers: m_currentModifiers is updated by slotMouseChanged. When drag ends via forceEnd (LMB
    // release), modifiers reflect the state at that moment. When drag ends via poll (isUserMove went
    // false), we use the last slotMouseChanged state; modifier released just before mouse may be stale.
    // This is acceptable for Snap Assist triggers - best-effort detection.

    // QDBusMessage::createMethodCall — purely local, no D-Bus introspection.
    // See callDragStarted() comment for rationale.
    QDBusMessage msg = QDBusMessage::createMethodCall(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowDrag,
                                                      QStringLiteral("dragStopped"));
    msg << windowId << static_cast<int>(cursorAtRelease.x()) << static_cast<int>(cursorAtRelease.y())
        << static_cast<int>(m_currentModifiers) << static_cast<int>(m_currentMouseButtons);
    QDBusPendingCall pendingCall = QDBusConnection::sessionBus().asyncCall(msg);

    // Use QPointer to safely handle window destruction during async call
    QPointer<KWin::EffectWindow> safeWindow = window;

    // Create watcher to handle the reply
    QDBusPendingCallWatcher* watcher = new QDBusPendingCallWatcher(pendingCall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, safeWindow, windowId, snapDragStartScreenId](QDBusPendingCallWatcher* w) {
                w->deleteLater();

                QDBusPendingReply<int, int, int, int, bool, QString, bool, bool, QString> reply = *w;
                if (reply.isError()) {
                    qCWarning(lcEffect) << "dragStopped call failed:" << reply.error().message();
                    return;
                }

                int snapX = reply.argumentAt<0>();
                int snapY = reply.argumentAt<1>();
                int snapWidth = reply.argumentAt<2>();
                int snapHeight = reply.argumentAt<3>();
                bool shouldSnap = reply.argumentAt<4>();
                QString releaseScreenId = reply.argumentAt<5>();
                bool restoreSizeOnly = reply.argumentAt<6>();
                bool snapAssistRequested = reply.argumentAt<7>();
                QString emptyZonesJson = reply.argumentAt<8>();

                qCInfo(lcEffect) << "dragStopped returned shouldSnap=" << shouldSnap
                                 << "releaseScreenId=" << releaseScreenId << "restoreSizeOnly=" << restoreSizeOnly
                                 << "geometry=" << QRect(snapX, snapY, snapWidth, snapHeight);

                if (shouldSnap && safeWindow) {
                    // Final fullscreen check before applying geometry - window could have
                    // transitioned to fullscreen between drag stop and this point
                    if (safeWindow->isFullScreen()) {
                        qCDebug(lcEffect) << "Window is fullscreen at drag stop, skipping snap";
                    } else {
                        QRect snapGeometry;
                        bool shouldApply = true;
                        if (restoreSizeOnly) {
                            // Drag-to-unsnap: apply only pre-snap width/height, keep current position
                            QRectF frame = safeWindow->frameGeometry();
                            snapGeometry =
                                QRect(static_cast<int>(frame.x()), static_cast<int>(frame.y()), snapWidth, snapHeight);
                            // Skip if already restored during drag (slotRestoreSizeDuringDrag) to avoid redundant
                            // moveResize
                            if (qAbs(frame.width() - snapWidth) <= 1 && qAbs(frame.height() - snapHeight) <= 1) {
                                shouldApply = false;
                                qCDebug(lcEffect)
                                    << "restore apply: already at correct size from during-drag restore, skipping";
                            }
                        } else {
                            snapGeometry = QRect(snapX, snapY, snapWidth, snapHeight);
                        }
                        if (shouldApply) {
                            // If the window is still in user-move state because only the
                            // activation mouse button is held (LMB already released),
                            // cancel KWin's interactive move so we can snap immediately.
                            // Without this, applySnapGeometry defers (100 ms retry) until
                            // ALL buttons are released, causing a noticeable delay when
                            // using a mouse button (e.g. RMB) for zone activation.
                            if (safeWindow->isUserMove() && !(m_currentMouseButtons & Qt::LeftButton)) {
                                KWin::Window* kw = safeWindow->window();
                                if (kw) {
                                    qCDebug(lcEffect) << "Cancelling interactive move"
                                                         " (activation button held, LMB released)";
                                    kw->cancelInteractiveMoveResize();
                                }
                            }
                            applySnapGeometry(safeWindow, snapGeometry);
                        }
                    }
                }

                // Auto-fill: if window was dropped without snapping to a zone, try snapping to
                // the first empty zone on the release screen (where the user released the drag).
                // Use daemon-provided releaseScreenId (cursor position), not window's current
                // screen - after cross-screen drag the window may still report the old screen.
                if (!shouldSnap && safeWindow && !releaseScreenId.isEmpty() && isDaemonReady("auto-fill on drop")) {
                    bool sticky = isWindowSticky(safeWindow);
                    auto onSnapSuccess = [this](const QString&, const QString& snappedScreenId) {
                        m_snapAssistHandler->showContinuationIfNeeded(snappedScreenId);
                    };
                    tryAsyncSnapCall(DBus::Interface::WindowTracking, QStringLiteral("snapToEmptyZone"),
                                     {windowId, releaseScreenId, sticky}, safeWindow, windowId, true, nullptr,
                                     onSnapSuccess);
                }

                // Snap Assist: if daemon requested, build candidates (unsnapped only) and call showSnapAssist.
                // All D-Bus calls are async to prevent compositor freeze if daemon is busy with
                // overlay teardown / layout change (see discussion #158).
                if (snapAssistRequested && !emptyZonesJson.isEmpty() && !releaseScreenId.isEmpty()) {
                    m_snapAssistHandler->asyncShow(windowId, releaseScreenId, emptyZonesJson);
                }

                // Cross-VS autotile transfer: if the window was dropped on an autotile
                // virtual screen from a different VS on the SAME physical monitor,
                // add it to autotile. KWin's outputChanged won't fire (same physical
                // monitor), so the autotile handler doesn't see the transfer.
                // Use m_dragBypassScreenId for autotile-bypass drags, or the captured
                // snapDragStartScreenId for snap-mode drags (m_snapDragStartScreenId
                // is already cleared by the time this async callback runs).
                if (safeWindow && !releaseScreenId.isEmpty() && m_autotileHandler->isAutotileScreen(releaseScreenId)) {
                    const QString oldScreenId =
                        !m_dragBypassScreenId.isEmpty() ? m_dragBypassScreenId : snapDragStartScreenId;
                    if (!oldScreenId.isEmpty() && VirtualScreenId::samePhysical(releaseScreenId, oldScreenId)) {
                        m_autotileHandler->notifyWindowAdded(safeWindow);
                        // The window was floating (snap-dragged from a non-autotile VS) —
                        // keep it floating on the destination. notifyWindowAdded tiles the
                        // window, so immediately restore its floating state and size.
                        m_autotileHandler->handleDragToFloat(safeWindow, windowId, releaseScreenId);
                        m_dragFloatedWindowIds.insert(windowId);
                        fireAndForgetDBusCall(DBus::Interface::WindowTracking,
                                              QStringLiteral("setWindowFloatingForScreen"),
                                              {windowId, releaseScreenId, true},
                                              QStringLiteral("setWindowFloatingForScreen - snap→autotile VS crossing"));
                        qCInfo(lcEffect) << "Snap→autotile cross-VS drag-to-float:" << windowId << oldScreenId << "->"
                                         << releaseScreenId;
                    }
                }
            });
}

void PlasmaZonesEffect::callCancelSnap()
{
    qCInfo(lcEffect) << "Calling cancelSnap (drag cancelled by Escape or external event)";
    // QDBusMessage::createMethodCall — purely local, no D-Bus introspection.
    QDBusMessage msg = QDBusMessage::createMethodCall(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::WindowDrag,
                                                      QStringLiteral("cancelSnap"));
    QDBusConnection::sessionBus().asyncCall(msg);
}

void PlasmaZonesEffect::tryAsyncSnapCall(const QString& interface, const QString& method, const QList<QVariant>& args,
                                         QPointer<KWin::EffectWindow> window, const QString& windowId,
                                         bool storePreSnap, std::function<void()> fallback,
                                         std::function<void(const QString&, const QString&)> onSnapSuccess,
                                         bool skipAnimation, std::function<void()> onComplete)
{
    QDBusPendingCall call = asyncMethodCall(interface, method, args);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, window, windowId, storePreSnap, method, fallback, onSnapSuccess, args, skipAnimation,
             onComplete](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                QDBusPendingReply<int, int, int, int, bool> reply = *w;
                if (reply.isError()) {
                    qCDebug(lcEffect) << method << "error:" << reply.error().message();
                    if (fallback)
                        fallback();
                    if (onComplete)
                        onComplete();
                    return;
                }
                if (reply.argumentAt<4>() && window) {
                    QRect geo(reply.argumentAt<0>(), reply.argumentAt<1>(), reply.argumentAt<2>(),
                              reply.argumentAt<3>());
                    qCInfo(lcEffect) << method << "snapping" << windowId << "to:" << geo;
                    if (storePreSnap)
                        ensurePreSnapGeometryStored(window, windowId, window ? window->frameGeometry() : QRectF());
                    applySnapGeometry(window, geo, false, skipAnimation);
                    // args[1] is screenId (e.g. for snapToEmptyZone, snapToLastZone)
                    if (onSnapSuccess && args.size() >= 2) {
                        onSnapSuccess(windowId, args[1].toString());
                    }
                    if (onComplete)
                        onComplete();
                    return;
                }
                if (fallback)
                    fallback();
                if (onComplete)
                    onComplete();
            });
}

void PlasmaZonesEffect::repaintSnapRegions(KWin::EffectWindow* window, const QRectF& oldFrame, const QRect& newGeo)
{
    window->addRepaintFull();
    if (oldFrame.isValid()) {
        KWin::effects->addRepaint(oldFrame.toAlignedRect());
    }
    KWin::effects->addRepaint(newGeo);
}

void PlasmaZonesEffect::applySnapGeometry(KWin::EffectWindow* window, const QRect& geometry, bool allowDuringDrag,
                                          bool skipAnimation)
{
    if (!window) {
        qCWarning(lcEffect) << "applyGeometry: window is null";
        return;
    }

    // Normalize so width/height are non-negative; reject invalid rects
    QRect geo = geometry.normalized();
    if (!geo.isValid() || geo.width() <= 0 || geo.height() <= 0) {
        qCWarning(lcEffect) << "applyGeometry: invalid or empty geometry:" << geometry;
        return;
    }

    // Don't call moveResize() on fullscreen windows, it can crash KWin.
    // See KDE bugs #429752, #301529, #489546.
    if (window->isFullScreen()) {
        qCDebug(lcEffect) << "applyGeometry: window is fullscreen, skipping";
        return;
    }

    // For X11/XWayland windows, KWin constrains the frame size to align with
    // WM_SIZE_HINTS (size increments for terminals like Ghostty, Kitty, etc.).
    // Pre-compute the constrained size and center the window in its zone so the
    // gap is distributed evenly instead of all at the bottom-right.
    // This applies to all snap operations (zone snap, autotile, resnap, etc.).
    // Wayland-native clients negotiate size async (constrainFrameSize only
    // checks min/max, not char-cell grid), so they're handled by the deferred
    // check in slotWindowFrameGeometryChanged().
    if (window->isX11Client()) {
        KWin::Window* kw = window->window();
        if (kw) {
            const QSizeF constrained = kw->constrainFrameSize(QSizeF(geo.size()));
            const int cw = qRound(constrained.width());
            const int ch = qRound(constrained.height());
            if (cw < geo.width() || ch < geo.height()) {
                // Clamp to non-negative: if min-size exceeds the zone in one
                // dimension, don't shift the window beyond the zone's edge.
                const int dx = qMax(0, geo.width() - cw) / 2;
                const int dy = qMax(0, geo.height() - ch) / 2;
                geo = QRect(geo.x() + dx, geo.y() + dy, cw, ch);
                qCDebug(lcEffect) << "Pre-centered X11 window with size constraints:"
                                  << "zone=" << geometry.size() << "constrained=" << constrained << "adjusted=" << geo;
            }
        }
    }

    // Skip no-op: if window is already at the target geometry, calling
    // moveResize() is redundant and can have subtle stacking side effects
    // on some KWin versions (e.g. during daemon restart double-processing).
    if (QRectF(geo) == window->frameGeometry()) {
        qCDebug(lcEffect) << "moveResize: window already at target geometry, skipping:" << geo;
        return;
    }

    qCDebug(lcEffect) << "Setting window geometry from" << window->frameGeometry() << "to" << geo;

    // Capture old frame before moveResize for repaint region
    const QRectF oldFrame = window->frameGeometry();

    // In KWin 6, we use the window's moveResize methods
    // When allowDuringDrag is false: defer if window is in user move/resize (snap on release)
    // When allowDuringDrag is true: apply immediately (snap-on-hover during drag)
    if (!allowDuringDrag && (window->isUserMove() || window->isUserResize())) {
        qCDebug(lcEffect) << "Window in user move/resize, deferring geometry via windowFinishUserMovedResized";
        QPointer<KWin::EffectWindow> safeWindow = window;
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(window, &KWin::EffectWindow::windowFinishUserMovedResized, this,
                        [this, safeWindow, geo, skipAnimation, conn](KWin::EffectWindow*) {
                            disconnect(*conn);
                            if (safeWindow && !safeWindow->isDeleted() && !safeWindow->isFullScreen()) {
                                applySnapGeometry(safeWindow, geo, false, skipAnimation);
                            }
                        });
        return;
    }

    // Animation: moveResize to the final geometry immediately, then morph
    // the window visually from its old position/size to the new one using
    // translate + scale in paintWindow(). This follows the standard KDE
    // effect pattern — effects are visual overlays, never per-frame moveResize.
    QPointF animStartPos;
    QSizeF animStartSize;
    if (!skipAnimation && !allowDuringDrag && m_windowAnimator->isEnabled()) {
        if (m_windowAnimator->hasAnimation(window)) {
            if (m_windowAnimator->isAnimatingToTarget(window, geo)) {
                return; // Already animating to this target
            }
            // Capture current visual state before changing anything (mid-flight redirect)
            animStartPos = m_windowAnimator->currentVisualPosition(window);
            animStartSize = m_windowAnimator->currentVisualSize(window);
            m_windowAnimator->removeAnimation(window);
        } else {
            animStartPos = oldFrame.topLeft();
            animStartSize = oldFrame.size();
        }

        // Apply final geometry immediately — client starts re-rendering at new size
        KWin::Window* kw = window->window();
        if (kw) {
            kw->moveResize(QRectF(geo));
        }

        // Start animation from old visual state to new geometry
        m_windowAnimator->startAnimation(window, animStartPos, animStartSize, geo);

        repaintSnapRegions(window, oldFrame, geo);
        return;
    }

    // No animation path (disabled, during drag, etc.): apply moveResize directly.
    if (m_windowAnimator->hasAnimation(window)) {
        m_windowAnimator->removeAnimation(window);
    }

    KWin::Window* kwinWindow = window->window();
    if (kwinWindow) {
        qCInfo(lcEffect) << "moveResize: QRect=" << geo << "-> QRectF=" << QRectF(geo);
        kwinWindow->moveResize(QRectF(geo));

        repaintSnapRegions(window, oldFrame, geo);
    } else {
        qCWarning(lcEffect) << "Cannot get underlying Window from EffectWindow";
    }
}

QString PlasmaZonesEffect::extractAppId(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return windowId;
    }
    int sep = windowId.indexOf(QLatin1Char('|'));
    return (sep > 0) ? windowId.left(sep) : windowId;
}

QString PlasmaZonesEffect::deriveShortNameFromWindowClass(const QString& windowClass)
{
    if (windowClass.isEmpty()) {
        return QString();
    }
    // Handle reverse-DNS: org.kde.dolphin -> dolphin
    int dotIdx = windowClass.lastIndexOf(QLatin1Char('.'));
    if (dotIdx >= 0 && dotIdx < windowClass.length() - 1) {
        return windowClass.mid(dotIdx + 1);
    }
    return windowClass;
}

void PlasmaZonesEffect::slotRestoreSizeDuringDrag(const QString& windowId, int width, int height)
{
    // Restore pre-snap size when cursor leaves zone during drag. The window may have been
    // snapped when the drag started (at zone size); when the user drags out of all zones,
    // we restore to floated state immediately so they see the window return to original size.
    // This complements the release path (dragStopped) which also handles restore.
    if (!m_dragTracker->isDragging() || m_dragTracker->draggedWindowId() != windowId) {
        return;
    }

    KWin::EffectWindow* window = m_dragTracker->draggedWindow();
    if (!window || !shouldHandleWindow(window)) {
        return;
    }

    if (width <= 0 || height <= 0) {
        return;
    }

    // Restore-size-only: keep current position, apply pre-snap width/height
    QRectF frame = window->frameGeometry();
    QRect geometry(static_cast<int>(frame.x()), static_cast<int>(frame.y()), width, height);

    qCDebug(lcEffect) << "Restoring size during drag:" << windowId << geometry;
    applySnapGeometry(window, geometry, true);
}

void PlasmaZonesEffect::notifyWindowClosed(KWin::EffectWindow* w)
{
    if (!w) {
        return;
    }

    const QString windowId = getWindowId(w);

    if (!isDaemonReady("notify windowClosed")) {
        return;
    }

    qCInfo(lcEffect) << "Notifying daemon: windowClosed" << windowId;
    fireAndForgetDBusCall(DBus::Interface::WindowTracking, QStringLiteral("windowClosed"), {windowId});
}

void PlasmaZonesEffect::notifyWindowActivated(KWin::EffectWindow* w)
{
    if (!w) {
        return;
    }

    // Skip non-manageable window types but NOT user-excluded apps — the daemon
    // must always know which window is active so that keyboard shortcuts can
    // correctly skip excluded windows instead of operating on a stale
    // m_lastActiveWindowId.
    const QString windowClass = w->windowClass();
    if (windowClass.contains(QLatin1String("plasmazonesd"), Qt::CaseInsensitive)
        || windowClass.contains(QLatin1String("plasmazones-editor"), Qt::CaseInsensitive)) {
        return;
    }
    if (windowClass.contains(QLatin1String("xdg-desktop-portal"), Qt::CaseInsensitive)) {
        return;
    }
    if (w->isSpecialWindow() || w->isDesktop() || w->isDock() || w->isFullScreen() || w->isSkipSwitcher()
        || w->isDialog() || w->isUtility() || w->isSplash() || w->isNotification() || w->isOnScreenDisplay()
        || w->isModal() || w->isPopupWindow()) {
        return;
    }

    if (!isDaemonReady("notify windowActivated")) {
        return;
    }

    QString windowId = getWindowId(w);
    QString screenId = getWindowScreenId(w);

    qCDebug(lcEffect) << "Notifying daemon: windowActivated" << windowId << "on screen" << screenId;
    fireAndForgetDBusCall(DBus::Interface::WindowTracking, QStringLiteral("windowActivated"), {windowId, screenId});

    // Notify autotile engine of focus change so m_windowToScreen is updated
    if (m_autotileHandler->isAutotileScreen(screenId)) {
        fireAndForgetDBusCall(DBus::Interface::Autotile, QStringLiteral("notifyWindowFocused"), {windowId, screenId},
                              QStringLiteral("notifyWindowFocused"));
    }
}

KWin::EffectWindow* PlasmaZonesEffect::findWindowById(const QString& windowId) const
{
    if (windowId.isEmpty()) {
        return nullptr;
    }

    // O(1) exact match via reverse cache
    auto it = m_windowIdReverse.constFind(windowId);
    if (it != m_windowIdReverse.constEnd() && it.value() && !it.value()->isDeleted()) {
        return it.value();
    }

    // Fallback: appId-based fuzzy match (for cross-session restore where
    // the UUID portion changed but the appId is the same)
    const QString targetAppId = extractAppId(windowId);
    KWin::EffectWindow* appMatch = nullptr;
    int matchCount = 0;

    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        const QString wId = getWindowId(w);
        if (extractAppId(wId) == targetAppId) {
            appMatch = w;
            ++matchCount;
        }
    }

    // Only use appId fallback when unambiguous (single instance of that app)
    return (matchCount == 1) ? appMatch : nullptr;
}

QVector<KWin::EffectWindow*> PlasmaZonesEffect::findAllWindowsById(const QString& windowId) const
{
    QVector<KWin::EffectWindow*> out;
    if (windowId.isEmpty()) {
        return out;
    }
    const QString targetAppId = extractAppId(windowId);
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        const QString wId = getWindowId(w);
        if (wId == windowId) {
            // Exact match — discard any appId matches accumulated from earlier
            // windows in the stacking order. Without this clear, a second instance
            // of the same app (same appId) triggers the disambiguation path in
            // slotWindowsTileRequested, which can assign the wrong EffectWindow to
            // the tile entry — leaving the new window untiled.
            return {w};
        }
        if (extractAppId(wId) == targetAppId) {
            out.append(w);
        }
    }
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Per-window borders (native OutlinedBorderItem)
// ═══════════════════════════════════════════════════════════════════════════════

void PlasmaZonesEffect::removeWindowBorder(const QString& windowId)
{
    auto it = m_windowBorders.find(windowId);
    if (it == m_windowBorders.end()) {
        return;
    }
    WindowBorder& wb = it.value();
    if (wb.clippedSurface) {
        wb.clippedSurface->setBorderRadius(wb.savedSurfaceRadius);
    }
    // QPointer: item may already be null if Qt parent-child ownership destroyed it
    delete wb.item.data();
    QObject::disconnect(wb.geometryConnection);
    m_windowBorders.erase(it);
}

void PlasmaZonesEffect::clearAllBorders()
{
    while (!m_windowBorders.isEmpty()) {
        removeWindowBorder(m_windowBorders.begin().key());
    }
}

void PlasmaZonesEffect::updateWindowBorder(const QString& windowId, KWin::EffectWindow* w)
{
    // Remove existing border for this window first
    removeWindowBorder(windowId);

    const int bw = m_autotileHandler->borderWidth();
    if (bw <= 0) {
        return;
    }

    if (!w || w->isMinimized() || w->isFullScreen()) {
        return;
    }

    if (!m_autotileHandler->shouldShowBorderForWindow(windowId)) {
        return;
    }

    // Choose color: active for focused window, inactive for others
    KWin::EffectWindow* active = KWin::effects->activeWindow();
    const bool isFocused = (w == active);
    const QColor bc = isFocused ? m_autotileHandler->borderColor() : m_autotileHandler->inactiveBorderColor();
    if (!bc.isValid() || bc.alpha() == 0) {
        return;
    }

    // The OutlinedBorderItem draws the border OUTSIDE the innerRect, but the
    // parent WindowItem clips children to the window frame.  Inset the innerRect
    // by borderWidth so the border draws fully inside the frame (no clipping).
    const QRectF frame = w->frameGeometry();
    const KWin::RectF innerRect(bw, bw, frame.width() - 2.0 * bw, frame.height() - 2.0 * bw);
    const int br = m_autotileHandler->borderRadius();
    const KWin::BorderOutline outline(bw, bc, KWin::BorderRadius(br));

    KWin::WindowItem* windowItem = w->windowItem();
    if (!windowItem) {
        return;
    }

    WindowBorder wb;
    wb.item = new KWin::OutlinedBorderItem(innerRect, outline, windowItem);

    // For borderless windows, clip the SurfaceItem corners to match the border radius.
    if (br > 0 && m_autotileHandler->isBorderlessWindow(windowId)) {
        KWin::SurfaceItem* surface = windowItem->surfaceItem();
        if (surface) {
            wb.savedSurfaceRadius = surface->borderRadius();
            surface->setBorderRadius(KWin::BorderRadius(br));
            wb.clippedSurface = surface;
        }
    }

    // Keep the border in sync when the window resizes or moves.
    const QString wid = windowId; // capture by value
    wb.geometryConnection =
        connect(w, &KWin::EffectWindow::windowFrameGeometryChanged, this,
                [this, wid, bw](KWin::EffectWindow* ew, const QRectF& /*oldGeo*/) {
                    auto it = m_windowBorders.find(wid);
                    if (it != m_windowBorders.end() && it->item) {
                        const QRectF f = ew->frameGeometry();
                        it->item->setInnerRect(KWin::RectF(bw, bw, f.width() - 2.0 * bw, f.height() - 2.0 * bw));
                    }
                });

    m_windowBorders.insert(windowId, wb);
}

void PlasmaZonesEffect::updateAllBorders()
{
    clearAllBorders();

    const int bw = m_autotileHandler->borderWidth();
    if (bw <= 0) {
        return;
    }

    // Iterate all effect windows and create borders for tiled ones
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        if (!w || w->isDeleted() || !w->isOnCurrentDesktop()) {
            continue;
        }
        const QString wid = getWindowId(w);
        if (m_autotileHandler->shouldShowBorderForWindow(wid)) {
            updateWindowBorder(wid, w);
        }
    }
}

void PlasmaZonesEffect::prePaintScreen(KWin::ScreenPrePaintData& data, std::chrono::milliseconds presentTime)
{
    // Update animation progress from presentTime and clean up completed ones
    m_windowAnimator->advanceAnimations(presentTime);

    if (m_windowAnimator->hasActiveAnimations()) {
        // Windows have translation transforms that move them outside their
        // frame geometry bounds — force full compositing mode.
        data.mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS;
    }

    KWin::effects->prePaintScreen(data, presentTime);
}

void PlasmaZonesEffect::postPaintScreen()
{
    // Schedule targeted repaints for active animations instead of full-screen
    m_windowAnimator->scheduleRepaints();
    KWin::effects->postPaintScreen();
}

void PlasmaZonesEffect::prePaintWindow(KWin::RenderView* view, KWin::EffectWindow* w, KWin::WindowPrePaintData& data,
                                       std::chrono::milliseconds presentTime)
{
    if (w && m_windowAnimator->hasAnimation(w)) {
        // Mark as transformed so paintWindow applies our translate+scale
        data.setTransformed();
    }

    KWin::effects->prePaintWindow(view, w, data, presentTime);
}

void PlasmaZonesEffect::paintWindow(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport,
                                    KWin::EffectWindow* w, int mask, const KWin::Region& deviceRegion,
                                    KWin::WindowPaintData& data)
{
    m_windowAnimator->applyTransform(w, data);
    KWin::effects->paintWindow(renderTarget, viewport, w, mask, deviceRegion, data);
}

} // namespace PlasmaZones

// KWin Effect Factory - creates the plugin
#include <effect/effect.h>

namespace KWin {

KWIN_EFFECT_FACTORY_SUPPORTED(PlasmaZones::PlasmaZonesEffect, "metadata.json",
                              return PlasmaZones::PlasmaZonesEffect::supported();)

} // namespace KWin

// MOC include - REQUIRED for the Q_OBJECT in KWIN_EFFECT_FACTORY_SUPPORTED
#include "plasmazoneseffect.moc"
