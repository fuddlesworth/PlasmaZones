// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../EditorController.h"
#include "../EditorGapsModel.h"
#include "../services/ILayoutService.h"
#include "../services/ZoneManager.h"
#include "../undo/UndoController.h"
#include "../../common/daemonscreenscale.h"
#include "core/types/constants.h"
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutUtils.h>
#include "core/platform/logging.h"
#include "core/utils/utils.h"
#include <PhosphorIdentity/VirtualScreenId.h>

#include "phosphor_i18n.h"
#include <memory>
#include <utility>
#include <QDBusMessage>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <QUuid>
#include <QtMath>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

// ---------------------------------------------------------------------------
// Group 1 - Screen targeting
// ---------------------------------------------------------------------------

void EditorController::cacheVirtualScreenGeometry(const QString& screenName)
{
    m_virtualScreenSize = QSize();
    m_virtualScreenRect = QRect();
    if (!PhosphorIdentity::VirtualScreenId::isVirtual(screenName)) {
        return;
    }
    const QDBusMessage reply = PhosphorProtocol::ClientHelpers::syncCall(
        PhosphorProtocol::Service::Interface::Screen, QStringLiteral("getScreenGeometry"), {screenName});
    if (reply.type() == QDBusMessage::ReplyMessage && reply.arguments().size() >= 1) {
        QRect geo = qdbus_cast<QRect>(reply.arguments().at(0));
        if (geo.isValid()) {
            m_virtualScreenSize = geo.size();
            QString physId = PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenName);
            QScreen* physScreen = PhosphorScreens::ScreenIdentity::findByIdOrName(physId);
            QPoint physOrigin = physScreen ? physScreen->geometry().topLeft() : QPoint();
            m_virtualScreenRect = QRect(geo.topLeft() - physOrigin, geo.size());
            qCDebug(lcEditor) << "Virtual screen" << screenName << "geometry:" << geo
                              << "relative rect:" << m_virtualScreenRect;
        }
    }
    if (!m_virtualScreenSize.isValid()) {
        // Without the daemon's answer the reference falls back to the whole
        // physical monitor (targetLogicalScreenSizeF), which is the closest
        // size the editor can know but not the virtual screen's own.
        qCWarning(lcEditor) << "Virtual screen" << screenName
                            << "geometry unavailable from the daemon; using its physical monitor's size";
    }
}

void EditorController::ensureTargetScreenScale()
{
    if (m_targetScreenScale > 0.0) {
        return;
    }
    // No target means the primary screen, the same fallback
    // targetLogicalScreenSizeF() measures.
    QString screenId = m_targetScreen;
    if (screenId.isEmpty()) {
        if (QScreen* primary = QGuiApplication::primaryScreen()) {
            screenId = PhosphorScreens::ScreenIdentity::identifierFor(primary);
        }
    }
    m_targetScreenScale = queryDaemonScreenScale(screenId, &m_targetScreenPixelSize);
    if (m_targetScreenScale <= 0.0) {
        // Left unknown, so the next trigger (a screen or geometry change, or
        // the daemon registering on the bus) asks again. Until then the
        // device-pixel numbers are drawn as logical ones.
        qCWarning(lcEditor) << "Could not read the scale of" << screenId
                            << "from the daemon; fixed sizes in device pixels are drawn unscaled until it answers";
    }
}

QVariantList EditorController::screenModel() const
{
    if (!m_availableScreenIds.isEmpty()) {
        return m_screenModel;
    }

    // Fallback: use Qt's physical screens (editor opened before daemon responded)
    QVariantList model;
    for (QScreen* screen : QGuiApplication::screens()) {
        QVariantMap entry;
        entry[QStringLiteral("name")] = PhosphorScreens::ScreenIdentity::identifierFor(screen);
        entry[QStringLiteral("displayName")] = screen->name();
        model.append(entry);
    }
    return model;
}

void EditorController::refreshAvailableScreens()
{
    QStringList screenIds;
    // Screen IDs (stable EDID-based identifiers). getAllScreenAssignments
    // only emits screens with stored entries — use the dedicated
    // enumeration method so freshly-configured systems with no per-screen
    // assignments still populate the editor's screen list.
    const QDBusMessage idsReply = PhosphorProtocol::ClientHelpers::syncCall(
        PhosphorProtocol::Service::Interface::LayoutRegistry, QStringLiteral("getAvailableScreenIds"), {});
    if (idsReply.type() == QDBusMessage::ReplyMessage && !idsReply.arguments().isEmpty()) {
        screenIds = idsReply.arguments().constFirst().toStringList();
    }

    // One getVirtualScreenConfig round-trip per physical parent, however many
    // of its virtual screens are listed.
    QHash<QString, QJsonArray> vsConfigCache;
    QVariantList model;
    for (const QString& screenId : std::as_const(screenIds)) {
        QVariantMap entry;
        entry[QStringLiteral("name")] = screenId;
        if (PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
            // Use user-configured display name from VS config, fall back to generic label
            QString vsDisplayName;
            const QString physId = PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId);
            const int vsIndex = PhosphorIdentity::VirtualScreenId::extractIndex(screenId);
            if (!vsConfigCache.contains(physId)) {
                const QDBusMessage reply = PhosphorProtocol::ClientHelpers::syncCall(
                    PhosphorProtocol::Service::Interface::Screen, QStringLiteral("getVirtualScreenConfig"), {physId});
                if (reply.type() == QDBusMessage::ReplyMessage && reply.arguments().size() >= 1) {
                    const QJsonObject root =
                        QJsonDocument::fromJson(reply.arguments().at(0).toString().toUtf8()).object();
                    vsConfigCache[physId] = root.value(QLatin1String("screens")).toArray();
                } else {
                    vsConfigCache[physId] = QJsonArray();
                }
            }
            // Matched by id: the config's array order is not the index order
            // (a reorder or an external write can permute it), and a sparse
            // configuration has no entry at a given position at all.
            const QJsonArray& screens = vsConfigCache[physId];
            for (const QJsonValue& value : screens) {
                const QJsonObject vs = value.toObject();
                if (vs.value(QLatin1String("id")).toString() == screenId) {
                    vsDisplayName = vs.value(QLatin1String("displayName")).toString();
                    break;
                }
            }
            if (vsDisplayName.isEmpty()) {
                vsDisplayName =
                    PhosphorI18n::tr("VS%1", "@item fallback name for an unnamed virtual screen").arg(vsIndex + 1);
            }
            entry[QStringLiteral("displayName")] = vsDisplayName;
        } else {
            // Physical screen: use connector name for brevity
            QScreen* screen = PhosphorScreens::ScreenIdentity::findByIdOrName(screenId);
            entry[QStringLiteral("displayName")] = screen ? screen->name() : screenId;
        }
        model.append(entry);
    }

    if (screenIds == m_availableScreenIds && model == m_screenModel) {
        return;
    }
    m_availableScreenIds = screenIds;
    m_screenModel = model;
    Q_EMIT availableScreenIdsChanged();
}

// Carry out a screen switch: swap the target, then load whatever layout that
// screen is assigned. All entry points (setTargetScreen when nothing is
// unsaved, confirmPendingTargetScreen once the user has answered the prompt,
// the forced plain-screen launch shape, and a launch whose named template
// failed to load on a fresh editor) land here so the switch itself is
// written once.
void EditorController::applyTargetScreen(const QString& screenName, bool forceLayoutMode)
{
    if (forceLayoutMode) {
        // The forwarded plain-screen launch edits the screen's assigned
        // LAYOUT: the mode and preview flips happen here, at the moment the
        // switch actually applies, so a parked-and-cancelled request leaves
        // a template or preview session untouched.
        setEditorModeInternal(ModeLayout);
        setPreviewMode(false);
    } else if (m_editorMode == ModeScrollingTemplate) {
        // Templates are screen-portable: a screen switch in template mode
        // moves the window without loading that screen's layout over the
        // template. The screen selector is hidden in template mode, so this
        // guards programmatic switches that did not opt into forceLayoutMode.
        setTargetScreenDirect(screenName);
        return;
    }

    // Captured BEFORE the swap: the deferred publishers below take their own
    // prev capture AFTER it, so their change guards can never see a plain
    // screen-size difference — the bottom of this function publishes that
    // case (see the final emit).
    const QSize sizeBeforeSwitch = targetScreenSize();
    const QString previousLayout = m_layoutId;
    m_targetScreen = screenName;

    cacheVirtualScreenGeometry(screenName);
    // The scale belongs to the previous output. loadLayout fetches the new
    // one if the incoming layout counts device pixels.
    m_targetScreenScale = 0.0;
    m_targetScreenPixelSize = QSize();
    // Clear the per-layout override — the previous layout's bbox doesn't
    // apply to the new screen. loadLayout below pins it again if the incoming
    // layout's fixed zones overflow this screen; createNewLayout never does.
    m_layoutBoundsOverride = QSizeF();

    Q_EMIT targetScreenChanged();
    refreshUsableAreaInsets();

    // Defer targetScreenSizeChanged + setReferenceScreenSize to
    // loadLayout/createNewLayout, which know the final reference size
    // (fixed-zone bounding box for fixed layouts; physical/VS size
    // otherwise). Emitting here would make QML react with the old
    // zones against the new size before the layout swap completes.
    if (!screenName.isEmpty() && m_layoutService) {
        QString layoutId = m_layoutService->getLayoutIdForScreen(screenName);
        qCDebug(lcEditor) << "applyTargetScreen:" << screenName << "daemon returned layoutId:" << layoutId
                          << "current layoutId:" << previousLayout;
        if (!layoutId.isEmpty()) {
            // This caller mutated screen state BEFORE the load, and the size
            // publication is deferred to the load — a failed load (a
            // daemon-assigned id that no longer resolves) must not leave the
            // old zones on the new screen with a stale reference size, so it
            // falls through to a fresh layout, which publishes both.
            if (!loadLayout(layoutId)) {
                qCWarning(lcEditor) << "applyTargetScreen: assigned layout" << layoutId
                                    << "failed to load - creating new layout";
                createNewLayout();
            }
        } else {
            qCInfo(lcEditor) << "No layout assigned to screen" << screenName << "- creating new layout";
            createNewLayout();
        }
    } else {
        // No layout will be loaded, so the loaded one stays: fetch its scale
        // for the new output if it needs one, then publish the new size.
        if (m_fixedGeometryInDevicePixels) {
            ensureTargetScreenScale();
        }
        republishTargetScreenSize(sizeBeforeSwitch);
        return;
    }

    // Publish a plain screen-size change the loaders' own guards cannot see
    // (their prev capture happens after the swap above, so switching between
    // differently sized screens with no fixed-zone override fires nothing).
    // A duplicate emit when the loader already published is a harmless
    // re-notify.
    if (targetScreenSize() != sizeBeforeSwitch) {
        Q_EMIT targetScreenSizeChanged();
    }
}

void EditorController::setTargetScreen(const QString& screenName)
{
    if (m_targetScreen == screenName) {
        return;
    }

    // The switch loads the new screen's layout over the current one, so any
    // unsaved edits go with it. Park the request and let the UI ask the user
    // rather than deciding for them. targetScreen() keeps reporting the old
    // screen until the answer arrives, which is what holds the screen-selector
    // binding on the current screen while the prompt is up.
    if (m_hasUnsavedChanges) {
        // A fresh ordinary park deliberately supersedes any earlier parked
        // launch, intent included: the user's newest request wins, and the
        // prompt describes the newest screen.
        m_pendingTargetScreen = screenName;
        m_pendingTargetEditsLayout = false;
        Q_EMIT targetScreenChangeRequiresConfirmation(screenName);
        return;
    }

    applyTargetScreen(screenName);
}

void EditorController::confirmPendingTargetScreen()
{
    if (!m_pendingTargetScreen) {
        return;
    }

    // Clear before applying: applyTargetScreen loads a layout, which re-enters
    // enough of the controller that leaving the pending screen set would let a
    // second confirm apply it twice.
    const QString screenName = *std::exchange(m_pendingTargetScreen, std::nullopt);
    const bool editsLayout = std::exchange(m_pendingTargetEditsLayout, false);
    // A same-screen confirm is only a no-op for an ordinary switch; the
    // forced launch shape still has a mode flip and a load to perform.
    if (screenName == m_targetScreen && !editsLayout) {
        return;
    }
    applyTargetScreen(screenName, editsLayout);
}

void EditorController::cancelPendingTargetScreen()
{
    m_pendingTargetScreen.reset();
    m_pendingTargetEditsLayout = false;
}

void EditorController::dropPendingTargetScreen()
{
    const bool wasParked = m_pendingTargetScreen.has_value();
    m_pendingTargetScreen.reset();
    m_pendingTargetEditsLayout = false;
    if (wasParked) {
        Q_EMIT pendingTargetScreenDropped();
    }
}

void EditorController::applyLaunch(const PendingLaunch& launch)
{
    // A parked import is not a launch shape; it only borrows the launch
    // prompt, so once answered it runs the import and nothing else.
    if (!launch.importPath.isEmpty()) {
        importLayoutNow(launch.importPath);
        return;
    }

    // The screen is applied with setTargetScreenDirect on every path that
    // loads or creates its own object (both template shapes, --new and
    // --layout): setTargetScreen would load the SCREEN's assigned layout,
    // and the caller is about to put its own over the top.
    // Preview mode is set unconditionally so state from a previous forwarded
    // launch cannot leak into a later non-preview launch on the same instance.
    auto switchScreen = [this](const QString& id, bool loadAssignedLayout) {
        if (id.isEmpty() || m_targetScreen == id) {
            return;
        }
        if (loadAssignedLayout) {
            setTargetScreen(id);
        } else {
            setTargetScreenDirect(id);
        }
    };

    // Restore the screen context a failed load stranded: the switch ran
    // before the load (the load needs the target screen for per-screen
    // geometry), so on failure the still-loaded previous session gets its
    // screen, bounds override and reference size back.
    const auto restoreScreenOnFailure = [this](const QString& prevScreen, const QSizeF& prevBounds) {
        if (m_targetScreen == prevScreen) {
            return;
        }
        setTargetScreenDirect(prevScreen);
        if (prevBounds.isValid() && prevBounds != m_layoutBoundsOverride) {
            const QSize previous = targetScreenSize();
            m_layoutBoundsOverride = prevBounds;
            republishTargetScreenSize(previous);
        }
    };

    if (launch.newTemplate) {
        // Template shapes first: they never carry preview (templates have no
        // read-only rendering) and switch screens without loading a layout.
        setPreviewMode(false);
        switchScreen(launch.screenId, /*loadAssignedLayout*/ false);
        createNewScrollingTemplate();
    } else if (!launch.templateId.isEmpty()) {
        // Preview drops only once the template actually loaded, matching
        // the layoutId branch's failed-load-leaves-the-session contract.
        const QString prevScreen = m_targetScreen;
        const QSizeF prevBounds = m_layoutBoundsOverride;
        switchScreen(launch.screenId, /*loadAssignedLayout*/ false);
        if (loadScrollingTemplate(launch.templateId)) {
            setPreviewMode(false);
        } else {
            if (m_layoutId.isEmpty()) {
                // Initial launch named a template that no longer exists, and
                // the failure signal has no receiver yet (the QML engine
                // loads after applyLaunchArgs) — without a fallback the user
                // gets a blank layout-mode editor. Open the target screen's
                // assigned layout instead.
                applyTargetScreen(m_targetScreen, /*forceLayoutMode*/ true);
            } else {
                // A live session survives the failed load; put its screen
                // context back.
                restoreScreenOnFailure(prevScreen, prevBounds);
            }
        }
    } else if (launch.createNew) {
        // createNewLayout flips the mode itself once it starts replacing
        // state; no eager flip here, so nothing changes if a future guard
        // makes it refuse.
        setPreviewMode(false);
        switchScreen(launch.screenId, /*loadAssignedLayout*/ false);
        createNewLayout();
    } else if (!launch.layoutId.isEmpty()) {
        // Switch screen first (direct, no auto-load) so loadLayout resolves
        // per-screen geometry and virtual-screen context against the target
        // screen rather than whatever was current before the forward. The
        // mode flip lives inside loadLayout (after its payload resolves) and
        // preview follows only a successful load, so a launch naming a
        // deleted or malformed layout leaves the current session's mode,
        // preview and — via the restore — screen context untouched.
        const QString prevScreen = m_targetScreen;
        const QSizeF prevBounds = m_layoutBoundsOverride;
        switchScreen(launch.screenId, /*loadAssignedLayout*/ false);
        if (loadLayout(launch.layoutId)) {
            setPreviewMode(launch.preview || PhosphorLayout::LayoutId::isAutotile(launch.layoutId));
        } else {
            restoreScreenOnFailure(prevScreen, prevBounds);
        }
    } else {
        // Plain screen shape edits that screen's assigned LAYOUT. The mode
        // and preview flips happen inside applyTargetScreen at the moment
        // the switch applies (forceLayoutMode), so a parked-and-cancelled
        // switch leaves a template session intact — and a same-screen
        // relaunch still loads rather than being swallowed by a no-op
        // switch. With unsaved edits the request parks on the screen prompt;
        // requestLaunch deliberately does NOT park this shape as well (see
        // the note there).
        const QString target = launch.screenId.isEmpty() ? m_targetScreen : launch.screenId;
        if (target.isEmpty()) {
            return;
        }
        const bool leavesSpecialMode = m_editorMode == ModeScrollingTemplate || m_previewMode;
        if (target == m_targetScreen && !leavesSpecialMode) {
            // Same screen in layout mode is a no-op only when the loaded
            // layout IS the screen's assigned one. A layout opened by id, or
            // a new one, can sit on this screen too, and this shape asks for
            // the assigned layout. A screen with nothing assigned is already
            // answered by a new layout, which is what a load would create.
            const QString assigned = m_layoutService ? m_layoutService->getLayoutIdForScreen(target) : QString();
            const bool editingAssigned = assigned.isEmpty()
                ? m_isNewLayout
                : (assigned == m_layoutId
                   || (!QUuid::fromString(assigned).isNull()
                       && QUuid::fromString(assigned) == QUuid::fromString(m_layoutId)));
            if (editingAssigned) {
                return;
            }
        }
        if (m_hasUnsavedChanges) {
            m_pendingTargetScreen = target;
            m_pendingTargetEditsLayout = true;
            Q_EMIT targetScreenChangeRequiresConfirmation(target);
            return;
        }
        applyTargetScreen(target, /*forceLayoutMode*/ true);
    }
}

void EditorController::requestLaunch(const QString& screenId, const QString& layoutId, bool createNew, bool preview,
                                     const QString& templateId, bool newTemplate)
{
    const PendingLaunch launch{screenId, layoutId, createNew, preview, templateId, newTemplate, /*importPath*/ {}};

    // createNewLayout() and loadLayout() replace what is loaded outright, with
    // no confirmation of their own, so with edits in flight they silently
    // destroy the user's work. Those two shapes get parked and the UI asks.
    //
    // The remaining shape (a screen with no layout named) is deliberately NOT
    // parked here. Its applyLaunch branch parks on the screen prompt itself
    // when edits are in flight, and double-gating it would prompt twice: the
    // answer to this prompt reaches applyLaunch with the edits still unsaved
    // — Discard here means "go anyway", not "revert the layout" — so that
    // branch would see a dirty controller and park a second time.
    // The template shapes replace the loaded object outright too, so they
    // take the same parking gate.
    const bool replacesLayoutOutright = createNew || !layoutId.isEmpty() || newTemplate || !templateId.isEmpty();
    if (m_hasUnsavedChanges && replacesLayoutOutright) {
        m_pendingLaunch = launch;
        Q_EMIT launchRequestRequiresConfirmation();
        return;
    }

    applyLaunch(launch);
}

void EditorController::confirmPendingLaunch()
{
    if (!m_pendingLaunch) {
        return;
    }

    // Clear before applying, for the same re-entrancy reason as
    // confirmPendingTargetScreen: applyLaunch loads a layout.
    const PendingLaunch launch = *std::exchange(m_pendingLaunch, std::nullopt);
    applyLaunch(launch);
}

void EditorController::cancelPendingLaunch()
{
    m_pendingLaunch.reset();
}

namespace {

/// Plan for where/how to show the editor window. Computed from the target
/// screen + virtual screen rect, then applied via a single code path whether
/// the window is visible or not, virtual-screen or physical.
struct EditorWindowPlan
{
    QScreen* screen = nullptr; ///< Physical QScreen to map onto.
    QRect geometry; ///< Absolute geometry to set before showing.
    bool fullScreen = false; ///< true → showFullScreen(); false → show() (used for VS region).
    bool frameless = false; ///< true → Qt::FramelessWindowHint (used for VS region).

    bool isValid() const
    {
        return screen != nullptr;
    }
};

/// Apply the computed plan to a window and present it. Used as the "apply
/// geometry then show" lambda inside both the deferred (visible → destroy-and-
/// remap) and direct (hidden → apply immediately) paths.
void applyEditorWindowPlan(QQuickWindow* win, const EditorWindowPlan& plan)
{
    win->setFlag(Qt::FramelessWindowHint, plan.frameless);
    win->setScreen(plan.screen);
    win->setGeometry(plan.geometry);
    if (plan.fullScreen) {
        win->showFullScreen();
    } else {
        win->show();
    }
}

} // anonymous namespace

void EditorController::showFullScreenOnTargetScreen(QQuickWindow* window)
{
    if (!window) {
        return;
    }

    // Every call supersedes a remap an earlier call deferred: two target
    // changes in one event-loop pass (a launch switch and its restore after
    // a failed load) would otherwise run both remaps in order and leave the
    // window on the first target. The stamp also lets the same-state early
    // return below trust window->screen(), which a still-queued remap would
    // be about to change.
    const quint64 generation = ++m_windowPlanGeneration;

    // No target screen → plain fullscreen on whatever output Qt picks.
    if (m_targetScreen.isEmpty()) {
        window->showFullScreen();
        return;
    }

    // Build a single plan covering both the virtual-screen (frameless, sized
    // to VS region) and physical-screen (full monitor) cases.
    EditorWindowPlan plan;
    if (m_virtualScreenRect.isValid()) {
        const QString physId = PhosphorIdentity::VirtualScreenId::extractPhysicalId(m_targetScreen);
        if (QScreen* physScreen = PhosphorScreens::ScreenIdentity::findByIdOrName(physId)) {
            plan.screen = physScreen;
            // Absolute VS coordinates = physical screen origin + VS offset.
            plan.geometry =
                QRect(physScreen->geometry().topLeft() + m_virtualScreenRect.topLeft(), m_virtualScreenRect.size());
            plan.fullScreen = false;
            plan.frameless = true;
        }
    }
    if (!plan.isValid()) {
        // Physical-screen path, full geometry. A virtual screen whose
        // geometry the daemon could not give resolves to its physical
        // monitor here too, so the window at least lands on the right output.
        if (QScreen* screen = PhosphorScreens::ScreenIdentity::findByIdOrName(m_targetScreen)) {
            plan.screen = screen;
            plan.geometry = screen->geometry();
            plan.fullScreen = true;
            plan.frameless = false;
        }
    }
    if (!plan.isValid()) {
        // Unknown target — fall back to plain fullscreen.
        window->setFlag(Qt::FramelessWindowHint, false);
        window->showFullScreen();
        return;
    }

    qCDebug(lcEditor) << "Editor window plan — screen:" << plan.screen->name() << "geometry:" << plan.geometry
                      << "fullScreen:" << plan.fullScreen << "frameless:" << plan.frameless;

    // Already shown the way the plan asks → nothing to do. No Wayland
    // workaround reliably brings an already-mapped fullscreen xdg_toplevel to
    // the front from a programmatic caller, so we don't even try. Same
    // screen alone is not enough: two virtual screens of one monitor, or a
    // virtual screen and its whole monitor, share the QScreen and differ in
    // geometry and in the fullscreen and frameless state.
    const bool shownAsPlanned = plan.fullScreen
        ? window->visibility() == QWindow::FullScreen
        : (window->visibility() != QWindow::FullScreen && window->geometry() == plan.geometry
           && window->flags().testFlag(Qt::FramelessWindowHint) == plan.frameless);
    if (window->screen() == plan.screen && shownAsPlanned && window->isVisible() && window->isExposed()) {
        return;
    }

    // Visible but needs relayout (screen switch or VS/physical toggle): Wayland
    // binds wl_output at surface-creation time, so we must destroy the native
    // window and let Qt recreate it with the new output. Defer to the next
    // event-loop tick so we don't tear down the platform surface from inside
    // a paint or D-Bus dispatch.
    if (window->isVisible()) {
        QPointer<QQuickWindow> safeWindow(window);
        QPointer<EditorController> self(this);
        QTimer::singleShot(0, window, [safeWindow, self, plan, generation]() {
            if (!safeWindow || !self || !plan.screen || generation != self->m_windowPlanGeneration) {
                return;
            }
            safeWindow->destroy();
            applyEditorWindowPlan(safeWindow.data(), plan);
        });
        return;
    }

    // Not yet visible — apply directly, no remap needed.
    applyEditorWindowPlan(window, plan);
}

void EditorController::setTargetScreenDirect(const QString& screenName)
{
    // Swaps the target without loading that screen's layout. The callers are
    // a launch that loads its own layout or template over the top, the
    // restore of a stranded session's screen after a failed load, and a
    // screen switch in template mode (templates are screen-portable).
    if (m_targetScreen == screenName) {
        return;
    }
    const QSize previous = targetScreenSize();
    m_targetScreen = screenName;

    cacheVirtualScreenGeometry(screenName);
    m_targetScreenScale = 0.0;
    m_targetScreenPixelSize = QSize();
    if (m_fixedGeometryInDevicePixels) {
        ensureTargetScreenScale();
    }
    m_layoutBoundsOverride = QSizeF();

    Q_EMIT targetScreenChanged();
    refreshUsableAreaInsets();
    republishTargetScreenSize(previous);
}

void EditorController::refreshTargetScreenGeometry()
{
    const QSize previous = targetScreenSize();
    cacheVirtualScreenGeometry(m_targetScreen);
    m_targetScreenScale = 0.0;
    m_targetScreenPixelSize = QSize();
    if (m_fixedGeometryInDevicePixels) {
        ensureTargetScreenScale();
    }
    // The override was measured against the old geometry. Template mode
    // holds none (beginTemplateSession dropped it) and must not grow one
    // from the layout session it keeps loaded underneath.
    if (m_editorMode == ModeLayout) {
        refreshFixedBoundsOverride(currentLayoutJson());
    }
    // Async: setInsets republishes again when the replies land.
    refreshUsableAreaInsets();
    republishTargetScreenSize(previous);
}

void EditorController::onDaemonScreenGeometryChanged(const QString& screenId)
{
    if (!m_targetScreen.isEmpty() && screenId == m_targetScreen) {
        refreshTargetScreenGeometry();
    }
}

void EditorController::onDaemonAvailableGeometryChanged(const QString& screenId)
{
    if (!m_targetScreen.isEmpty() && screenId == m_targetScreen) {
        // Async; the replies reach setInsets, which republishes the
        // reference and re-derives the bounds override only when the insets
        // really moved.
        refreshUsableAreaInsets();
    }
}

void EditorController::onDaemonVirtualScreensChanged(const QString& physicalScreenId)
{
    // The screen list's virtual screens and their names moved with it.
    refreshAvailableScreens();
    if (PhosphorIdentity::VirtualScreenId::isVirtual(m_targetScreen)
        && PhosphorIdentity::VirtualScreenId::extractPhysicalId(m_targetScreen) == physicalScreenId) {
        refreshTargetScreenGeometry();
    }
}

// ---------------------------------------------------------------------------
// Group 2 - PhosphorZones::Layout lifecycle
// ---------------------------------------------------------------------------

/**
 * @brief Creates a new empty layout
 *
 * Generates a new layout ID and initializes an empty layout.
 * Emits signals to notify QML of the new layout state.
 */
void EditorController::createNewLayout()
{
    // Editing a layout, whatever mode the editor was in before.
    setEditorModeInternal(ModeLayout);
    // The mode setter drops a parked screen switch only on a genuine mode
    // change, and this call commonly runs in layout mode already, so a park
    // answering for the layout being replaced would otherwise outlive it.
    dropPendingTargetScreen();

    // Drop any commands from the previous session: this call commonly runs
    // with the mode ALREADY ModeLayout (the new-layout button, the
    // no-assigned-layout branch), so the flip above cannot be relied on to
    // have discarded anything, and a stale template or layout command would
    // apply invisible state onto the fresh layout on Ctrl+Z. loadLayout
    // clears identically once its payload resolves.
    if (m_undoController) {
        m_undoController->clear();
    }

    // A fresh layout has no fixed-zone bounding box to reference. Drop any
    // override from the previous layout so QML normalizes against the
    // screen geometry that setTargetScreen already cached — no D-Bus
    // round-trip needed.
    const QSize prevSize = targetScreenSize();
    m_layoutBoundsOverride = QSizeF();

    m_layoutId = QUuid::createUuid().toString();
    m_layoutName = PhosphorI18n::tr("New Layout");
    if (m_zoneManager) {
        m_zoneManager->clearAllZones();
    }
    m_selectedZoneId.clear();
    m_selectedZoneIds.clear();
    m_isNewLayout = true;
    m_hasUnsavedChanges = true;

    // Reset per-layout gap overrides (-1 = use global). Signals are emitted
    // together with the other layout signals below.
    m_gaps->resetOverrides();
    m_overlayDisplayMode = -1;
    m_useFullScreenGeometry = false;
    const bool oldFixedUnit = std::exchange(m_fixedGeometryInDevicePixels, false);
    m_aspectRatioClass = 0;

    // Publish the screen-derived reference size if it moved: the previous
    // layout had overridden it to a fixed-zone bounding box, or counted its
    // fixed sizes in device pixels (or covered the full screen), each of
    // which the reset above undoes, or setTargetScreen deferred the
    // emission to us.
    const QSize newSize = targetScreenSize();
    if (newSize != prevSize) {
        Q_EMIT targetScreenSizeChanged();
    }
    m_zoneManager->setReferenceScreenSize(newSize);

    // The visibility allow-lists are per-layout state like everything else
    // reset above, and saveLayout serializes them unconditionally. Left alone,
    // a New Layout started from one restricted to a single screen, desktop or
    // activity inherited those restrictions and saved them into the new
    // layout, which then did not appear anywhere the user had not thought to
    // look. The emits matter as much as the clear: VisibilitySettingsDialog
    // re-derives its checkboxes only from these three signals, so clearing
    // without emitting would leave the dialog showing the inherited lists.
    m_allowedScreens.clear();
    m_allowedDesktopsInt.clear();
    m_allowedActivities.clear();

    ++m_zonesVersion;
    Q_EMIT layoutIdChanged();
    Q_EMIT layoutNameChanged();
    Q_EMIT zonesChanged();
    Q_EMIT selectedZoneIdChanged();
    Q_EMIT selectedZoneIdsChanged();
    Q_EMIT hasUnsavedChangesChanged();
    m_gaps->emitOverrideSignals();
    Q_EMIT overlayDisplayModeChanged();
    Q_EMIT useFullScreenGeometryChanged();
    if (oldFixedUnit) {
        Q_EMIT fixedGeometryInDevicePixelsChanged();
    }
    Q_EMIT aspectRatioClassChanged();
    Q_EMIT allowedScreensChanged();
    Q_EMIT allowedDesktopsChanged();
    Q_EMIT allowedActivitiesChanged();
}

bool EditorController::loadLayout(const QString& layoutId)
{
    if (layoutId.isEmpty()) {
        Q_EMIT layoutLoadFailed(PhosphorI18n::tr("Layout ID cannot be empty"));
        return false;
    }

    if (!m_layoutService) {
        Q_EMIT layoutLoadFailed(PhosphorI18n::tr("Layout service not initialized"));
        return false;
    }

    // Try the in-process PhosphorZones::LayoutRegistry first — opens the editor instantly
    // even when the daemon is starting up, daemon is down, or the user
    // launched the editor as a standalone tool. Falls back to D-Bus
    // (DBusLayoutService::loadLayout via m_layoutService) when the layout
    // isn't on disk yet (just-created in another process, etc.) or when
    // the ID is an autotile algorithm preview that the local PhosphorZones::LayoutRegistry
    // can't produce.
    QString jsonLayout;
    if (m_localLayoutManager) {
        const QUuid uuid = QUuid::fromString(layoutId);
        if (!uuid.isNull()) {
            if (PhosphorZones::Layout* layout = m_localLayoutManager->layoutById(uuid)) {
                jsonLayout = QString::fromUtf8(QJsonDocument(layout->toJson()).toJson());
            }
        }
    }
    if (jsonLayout.isEmpty()) {
        jsonLayout = m_layoutService->loadLayout(layoutId);
    }
    if (jsonLayout.isEmpty()) {
        // Error signal already emitted by service
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(jsonLayout.toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        Q_EMIT layoutLoadFailed(PhosphorI18n::tr("Invalid layout data format"));
        qCWarning(lcEditor) << "Invalid JSON for layout" << layoutId;
        return false;
    }

    // Only flip modes once the payload resolved — a failed load leaves the
    // current editing MODE (template or layout) untouched. Screen context a
    // launch switched before calling in is the caller's to restore; see
    // applyLaunch's restoreScreenOnFailure.
    setEditorModeInternal(ModeLayout);
    // Same-mode replacement: see createNewLayout.
    dropPendingTargetScreen();

    QJsonObject layoutObj = doc.object();
    m_layoutId = layoutObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::Id)].toString();
    m_layoutName = layoutObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::Name)].toString();

    // Resolve the final reference size before building zones. The editor
    // canvas fills the area zones occupy on the live screen, and EditorZone
    // scales each fixed zone by targetScreenSize (fixedPixels /
    // targetScreenSize.width * canvasWidth), so the reference MUST share that
    // area's aspect ratio or fixed zones stretch. The raw fixed-zone bounding
    // box is the wrong reference whenever the zones don't span the full
    // screen — a 16:9 layout whose fixed zones only reach 2560px wide yields
    // a 2560x2160 bbox and renders 1.5x too wide (discussion #593).
    //
    // refreshFixedBoundsOverride clears the override first so
    // targetLogicalScreenSize() reports the live screen/VS area, then re-pins
    // only when the layout's fixed zones genuinely exceed it (e.g. a
    // 3840x2160 layout on a 3840x2126 panel-reduced screen).
    // fixedZoneReferenceGeometry() encodes exactly this "use the recalc
    // geometry unless the bbox overflows it" rule, the same helper the
    // preview path uses, seeded by recalculating a throwaway Layout against
    // the live area first.
    const QSize prevSize = targetScreenSize();
    // The unit of the fixed numbers and the area they are laid out in go
    // first, since the reference is measured in them. Their change signals
    // go out with the other per-layout flags below.
    const bool oldFixedUnit = m_fixedGeometryInDevicePixels;
    m_fixedGeometryInDevicePixels =
        layoutObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::FixedGeometryInDevicePixels)].toBool(false);
    if (m_fixedGeometryInDevicePixels) {
        ensureTargetScreenScale();
    }
    const bool oldUseFullScreen = m_useFullScreenGeometry;
    m_useFullScreenGeometry =
        layoutObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::UseFullScreenGeometry)].toBool(false);
    refreshFixedBoundsOverride(layoutObj);
    const QSize newSize = targetScreenSize();
    if (newSize != prevSize) {
        // Publish the new reference size *before* setZones below so the
        // QML Repeater's delegate creation sees the correct dimensions on
        // first sync (otherwise zones jump to recomputed positions).
        Q_EMIT targetScreenSizeChanged();
    }
    m_zoneManager->setReferenceScreenSize(newSize);

    // Parse zones
    QVariantList zones;
    QJsonArray zonesArray = layoutObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::Zones)].toArray();
    for (const QJsonValue& zoneVal : zonesArray) {
        QJsonObject zoneObj = zoneVal.toObject();
        QVariantMap zone;

        zone[::PhosphorZones::ZoneJsonKeys::Id] = zoneObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::Id)].toString();
        zone[::PhosphorZones::ZoneJsonKeys::Name] =
            zoneObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::Name)].toString();
        zone[::PhosphorZones::ZoneJsonKeys::ZoneNumber] =
            zoneObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::ZoneNumber)].toInt();

        QJsonObject relGeo = zoneObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::RelativeGeometry)].toObject();
        zone[::PhosphorZones::ZoneJsonKeys::X] = relGeo[QLatin1String(::PhosphorZones::ZoneJsonKeys::X)].toDouble();
        zone[::PhosphorZones::ZoneJsonKeys::Y] = relGeo[QLatin1String(::PhosphorZones::ZoneJsonKeys::Y)].toDouble();
        zone[::PhosphorZones::ZoneJsonKeys::Width] =
            relGeo[QLatin1String(::PhosphorZones::ZoneJsonKeys::Width)].toDouble();
        zone[::PhosphorZones::ZoneJsonKeys::Height] =
            relGeo[QLatin1String(::PhosphorZones::ZoneJsonKeys::Height)].toDouble();

        // Geometry mode (default: Relative = 0)
        int geoMode = zoneObj.contains(QLatin1String(::PhosphorZones::ZoneJsonKeys::GeometryMode))
            ? zoneObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::GeometryMode)].toInt()
            : 0;
        zone[::PhosphorZones::ZoneJsonKeys::GeometryMode] = geoMode;

        if (geoMode == static_cast<int>(PhosphorZones::ZoneGeometryMode::Fixed)
            && zoneObj.contains(QLatin1String(::PhosphorZones::ZoneJsonKeys::FixedGeometry))) {
            QJsonObject fixedGeo = zoneObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::FixedGeometry)].toObject();
            zone[::PhosphorZones::ZoneJsonKeys::FixedX] =
                fixedGeo[QLatin1String(::PhosphorZones::ZoneJsonKeys::X)].toDouble();
            zone[::PhosphorZones::ZoneJsonKeys::FixedY] =
                fixedGeo[QLatin1String(::PhosphorZones::ZoneJsonKeys::Y)].toDouble();
            zone[::PhosphorZones::ZoneJsonKeys::FixedWidth] =
                fixedGeo[QLatin1String(::PhosphorZones::ZoneJsonKeys::Width)].toDouble();
            zone[::PhosphorZones::ZoneJsonKeys::FixedHeight] =
                fixedGeo[QLatin1String(::PhosphorZones::ZoneJsonKeys::Height)].toDouble();
            // The relative copy the file carries was normalized against
            // whatever reference its writer had (the daemon's last screen,
            // clamped to 0–1). Derive it from the pixels against the reference
            // published above instead, as the unit toggle and every fixed
            // edit do, so the snapping, fill and divider math that reads the
            // relative copy sees where the zone is actually drawn.
            m_zoneManager->syncRelativeFromFixed(zone);
        }

        // Appearance
        QJsonObject appearance = zoneObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::Appearance)].toObject();
        zone[::PhosphorZones::ZoneJsonKeys::HighlightColor] =
            appearance[QLatin1String(::PhosphorZones::ZoneJsonKeys::HighlightColor)].toString();
        zone[::PhosphorZones::ZoneJsonKeys::InactiveColor] =
            appearance[QLatin1String(::PhosphorZones::ZoneJsonKeys::InactiveColor)].toString();
        zone[::PhosphorZones::ZoneJsonKeys::BorderColor] =
            appearance[QLatin1String(::PhosphorZones::ZoneJsonKeys::BorderColor)].toString();
        // Load all appearance properties with defaults if missing
        zone[::PhosphorZones::ZoneJsonKeys::ActiveOpacity] =
            appearance.contains(QLatin1String(::PhosphorZones::ZoneJsonKeys::ActiveOpacity))
            ? appearance[QLatin1String(::PhosphorZones::ZoneJsonKeys::ActiveOpacity)].toDouble()
            : ::PhosphorZones::ZoneDefaults::Opacity;
        zone[::PhosphorZones::ZoneJsonKeys::InactiveOpacity] =
            appearance.contains(QLatin1String(::PhosphorZones::ZoneJsonKeys::InactiveOpacity))
            ? appearance[QLatin1String(::PhosphorZones::ZoneJsonKeys::InactiveOpacity)].toDouble()
            : ::PhosphorZones::ZoneDefaults::InactiveOpacity;
        zone[::PhosphorZones::ZoneJsonKeys::BorderWidth] =
            appearance.contains(QLatin1String(::PhosphorZones::ZoneJsonKeys::BorderWidth))
            ? appearance[QLatin1String(::PhosphorZones::ZoneJsonKeys::BorderWidth)].toInt()
            : ::PhosphorZones::ZoneDefaults::BorderWidth;
        zone[::PhosphorZones::ZoneJsonKeys::BorderRadius] =
            appearance.contains(QLatin1String(::PhosphorZones::ZoneJsonKeys::BorderRadius))
            ? appearance[QLatin1String(::PhosphorZones::ZoneJsonKeys::BorderRadius)].toInt()
            : ::PhosphorZones::ZoneDefaults::BorderRadius;
        bool useCustomColorsValue = appearance.contains(QLatin1String(::PhosphorZones::ZoneJsonKeys::UseCustomColors))
            ? appearance[QLatin1String(::PhosphorZones::ZoneJsonKeys::UseCustomColors)].toBool()
            : false;
        zone[QLatin1String(::PhosphorZones::ZoneJsonKeys::UseCustomColors)] = useCustomColorsValue;

        // Per-zone overlay display mode override (-1 = use layout/global)
        zone[::PhosphorZones::ZoneJsonKeys::OverlayDisplayMode] =
            appearance.contains(QLatin1String(::PhosphorZones::ZoneJsonKeys::OverlayDisplayMode))
            ? appearance[QLatin1String(::PhosphorZones::ZoneJsonKeys::OverlayDisplayMode)].toInt(-1)
            : -1;

        zones.append(zone);
    }

    m_zoneManager->setZones(zones);

    // Load visibility filtering allow-lists
    PhosphorZones::LayoutUtils::deserializeAllowLists(layoutObj, m_allowedScreens, m_allowedDesktopsInt,
                                                      m_allowedActivities);

    // Query available context info from daemon via D-Bus
    // Clear first so stale data is not shown if daemon is unavailable. The
    // screen list is refreshed at the end, where its change signal goes out.
    m_virtualDesktopCount = 1;
    m_virtualDesktopNames.clear();
    m_activitiesAvailable = false;
    m_availableActivities.clear();
    {
        const auto callLayoutRegistry = [](const QString& method, const QVariantList& args = {}) -> QDBusMessage {
            return PhosphorProtocol::ClientHelpers::syncCall(PhosphorProtocol::Service::Interface::LayoutRegistry,
                                                             method, args);
        };
        const auto firstArg = [](const QDBusMessage& reply) -> QVariant {
            return (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
                ? reply.arguments().constFirst()
                : QVariant{};
        };

        // Virtual desktops
        if (auto v = firstArg(callLayoutRegistry(QStringLiteral("getVirtualDesktopCount"))); v.isValid()) {
            m_virtualDesktopCount = v.toInt();
        }
        if (auto v = firstArg(callLayoutRegistry(QStringLiteral("getVirtualDesktopNames"))); v.isValid()) {
            m_virtualDesktopNames = v.toStringList();
        }

        // Activities
        if (auto v = firstArg(callLayoutRegistry(QStringLiteral("isActivitiesAvailable"))); v.isValid()) {
            m_activitiesAvailable = v.toBool();
        }
        if (m_activitiesAvailable) {
            if (auto v = firstArg(callLayoutRegistry(QStringLiteral("getAllActivitiesInfo"))); v.isValid()) {
                QJsonDocument activitiesDoc = QJsonDocument::fromJson(v.toString().toUtf8());
                if (activitiesDoc.isArray()) {
                    const auto arr = activitiesDoc.array();
                    for (const auto& a : arr) {
                        m_availableActivities.append(a.toObject().toVariantMap());
                    }
                }
            }
        }
    }

    // Load per-layout gap overrides (-1 = use global setting). The gap
    // sub-model reads the keys and emits its own change signals.
    m_gaps->loadFromJson(layoutObj);
    int oldAspectRatioClass = m_aspectRatioClass;
    // fromJsonValue accepts the canonical string from Layout::toJson and the int
    // from an editor save round-trip, and maps a missing key to Any.
    m_aspectRatioClass = static_cast<int>(PhosphorLayout::ScreenClassification::fromJsonValue(
        layoutObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::AspectRatioClassKey)]));
    int oldOverlayDisplayMode = m_overlayDisplayMode;
    m_overlayDisplayMode = layoutObj.contains(QLatin1String(::PhosphorZones::ZoneJsonKeys::OverlayDisplayMode))
        ? layoutObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::OverlayDisplayMode)].toInt(-1)
        : -1;

    m_selectedZoneId.clear();
    m_selectedZoneIds.clear();
    m_isNewLayout = false;
    m_hasUnsavedChanges = false;

    // Clear undo stack when loading a layout
    if (m_undoController) {
        m_undoController->clear();
    }

    ++m_zonesVersion;
    Q_EMIT layoutIdChanged();
    Q_EMIT layoutNameChanged();
    Q_EMIT zonesChanged();
    Q_EMIT selectedZoneIdChanged();
    Q_EMIT selectedZoneIdsChanged();
    Q_EMIT hasUnsavedChangesChanged();

    // Gap change signals were emitted inside EditorGapsModel::loadFromJson above.
    if (m_useFullScreenGeometry != oldUseFullScreen) {
        Q_EMIT useFullScreenGeometryChanged();
    }
    if (m_fixedGeometryInDevicePixels != oldFixedUnit) {
        Q_EMIT fixedGeometryInDevicePixelsChanged();
    }
    if (m_aspectRatioClass != oldAspectRatioClass) {
        Q_EMIT aspectRatioClassChanged();
    }
    if (m_overlayDisplayMode != oldOverlayDisplayMode) {
        Q_EMIT overlayDisplayModeChanged();
    }

    // Emit visibility filtering signals
    Q_EMIT allowedScreensChanged();
    Q_EMIT allowedDesktopsChanged();
    Q_EMIT allowedActivitiesChanged();
    refreshAvailableScreens();
    Q_EMIT virtualDesktopCountChanged();
    Q_EMIT virtualDesktopNamesChanged();
    Q_EMIT activitiesAvailableChanged();
    Q_EMIT availableActivitiesChanged();
    return true;
}

QJsonObject EditorController::currentLayoutJson() const
{
    QJsonObject layoutObj;
    layoutObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::Id)] = m_layoutId;
    layoutObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::Name)] = m_layoutName;

    QJsonArray zonesArray;
    QVariantList zones = m_zoneManager->zones();
    for (const QVariant& zoneVar : zones) {
        QVariantMap zone = zoneVar.toMap();
        QJsonObject zoneObj;

        zoneObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::Id)] = zone[::PhosphorZones::ZoneJsonKeys::Id].toString();
        zoneObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::Name)] =
            zone[::PhosphorZones::ZoneJsonKeys::Name].toString();
        zoneObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::ZoneNumber)] =
            zone[::PhosphorZones::ZoneJsonKeys::ZoneNumber].toInt();

        // Clamped to 0–1, the range the key is defined over, as Zone::toJson
        // does. A relative zone is already inside it, but a fixed zone's
        // relative copy is derived from its pixels and runs past 1 when the
        // zone overflows the reference (a duplicate offset past the edge),
        // and the daemon's schema gate rejects the whole save for that. The
        // fixed rect below round-trips untouched, so the clamp only bounds
        // the derived hint.
        QJsonObject relGeo;
        relGeo[QLatin1String(::PhosphorZones::ZoneJsonKeys::X)] =
            qBound(0.0, zone[::PhosphorZones::ZoneJsonKeys::X].toDouble(), 1.0);
        relGeo[QLatin1String(::PhosphorZones::ZoneJsonKeys::Y)] =
            qBound(0.0, zone[::PhosphorZones::ZoneJsonKeys::Y].toDouble(), 1.0);
        relGeo[QLatin1String(::PhosphorZones::ZoneJsonKeys::Width)] =
            qBound(0.0, zone[::PhosphorZones::ZoneJsonKeys::Width].toDouble(), 1.0);
        relGeo[QLatin1String(::PhosphorZones::ZoneJsonKeys::Height)] =
            qBound(0.0, zone[::PhosphorZones::ZoneJsonKeys::Height].toDouble(), 1.0);
        zoneObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::RelativeGeometry)] = relGeo;

        // Write geometry mode and fixed geometry when Fixed
        int geoMode = zone.value(::PhosphorZones::ZoneJsonKeys::GeometryMode, 0).toInt();
        if (geoMode == static_cast<int>(PhosphorZones::ZoneGeometryMode::Fixed)) {
            zoneObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::GeometryMode)] = geoMode;
            QJsonObject fixedGeo;
            fixedGeo[QLatin1String(::PhosphorZones::ZoneJsonKeys::X)] =
                zone.value(::PhosphorZones::ZoneJsonKeys::FixedX, 0.0).toDouble();
            fixedGeo[QLatin1String(::PhosphorZones::ZoneJsonKeys::Y)] =
                zone.value(::PhosphorZones::ZoneJsonKeys::FixedY, 0.0).toDouble();
            fixedGeo[QLatin1String(::PhosphorZones::ZoneJsonKeys::Width)] =
                zone.value(::PhosphorZones::ZoneJsonKeys::FixedWidth, 0.0).toDouble();
            fixedGeo[QLatin1String(::PhosphorZones::ZoneJsonKeys::Height)] =
                zone.value(::PhosphorZones::ZoneJsonKeys::FixedHeight, 0.0).toDouble();
            zoneObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::FixedGeometry)] = fixedGeo;
        }

        QJsonObject appearance;
        appearance[QLatin1String(::PhosphorZones::ZoneJsonKeys::HighlightColor)] =
            zone[::PhosphorZones::ZoneJsonKeys::HighlightColor].toString();
        appearance[QLatin1String(::PhosphorZones::ZoneJsonKeys::InactiveColor)] =
            zone[::PhosphorZones::ZoneJsonKeys::InactiveColor].toString();
        appearance[QLatin1String(::PhosphorZones::ZoneJsonKeys::BorderColor)] =
            zone[::PhosphorZones::ZoneJsonKeys::BorderColor].toString();
        // Include all appearance properties for persistence
        appearance[QLatin1String(::PhosphorZones::ZoneJsonKeys::ActiveOpacity)] =
            zone.contains(::PhosphorZones::ZoneJsonKeys::ActiveOpacity)
            ? zone[::PhosphorZones::ZoneJsonKeys::ActiveOpacity].toDouble()
            : ::PhosphorZones::ZoneDefaults::Opacity;
        appearance[QLatin1String(::PhosphorZones::ZoneJsonKeys::InactiveOpacity)] =
            zone.contains(::PhosphorZones::ZoneJsonKeys::InactiveOpacity)
            ? zone[::PhosphorZones::ZoneJsonKeys::InactiveOpacity].toDouble()
            : ::PhosphorZones::ZoneDefaults::InactiveOpacity;
        appearance[QLatin1String(::PhosphorZones::ZoneJsonKeys::BorderWidth)] =
            zone.contains(::PhosphorZones::ZoneJsonKeys::BorderWidth)
            ? zone[::PhosphorZones::ZoneJsonKeys::BorderWidth].toInt()
            : ::PhosphorZones::ZoneDefaults::BorderWidth;
        appearance[QLatin1String(::PhosphorZones::ZoneJsonKeys::BorderRadius)] =
            zone.contains(::PhosphorZones::ZoneJsonKeys::BorderRadius)
            ? zone[::PhosphorZones::ZoneJsonKeys::BorderRadius].toInt()
            : ::PhosphorZones::ZoneDefaults::BorderRadius;
        appearance[QLatin1String(::PhosphorZones::ZoneJsonKeys::UseCustomColors)] =
            zone.contains(QLatin1String(::PhosphorZones::ZoneJsonKeys::UseCustomColors))
            ? zone[QLatin1String(::PhosphorZones::ZoneJsonKeys::UseCustomColors)].toBool()
            : false;
        // Per-zone overlay display mode override (only if set)
        int zoneOverlayMode = zone.value(::PhosphorZones::ZoneJsonKeys::OverlayDisplayMode, -1).toInt();
        if (zoneOverlayMode >= 0) {
            appearance[QLatin1String(::PhosphorZones::ZoneJsonKeys::OverlayDisplayMode)] = zoneOverlayMode;
        }
        zoneObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::Appearance)] = appearance;

        zonesArray.append(zoneObj);
    }
    layoutObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::Zones)] = zonesArray;

    // Include per-layout gap overrides (only the ones actually set)
    m_gaps->writeToJson(layoutObj);

    // Include per-layout overlay display mode override (only if set)
    if (m_overlayDisplayMode >= 0) {
        layoutObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::OverlayDisplayMode)] = m_overlayDisplayMode;
    }

    // Include full screen geometry mode (only if enabled)
    if (m_useFullScreenGeometry) {
        layoutObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::UseFullScreenGeometry)] = true;
    }

    // Include the unit of fixed-size zones (only if device pixels)
    if (m_fixedGeometryInDevicePixels) {
        layoutObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::FixedGeometryInDevicePixels)] = true;
    }

    // Include aspect ratio class (only if not Any) — serialize as int for updateLayout D-Bus,
    // which converts to the canonical string format via PhosphorZones::Layout::setAspectRatioClassInt()
    if (m_aspectRatioClass != 0) {
        layoutObj[QLatin1String(::PhosphorZones::ZoneJsonKeys::AspectRatioClassKey)] = m_aspectRatioClass;
    }

    // Include visibility filtering allow-lists (only if non-empty)
    PhosphorZones::LayoutUtils::serializeAllowLists(layoutObj, m_allowedScreens, m_allowedDesktopsInt,
                                                    m_allowedActivities);
    return layoutObj;
}

void EditorController::refreshFixedBoundsOverride(const QJsonObject& layoutObj)
{
    // Clear the override first so the size below is the live screen's (or the
    // virtual screen's), then re-pin only when the layout's fixed zones
    // genuinely exceed it — see the load path's note. Logical units
    // throughout: the throwaway layout is recalculated at the target output's
    // scale, so a layout counting device pixels has its zones divided by it
    // before they are measured against the logical screen.
    m_layoutBoundsOverride = QSizeF();
    const QSizeF screenSize = targetLogicalScreenSizeF();
    std::unique_ptr<PhosphorZones::Layout> tmp(PhosphorZones::Layout::fromJson(layoutObj));
    if (tmp && !screenSize.isEmpty()) {
        tmp->recalculateZoneGeometries(QRectF(QPointF(), screenSize), m_targetScreenScale);
        const QRectF refGeo = tmp->fixedZoneReferenceGeometry();
        // Pinned on overflow only, and only on the overflowing axis: a
        // layout wider than the screen but no taller keeps the screen's
        // height, or the canvas (which fills the screen's area) would
        // stretch its zones vertically by the bbox's shortfall. Compared in
        // whole pixels so a sub-pixel overflow (a device-pixel extent divided
        // by a fractional scale) does not pin anything.
        const QSizeF bounds = refGeo.size().expandedTo(screenSize);
        if (!refGeo.isEmpty() && bounds.toSize() != screenSize.toSize()) {
            m_layoutBoundsOverride = bounds;
        }
    }
}

void EditorController::republishTargetScreenSize(const QSize& previous)
{
    const QSize current = targetScreenSize();
    if (current != previous) {
        Q_EMIT targetScreenSizeChanged();
    }
    // The zone manager may already hold this reference (the loaders publish
    // it themselves before parsing), and re-deriving every fixed zone's
    // relative copy against an unchanged reference only churns signals.
    if (m_zoneManager->referenceScreenSize() != current) {
        m_zoneManager->setReferenceScreenSize(current);
        m_zoneManager->resyncFixedZones();
    }
}

/**
 * @brief Saves the current layout to the daemon
 *
 * Serializes the layout to JSON and sends it to the daemon via D-Bus.
 * Creates a new layout if isNewLayout is true, otherwise updates existing layout.
 * Emits layoutSaveFailed signal on error, layoutSaved on success.
 *
 * Returns whether the save landed. Every false return has already emitted
 * layoutSaveFailed and left m_hasUnsavedChanges set, so a caller that chains a
 * layout-replacing action onto a save can gate it on this and leave the user's
 * work in place when the daemon refuses the payload.
 */
bool EditorController::saveLayout()
{
    // Single save verb for both editing modes: every QML save path (Save
    // button, Ctrl+S, the three unsaved-changes prompts) calls this, so the
    // dispatch lives here rather than in each of them.
    if (m_editorMode == ModeScrollingTemplate) {
        return saveScrollingTemplateNow();
    }

    if (!m_layoutService || !m_zoneManager) {
        Q_EMIT layoutSaveFailed(PhosphorI18n::tr("Services not initialized"));
        return false;
    }

    const QJsonObject layoutObj = currentLayoutJson();

    QJsonDocument doc(layoutObj);
    QString jsonStr = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));

    // Use layout service to save
    if (m_isNewLayout) {
        QString newLayoutId = m_layoutService->createLayout(jsonStr);
        if (newLayoutId.isEmpty()) {
            // Error signal already emitted by service
            return false;
        }
        if (m_layoutId != newLayoutId) {
            m_layoutId = newLayoutId;
            Q_EMIT layoutIdChanged();
        }
        m_isNewLayout = false;
    } else {
        bool success = m_layoutService->updateLayout(jsonStr);
        if (!success) {
            // Error signal already emitted by service
            return false;
        }
    }

    m_hasUnsavedChanges = false;

    // Bring the in-process registry up to what was just saved, now rather
    // than behind m_layoutReloadTimer. The daemon writes the file before it
    // replies, but its change signals reach us only after this call returns,
    // and a prompt's Save is followed in the same call stack by the load it
    // was guarding. A load of this very layout would otherwise open the
    // pre-save snapshot through the fast path, show it clean, and the next
    // save would overwrite the edits just saved.
    reloadLocalLayouts();

    // Mark undo stack as clean after successful save
    if (m_undoController) {
        m_undoController->setClean();
    }

    // Note: We intentionally do NOT assign the layout to a screen here.
    // PhosphorZones::Layout assignment should be a separate, explicit user action.
    // This prevents saving a layout from inadvertently changing the active layout.

    Q_EMIT hasUnsavedChangesChanged();
    Q_EMIT layoutSaved();
    return true;
}

/**
 * @brief Discards unsaved changes and closes the editor
 *
 * Reloads the layout from the daemon if it's not a new layout,
 * effectively discarding any unsaved changes.
 */
void EditorController::discardChanges()
{
    // The reload results are deliberately ignored: the user asked to
    // discard, so a reload that fails (the object was deleted in another
    // process) changes nothing about the close — the edits were forfeit
    // either way.
    if (!m_isNewLayout && !m_layoutId.isEmpty()) {
        if (m_editorMode == ModeScrollingTemplate) {
            loadScrollingTemplate(m_layoutId);
        } else {
            loadLayout(m_layoutId);
        }
    }
    Q_EMIT editorClosed();
}

// ---------------------------------------------------------------------------
// Group 3 - Import/Export
// ---------------------------------------------------------------------------

void EditorController::importLayout(const QString& filePath)
{
    if (filePath.isEmpty()) {
        Q_EMIT layoutLoadFailed(PhosphorI18n::tr("File path cannot be empty"));
        return;
    }

    // The import loads the imported layout over the current one, so unsaved
    // edits get the same prompt a forwarded launch does, and nothing reaches
    // the daemon until the user answers: a cancelled import must not leave a
    // new layout behind in the registry.
    if (m_hasUnsavedChanges) {
        PendingLaunch launch;
        launch.importPath = filePath;
        m_pendingLaunch = launch;
        Q_EMIT launchRequestRequiresConfirmation();
        return;
    }

    importLayoutNow(filePath);
}

void EditorController::importLayoutNow(const QString& filePath)
{
    const QDBusMessage reply = PhosphorProtocol::ClientHelpers::syncCall(
        PhosphorProtocol::Service::Interface::LayoutRegistry, QStringLiteral("importLayout"), {filePath});
    if (reply.type() != QDBusMessage::ReplyMessage) {
        QString error = PhosphorI18n::tr("Failed to import layout: %1").arg(reply.errorMessage());
        qCWarning(lcEditor) << error;
        Q_EMIT layoutLoadFailed(error);
        return;
    }

    // An empty id is how the daemon reports a rejection: a file that is not
    // there, not readable, not JSON, or not a layout. Nothing was imported, so
    // say that rather than describing the empty reply.
    const QString newLayoutId = reply.arguments().value(0).toString();
    if (newLayoutId.isEmpty()) {
        const QString error = PhosphorI18n::tr("That file is not a layout this app can read.");
        qCWarning(lcEditor) << "importLayout: daemon rejected" << filePath;
        Q_EMIT layoutLoadFailed(error);
        return;
    }

    // Load the imported layout into the editor
    loadLayout(newLayoutId);
}

/**
 * @brief Exports the current layout to a JSON file
 * @param filePath Path where the JSON file should be saved
 *
 * Calls the D-Bus exportLayout method to save the current layout to a file.
 * The daemon exports its saved copy, so a layout with unsaved edits (a new
 * one included) is refused rather than exported without them. The Export
 * button is disabled in that state, so the refusal is the backstop for a
 * caller that bypasses it.
 * Emits layoutExported on success and layoutExportFailed if the export fails.
 */
void EditorController::exportLayout(const QString& filePath)
{
    if (filePath.isEmpty()) {
        Q_EMIT layoutExportFailed(PhosphorI18n::tr("File path cannot be empty"));
        return;
    }

    if (m_layoutId.isEmpty()) {
        Q_EMIT layoutExportFailed(PhosphorI18n::tr("No layout loaded to export"));
        return;
    }

    if (m_isNewLayout || m_hasUnsavedChanges) {
        Q_EMIT layoutExportFailed(PhosphorI18n::tr("Save the layout before exporting it."));
        return;
    }

    const QDBusMessage reply = PhosphorProtocol::ClientHelpers::syncCall(
        PhosphorProtocol::Service::Interface::LayoutRegistry, QStringLiteral("exportLayout"), {m_layoutId, filePath});
    if (reply.type() != QDBusMessage::ReplyMessage) {
        QString error = PhosphorI18n::tr("Failed to export layout: %1").arg(reply.errorMessage());
        qCWarning(lcEditor) << error;
        Q_EMIT layoutExportFailed(error);
        return;
    }

    // A reply arrived, which only says the daemon was reachable. It answers
    // false for a destination it could not open, write or commit, and that is a
    // ReplyMessage like any other, so reporting saved on the strength of the
    // message type alone would call a failed export a success. It also answers
    // false for a path it refuses as unsafe and for a layout id it does not
    // hold (deleted in another process since this one loaded it). The message
    // names the write, the only cause the user can act on from here.
    if (reply.arguments().isEmpty() || !reply.arguments().first().toBool()) {
        const QString error = PhosphorI18n::tr("Could not write the export. Check that the folder is writable.");
        qCWarning(lcEditor) << "exportLayout: daemon could not write" << filePath;
        Q_EMIT layoutExportFailed(error);
        return;
    }

    Q_EMIT layoutExported();
}

} // namespace PlasmaZones
