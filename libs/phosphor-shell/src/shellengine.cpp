// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/ShellEngine.h>
#include <PhosphorShell/Environment.h>
#include <PhosphorShell/FileView.h>
#include <PhosphorShell/FloatingWindow.h>
#include <PhosphorShell/LazyLoader.h>
#include <PhosphorShell/Toplevels.h>
#include <PhosphorShell/PanelWindow.h>
#include <PhosphorShell/PersistentProperties.h>
#include <PhosphorShell/PopupWindow.h>
#include <PhosphorShell/Process.h>
#include <PhosphorShell/ScreenModel.h>
#include <PhosphorShell/ShellGlobal.h>
#include <PhosphorShell/SystemClock.h>
#include <PhosphorShell/Variants.h>

#include <PhosphorWayland/IdleInhibitor.h>

#include <PhosphorLayer/ILayerShellTransport.h>
#include <PhosphorLayer/IScreenProvider.h>
#include <PhosphorLayer/Role.h>
#include <PhosphorLayer/Surface.h>
#include <PhosphorLayer/SurfaceConfig.h>
#include <PhosphorLayer/SurfaceFactory.h>
#include <PhosphorRendering/ShaderEffect.h>

#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QSize>
#include <QTimer>

#include <cmath>
#include <mutex>

Q_LOGGING_CATEGORY(lcShellEngine, "phosphorshell.engine")

namespace PhosphorShell {

ShellEngine::ShellEngine(Deps deps, QObject* parent)
    : QObject(parent)
    , m_deps(deps)
{
    m_reloadTimer = new QTimer(this);
    m_reloadTimer->setSingleShot(true);
    m_reloadTimer->setInterval(100);
    connect(m_reloadTimer, &QTimer::timeout, this, &ShellEngine::onFileChanged);
}

ShellEngine::~ShellEngine()
{
    teardown();
}

bool ShellEngine::load(const QUrl& shellUrl)
{
    if (shellUrl.isEmpty()) {
        Q_EMIT failed(QStringLiteral("No shell.qml found"));
        return false;
    }
    // Defensive null-check on the deps the load() body unconditionally
    // dereferences (ScreenModel construction, the notifier connect, and
    // surface creation in materializePanels). A consumer that constructs
    // `ShellEngine(Deps{})` with default-null pointers gets a clean
    // failed() signal instead of a crash deep inside the load path.
    if (!m_deps.screenProvider) {
        Q_EMIT failed(QStringLiteral("ShellEngine: screenProvider must be non-null"));
        return false;
    }
    if (!m_deps.surfaceFactory) {
        Q_EMIT failed(QStringLiteral("ShellEngine: surfaceFactory must be non-null"));
        return false;
    }

    m_shellUrl = shellUrl;

    m_screenModel = new ScreenModel(m_deps.screenProvider, this);
    m_shellGlobal = new ShellGlobal(this);
    m_shellGlobal->setScreenModel(m_screenModel);

    // Screen-topology changes drive a hot reload. The notifier is
    // optional — a screen provider need not expose one — so guard the
    // connect; a null notifier means no automatic reload on monitor
    // hotplug / resolution changes (the shell still loads and runs).
    if (auto* notifier = m_deps.screenProvider->notifier()) {
        connect(notifier, &PhosphorLayer::ScreenProviderNotifier::screensChanged, this, &ShellEngine::onScreensChanged);
    } else {
        qCWarning(lcShellEngine)
            << "screenProvider exposes no notifier — shell will not reload on screen-topology changes";
    }

    // QML type registration is process-global (Qt's registry, not per-
    // engine). Guard with std::call_once so multiple ShellEngines in
    // the same process (sequential tests, future multi-shell daemon)
    // don't trip Qt's "type already registered" warning on the second
    // construction. The registrations themselves are unchanged.
    static std::once_flag s_qmlRegistered;
    std::call_once(s_qmlRegistered, [] {
        qmlRegisterType<PanelWindow>("Phosphor.Shell", 1, 0, "PanelWindow");
        qmlRegisterType<PopupWindow>("Phosphor.Shell", 1, 0, "PopupWindow");
        qmlRegisterType<FloatingWindow>("Phosphor.Shell", 1, 0, "FloatingWindow");
        qmlRegisterType<Variants>("Phosphor.Shell", 1, 0, "Variants");
        qmlRegisterType<LazyLoader>("Phosphor.Shell", 1, 0, "LazyLoader");
        qmlRegisterType<Process>("Phosphor.Shell", 1, 0, "Process");
        qmlRegisterType<FileView>("Phosphor.Shell", 1, 0, "FileView");
        qmlRegisterType<PersistentProperties>("Phosphor.Shell", 1, 0, "PersistentProperties");
        qmlRegisterType<PhosphorRendering::ShaderEffect>("Phosphor.Shell", 1, 0, "ShaderBackground");
        // ForeignToplevel is uncreatable from QML — it's only ever vended by
        // Toplevels via the toplevelAdded signal / toplevels list. Registering
        // it as uncreatable lets QML resolve `PhosphorWayland.ForeignToplevel`
        // type names in delegates (`required property var modelData` doesn't
        // need the registration, but `as ForeignToplevel` casts do).
        qmlRegisterUncreatableType<PhosphorWayland::ForeignToplevel>(
            "Phosphor.Shell", 1, 0, "ForeignToplevel",
            QStringLiteral("ForeignToplevel is owned by Toplevels and cannot be constructed from QML"));
        qmlRegisterType<SystemClock>("Phosphor.Shell", 1, 0, "SystemClock");
        // Surface-bound idle inhibition (zwp-idle-inhibit-v1): a QML window keeps
        // its own output awake while visible. This stays a foundation primitive.
        // Session-wide idle monitoring (ext-idle-notify-v1) is NOT registered here:
        // it is owned by Phosphor.Service.Idle's IdleService (a multi-stage timeout
        // policy + surface-less inhibition), registered in src/shell/main.cpp, so a
        // single monitor arms each timeout.
        qmlRegisterType<PhosphorWayland::IdleInhibitor>("Phosphor.Shell", 1, 0, "IdleInhibitor");
        qmlRegisterSingletonType<Toplevels>("Phosphor.Shell", 1, 0, "Toplevels", &Toplevels::create);
    });

    if (!buildAndMaterialize()) {
        return false;
    }
    setupWatcher();
    Q_EMIT loaded();
    return true;
}

bool ShellEngine::buildAndMaterialize()
{
    m_engine = std::make_unique<QQmlEngine>(this);
    m_engine->rootContext()->setContextProperty(QStringLiteral("PhosphorShell"), m_shellGlobal);
    // Bind the Environment singleton on the (possibly fresh) engine. On
    // hot-reload the previous engine's Environment was destroyed with it;
    // QML looking up `Environment.get(...)` on an engine without this
    // line silently returns undefined.
    m_engine->rootContext()->setContextProperty(QStringLiteral("Environment"), new Environment(m_engine.get()));
    // Run the per-engine hooks AFTER our own context-property setup and
    // BEFORE the shell QML is parsed. Image providers, custom context
    // properties, and engine-scoped singletons all need to be in place
    // by the time QQmlComponent walks the QML tree.
    for (const auto& hook : m_engineHooks) {
        if (hook) {
            hook(m_engine.get());
        }
    }

    QQmlComponent component(m_engine.get(), m_shellUrl, QQmlComponent::PreferSynchronous);
    if (component.isError()) {
        const QString errors = component.errorString();
        qCWarning(lcShellEngine) << "Failed to load shell.qml:" << errors;
        Q_EMIT failed(errors);
        return false;
    }

    m_rootObject.reset(component.create());
    if (!m_rootObject) {
        const QString errors = component.errorString();
        qCWarning(lcShellEngine) << "Failed to instantiate shell.qml:" << errors;
        Q_EMIT failed(errors);
        return false;
    }
    m_rootRef = m_rootObject.get();

    materializePanels();
    return true;
}

void ShellEngine::addEngineHook(EngineHook hook)
{
    if (!hook) {
        return;
    }
    m_engineHooks.push_back(std::move(hook));
    // If the engine is already up (caller registered the hook after
    // load()), run the hook against it once so they don't have to
    // worry about ordering. Hot-reload picks it up automatically.
    if (m_engine) {
        m_engineHooks.back()(m_engine.get());
    }
}

QQmlEngine* ShellEngine::engine() const
{
    return m_engine.get();
}

void ShellEngine::teardown()
{
    for (auto& surface : m_surfaces) {
        surface->hide();
    }
    m_surfaces.clear(); // unique_ptr destructors run, no manual delete
    // Drop singleton entries before the QQmlEngine destroys their backing
    // PersistentProperties. QPointer auto-nulls on destruction, so the map
    // stays safe to query, but we'd accumulate one stale (null) entry per
    // reloadId per hot-reload cycle — and the next reload's
    // materializePanels() rebuild walks that growing hash.
    if (m_shellGlobal) {
        m_shellGlobal->clearSingletons();
    }
    m_rootObject.reset();
    m_engine.reset();
}

void ShellEngine::setupWatcher()
{
    if (m_watcher) {
        return;
    }

    m_watcher = new QFileSystemWatcher(this);

    const QString filePath = m_shellUrl.toLocalFile();
    m_watcher->addPath(filePath);

    const QString dir = QFileInfo(filePath).absolutePath();
    m_watcher->addPath(dir);

    auto kickReload = [this]() {
        m_reloadTimer->start();
    };
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, kickReload);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, kickReload);

    qCDebug(lcShellEngine) << "Watching for changes:" << filePath;
}

void ShellEngine::onScreensChanged()
{
    // Coalesce bursts of screen-topology events (KVM switches, lid toggles,
    // and DPMS wake-ups can fire screensChanged several times in quick
    // succession). Routing through the same debounce timer that handles
    // file-change reloads avoids tearing down the engine more than once
    // per topology transition. m_reloadTimer is created unconditionally
    // in the constructor and lives as long as `this`, so no null-guard
    // is needed.
    qCDebug(lcShellEngine) << "Screen topology changed, scheduling shell reload";
    m_reloadTimer->start();
}

void ShellEngine::onFileChanged()
{
    // Triggered by both the file watcher and screen-topology changes
    // (onScreensChanged routes through the same debounce timer), so the
    // message stays neutral about the cause.
    qCDebug(lcShellEngine) << "Reloading shell...";

    savePersistentState();
    teardown();

    // Re-arm the file watch FIRST — editors that save via atomic
    // rename invalidate the old watch on every save, and we want the
    // next save to fire onFileChanged even if THIS reload fails. The
    // earlier rev only re-armed in the success branch, so a single
    // failed reload (broken QML) silenced the watcher and the user
    // had to restart the shell to recover.
    if (m_watcher) {
        const QString filePath = m_shellUrl.toLocalFile();
        if (!m_watcher->files().contains(filePath)) {
            m_watcher->addPath(filePath);
        }
    }

    // buildAndMaterialize() rebuilds the QQmlEngine and re-runs the
    // engine hooks. It deliberately does NOT call qmlRegisterType: those
    // are PROCESS-global, set up once in load() at startup. A new
    // QQmlEngine sees the existing registrations and resolves
    // "Phosphor.Shell" types correctly. If a future Qt scopes
    // registrations per-engine, that assumption would need revisiting —
    // current Qt 6.x keeps the registry global.
    if (!buildAndMaterialize()) {
        return;
    }
    restorePersistentState();

    qCDebug(lcShellEngine) << "Reload complete," << m_surfaces.size() << "surface(s)";
    Q_EMIT reloaded();
}

static PhosphorLayer::Anchor edgeToAnchor(PanelWindow::Edge edge)
{
    switch (edge) {
    case PanelWindow::Top:
        return PhosphorLayer::Anchor::Top;
    case PanelWindow::Bottom:
        return PhosphorLayer::Anchor::Bottom;
    case PanelWindow::Left:
        return PhosphorLayer::Anchor::Left;
    case PanelWindow::Right:
        return PhosphorLayer::Anchor::Right;
    }
    Q_UNREACHABLE_RETURN(PhosphorLayer::Anchor::Top);
}

void ShellEngine::materializePanels()
{
    QList<PanelWindow*> panels;

    auto* rootPanel = qobject_cast<PanelWindow*>(m_rootObject.get());
    if (rootPanel) {
        panels.append(rootPanel);
    }

    const auto children = m_rootObject->findChildren<PanelWindow*>();
    panels.append(children);

    for (PanelWindow* panel : panels) {
        if (panel->alignment() != PanelWindow::Fill && panel->panelLength() < 0) {
            qCWarning(lcShellEngine) << "PanelWindow has alignment=" << panel->alignment()
                                     << "but panelLength=-1 (Fill) — set panelLength to a non-negative"
                                     << " value (0 = auto-fit, >0 = explicit pin) or change alignment to Fill";
        }
        QScreen* targetScreen = panel->screen() ? panel->screen() : m_deps.screenProvider->primary();
        if (!targetScreen) {
            qCWarning(lcShellEngine) << "Skipping PanelWindow: no screen available "
                                     << "(neither panel.screen nor screenProvider.primary())";
            continue;
        }
        const QSize screenSize = targetScreen->size();
        const bool horizontal = (panel->edge() == PanelWindow::Top || panel->edge() == PanelWindow::Bottom);
        const PhosphorLayer::Anchor primaryAnchor = edgeToAnchor(panel->edge());

        PhosphorLayer::Anchors anchors;
        QMargins layerMargins = panel->margins();
        QSize panelSize;
        // Surface-axis thickness includes the shadow strip; the
        // exclusiveZone advertised to the compositor stays at the
        // VISIBLE thickness so other windows don't reserve the
        // shadow space. The shader is responsible for rendering the
        // shadow into the extra strip.
        const int surfaceThickness = panel->thickness() + panel->shadowSize();

        if (panel->alignment() == PanelWindow::Fill || panel->panelLength() < 0) {
            if (horizontal) {
                anchors = primaryAnchor | PhosphorLayer::Anchor::Left | PhosphorLayer::Anchor::Right;
                panelSize = QSize(screenSize.width(), surfaceThickness);
            } else {
                anchors = primaryAnchor | PhosphorLayer::Anchor::Top | PhosphorLayer::Anchor::Bottom;
                panelSize = QSize(surfaceThickness, screenSize.height());
            }
        } else {
            int length = panel->panelLength();
            if (length == 0) {
                // Auto-fit: derive length from the panel's implicit size.
                //
                // The user binds implicitWidth (or implicitHeight for vertical
                // panels) to the content's implicit size — typically a Row/
                // Column inside the panel:
                //
                //     PanelWindow {
                //         implicitWidth: contentRow.implicitWidth
                //         Row { id: contentRow; ... }
                //     }
                //
                // We deliberately do NOT measure panel->childrenRect() because
                // decorative items anchored to the panel (anchors.fill: parent
                // for a ShaderBackground; anchors.centerIn: parent for a content
                // Row) make childrenRect depend on panel.width — and we'd be
                // *setting* panel.width from that measurement. Qt's anchor system
                // detects the cycle ("Possible anchor loop detected on centerIn")
                // and freezes the affected anchors, leaving content stuck at
                // its first-measured position. Using implicitWidth, which the
                // user binds to a child whose size is independent of the panel,
                // breaks the cycle.
                //
                // Falls back to the QML width/height when implicitWidth is unset
                // so callers that prefer explicit sizing still work.
                const qreal implicitLength = horizontal ? panel->implicitWidth() : panel->implicitHeight();
                if (implicitLength > 0.0) {
                    length = qMax(1, static_cast<int>(std::ceil(implicitLength)));
                } else {
                    length = horizontal ? qMax(1, static_cast<int>(panel->width()))
                                        : qMax(1, static_cast<int>(panel->height()));
                }
            }

            switch (panel->alignment()) {
            case PanelWindow::Start:
                if (horizontal) {
                    anchors = primaryAnchor | PhosphorLayer::Anchor::Left;
                    panelSize = QSize(length, surfaceThickness);
                } else {
                    anchors = primaryAnchor | PhosphorLayer::Anchor::Top;
                    panelSize = QSize(surfaceThickness, length);
                }
                break;
            case PanelWindow::End:
                if (horizontal) {
                    anchors = primaryAnchor | PhosphorLayer::Anchor::Right;
                    panelSize = QSize(length, surfaceThickness);
                } else {
                    anchors = primaryAnchor | PhosphorLayer::Anchor::Bottom;
                    panelSize = QSize(surfaceThickness, length);
                }
                break;
            case PanelWindow::Center:
                if (horizontal) {
                    anchors = primaryAnchor | PhosphorLayer::Anchor::Left | PhosphorLayer::Anchor::Right;
                    const int margin = (screenSize.width() - length) / 2;
                    layerMargins.setLeft(qMax(layerMargins.left(), margin));
                    layerMargins.setRight(qMax(layerMargins.right(), margin));
                    panelSize = QSize(length, surfaceThickness);
                } else {
                    anchors = primaryAnchor | PhosphorLayer::Anchor::Top | PhosphorLayer::Anchor::Bottom;
                    const int margin = (screenSize.height() - length) / 2;
                    layerMargins.setTop(qMax(layerMargins.top(), margin));
                    layerMargins.setBottom(qMax(layerMargins.bottom(), margin));
                    panelSize = QSize(surfaceThickness, length);
                }
                break;
            case PanelWindow::Fill:
                break;
            }
        }

        PhosphorLayer::Role role;
        role = role.withAnchors(anchors).withScopePrefix(QStringLiteral("phosphor-shell"));
        // Per-panel keyboard interactivity from the QML property —
        // defaults to None so clicking the panel doesn't steal focus
        // from the user's active app (matches Plasma's panel
        // behaviour). Popups attached to the panel still get their
        // own xdg_popup grab and can receive keyboard input even
        // when the parent panel is None.
        PhosphorLayer::KeyboardInteractivity interactivity = PhosphorLayer::KeyboardInteractivity::None;
        switch (panel->keyboardFocus()) {
        case PanelWindow::None:
            interactivity = PhosphorLayer::KeyboardInteractivity::None;
            break;
        case PanelWindow::OnDemand:
            interactivity = PhosphorLayer::KeyboardInteractivity::OnDemand;
            break;
        case PanelWindow::Exclusive:
            interactivity = PhosphorLayer::KeyboardInteractivity::Exclusive;
            break;
        }
        role = role.withKeyboard(interactivity);

        switch (panel->panelLayer()) {
        case PanelWindow::LayerBackground:
            role = role.withLayer(PhosphorLayer::Layer::Background);
            break;
        case PanelWindow::LayerBottom:
            role = role.withLayer(PhosphorLayer::Layer::Bottom);
            break;
        case PanelWindow::LayerTop:
            role = role.withLayer(PhosphorLayer::Layer::Top);
            break;
        case PanelWindow::LayerOverlay:
            role = role.withLayer(PhosphorLayer::Layer::Overlay);
            break;
        }

        if (panel->alignment() == PanelWindow::Fill && panel->exclusiveZoneEnabled()) {
            role = role.withExclusiveZone(panel->thickness());
        } else if (panel->exclusiveZone() >= 0) {
            role = role.withExclusiveZone(panel->exclusiveZone());
        } else {
            role = role.withExclusiveZone(0);
        }

        panel->setWidth(panelSize.width());
        panel->setHeight(panelSize.height());

        // Detach panel from its QML parent BEFORE wrapping in unique_ptr.
        // findChildren returned panel through its QObject parent chain, so
        // both the QML root AND the unique_ptr would otherwise own it; if
        // surfaceFactory->create() returns null below, the unique_ptr
        // destructs and deletes panel, then m_rootObject.reset() during
        // teardown would double-free. Surface re-parents panel to its
        // wrapper QQuickWindow on success.
        // Order: clear visual parent BEFORE QObject parent so QQuickItem's
        // visual-parent bookkeeping doesn't observe a transient QObject-
        // parent mismatch (matches Qt practice elsewhere in the codebase).
        panel->setParentItem(nullptr);
        panel->setParent(nullptr);

        // Build the SurfaceConfig FIRST, then atomically transfer ownership.
        // Constructing cfg before the release keeps panel owned by
        // m_rootObject across any throwing operations on cfg's other
        // fields (role, debugName, etc.) — only the unique_ptr move is
        // noexcept, so we can pair `release()` and `cfg.contentItem =`
        // adjacently without an exception window in between.
        PhosphorLayer::SurfaceConfig cfg;
        cfg.role = role;
        cfg.screen = targetScreen;
        cfg.initialSize = panelSize;
        cfg.marginsOverride = layerMargins;
        cfg.debugName = QStringLiteral("phosphor-shell-panel");

        // Hand off rootPanel ownership in one noexcept step:
        // unique_ptr::release returns a raw pointer and clears its source;
        // the unique_ptr ctor that takes a raw pointer is also noexcept.
        // No exception can fire between these two lines, so the panel is
        // never orphaned. For non-rootPanel iterations the release is a
        // no-op-equivalent because m_rootObject still holds the QML root
        // (the wrapping Item), not this panel.
        if (panel == rootPanel) {
            // m_rootRef tracks the live root via QPointer regardless of
            // unique_ptr ownership, so we discard the released raw
            // pointer — explicit (void) silences any [[nodiscard]] from
            // a hardened C++23 stdlib build.
            (void)m_rootObject.release();
        }
        cfg.contentItem = std::unique_ptr<QQuickItem>(panel);

        // Pass nullptr as parent — m_surfaces (vector of unique_ptr) is
        // the single owner. Passing `this` would double-own (QObject parent
        // + unique_ptr) and double-free during ~ShellEngine.
        const bool wasRootPanel = (panel == rootPanel);
        auto* surface = m_deps.surfaceFactory->create(std::move(cfg), nullptr);
        if (surface) {
            surface->show();
            m_surfaces.emplace_back(surface);
            qCDebug(lcShellEngine) << "Created panel surface on edge" << panel->edge() << "alignment"
                                   << panel->alignment() << "size" << panelSize;

            // Dynamic auto-fit: when panelLength == 0, the panel resizes to
            // follow content changes (clock text loading, fluctuating CPU%
            // strings, etc.). The Fill case already spans the screen so it
            // never needs this; non-zero panelLength is an explicit pin.
            if (panel->panelLength() == 0 && panel->alignment() != PanelWindow::Fill) {
                installDynamicAutoFit(panel, surface, screenSize);
            }
        } else {
            qCWarning(lcShellEngine) << "Failed to create surface for PanelWindow";
            // For child panels we soldier on — losing one panel still
            // leaves a usable shell. For the ROOT panel, the cfg.contentItem
            // destructor has already deleted the QML root and m_rootRef
            // QPointer auto-cleared. m_rootObject was released
            // up-front so there's nothing to roll back, but every later
            // step (PersistentProperties scan, hot-reload save/restore)
            // would silently no-op against the null root. Fail loudly
            // and tear down what we've built so the embedder can react
            // (retry, fallback, exit) instead of running headless.
            if (wasRootPanel) {
                qCCritical(lcShellEngine) << "Root panel surface creation failed — aborting load";
                m_surfaces.clear();
                Q_EMIT failed(QStringLiteral("Failed to create surface for root PanelWindow"));
                return;
            }
        }
    }

    // Singletons were cleared in teardown() before this engine was built;
    // the registerSingleton() loop below populates the map for this
    // generation. Use m_rootRef rather than m_rootObject — the loop above
    // releases m_rootObject when the QML root is a PanelWindow (Surface
    // takes ownership via cfg.contentItem). m_rootRef (QPointer) still
    // tracks the live root regardless of which path we took, so
    // findChildren works for both rootPanel and Item-rooted shells.
    if (m_rootRef) {
        const auto persists = m_rootRef->findChildren<PersistentProperties*>();
        for (auto* p : persists) {
            if (!p->reloadId().isEmpty()) {
                m_shellGlobal->registerSingleton(p->reloadId(), p);
            }
        }
    }
}

void ShellEngine::installDynamicAutoFit(PanelWindow* panel, PhosphorLayer::Surface* surface, QSize screenSize)
{
    // Re-derive the auto-fit length and (for Center alignment) margins each
    // time the panel's implicit size changes — i.e. when whatever the user
    // bound implicitWidth/implicitHeight to (typically a content Row's
    // implicitWidth) updates. Listening on implicit-size signals instead of
    // childrenRectChanged sidesteps the panel.width ↔ children-anchor cycle
    // documented in materializePanels().
    //
    // The connect target is `surface`, so Qt auto-disconnects the lambda
    // when the surface is destroyed (Surface owns the panel as its rootItem,
    // so they share a lifetime).
    const PanelWindow::Edge edge = panel->edge();
    const PanelWindow::Alignment alignment = panel->alignment();
    // Surface thickness — includes shadowSize. Matches the size we
    // initially passed to the surface factory; using `thickness` alone
    // here would shrink the surface on the first auto-fit tick and
    // clip the shadow strip.
    const int surfaceThickness = panel->thickness() + panel->shadowSize();
    const QMargins userMargins = panel->margins();
    const bool horizontal = (edge == PanelWindow::Top || edge == PanelWindow::Bottom);

    auto resize = [panel, surface, horizontal, alignment, surfaceThickness, userMargins, screenSize]() {
        auto* window = surface->window();
        if (!window) {
            return;
        }
        auto* handle = surface->transport();
        if (!handle) {
            return;
        }
        const qreal implicitLength = horizontal ? panel->implicitWidth() : panel->implicitHeight();
        if (implicitLength <= 0.0) {
            return;
        }
        const int newLength = qMax(1, static_cast<int>(std::ceil(implicitLength)));
        const QSize newSize = horizontal ? QSize(newLength, surfaceThickness) : QSize(surfaceThickness, newLength);
        if (window->size() == newSize) {
            return;
        }

        // Compositor-driven resize path (mirrors Quickshell's WlrLayershell).
        // We deliberately do NOT call `window->resize()` from app code:
        // QWindow::resize is silently clamped against `windowMinimumSize` /
        // `windowMaximumSize` (QTBUG-118604), so client-initiated resizes drop
        // to the old size and the buffer never grows. Instead we push the new
        // size through the layer-shell protocol via setDesiredSize, which the
        // QPA reads in applyProperties() and sends as zwlr_layer_surface_v1::
        // set_size + wl_surface_commit. The compositor's configure response
        // then drives Qt's actual QWindow resize through resizeFromApplyConfigure
        // (see LayerShellWindow::handleConfigure), which is the path Qt expects
        // and does NOT clamp.
        //
        // For Center alignment also update margins so the panel stays centred
        // as it grows (margins are how the wlr-layer-shell protocol expresses
        // centred positioning when both Left+Right anchors are set).
        if (alignment == PanelWindow::Center) {
            QMargins newMargins = userMargins;
            if (horizontal) {
                const int margin = (screenSize.width() - newLength) / 2;
                newMargins.setLeft(qMax(userMargins.left(), margin));
                newMargins.setRight(qMax(userMargins.right(), margin));
            } else {
                const int margin = (screenSize.height() - newLength) / 2;
                newMargins.setTop(qMax(userMargins.top(), margin));
                newMargins.setBottom(qMax(userMargins.bottom(), margin));
            }
            handle->setMargins(newMargins);
        }
        handle->setDesiredSize(newSize);
    };

    if (horizontal) {
        QObject::connect(panel, &QQuickItem::implicitWidthChanged, surface, resize);
    } else {
        QObject::connect(panel, &QQuickItem::implicitHeightChanged, surface, resize);
    }
}

void ShellEngine::savePersistentState()
{
    // Use m_rootRef (non-owning QPointer) instead of m_rootObject because the
    // root may have been a PanelWindow whose ownership was transferred to a
    // Surface in materializePanels — m_rootObject is then released, but the
    // QObject is still alive (parented to the wrapper window) and m_rootRef
    // still tracks it.
    if (!m_rootRef) {
        return;
    }

    m_persistentState.clear();
    const auto persists = m_rootRef->findChildren<PersistentProperties*>();
    for (const auto* p : persists) {
        if (!p->reloadId().isEmpty()) {
            m_persistentState[p->reloadId()] = p->saveState();
        }
    }
    qCDebug(lcShellEngine) << "Saved" << m_persistentState.size() << "persistent state(s)";
}

void ShellEngine::restorePersistentState()
{
    if (!m_rootRef || m_persistentState.isEmpty()) {
        return;
    }

    const auto persists = m_rootRef->findChildren<PersistentProperties*>();
    for (auto* p : persists) {
        if (m_persistentState.contains(p->reloadId())) {
            p->restoreState(m_persistentState[p->reloadId()]);
        }
    }
    qCDebug(lcShellEngine) << "Restored persistent state(s)";
}

} // namespace PhosphorShell
