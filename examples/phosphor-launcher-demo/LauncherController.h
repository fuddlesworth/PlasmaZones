// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <PhosphorRegistry/ILauncherProviderFactory.h>
#include <PhosphorRegistry/Registry.h>

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace PhosphorServiceClipboard {
class ClipboardService;
}

namespace PhosphorShellLauncher {
class LauncherModel;
}

namespace PhosphorLauncherDemo {

// ILauncherProviderFactory over a construction function. The kind of
// helper a real shell provides for its built-in providers; a plugin
// author mirrors the pattern or supplies a factory class of their own.
class FunctionProviderFactory : public PhosphorRegistry::ILauncherProviderFactory
{
public:
    using Create = std::function<PhosphorRegistry::ILauncherProvider*(QObject*)>;

    FunctionProviderFactory(QString id, QString displayName, QStringList capabilities, Create create);
    ~FunctionProviderFactory() override = default;
    Q_DISABLE_COPY_MOVE(FunctionProviderFactory)

    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString displayName() const override;
    [[nodiscard]] QStringList capabilities() const override;
    [[nodiscard]] PhosphorRegistry::ILauncherProvider* createProvider(QObject* parent) override;

private:
    QString m_id;
    QString m_displayName;
    QStringList m_capabilities;
    Create m_create;
};

// Owns the Registry<ILauncherProviderFactory>, registers the built-in
// providers at construction, creates each through its factory and adds
// it to the LauncherModel the surface binds. This is the registry-backed
// host the library's README describes; the surface and the model stay
// registry-agnostic.
//
// The clipboard service is constructed here and passed to its provider
// by pointer: the provider reads it by name and does not link it, so the
// host is where the service lives.
class LauncherController : public QObject
{
    Q_OBJECT

public:
    explicit LauncherController(QObject* parent = nullptr);
    ~LauncherController() override;

    [[nodiscard]] PhosphorShellLauncher::LauncherModel* model() const;

private:
    PhosphorRegistry::Registry<PhosphorRegistry::ILauncherProviderFactory> m_registry;
    PhosphorServiceClipboard::ClipboardService* m_clipboard = nullptr;
    PhosphorShellLauncher::LauncherModel* m_model = nullptr;
};

} // namespace PhosphorLauncherDemo
