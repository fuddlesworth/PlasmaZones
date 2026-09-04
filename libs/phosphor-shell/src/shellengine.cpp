// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/ShellEngine.h>
#include <PhosphorShell/Environment.h>
#include <PhosphorShell/FileView.h>
#include <PhosphorShell/FloatingWindow.h>
#include <PhosphorShell/LazyLoader.h>
#include <PhosphorShell/Toplevels.h>
#include <PhosphorShell/Workspaces.h>
#include <PhosphorShell/PanelWindow.h>
#include <PhosphorShell/PerScreenPanels.h>
#include <PhosphorShell/PersistentProperties.h>
#include <PhosphorShell/PopupWindow.h>
#include <PhosphorShell/Process.h>
#include <PhosphorShell/ScreenModel.h>
#include <PhosphorShell/ShellGlobal.h>
#include <PhosphorShell/SystemClock.h>
#include <PhosphorShell/SystemUsage.h>
#include <PhosphorShell/Variants.h>

#include <PhosphorWayland/IdleInhibitor.h>

#include <PhosphorLayer/ILayerShellTransport.h>
#include <PhosphorLayer/IScreenProvider.h>
#include <PhosphorLayer/Role.h>
#include <PhosphorLayer/Surface.h>
#include <PhosphorLayer/SurfaceConfig.h>
#include <PhosphorLayer/SurfaceFactory.h>
#include <PhosphorRendering/ShaderEffect.h>

#include <QPointer>
#include <QRect>
#include <QRegion>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QLoggingCategory>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QSize>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>

namespace {
// Coalescing window for a rebuild. It serves TWO requirements, which is worth
// knowing before retuning it: an editor writing shell.qml (often several
// events per save), and a screen-topology change routed through the same
// timer so a KVM switch or lid toggle does not rebuild once per output. A
// physical transition can span longer than this, in which case it drives more
// than one rebuild; raising it to fix that would also delay every hot reload.
constexpr int kReloadDebounceMs = 100;
} // namespace

Q_LOGGING_CATEGORY(lcShellEngine, "phosphorshell.engine")

namespace PhosphorShell {

ShellEngine::ShellEngine(Deps deps, QObject* parent)
    : QObject(parent)
    , m_deps(deps)
{
    m_reloadTimer = new QTimer(this);
    m_reloadTimer->setSingleShot(true);
    m_reloadTimer->setInterval(kReloadDebounceMs);
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

    // Single-shot. A second load() would construct a second ScreenModel and
    // ShellGlobal parented to `this`, leaking the first pair for this object's
    // lifetime, orphaning the old singleton map, and duplicating the
    // screensChanged connection. Recovery after a failed load is a fresh
    // ShellEngine, not a retry on this one.
    if (m_screenModel) {
        Q_EMIT failed(QStringLiteral("ShellEngine: load() has already run on this instance"));
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
        // One panel per screen. Distinct from Variants and PerScreen
        // because materializePanels() takes ownership of what it finds:
        // instances must be QObject children to be discovered at all, and
        // their creator must never destroy them afterwards. See the class
        // docs for why the stock instantiators cannot satisfy both.
        qmlRegisterType<PerScreenPanels>("Phosphor.Shell", 1, 0, "PerScreenPanels");
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
        // CPU / memory sampling. Kept in C++ rather than parsed from
        // /proc in QML: the jiffy-delta arithmetic and the malformed-layout
        // handling are logic, not presentation.
        qmlRegisterType<SystemUsage>("Phosphor.Shell", 1, 0, "SystemUsage");
        // Surface-bound idle inhibition (zwp-idle-inhibit-v1): a QML window keeps
        // its own output awake while visible. This stays a foundation primitive.
        // Session-wide idle monitoring (ext-idle-notify-v1) is NOT registered here:
        // it is owned by Phosphor.Service.Idle's IdleService (a multi-stage timeout
        // policy + surface-less inhibition), registered in src/shell/main.cpp, so a
        // single monitor arms each timeout.
        qmlRegisterType<PhosphorWayland::IdleInhibitor>("Phosphor.Shell", 1, 0, "IdleInhibitor");
        qmlRegisterSingletonType<Toplevels>("Phosphor.Shell", 1, 0, "Toplevels", &Toplevels::create);
        // Compositor workspaces (KWin's virtual desktops today). A
        // singleton for the same reason Toplevels is one: the underlying
        // manager holds one D-Bus subscription per process.
        qmlRegisterSingletonType<Workspaces>("Phosphor.Shell", 1, 0, "Workspaces", &Workspaces::create);
    });

    // Watch BEFORE building. A failed initial load still leaves the watcher
    // armed, so editing the offending shell.qml recovers the process rather
    // than requiring a restart.
    setupWatcher();
    if (!buildAndMaterialize()) {
        return false;
    }
    Q_EMIT loaded();
    return true;
}

bool ShellEngine::buildAndMaterialize()
{
    // No QObject parent: the unique_ptr is the SOLE owner, and every path
    // that discards the engine (teardown, reload, destruction) resets it.
    // Parenting to `this` as well would double-own — safe only while
    // ~ShellEngine happens to run teardown() before ~QObject sweeps
    // children, an ordering nothing enforces.
    m_engine = std::make_unique<QQmlEngine>();
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
    // Index, not a range-for. addEngineHook appends, and the header does not
    // forbid a hook from registering another, so iterating by reference over
    // a vector that can reallocate mid-loop is undefined behaviour. Snapshot
    // the size too, so a hook added during this pass is not also driven here:
    // addEngineHook already invokes a late arrival itself once the engine
    // exists, and running it twice on the same engine is worse than not
    // running it at all.
    const size_t hookCount = m_engineHooks.size();
    for (size_t i = 0; i < hookCount && i < m_engineHooks.size(); ++i) {
        const auto& hook = m_engineHooks.at(i);
        if (hook) {
            hook(m_engine.get());
        }
    }

    // Every failure exit below takes the SAME shape: tear down, then emit
    // `failed`. Uniform so `engine()` is null after any failure rather than
    // non-null after some and null after others, and so a handler that
    // rebuilds synchronously is not undone by a teardown running after it.
    const auto fail = [this](const QString& reason) {
        teardown();
        Q_EMIT failed(reason);
        return false;
    };

    QQmlComponent component(m_engine.get(), m_shellUrl, QQmlComponent::PreferSynchronous);
    if (component.isError()) {
        const QString errors = component.errorString();
        qCWarning(lcShellEngine) << "Failed to load shell.qml:" << errors;
        return fail(errors);
    }

    m_rootObject.reset(component.create());
    if (!m_rootObject) {
        const QString errors = component.errorString();
        qCWarning(lcShellEngine) << "Failed to instantiate shell.qml:" << errors;
        return fail(errors);
    }
    m_rootRef = m_rootObject.get();

    // A false return means the ROOT panel's surface failed. Its QML root is
    // already destroyed, so every later step (PersistentProperties scan,
    // hot-reload save/restore) would no-op against a null root and the shell
    // would run headless with no surfaces.
    QString materializeError;
    if (!materializePanels(&materializeError)) {
        return fail(materializeError);
    }
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

QMargins ShellEngine::reservedMarginsFor(QScreen* screen) const
{
    QMargins margins;
    if (!screen) {
        return margins;
    }
    for (const ReservedEdge& reserved : m_reserved) {
        // A dead QPointer compares equal to nullptr, never to a live screen.
        if (reserved.screen != screen || reserved.zone <= 0) {
            continue;
        }
        // Largest wins per edge: two panels reserving the same edge overlap
        // from the popout's point of view, they do not stack.
        switch (static_cast<PanelWindow::Edge>(reserved.edge)) {
        case PanelWindow::Top:
            margins.setTop(std::max(margins.top(), reserved.zone));
            break;
        case PanelWindow::Bottom:
            margins.setBottom(std::max(margins.bottom(), reserved.zone));
            break;
        case PanelWindow::Left:
            margins.setLeft(std::max(margins.left(), reserved.zone));
            break;
        case PanelWindow::Right:
            margins.setRight(std::max(margins.right(), reserved.zone));
            break;
        }
    }
    return margins;
}

void ShellEngine::teardown()
{
    for (auto& surface : m_surfaces) {
        surface->hide();
    }
    m_surfaces.clear(); // unique_ptr destructors run, no manual delete
    // The reservations describe the surfaces just torn down; a reload
    // rebuilds both together.
    m_reserved.clear();
    // Same generation, same lifetime. savePersistentState() runs before
    // teardown, so clearing here does not cost the state it collected.
    m_panels.clear();
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
    // A qrc: shell URL has no local file to watch, and it is a reachable case
    // (ShellLoader falls back to the bundled example). Without this the two
    // addPath calls below would each log "path is empty" at every startup.
    if (!m_shellUrl.isLocalFile()) {
        qCDebug(lcShellEngine) << "shell URL is not a local file; hot reload disabled";
        return;
    }
    if (m_watcher) {
        return;
    }

    m_watcher = new QFileSystemWatcher(this);

    // Both returns are checked. When the per-user inotify watch limit is
    // exhausted these fail, and hot reload then stops working for the life of
    // the process with nothing logged at any level: the re-arm below can only
    // run from a reload, and the missing watch is what would have caused one.
    const QString filePath = m_shellUrl.toLocalFile();
    if (!m_watcher->addPath(filePath)) {
        qCWarning(lcShellEngine) << "could not watch" << filePath
                                 << "— hot reload is disabled (inotify watch limit reached?)";
    }

    const QString dir = QFileInfo(filePath).absolutePath();
    if (!m_watcher->addPath(dir)) {
        qCWarning(lcShellEngine) << "could not watch" << dir << "— an atomic-rename save will not trigger a reload";
    }

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

    // Before savePersistentState, and well before teardown: consumers
    // holding objects created by the outgoing engine must drop them while
    // that engine still exists.
    Q_EMIT aboutToReload();

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
        if (!m_watcher->files().contains(filePath) && !m_watcher->addPath(filePath)) {
            qCWarning(lcShellEngine) << "could not re-arm the watch on" << filePath << "— hot reload is now disabled";
        }
        // The DIRECTORY watch can drop the same way: replacing or
        // recreating the shell.qml directory (atomic dir swaps, build
        // steps) silently invalidates it, after which rename-style saves
        // stop triggering reloads until restart. Re-arm it alongside the
        // file watch.
        const QString dirPath = QFileInfo(filePath).absolutePath();
        if (!m_watcher->directories().contains(dirPath) && !m_watcher->addPath(dirPath)) {
            qCWarning(lcShellEngine) << "could not re-arm the watch on" << dirPath
                                     << "— rename-style saves will not trigger a reload";
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

bool ShellEngine::materializePanels(QString* failureReason)
{
    // QPointer, not raw: a panel whose surface fails is destroyed by
    // cfg.contentItem's destructor, and a NESTED panel underneath it dies with
    // its parent while still sitting in this list. A raw pointer would be
    // dereferenced on the next iteration.
    QList<QPointer<PanelWindow>> panels;

    auto* rootPanel = qobject_cast<PanelWindow*>(m_rootObject.get());
    if (rootPanel) {
        panels.append(rootPanel);
    }

    const auto children = m_rootObject->findChildren<PanelWindow*>();
    panels.reserve(panels.size() + children.size());
    for (PanelWindow* child : children) {
        panels.append(child);
    }

    for (const QPointer<PanelWindow>& panelRef : panels) {
        // A panel destroyed as collateral of an earlier panel's failure.
        if (!panelRef) {
            continue;
        }
        PanelWindow* panel = panelRef;
        // A panel nested inside another panel is silently re-hosted: the outer
        // one's Surface adopts the whole subtree, then this iteration tears the
        // inner one back out and gives it a surface of its own. Safe, but the
        // QML author never asked for it and gets no other signal.
        for (QObject* ancestor = panel->parent(); ancestor; ancestor = ancestor->parent()) {
            if (qobject_cast<PanelWindow*>(ancestor)) {
                qCWarning(lcShellEngine)
                    << "PanelWindow is nested inside another PanelWindow; it will be detached and given its own"
                    << "layer surface rather than rendered inside its parent";
                break;
            }
        }
        // ONE definition of "this panel behaves as Fill", used by the sizing
        // branch AND the exclusive-zone branches below. The fallback case
        // (non-Fill alignment with panelLength=-1) spans the whole edge, so
        // it must also reserve like a Fill panel; testing alignment alone in
        // the zone branch left the fallback spanning the edge with zone 0
        // and windows tiling underneath it.
        const bool effectiveFill = panel->alignment() == PanelWindow::Fill || panel->panelLength() < 0;
        if (panel->alignment() != PanelWindow::Fill && panel->panelLength() < 0) {
            qCWarning(lcShellEngine) << "PanelWindow has alignment=" << panel->alignment()
                                     << "but panelLength=-1 (Fill) — set panelLength to a non-negative"
                                     << " value (0 = auto-fit, >0 = explicit pin) or change alignment to Fill."
                                     << "Falling back to Fill for this panel.";
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

        if (effectiveFill) {
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

        if (panel->panelLayer() == PanelWindow::LayerOverlay) {
            // Role::isValid REJECTS an Overlay that reserves or respects a
            // zone, and the factory refuses to create on an invalid role, so
            // every branch below would make an overlay panel fail outright.
            // -1 is the only value an overlay can carry.
            // exclusiveZoneEnabled defaults to TRUE, so testing it alone
            // would warn for every overlay panel that never asked for
            // anything. Warn only when a branch below would really have
            // reserved a zone.
            const bool wouldHaveReserved =
                panel->exclusiveZone() >= 0 || (effectiveFill && panel->exclusiveZoneEnabled());
            if (wouldHaveReserved) {
                qCWarning(lcShellEngine)
                    << "PanelWindow asks for an exclusive zone on the Overlay layer, which cannot reserve one;"
                    << "ignoring the zone request";
            }
            role = role.withExclusiveZone(-1);
        } else if (effectiveFill && panel->exclusiveZoneEnabled()) {
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

        // m_surfaces (a vector of unique_ptr) is the single owner — but
        // the factory does NOT hand back an unparented object. `create`
        // ends in `parent ? parent : this`, so a null parent parents the
        // Surface to the FACTORY, and the unique_ptr below would then be a
        // second owner. That is safe today only because main declares the
        // factory before the engine, so reverse destruction happens to run
        // these unique_ptrs first; any reordering makes it a double free.
        // Detach explicitly right after creation so the unique_ptr really
        // is the sole owner, rather than relying on declaration order.
        const bool wasRootPanel = (panel == rootPanel);
        // Own it immediately. create() parents the Surface to the FACTORY
        // when the parent argument is null, so the setParent(nullptr) below
        // detaches it and leaves this unique_ptr the sole owner — do not
        // delete that line as redundant. Until the surface lands in
        // m_surfaces the unique_ptr is the only thing that will free it, and
        // the Failed branch below needs to DESTROY it rather than just stop
        // referring to it. The surface has already adopted the panel as its
        // content item, so destroying it takes the panel with it.
        std::unique_ptr<PhosphorLayer::Surface> ownedSurface(m_deps.surfaceFactory->create(std::move(cfg), nullptr));
        if (ownedSurface) {
            ownedSurface->setParent(nullptr);
            ownedSurface->show();
            // AFTER show(), not before. create() only constructs — it never
            // warms or attaches — so the state is always Constructed on
            // return and a pre-show check can never see Failed. The
            // transition happens inside show()'s warm/attach drive, where a
            // content error or a transport rejection lands.
            //
            // reset() rather than abandoning the pointer: the factory owns
            // whatever it hands back when the parent argument is null, so
            // simply dropping the local would leave a Failed surface alive
            // for the process lifetime still holding the panel (and, for a
            // root panel, the whole QML root) — with m_rootRef non-null, so
            // the fail-loud branch below would not even fire correctly.
            if (ownedSurface->state() == PhosphorLayer::Surface::State::Failed) {
                qCWarning(lcShellEngine) << "Panel surface reached Failed state after show()";
                ownedSurface.reset();
            }
        }
        if (ownedSurface) {
            auto* surface = ownedSurface.get();
            m_surfaces.emplace_back(std::move(ownedSurface));
            // What this panel reserved, for reservedMarginsFor(). The zone
            // recorded is the Role's, i.e. what was actually advertised —
            // an Overlay panel that asked for one gets -1 and reserves
            // nothing, and that is what a popout hanging from it must see.
            // Record the RESOLVED screen, not the panel's own property. A
            // panel that leaves `screen` unset is materialized on the primary
            // output, and storing the unset value would leave a null QPointer
            // here that never matches a live screen, so reservedMarginsFor()
            // would report zero margins for a panel that is genuinely
            // reserving space.
            m_reserved.push_back(
                {QPointer<QScreen>(targetScreen), static_cast<int>(panel->edge()), role.exclusiveZone});
            // The panel is no longer a descendant of the QML root (it was
            // detached above and handed to the Surface), so record it here
            // or collectPersistentProperties() cannot see inside it.
            m_panels.emplace_back(panel);
            qCDebug(lcShellEngine) << "Created panel surface on edge" << panel->edge() << "alignment"
                                   << panel->alignment() << "size" << panelSize;

            // Dynamic auto-fit: when panelLength == 0, the panel resizes to
            // follow content changes (clock text loading, fluctuating CPU%
            // strings, etc.). The Fill case already spans the screen so it
            // never needs this; non-zero panelLength is an explicit pin.
            if (panel->panelLength() == 0 && panel->alignment() != PanelWindow::Fill) {
                installDynamicAutoFit(panel, surface, screenSize);
            }

            // Confine pointer input to the visible band. Must run AFTER
            // show(): setMask is a silent no-op with no platform window
            // and Qt never re-applies it.
            installInputRegion(panel, surface);
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
                // Keep the two in step. The caller's failure path runs
                // teardown(), which clears both, but leaving reservations
                // that describe destroyed surfaces here would bite the first
                // time this function grows a second failure exit.
                m_reserved.clear();
                m_panels.clear();
                // Report rather than emit. `failed` has to reach the consumer
                // AFTER teardown, or a handler that synchronously rebuilds
                // would have its fresh engine destroyed by the teardown that
                // runs when this returns.
                if (failureReason) {
                    *failureReason = QStringLiteral("Failed to create surface for root PanelWindow");
                }
                return false;
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
    const auto persists = collectPersistentProperties();
    for (auto* p : persists) {
        if (!p->reloadId().isEmpty()) {
            m_shellGlobal->registerSingleton(p->reloadId(), p);
        }
    }
    return true;
}

QList<PersistentProperties*> ShellEngine::collectPersistentProperties() const
{
    QList<PersistentProperties*> found;
    const auto absorb = [&found](QObject* subtree) {
        if (!subtree) {
            return;
        }
        const auto children = subtree->findChildren<PersistentProperties*>();
        for (auto* p : children) {
            // The root may itself be one of the materialized panels, and a
            // panel nested inside another would be reached twice, so dedupe
            // rather than registering or saving the same object repeatedly.
            if (!found.contains(p)) {
                found.append(p);
            }
        }
    };

    absorb(m_rootRef);
    for (const auto& panel : m_panels) {
        absorb(panel);
    }
    return found;
}

void ShellEngine::installInputRegion(PanelWindow* panel, PhosphorLayer::Surface* surface)
{
    // The surface is `thickness + shadowSize` deep on the edge-perpendicular
    // axis (see surfaceThickness above), but only `thickness` of it is
    // painted. The remainder exists purely so a drop shadow has somewhere to
    // render. Without an input region that transparent strip is still part
    // of the wl_surface, so it swallows clicks along the edge of whatever
    // tiles beneath the panel — and since the advertised exclusiveZone is
    // `thickness`, windows are placed exactly where the strip overlaps them.
    //
    // QWindow::setMask is the supported route: it takes device-independent
    // coordinates, runs them through QHighDpiScaling, and hands the result
    // to wl_surface.set_input_region. The mask and the window geometry take
    // the same conversion, so expressing the region in the same units as
    // thickness() is correct on any output scale without touching buffer
    // scale directly. Setting the region by hand would mean binding
    // wl_compositor ourselves purely to reach wl_compositor.create_region.
    auto* window = surface->window();
    if (!window) {
        qCWarning(lcShellEngine) << "Panel surface has no window — input region not applied;"
                                 << "the shadow strip will swallow clicks";
        return;
    }
    // Deliberately NOT gated on window->handle(). QWindow::setMask stores
    // the region in QWindowPrivate unconditionally — the platform call is
    // what it skips when there is no platform window, not the caching — and
    // QWaylandWindow::initWindow replays QWindow::mask() through setMask
    // every time it creates the wl_surface. So applying the mask before the
    // platform window exists is harmless and lands as soon as it does.
    // Returning early instead would install NO connections at all, leaving
    // that panel with no input region for its entire life and no retry
    // path, which is the very bug this function exists to prevent.

    // `window` as the connection context so every subscription dies with the
    // surface's window rather than outliving it into a reload.
    //
    // `panel` as a QPointer, not a raw pointer: the widthChanged/heightChanged
    // connections are keyed to `window`, so they outlive the panel if it is
    // ever destroyed first, and a raw pointer would leave the `if (!panel)`
    // guard below reading a freed object's address rather than null. The
    // current ownership graph happens to make that unreachable (the panel is
    // a QObject child of `window`), but the guard should mean what it says.
    const QPointer<PanelWindow> guardedPanel(panel);
    // Edge is captured BY VALUE, matching the closing comment's contract:
    // panel geometry is fixed at materialization (installDynamicAutoFit
    // snapshots the same way). Sampling the live edge/thickness here would
    // let a post-materialization QML write move the input band on the next
    // resize tick while the surface size, anchors and exclusive zone stayed
    // frozen — exactly the partially-applied state the "deliberately NOT
    // connected" note below rules out.
    //
    // The band DEPTH is the one exception, and it is read live through
    // effectiveInputThickness(). That resolves to the captured-equivalent
    // `thickness` unless the panel sets `interactiveThickness`, which is
    // the opt-in for a surface that paints into its shadow strip and needs
    // clicks there — a bar with a popout growing out of it. Moving only the
    // input region is exactly what that case wants: the surface, anchors
    // and exclusive zone must NOT follow, or every window tiled below the
    // bar would shift when a popout opened. The interactiveThicknessChanged
    // connection below is what makes the live read take effect.
    const auto apply = [guardedPanel, window, edge = panel->edge()] {
        if (!guardedPanel) {
            return;
        }
        const QRect visible = PanelWindow::visibleBand(edge, guardedPanel->effectiveInputThickness(), window->size());
        if (visible.isEmpty()) {
            return;
        }
        // QWaylandWindow::setMask early-returns on an unchanged region (the
        // dedup is in the platform plugin, not in QWindow::setMask), so
        // calling this on every resize tick is cheap and needs no cache.
        window->setMask(QRegion(visible));
        // setMask updates the pending input region but does NOT commit the
        // surface. A repaint normally follows and carries it, but a resize
        // that changes nothing visible would leave the region pending
        // indefinitely.
        window->requestUpdate();
    };

    apply();

    // The region is derived from the window's CURRENT size, so it has to be
    // recomputed whenever that changes — auto-fit panels resize as their
    // content does, and the compositor can reconfigure at any time.
    //
    // The region trails the surface by one frame on a compositor-driven
    // resize: applyConfigure runs on the render thread and commits the new
    // size immediately, while the geometry change reaches the GUI thread
    // (and therefore this lambda) through QWindowSystemInterface afterwards.
    // Harmless for a band whose width lags a frame, and closing it would
    // mean a blocking round trip.
    connect(window, &QQuickWindow::widthChanged, window, apply);
    connect(window, &QQuickWindow::heightChanged, window, apply);
    // Re-apply on show. The mask lives in per-wl_surface state, so a
    // hide/show cycle that tears the surface down and rebuilds it would
    // otherwise come back with no input region and resume swallowing
    // clicks, with no size change to trigger the connections above.
    //
    // QUEUED, not direct. QWindow emits visibleChanged at the TOP of
    // setVisible, before create() and before the platform window is shown,
    // so a direct call would run while the wl_surface is still gone —
    // QWaylandWindow::setMask early-returns on a null surface WITHOUT even
    // caching the region, and the rebuilt surface would then come up with
    // the cleared, empty mask. Deferring puts the re-apply after
    // setVisible has finished building the platform window.
    connect(window, &QWindow::visibleChanged, window, [window, apply](bool nowVisible) {
        if (!nowVisible) {
            return;
        }
        QMetaObject::invokeMethod(window, apply, Qt::QueuedConnection);
    });

    // The same caching gap applies to a resize delivered while the surface
    // is down, which is why the show path replays unconditionally rather
    // than assuming the width/height connections already covered it.
    //
    // Masking composes correctly with Qt::WindowTransparentForInput, which
    // phosphor-layer sets on its hide path: QWaylandWindow::updateInputRegion
    // builds an empty region first and then tests the transparent-input flag
    // to choose between it and the mask, so transparent-for-input wins and a
    // masked panel still goes fully click-through while hidden.

    // Connected to interactiveThicknessChanged, unlike thicknessChanged
    // below. This is the ONE post-materialization geometry write a panel
    // may make, and it is safe for the exact reason the others are not: it
    // moves the input region ALONE and is meant to. A bar that grows a
    // popout into its shadow strip has to make that strip clickable while
    // the popout is open, and must NOT disturb the surface size, anchors or
    // exclusive zone doing it, or every window tiled below would shift.
    // `thickness` cannot express that — widening it moves the exclusive
    // zone — which is why the depth is a separate property rather than a
    // relaxation of the rule below.
    connect(panel, &PanelWindow::interactiveThicknessChanged, window, apply);

    // Deliberately NOT connected to thicknessChanged / edgeChanged.
    //
    // Those are QML-writable, but nothing else in materializePanels reacts
    // to them after the fact: the surface size, the anchors, the exclusive
    // zone and the auto-fit's captured screen size are all computed once,
    // here, and never recomputed. Tracking them in the input region alone
    // would produce a surface whose clickable band disagrees with its own
    // geometry — and for an edge change it is strictly worse than doing
    // nothing, because the band would move to the opposite side of a
    // surface still anchored the old way, leaving the panel entirely
    // unclickable and the shadow strip as the only live area. That is the
    // bug this function exists to fix, inverted.
    //
    // Panel geometry is fixed at materialization. If it ever needs to be
    // live, the whole surface has to be re-materialized (size, anchors,
    // exclusive zone, margins, and mask together), not just this one piece.
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
    if (!m_rootRef) {
        return;
    }

    m_persistentState.clear();
    const auto persists = collectPersistentProperties();
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

    const auto persists = collectPersistentProperties();
    for (auto* p : persists) {
        if (m_persistentState.contains(p->reloadId())) {
            p->restoreState(m_persistentState[p->reloadId()]);
        }
    }
    qCDebug(lcShellEngine) << "Restored persistent state(s)";
}

} // namespace PhosphorShell
