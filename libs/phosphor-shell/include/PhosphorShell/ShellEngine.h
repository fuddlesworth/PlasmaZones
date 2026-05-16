// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QUrl>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <vector>

QT_BEGIN_NAMESPACE
class QFileSystemWatcher;
class QQmlEngine;
class QTimer;
QT_END_NAMESPACE

namespace PhosphorLayer {
class IScreenProvider;
class Surface;
class SurfaceFactory;
} // namespace PhosphorLayer

namespace PhosphorShell {

class PanelWindow;
class ScreenModel;
class ShellGlobal;

class PHOSPHORSHELL_EXPORT ShellEngine : public QObject
{
    Q_OBJECT
public:
    struct Deps
    {
        PhosphorLayer::SurfaceFactory* surfaceFactory = nullptr;
        PhosphorLayer::IScreenProvider* screenProvider = nullptr;
    };

    explicit ShellEngine(Deps deps, QObject* parent = nullptr);
    ~ShellEngine() override;

    bool load(const QUrl& shellUrl);
    [[nodiscard]] QQmlEngine* engine() const;

    /// Register a callback that fires whenever a fresh QQmlEngine is
    /// created — at startup AND on hot-reload (file watcher triggers
    /// rebuild). Use this to install image providers, register
    /// engine-scoped singletons, or set additional context properties
    /// without forcing ShellEngine to depend on the modules that
    /// supply them. The callback is invoked synchronously, in
    /// registration order, after the engine's own context properties
    /// are set but before any QML is loaded.
    using EngineHook = std::function<void(QQmlEngine*)>;
    void addEngineHook(EngineHook hook);

Q_SIGNALS:
    void loaded();
    void reloaded();
    void failed(const QString& reason);

private Q_SLOTS:
    void onFileChanged();
    void onScreensChanged();

private:
    /// Build a fresh QQmlEngine, instantiate the shell QML, and
    /// materialize its panels. Shared by the initial load() and the
    /// hot-reload onFileChanged() path. Emits failed() and returns false
    /// if the QML fails to load or instantiate.
    [[nodiscard]] bool buildAndMaterialize();
    void materializePanels();
    void installDynamicAutoFit(PanelWindow* panel, PhosphorLayer::Surface* surface, QSize screenSize);
    void teardown();
    void setupWatcher();
    void savePersistentState();
    void restorePersistentState();

    QUrl m_shellUrl;
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QObject> m_rootObject;
    // Non-owning observer of the QML root. When the root is a PanelWindow,
    // ownership transfers to a Surface and m_rootObject is released — but
    // findChildren scans (PersistentProperties save/restore) still need a
    // pointer to walk the tree. QPointer auto-clears when the wrapper
    // window destroys the root.
    QPointer<QObject> m_rootRef;
    Deps m_deps;
    std::vector<std::unique_ptr<PhosphorLayer::Surface>> m_surfaces;
    QFileSystemWatcher* m_watcher = nullptr;
    QTimer* m_reloadTimer = nullptr;
    ScreenModel* m_screenModel = nullptr;
    ShellGlobal* m_shellGlobal = nullptr;
    QHash<QString, QVariantMap> m_persistentState;
    std::vector<EngineHook> m_engineHooks;
};

} // namespace PhosphorShell
