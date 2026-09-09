// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// DecorationPageController shader-browser bridge — the ShaderBrowserPage
// contract (install / open-directory / usages) over the surface-pack registry
// and the decoration profile tree. Mirrors animationspagecontroller_shaders.cpp;
// the security-sensitive install path is the shared ShaderPackInstaller.

#include "decorationpagecontroller.h"

#include "config/configdefaults.h"
#include "core/interfaces/isettings.h"
#include "core/platform/logging.h"
#include "phosphor_i18n.h"
#include "settings/services/shaderpackinstaller.h"

#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/DecorationProfileTree.h>

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1Char>
#include <QLatin1String>
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
    // `osd` appears under both the window tree and the Phosphor shell tree, and
    // the two surfaces have different card labels, so the label depends on the
    // branch the token was reached through.
    const auto tokenLabel = [](const QString& token, bool phosphorShell) -> QString {
        if (token == QLatin1String("window"))
            return PhosphorI18n::tr("Windows");
        if (token == QLatin1String("tiled"))
            return PhosphorI18n::tr("Tiled");
        if (token == QLatin1String("snapped"))
            return PhosphorI18n::tr("Snapped");
        if (token == QLatin1String("floating"))
            return PhosphorI18n::tr("Floating");
        if (token == QLatin1String("osd"))
            return phosphorShell ? PhosphorI18n::tr("OSD Bands", "@item the Phosphor shell's on-screen display bands")
                                 : PhosphorI18n::tr("OSDs");
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
        if (token == QLatin1String("shell"))
            return PhosphorI18n::tr("Shell");
        if (token == QLatin1String("panel"))
            return PhosphorI18n::tr("Panels");
        if (token == QLatin1String("appletPopup"))
            return PhosphorI18n::tr("Applet Popups");
        if (token == QLatin1String("phosphor"))
            return PhosphorI18n::tr("Phosphor Shell", "@item breadcrumb level for the Phosphor shell's own surfaces");
        if (token == QLatin1String("bar"))
            return PhosphorI18n::tr("Bar", "@item the Phosphor shell's top bar surface, not a progress or menu bar");
        if (token == QLatin1String("popout"))
            return PhosphorI18n::tr("Popouts", "@item panels that pop out from the Phosphor shell bar");
        if (token == QLatin1String("notification"))
            return PhosphorI18n::tr("Notifications", "@item the Phosphor shell's notification toasts");
        if (token == QLatin1String("picker"))
            return PhosphorI18n::tr("Wallpaper Picker");
        if (token == QLatin1String("lock"))
            return PhosphorI18n::tr("Lock Screen");
        if (token == QLatin1String("pointer"))
            return PhosphorI18n::tr("Pointer");
        return token;
    };
    const QStringList tokens = path.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    const bool phosphorShell = tokens.contains(QLatin1String("phosphor"));
    QStringList labels;
    labels.reserve(tokens.size());
    for (const QString& t : tokens) {
        labels.append(tokenLabel(t, phosphorShell));
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

namespace {

/// Which family a dropped pack belongs to, read from its own metadata.
///
/// This browser lists both families and its import affordance is one card for
/// the page, not one per row, so there is no selection to route on and the type
/// filter is an exclusion set that is usually "show both". The pack itself is
/// the only thing that knows, and it does: the two schemas set
/// `additionalProperties: false`, so `layer` / `reach` / `trailSeconds` are
/// rejected by the surface schema and `needsBackdrop` / `multipass` /
/// `bufferShaders` by the pointer one. A pack declaring none of them is
/// ambiguous and stays on the surface path, which is where every pack went
/// before this existed.
bool droppedPackIsPointer(const QString& sourceUrl)
{
    const QString dir = QUrl(sourceUrl).isLocalFile() ? QUrl(sourceUrl).toLocalFile() : sourceUrl;
    QFile metadata(QDir(dir).filePath(QStringLiteral("metadata.json")));
    if (!metadata.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QJsonObject obj = QJsonDocument::fromJson(metadata.readAll()).object();
    return obj.contains(QLatin1String("layer")) || obj.contains(QLatin1String("trailSeconds"))
        || obj.contains(QLatin1String("reachParam"));
}

} // namespace

bool DecorationPageController::installShaderPack(const QString& sourceUrl)
{
    // A pointer pack dropped into the surface directory is not an error the
    // user ever sees: the install reports success and the pack simply never
    // appears, because the pointer registry scans a different root.
    const bool pointer = droppedPackIsPointer(sourceUrl);
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString target =
        pointer ? QDir::cleanPath(base + ConfigDefaults::userPointerSubdir()) : userShaderDirectoryPath();

    const auto result = ShaderPackInstaller::install(sourceUrl, target);
    if (result != ShaderPackInstaller::Result::Success) {
        const QString message = ShaderPackInstaller::errorMessage(result);
        qCWarning(lcConfig) << "installShaderPack" << (pointer ? "(pointer):" : "(surface):") << message
                            << "— source:" << sourceUrl;
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
