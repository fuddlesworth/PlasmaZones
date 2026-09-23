// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// ShaderRegistry's parameter validation / preset / coercion methods, split out
// of shaderregistry.cpp to keep it under the file-size ceiling. Same class,
// separate TU, no API change. These use only public helpers (no anonymous-
// namespace member of shaderregistry.cpp), so the split is clean.

#include <PhosphorShaders/ShaderRegistry.h>

#include <PhosphorShaders/CustomParamsKey.h>

#include <QColor>
#include <QLoggingCategory>

namespace PhosphorShaders {

Q_DECLARE_LOGGING_CATEGORY(lcShaderRegistry)

// ═══════════════════════════════════════════════════════════════════════════════
// Parameters, Presets & Validation
// ═══════════════════════════════════════════════════════════════════════════════

QVariantMap ShaderRegistry::presetParams(const QString& shaderId, const QString& presetName) const
{
    const ShaderInfo info = shader(shaderId);
    if (!info.isValid() || !info.presets.contains(presetName)) {
        return {};
    }
    // Validate and fill defaults for any missing parameters
    return validateAndCoerceParams(shaderId, info.presets.value(presetName));
}

QStringList ShaderRegistry::shaderPresetNames(const QString& shaderId) const
{
    const ShaderInfo info = shader(shaderId);
    if (!info.isValid()) {
        return {};
    }
    return info.presets.keys();
}

QVariantList ShaderRegistry::shaderPresetsVariant(const QString& shaderId) const
{
    const ShaderInfo info = shader(shaderId);
    if (!info.isValid()) {
        return {};
    }
    QVariantList result;
    for (auto it = info.presets.constBegin(); it != info.presets.constEnd(); ++it) {
        QVariantMap entry;
        entry[QStringLiteral("name")] = it.key();
        entry[QStringLiteral("params")] = it.value();
        result.append(entry);
    }
    return result;
}

bool ShaderRegistry::validateParams(const QString& id, const QVariantMap& params) const
{
    const ShaderInfo info = shader(id);
    if (!info.isValid()) {
        return false;
    }

    for (const ParameterInfo& param : info.parameters) {
        if (params.contains(param.id)) {
            if (!validateParameterValue(param, params.value(param.id))) {
                qCWarning(lcShaderRegistry) << "Invalid shader parameter:" << param.id << "for shader:" << id;
                return false;
            }
        }
    }
    return true;
}

bool ShaderRegistry::validateParameterValue(const ParameterInfo& param, const QVariant& value) const
{
    if (param.type == QLatin1String("float")) {
        bool ok = false;
        double v = value.toDouble(&ok);
        if (!ok)
            return false;
        if (param.minValue.isValid() && v < param.minValue.toDouble())
            return false;
        if (param.maxValue.isValid() && v > param.maxValue.toDouble())
            return false;
    } else if (param.type == QLatin1String("int")) {
        bool ok = false;
        int v = value.toInt(&ok);
        if (!ok)
            return false;
        if (param.minValue.isValid() && v < param.minValue.toInt())
            return false;
        if (param.maxValue.isValid() && v > param.maxValue.toInt())
            return false;
    } else if (param.type == QLatin1String("color")) {
        // Metatype-first, matching the surface registry's coercion: a QColor
        // variant stringifies to "" via toString(), so the string-constructor
        // path alone silently failed genuine QColor values.
        if (value.metaType().id() == QMetaType::QColor) {
            if (!value.value<QColor>().isValid())
                return false;
        } else {
            QColor c(value.toString());
            if (!c.isValid())
                return false;
        }
    } else if (param.type == QLatin1String("bool")) {
        // canConvert<bool> is true for nearly every metatype (any QString
        // converts), so restrict to the types that carry a real truth value.
        const int mt = value.metaType().id();
        if (mt != QMetaType::Bool && mt != QMetaType::Int && mt != QMetaType::UInt && mt != QMetaType::LongLong
            && mt != QMetaType::ULongLong && mt != QMetaType::Double)
            return false;
    } else if (param.type == QLatin1String("image")) {
        // An image value is a path (or empty for "no texture"); accept only
        // genuine strings — canConvert<QString> is true for numbers too.
        if (value.metaType().id() != QMetaType::QString)
            return false;
    } else {
        // Unknown type: fail CLOSED.
        //
        // Currently UNREACHABLE on every live path, and deliberately kept as a
        // backstop rather than because a caller needs it today: `type` is a
        // required enum in data/schemas/shader-metadata.schema.json and the schema
        // gate runs before the metadata is parsed, so every value arriving here
        // carries a validated type. In fact all three of `validateParams`,
        // `validateAndCoerceParams` and `presetParams` are currently DEAD: the
        // editor rolls its own preset lookup over the cached variant map
        // (EditorController::presetParams) and returns the values raw, so this
        // validate-and-coerce chain has no live consumer. They are kept as the
        // intended, tested validation surface for a future caller — routing the
        // editor through the registry would revive them (and close the preset
        // image-path validation gap parseShaderMetadata now guards at parse
        // time). The point is that such a caller bypassing the schema gate
        // should hit something here rather than have an unrecognised type
        // waved through.
        return false;
    }
    return true;
}

QVariantMap ShaderRegistry::validateAndCoerceParams(const QString& id, const QVariantMap& params) const
{
    QVariantMap result;
    const ShaderInfo info = shader(id);
    if (!info.isValid()) {
        return result;
    }

    for (const ParameterInfo& param : info.parameters) {
        if (params.contains(param.id) && validateParameterValue(param, params.value(param.id))) {
            result[param.id] = params.value(param.id);
        } else {
            if (params.contains(param.id)) {
                // A value was supplied but failed validation. Warn (matching
                // every other JSON-boundary guard in this library) rather than
                // silently swapping in the default, which otherwise reads to
                // the author as "my preset value was accepted".
                qCWarning(lcShaderRegistry)
                    << "Shader" << id << "param" << param.id << "failed validation; using default";
            }
            result[param.id] = param.defaultValue;
        }
    }
    return result;
}

QVariantMap ShaderRegistry::defaultParams(const QString& id) const
{
    QVariantMap result;
    const ShaderInfo info = shader(id);
    for (const ParameterInfo& param : info.parameters) {
        result[param.id] = param.defaultValue;
    }
    return result;
}

} // namespace PhosphorShaders
