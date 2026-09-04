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
    // STARTUP COST, stated because this runs before the first frame.
    // Every provider is materialised here rather than on first open, so
    // that opening the launcher is instant: a user pressing the shortcut
    // does not wait for an applications scan. The two costs that buys
    // are the clipboard service's constructor, which reads persisted
    // history from disk and binds a Wayland data-control source, and the
    // applications provider's first scan. The scan is already posted to
    // the event loop rather than run inline, so it lands after the first
    // frame; the clipboard service is not, and moving it would mean
    // teaching the provider to attach to a service that arrives later.
    //
    // Registration order is the pill order and the tie-break between
    // providers with equal best scores. Capabilities are advisory until
    // the Phase 5 runtime, declared so the built-ins exercise the manifest
    // surface a plugin will.
    const auto reg = [this](const QString& id, const QString& name, const QString& capability,
                            FunctionProviderFactory::Create create) {
        m_registry.registerFactory(
            std::make_shared<FunctionProviderFactory>(id, name, QStringList{capability}, std::move(create)));
    };
    reg(QStringLiteral("apps"), QCoreApplication::translate("PhosphorShellLauncher", "Applications"),
        QStringLiteral("apps.launch"), [](QObject* p) {
            return new PhosphorShellLauncher::AppsProvider(p);
        });
    reg(QStringLiteral("windows"), QCoreApplication::translate("PhosphorShellLauncher", "Windows"),
        QStringLiteral("windows.activate"), [this](QObject* p) {
            return new PhosphorShellLauncher::WindowsProvider(m_toplevels->model(), p);
        });
    reg(QStringLiteral("calculator"), QCoreApplication::translate("PhosphorShellLauncher", "Calculator"),
        QStringLiteral("clipboard.write"), [](QObject* p) {
            return new PhosphorShellLauncher::CalculatorProvider(p);
        });
    reg(QStringLiteral("clipboard"), QCoreApplication::translate("PhosphorShellLauncher", "Clipboard"),
        QStringLiteral("clipboard.read"), [this](QObject* p) {
            return new PhosphorShellLauncher::ClipboardProvider(m_clipboard, p);
        });
    reg(QStringLiteral("command"), QCoreApplication::translate("PhosphorShellLauncher", "Run Command"),
        QStringLiteral("process.spawn"), [](QObject* p) {
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
