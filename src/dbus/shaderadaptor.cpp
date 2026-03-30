// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shaderadaptor.h"
#include "../core/shaderregistry.h"

namespace PlasmaZones {

ShaderAdaptor::ShaderAdaptor(ShaderRegistry* registry, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_registry(registry)
{
    // Relay ShaderRegistry signals to D-Bus
    if (m_registry) {
        connect(m_registry, &ShaderRegistry::shaderCompilationStarted, this, &ShaderAdaptor::shaderCompilationStarted);
        connect(m_registry, &ShaderRegistry::shaderCompilationFinished, this,
                &ShaderAdaptor::shaderCompilationFinished);
        connect(m_registry, &ShaderRegistry::shadersChanged, this, &ShaderAdaptor::shadersChanged);
    }
}

QVariantList ShaderAdaptor::availableShaders()
{
    return m_registry ? m_registry->availableShadersVariant() : QVariantList();
}

QVariantMap ShaderAdaptor::shaderInfo(const QString& shaderId)
{
    return m_registry ? m_registry->shaderInfo(shaderId) : QVariantMap();
}

QVariantMap ShaderAdaptor::defaultShaderParams(const QString& shaderId)
{
    return m_registry ? m_registry->defaultParams(shaderId) : QVariantMap();
}

QVariantMap ShaderAdaptor::translateShaderParams(const QString& shaderId, const QVariantMap& params)
{
    return m_registry ? m_registry->translateParamsToUniforms(shaderId, params) : QVariantMap();
}

bool ShaderAdaptor::shadersEnabled()
{
    return m_registry ? m_registry->shadersEnabled() : false;
}

bool ShaderAdaptor::userShadersEnabled()
{
    return m_registry ? m_registry->userShadersEnabled() : false;
}

QString ShaderAdaptor::userShaderDirectory()
{
    return m_registry ? m_registry->userShaderDirectory() : QString();
}

void ShaderAdaptor::openUserShaderDirectory()
{
    if (m_registry) {
        m_registry->openUserShaderDirectory();
    }
}

void ShaderAdaptor::refreshShaders()
{
    if (m_registry) {
        m_registry->refresh();
    }
}

} // namespace PlasmaZones
