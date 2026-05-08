// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

#include <QObject>
#include <QUrl>

#include <memory>
#include <vector>

QT_BEGIN_NAMESPACE
class QQmlEngine;
QT_END_NAMESPACE

namespace PhosphorLayer {
class IScreenProvider;
class Surface;
class SurfaceFactory;
} // namespace PhosphorLayer

namespace PhosphorShell {

class PanelWindow;

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
    void failed(const QString& reason);

private:
    void materializePanels();

    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QObject> m_rootObject;
    Deps m_deps;
    std::vector<PhosphorLayer::Surface*> m_surfaces;
};

} // namespace PhosphorShell
