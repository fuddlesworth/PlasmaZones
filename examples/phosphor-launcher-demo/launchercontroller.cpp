// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "LauncherController.h"

#include <PhosphorServiceClipboard/ClipboardService.h>
#include <PhosphorShellLauncher/AppsProvider.h>
#include <PhosphorShellLauncher/CalculatorProvider.h>
#include <PhosphorShellLauncher/ClipboardProvider.h>
#include <PhosphorShellLauncher/CommandProvider.h>
#include <PhosphorShellLauncher/LauncherModel.h>

#include <QDebug>

#include <utility>

namespace PhosphorLauncherDemo {

using PhosphorRegistry::ILauncherProvider;

FunctionProviderFactory::FunctionProviderFactory(QString id, QString displayName, QStringList capabilities,
                                                 Create create)
    : m_id(std::move(id))
    , m_displayName(std::move(displayName))
    , m_capabilities(std::move(capabilities))
    , m_create(std::move(create))
{
}

QString FunctionProviderFactory::id() const
{
    return m_id;
}

QString FunctionProviderFactory::displayName() const
{
    return m_displayName;
}

QStringList FunctionProviderFactory::capabilities() const
{
    return m_capabilities;
}

ILauncherProvider* FunctionProviderFactory::createProvider(QObject* parent)
{
    return m_create ? m_create(parent) : nullptr;
}

LauncherController::LauncherController(QObject* parent)
    : QObject(parent)
    , m_clipboard(new PhosphorServiceClipboard::ClipboardService(this))
    , m_model(new PhosphorShellLauncher::LauncherModel(this))
{
    // Register the built-ins as ILauncherProviderFactory instances, the
    // same seam a plugin would come through. Capabilities are advisory
    // until the Phase 5 runtime, declared now so the built-ins exercise
    // the manifest surface. The windows provider is not registered here:
    // it needs the shell's Toplevels model, which the shell owns.
    const auto reg = [this](const QString& id, const QString& name, const QString& capability,
                            FunctionProviderFactory::Create create) {
        m_registry.registerFactory(
            std::make_shared<FunctionProviderFactory>(id, name, QStringList{capability}, std::move(create)));
    };
    reg(QStringLiteral("apps"), QStringLiteral("Applications"), QStringLiteral("apps.launch"), [](QObject* p) {
        return new PhosphorShellLauncher::AppsProvider(p);
    });
    reg(QStringLiteral("calculator"), QStringLiteral("Calculator"), QStringLiteral("clipboard.write"), [](QObject* p) {
        return new PhosphorShellLauncher::CalculatorProvider(p);
    });
    reg(QStringLiteral("command"), QStringLiteral("Run Command"), QStringLiteral("process.spawn"), [](QObject* p) {
        return new PhosphorShellLauncher::CommandProvider(p);
    });
    reg(QStringLiteral("clipboard"), QStringLiteral("Clipboard"), QStringLiteral("clipboard.read"), [this](QObject* p) {
        return new PhosphorShellLauncher::ClipboardProvider(m_clipboard, p);
    });

    // Materialise each registered provider into the model. Registration
    // order is the pill order; a factory returning null is "unavailable
    // here" per the contract, so it simply contributes nothing.
    for (const QString& id : m_registry.ids()) {
        const auto factory = m_registry.factory(id);
        if (!factory) {
            continue;
        }
        ILauncherProvider* provider = factory->createProvider(m_model);
        if (!provider) {
            qInfo() << "launcher provider" << id << "unavailable in this environment";
            continue;
        }
        m_model->addProvider(provider);
    }
}

LauncherController::~LauncherController() = default;

PhosphorShellLauncher::LauncherModel* LauncherController::model() const
{
    return m_model;
}

} // namespace PhosphorLauncherDemo
