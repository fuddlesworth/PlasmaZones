// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/ShellEngine.h>
#include <PhosphorShell/Environment.h>
#include <PhosphorShell/FileView.h>
#include <PhosphorShell/FloatingWindow.h>
#include <PhosphorShell/LazyLoader.h>
#include <PhosphorShell/PanelWindow.h>
#include <PhosphorShell/PersistentProperties.h>
#include <PhosphorShell/PopupWindow.h>
#include <PhosphorShell/Process.h>
#include <PhosphorShell/ScreenModel.h>
#include <PhosphorShell/ShellGlobal.h>
#include <PhosphorShell/Variants.h>

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

    m_shellUrl = shellUrl;

    m_screenModel = new ScreenModel(m_deps.screenProvider, this);
    m_shellGlobal = new ShellGlobal(this);
    m_shellGlobal->setScreenModel(m_screenModel);

    connect(m_deps.screenProvider->notifier(), &PhosphorLayer::ScreenProviderNotifier::screensChanged, this,
            &ShellEngine::onScreensChanged);

    qmlRegisterType<PanelWindow>("Phosphor.Shell", 1, 0, "PanelWindow");
    qmlRegisterType<PopupWindow>("Phosphor.Shell", 1, 0, "PopupWindow");
    qmlRegisterType<FloatingWindow>("Phosphor.Shell", 1, 0, "FloatingWindow");
    qmlRegisterType<Variants>("Phosphor.Shell", 1, 0, "Variants");
    qmlRegisterType<LazyLoader>("Phosphor.Shell", 1, 0, "LazyLoader");
    qmlRegisterType<Process>("Phosphor.Shell", 1, 0, "Process");
    qmlRegisterType<FileView>("Phosphor.Shell", 1, 0, "FileView");
    qmlRegisterType<PersistentProperties>("Phosphor.Shell", 1, 0, "PersistentProperties");
    qmlRegisterType<PhosphorRendering::ShaderEffect>("Phosphor.Shell", 1, 0, "ShaderBackground");

    m_engine = std::make_unique<QQmlEngine>(this);
    m_engine->rootContext()->setContextProperty(QStringLiteral("PhosphorShell"), m_shellGlobal);
    m_engine->rootContext()->setContextProperty(QStringLiteral("Environment"), new Environment(m_engine.get()));

    QQmlComponent component(m_engine.get(), shellUrl, QQmlComponent::PreferSynchronous);
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

    materializePanels();
    setupWatcher();
    Q_EMIT loaded();
    return true;
}

QQmlEngine* ShellEngine::engine() const
{
    return m_engine.get();
}

void ShellEngine::teardown()
{
    for (auto* surface : m_surfaces) {
        surface->hide();
        delete surface;
    }
    m_surfaces.clear();
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

    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this]() {
        m_reloadTimer->start();
    });
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
        m_reloadTimer->start();
    });

    qCDebug(lcShellEngine) << "Watching for changes:" << filePath;
}

void ShellEngine::onScreensChanged()
{
    qCInfo(lcShellEngine) << "Screen topology changed, reloading shell...";
    onFileChanged();
}

void ShellEngine::onFileChanged()
{
    qCInfo(lcShellEngine) << "Shell config changed, reloading...";

    savePersistentState();
    teardown();

    m_engine = std::make_unique<QQmlEngine>(this);
    m_engine->rootContext()->setContextProperty(QStringLiteral("PhosphorShell"), m_shellGlobal);

    QQmlComponent component(m_engine.get(), m_shellUrl, QQmlComponent::PreferSynchronous);
    if (component.isError()) {
        qCWarning(lcShellEngine) << "Reload failed:" << component.errorString();
        Q_EMIT failed(component.errorString());
        return;
    }

    m_rootObject.reset(component.create());
    if (!m_rootObject) {
        qCWarning(lcShellEngine) << "Reload instantiation failed:" << component.errorString();
        Q_EMIT failed(component.errorString());
        return;
    }

    materializePanels();
    restorePersistentState();

    // Re-arm the file watch (editors that save via atomic rename invalidate the old watch)
    const QString filePath = m_shellUrl.toLocalFile();
    if (!m_watcher->files().contains(filePath)) {
        m_watcher->addPath(filePath);
    }

    qCInfo(lcShellEngine) << "Reload complete," << m_surfaces.size() << "panel(s)";
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
    return PhosphorLayer::Anchor::Top;
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
        QScreen* targetScreen = panel->screen() ? panel->screen() : m_deps.screenProvider->primary();
        const QSize screenSize = targetScreen ? targetScreen->size() : QSize(1920, 1080);
        const bool horizontal = (panel->edge() == PanelWindow::Top || panel->edge() == PanelWindow::Bottom);
        const PhosphorLayer::Anchor primaryAnchor = edgeToAnchor(panel->edge());

        PhosphorLayer::Anchors anchors;
        QMargins layerMargins = panel->margins();
        QSize panelSize;

        if (panel->alignment() == PanelWindow::Fill || panel->panelLength() < 0) {
            if (horizontal) {
                anchors = primaryAnchor | PhosphorLayer::Anchor::Left | PhosphorLayer::Anchor::Right;
                panelSize = QSize(screenSize.width(), panel->thickness());
            } else {
                anchors = primaryAnchor | PhosphorLayer::Anchor::Top | PhosphorLayer::Anchor::Bottom;
                panelSize = QSize(panel->thickness(), screenSize.height());
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
                    panelSize = QSize(length, panel->thickness());
                } else {
                    anchors = primaryAnchor | PhosphorLayer::Anchor::Top;
                    panelSize = QSize(panel->thickness(), length);
                }
                break;
            case PanelWindow::End:
                if (horizontal) {
                    anchors = primaryAnchor | PhosphorLayer::Anchor::Right;
                    panelSize = QSize(length, panel->thickness());
                } else {
                    anchors = primaryAnchor | PhosphorLayer::Anchor::Bottom;
                    panelSize = QSize(panel->thickness(), length);
                }
                break;
            case PanelWindow::Center:
                if (horizontal) {
                    anchors = primaryAnchor | PhosphorLayer::Anchor::Left | PhosphorLayer::Anchor::Right;
                    const int margin = (screenSize.width() - length) / 2;
                    layerMargins.setLeft(qMax(layerMargins.left(), margin));
                    layerMargins.setRight(qMax(layerMargins.right(), margin));
                    panelSize = QSize(length, panel->thickness());
                } else {
                    anchors = primaryAnchor | PhosphorLayer::Anchor::Top | PhosphorLayer::Anchor::Bottom;
                    const int margin = (screenSize.height() - length) / 2;
                    layerMargins.setTop(qMax(layerMargins.top(), margin));
                    layerMargins.setBottom(qMax(layerMargins.bottom(), margin));
                    panelSize = QSize(panel->thickness(), length);
                }
                break;
            case PanelWindow::Fill:
                break;
            }
        }

        PhosphorLayer::Role role;
        role = role.withAnchors(anchors).withScopePrefix(QStringLiteral("phosphor-shell"));
        role = role.withKeyboard(PhosphorLayer::KeyboardInteractivity::OnDemand);

        switch (panel->layer()) {
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

        PhosphorLayer::SurfaceConfig cfg;
        cfg.role = role;
        cfg.contentItem = std::unique_ptr<QQuickItem>(panel);
        cfg.screen = targetScreen;
        cfg.initialSize = panelSize;
        cfg.marginsOverride = layerMargins;
        cfg.debugName = QStringLiteral("phosphor-shell-panel");

        auto* surface = m_deps.surfaceFactory->create(std::move(cfg), this);
        if (surface) {
            surface->show();
            m_surfaces.push_back(surface);
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
        }
    }

    m_shellGlobal->clearSingletons();
    const auto persists = m_rootObject->findChildren<PersistentProperties*>();
    for (auto* p : persists) {
        if (!p->reloadId().isEmpty()) {
            m_shellGlobal->registerSingleton(p->reloadId(), p);
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
    const int thickness = panel->thickness();
    const QMargins userMargins = panel->margins();
    const bool horizontal = (edge == PanelWindow::Top || edge == PanelWindow::Bottom);

    auto resize = [panel, surface, horizontal, alignment, thickness, userMargins, screenSize]() {
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
        const QSize newSize = horizontal ? QSize(newLength, thickness) : QSize(thickness, newLength);
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
    if (!m_rootObject) {
        return;
    }

    m_persistentState.clear();
    const auto persists = m_rootObject->findChildren<PersistentProperties*>();
    for (const auto* p : persists) {
        if (!p->reloadId().isEmpty()) {
            m_persistentState[p->reloadId()] = p->saveState();
        }
    }
    qCDebug(lcShellEngine) << "Saved" << m_persistentState.size() << "persistent state(s)";
}

void ShellEngine::restorePersistentState()
{
    if (!m_rootObject || m_persistentState.isEmpty()) {
        return;
    }

    const auto persists = m_rootObject->findChildren<PersistentProperties*>();
    for (auto* p : persists) {
        if (m_persistentState.contains(p->reloadId())) {
            p->restoreState(m_persistentState[p->reloadId()]);
        }
    }
    qCDebug(lcShellEngine) << "Restored persistent state(s)";
}

} // namespace PhosphorShell
