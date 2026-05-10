// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

#include <QHash>
#include <QObject>
#include <QSize>
#include <QUrl>
#include <QVariantMap>

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
    QQmlEngine* engine() const;

Q_SIGNALS:
    void loaded();
    void reloaded();
    void failed(const QString& reason);

private Q_SLOTS:
    void onFileChanged();
    void onScreensChanged();

private:
    void materializePanels();
    void installDynamicAutoFit(PanelWindow* panel, PhosphorLayer::Surface* surface, QSize screenSize);
    void teardown();
    void setupWatcher();
    void savePersistentState();
    void restorePersistentState();

    QUrl m_shellUrl;
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QObject> m_rootObject;
    Deps m_deps;
    std::vector<std::unique_ptr<PhosphorLayer::Surface>> m_surfaces;
    QFileSystemWatcher* m_watcher = nullptr;
    QTimer* m_reloadTimer = nullptr;
    ScreenModel* m_screenModel = nullptr;
    ShellGlobal* m_shellGlobal = nullptr;
    QHash<QString, QVariantMap> m_persistentState;
};

} // namespace PhosphorShell
