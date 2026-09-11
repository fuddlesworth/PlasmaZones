// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShaders/ShaderPreset.h>

#include <QJsonValue>

namespace PhosphorShaders {

namespace {
constexpr auto JsonFieldId = "id";
constexpr auto JsonFieldName = "name";
constexpr auto JsonFieldPackId = "packId";
constexpr auto JsonFieldParams = "params";
} // namespace

QLatin1StringView shaderFamilyToken(ShaderFamily family)
{
    switch (family) {
    case ShaderFamily::Animation:
        return QLatin1StringView("animation");
    case ShaderFamily::Surface:
        return QLatin1StringView("surface");
    case ShaderFamily::Pointer:
        return QLatin1StringView("pointer");
    case ShaderFamily::Overlay:
        return QLatin1StringView("overlay");
    }
    // Unreachable for a value of the enum; no default label, so adding a
    // family is a compile error here rather than a silent empty token.
    return QLatin1StringView("");
}

std::optional<ShaderFamily> shaderFamilyFromToken(QStringView token)
{
    if (token == QLatin1StringView("animation"))
        return ShaderFamily::Animation;
    if (token == QLatin1StringView("surface"))
        return ShaderFamily::Surface;
    if (token == QLatin1StringView("pointer"))
        return ShaderFamily::Pointer;
    if (token == QLatin1StringView("overlay"))
        return ShaderFamily::Overlay;
    return std::nullopt;
}

QJsonObject ShaderPreset::toJson() const
{
    QJsonObject obj;
    obj.insert(QLatin1String(JsonFieldId), id);
    obj.insert(QLatin1String(JsonFieldName), name);
    obj.insert(QLatin1String(JsonFieldPackId), packId);
    obj.insert(QLatin1String(JsonFieldParams), QJsonObject::fromVariantMap(params));
    return obj;
}

bool ShaderPreset::isUsableId(const QString& id)
{
    // A preset id becomes a FILENAME on the write side (`<dir>/<id>.json`), so
    // an id carrying a separator, a parent-directory hop or a NUL escapes the
    // preset directory the moment the user renames or updates that preset. The
    // id arrives from a hand-editable file, so this is a system boundary.
    //
    // Checked here rather than only at the write site so a malformed id never
    // reaches the registry at all, and again at the write site, because the
    // bridge must not depend on an invariant it cannot see.
    if (id.isEmpty() || id.size() > MaxIdChars) {
        return false;
    }
    if (id == QLatin1String(".") || id == QLatin1String("..")) {
        return false;
    }
    return !id.contains(QLatin1Char('/')) && !id.contains(QLatin1Char('\\')) && !id.contains(QLatin1Char('\0'));
}

ShaderPreset ShaderPreset::fromJson(const QJsonObject& obj, const QString& fallbackId)
{
    ShaderPreset preset;
    preset.id = obj.value(QLatin1String(JsonFieldId)).toString();
    if (!isUsableId(preset.id)) {
        // Fall back to the filename stem rather than refusing the file: the stem
        // is a real path component by construction, so it cannot escape, and the
        // preset stays loadable instead of vanishing over a bad `id` field.
        preset.id = fallbackId;
    }
    if (!isUsableId(preset.id)) {
        // Neither the declared id nor the fallback is usable. Leave the id empty
        // so isValid() refuses it and the caller logs and skips.
        preset.id.clear();
        return preset;
    }
    preset.name = obj.value(QLatin1String(JsonFieldName)).toString();
    // Bound the name the way the settings app's own rename dialog does. Without
    // it a hand-written file could put an arbitrarily long single-line name into
    // every picker row; the UI validator only ever saw names the user typed.
    if (preset.name.size() > MaxNameChars) {
        preset.name.truncate(MaxNameChars);
    }
    preset.packId = obj.value(QLatin1String(JsonFieldPackId)).toString();
    // A present-but-non-object `params` is a corrupt or hand-mangled file. Take
    // it as empty rather than as "no parameters": the caller checks isValid()
    // on id/packId, and a preset that resolves to all-defaults is a legitimate
    // thing to save, so there is nothing here to distinguish. The keys are a
    // system boundary and are NOT filtered against the pack's declared ids —
    // an id the pack does not know is inert at resolve time, and filtering here
    // would need the pack registry this type deliberately does not depend on.
    preset.params = obj.value(QLatin1String(JsonFieldParams)).toObject().toVariantMap();
    // A user preset always comes from a file; the loader stamps sourcePath.
    return preset;
}

bool ShaderPreset::operator==(const ShaderPreset& other) const
{
    // JSON-normalised parameter compare, for the same reason
    // OverlayShaderProfile does it: a map built in C++ (int variants) must
    // compare equal to the same values read back from disk (doubles), or a
    // reload that changed nothing would look like a change and re-emit
    // presetsChanged — which invalidates every compiled surface pack.
    return id == other.id && name == other.name && packId == other.packId && readOnly == other.readOnly
        && sourcePath == other.sourcePath
        && QJsonObject::fromVariantMap(params) == QJsonObject::fromVariantMap(other.params);
}

QVariantMap overlayPresetDeltas(const QVariantMap& base, const QVariantMap& deltas)
{
    QVariantMap result = base;
    for (auto it = deltas.constBegin(); it != deltas.constEnd(); ++it) {
        result.insert(it.key(), it.value());
    }
    return result;
}

} // namespace PhosphorShaders
