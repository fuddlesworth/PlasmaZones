// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/ShellEngine.h>
#include <PhosphorShell/PanelWindow.h>

#include <PhosphorLayer/IScreenProvider.h>
#include <PhosphorLayer/Role.h>
#include <PhosphorLayer/Surface.h>
#include <PhosphorLayer/SurfaceConfig.h>
#include <PhosphorLayer/SurfaceFactory.h>

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

    m_engine = std::make_unique<QQmlEngine>(this);

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

void ShellEngine::onFileChanged()
{
    qCInfo(lcShellEngine) << "Shell config changed, reloading...";

    teardown();

    m_engine = std::make_unique<QQmlEngine>(this);

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

    // Re-arm the file watch (editors that save via atomic rename invalidate the old watch)
    const QString filePath = m_shellUrl.toLocalFile();
    if (!m_watcher->files().contains(filePath)) {
        m_watcher->addPath(filePath);
    }

    qCInfo(lcShellEngine) << "Reload complete," << m_surfaces.size() << "panel(s)";
    Q_EMIT reloaded();
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
        PhosphorLayer::Role role;

        switch (panel->edge()) {
        case PanelWindow::Top:
            role = PhosphorLayer::Roles::TopPanel;
            break;
        case PanelWindow::Bottom:
            role = PhosphorLayer::Roles::BottomPanel;
            break;
        case PanelWindow::Left:
            role = PhosphorLayer::Roles::LeftDock;
            break;
        case PanelWindow::Right:
            role = PhosphorLayer::Roles::RightDock;
            break;
        }

        role = role.withScopePrefix(QStringLiteral("phosphor-shell"));

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

        if (panel->exclusiveZoneEnabled()) {
            role = role.withExclusiveZone(panel->thickness());
        } else if (panel->exclusiveZone() >= 0) {
            role = role.withExclusiveZone(panel->exclusiveZone());
        }

        QScreen* targetScreen = panel->screen() ? panel->screen() : m_deps.screenProvider->primary();
        const QSize screenSize = targetScreen ? targetScreen->size() : QSize(1920, 1080);

        QSize panelSize;
        if (panel->edge() == PanelWindow::Top || panel->edge() == PanelWindow::Bottom) {
            panelSize = QSize(screenSize.width(), panel->thickness());
        } else {
            panelSize = QSize(panel->thickness(), screenSize.height());
        }

        PhosphorLayer::SurfaceConfig cfg;
        cfg.role = role;
        cfg.contentItem = std::unique_ptr<QQuickItem>(panel);
        cfg.screen = targetScreen;
        cfg.initialSize = panelSize;
        cfg.debugName = QStringLiteral("phosphor-shell-panel");

        auto* surface = m_deps.surfaceFactory->create(std::move(cfg), this);
        if (surface) {
            surface->show();
            m_surfaces.push_back(surface);
            qCDebug(lcShellEngine) << "Created panel surface on edge" << panel->edge();
        } else {
            qCWarning(lcShellEngine) << "Failed to create surface for PanelWindow";
        }
    }
}

} // namespace PhosphorShell
