// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServicePolkit/QmlRegistration.h>

#include <PhosphorServicePolkit/AuthRequest.h>
#include <PhosphorServicePolkit/PolkitAgent.h>

#include <QQmlEngine>

#include <mutex>

namespace PhosphorServicePolkit {

namespace {
constexpr int kModuleVersionMajor = 1;
constexpr int kModuleVersionMinor = 0;
constexpr const char* kModule = "Phosphor.Service.Polkit";
} // namespace

void registerQmlTypes()
{
    // qmlRegister* is process-global, not per-engine. Repeat calls overwrite the
    // previous registration and Qt logs a duplicate-registration warning. A
    // hot-reloading shell builds a fresh QQmlEngine per reload and would call
    // this every time; the call_once guard makes the function safe to invoke
    // from every engine setup, matching the sibling service-lib pattern.
    static std::once_flag once;
    std::call_once(once, [] {
        // Instantiable entry point. The shell constructs the agent in QML and
        // calls registerAgent() to opt into becoming the session's agent; a
        // plain type, NOT a singleton.
        qmlRegisterType<PolkitAgent>(kModule, kModuleVersionMajor, kModuleVersionMinor, "PolkitAgent");

        // Pointer-receivable type. Exposed as a Q_PROPERTY / signal arg from the
        // agent, never constructed in QML; uncreatable makes its metatype known
        // so QML can read its properties and set selectedIdentity.
        qmlRegisterUncreatableType<AuthRequest>(
            kModule, kModuleVersionMajor, kModuleVersionMinor, "AuthRequest",
            QStringLiteral("AuthRequest is owned by PolkitAgent; bind via the agent's activeRequest"));
    });
}

} // namespace PhosphorServicePolkit
