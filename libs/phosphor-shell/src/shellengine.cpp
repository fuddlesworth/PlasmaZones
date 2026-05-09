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
#include <QScreen>
#include <QSize>
#include <QTimer>

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
            const int length = panel->panelLength();

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
