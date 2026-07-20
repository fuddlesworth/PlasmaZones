// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// DecorationPageController shader-browser bridge — the ShaderBrowserPage
// contract (install / open-directory / usages) over the surface-pack registry
// and the decoration profile tree. Mirrors animationspagecontroller_shaders.cpp;
// the security-sensitive install path is the shared ShaderPackInstaller.

#include "decorationpagecontroller.h"

#include "../config/configdefaults.h"
#include "../core/isettings.h"
#include "../core/logging.h"
#include "../phosphor_i18n.h"
#include "shaderpackinstaller.h"

#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/DecorationProfileTree.h>

#include <QDesktopServices>
#include <QDir>
#include <QLatin1Char>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

namespace PlasmaZones {

namespace {

/// Human label for a decoration surface path ("window.tiled" ->
/// "Windows → Tiled"). Tokens outside the supported taxonomy fall back to
/// the raw token so an unknown path stays identifiable rather than blank.
QString surfacePathLabel(const QString& path)
{
    const auto tokenLabel = [](const QString& token) -> QString {
        if (token == QLatin1String("window"))
            return PhosphorI18n::tr("Windows");
        if (token == QLatin1String("tiled"))
            return PhosphorI18n::tr("Tiled");
        if (token == QLatin1String("snapped"))
            return PhosphorI18n::tr("Snapped");
        if (token == QLatin1String("floating"))
            return PhosphorI18n::tr("Floating");
        if (token == QLatin1String("osd"))
            return PhosphorI18n::tr("OSDs");
        if (token == QLatin1String("popup"))
            return PhosphorI18n::tr("Popups");
        if (token == QLatin1String("snapAssist"))
            return PhosphorI18n::tr("Snap Assist");
        if (token == QLatin1String("zoneSelector"))
            return PhosphorI18n::tr("Zone Selector");
        if (token == QLatin1String("layoutPicker"))
            return PhosphorI18n::tr("Layout Picker");
        if (token == QLatin1String("cheatsheet"))
            return PhosphorI18n::tr("Shortcut Cheatsheet");
        return token;
    };
    const QStringList tokens = path.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    QStringList labels;
    labels.reserve(tokens.size());
    for (const QString& t : tokens) {
        labels.append(tokenLabel(t));
    }
    // Literal breadcrumb separator between taxonomy levels.
    return labels.join(QStringLiteral(" \u2192 "));
}

} // namespace

QString DecorationPageController::userShaderDirectoryPath() const
{
    // cleanPath normalises any stray double-slash — same defensive shape as
    // AnimationsPageController::userShaderDirectoryPath.
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir::cleanPath(base + ConfigDefaults::userSurfaceSubdir());
}

bool DecorationPageController::installShaderPack(const QString& sourceUrl)
{
    const auto result = ShaderPackInstaller::install(sourceUrl, userShaderDirectoryPath());
    if (result != ShaderPackInstaller::Result::Success) {
        const QString message = ShaderPackInstaller::errorMessage(result);
        qCWarning(lcConfig) << "installShaderPack (surface):" << message << "— source:" << sourceUrl;
        Q_EMIT toastRequested(message);
        return false;
    }
    // The registry's file watcher rescans on its own; shaderEffectsChanged
    // re-emits from the registry when the new pack lands.
    return true;
}

void DecorationPageController::openUserShaderDirectory()
{
    const QString dir = userShaderDirectoryPath();
    if (!QDir().mkpath(dir)) {
        qCWarning(lcConfig) << "openUserShaderDirectory (surface): mkpath failed for" << dir;
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not create the user shader directory."));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

QVariantList DecorationPageController::shaderEffectUsages(const QString& effectId) const
{
    using PhosphorSurfaceShaders::DecorationProfile;
    using PhosphorSurfaceShaders::DecorationProfileTree;
    if (!m_settings || effectId.isEmpty()) {
        return {};
    }
    const DecorationProfileTree& tree = this->tree();
    QVariantList out;
    // The baseline is a real chain the resolve walk falls back to (D-Bus can set
    // one), so a pack used only there would otherwise report zero usages.
    const DecorationProfile baseline = tree.baseline();
    if (baseline.chain && baseline.chain->contains(effectId)) {
        QVariantMap entry;
        entry.insert(QLatin1String("path"), QString());
        entry.insert(QLatin1String("label"), PhosphorI18n::tr("Global default"));
        out.append(entry);
    }
    const QStringList overridden = tree.overriddenPaths();
    for (const QString& p : overridden) {
        const DecorationProfile profile = tree.directOverride(p);
        if (!profile.chain || !profile.chain->contains(effectId)) {
            continue;
        }
        QVariantMap entry;
        entry.insert(QLatin1String("path"), p);
        entry.insert(QLatin1String("label"), surfacePathLabel(p));
        out.append(entry);
    }
    // Alphabetical UI order. overriddenPaths() returns insertion order, which is
    // deterministic but not what a reader scanning the list expects.
    std::sort(out.begin(), out.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value(QLatin1String("label")).toString() < b.toMap().value(QLatin1String("label")).toString();
    });
    return out;
}

} // namespace PlasmaZones
