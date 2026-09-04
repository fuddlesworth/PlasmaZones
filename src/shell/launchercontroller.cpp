// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "LauncherController.h"

#include <PhosphorServiceClipboard/ClipboardService.h>
#include <PhosphorShell/Toplevels.h>
#include <PhosphorShellLauncher/AppsProvider.h>
#include <PhosphorShellLauncher/CalculatorProvider.h>
#include <PhosphorShellLauncher/ClipboardProvider.h>
#include <PhosphorShellLauncher/CommandProvider.h>
#include <PhosphorShellLauncher/LauncherModel.h>
#include <PhosphorShellLauncher/WindowsProvider.h>

#include <QLoggingCategory>

#include <memory>
#include <utility>

namespace {
Q_LOGGING_CATEGORY(lcLauncher, "phosphorshell.launcher")
}

namespace PhosphorShellApp {

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
    , m_toplevels(new PhosphorShell::Toplevels(this))
    , m_model(new PhosphorShellLauncher::LauncherModel(this))
{
    // Registration order is the pill order and the tie-break between
    // providers with equal best scores. Capabilities are advisory until
    // the Phase 5 runtime, declared so the built-ins exercise the manifest
    // surface a plugin will.
    const auto reg = [this](const QString& id, const QString& name, const QString& capability,
                            FunctionProviderFactory::Create create) {
        m_registry.registerFactory(
            std::make_shared<FunctionProviderFactory>(id, name, QStringList{capability}, std::move(create)));
    };
    reg(QStringLiteral("apps"), QStringLiteral("Applications"), QStringLiteral("apps.launch"), [](QObject* p) {
        return new PhosphorShellLauncher::AppsProvider(p);
    });
    reg(QStringLiteral("windows"), QStringLiteral("Windows"), QStringLiteral("windows.activate"), [this](QObject* p) {
        return new PhosphorShellLauncher::WindowsProvider(m_toplevels->model(), p);
    });
    reg(QStringLiteral("calculator"), QStringLiteral("Calculator"), QStringLiteral("clipboard.write"), [](QObject* p) {
        return new PhosphorShellLauncher::CalculatorProvider(p);
    });
    reg(QStringLiteral("clipboard"), QStringLiteral("Clipboard"), QStringLiteral("clipboard.read"), [this](QObject* p) {
        return new PhosphorShellLauncher::ClipboardProvider(m_clipboard, p);
    });
    reg(QStringLiteral("command"), QStringLiteral("Run Command"), QStringLiteral("process.spawn"), [](QObject* p) {
        return new PhosphorShellLauncher::CommandProvider(p);
    });

    for (const QString& id : m_registry.ids()) {
        const auto factory = m_registry.factory(id);
        if (!factory) {
            continue;
        }
        ILauncherProvider* provider = factory->createProvider(m_model);
        if (!provider) {
            qCInfo(lcLauncher) << "provider" << id << "unavailable in this environment";
            continue;
        }
        m_model->addProvider(provider);
    }
    qCDebug(lcLauncher) << "launcher ready with" << m_model->providerObjects().size() << "provider(s)";
}

LauncherController::~LauncherController() = default;

PhosphorShellLauncher::LauncherModel* LauncherController::model() const
{
    return m_model;
}

} // namespace PhosphorShellApp
