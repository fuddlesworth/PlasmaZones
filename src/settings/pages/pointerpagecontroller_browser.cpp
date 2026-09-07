// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// PointerPageController shader-browser bridge — the ShaderBrowserPage contract
// (install / open-directory / usages) over the pointer-pack registry and the
// user's pointer chain. Mirrors decorationpagecontroller_browser.cpp; the
// security-sensitive install path is the shared ShaderPackInstaller.

#include "pointerpagecontroller.h"

#include "config/configdefaults.h"
#include "core/interfaces/isettings.h"
#include "core/platform/logging.h"
#include "phosphor_i18n.h"
#include "settings/services/shaderpackinstaller.h"

#include <PhosphorPointer/PointerProfile.h>

#include <QDesktopServices>
#include <QDir>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QUrl>

namespace PlasmaZones {

QString PointerPageController::userShaderDirectoryPath() const
{
    // cleanPath normalises any stray double-slash — same defensive shape as
    // DecorationPageController::userShaderDirectoryPath.
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir::cleanPath(base + ConfigDefaults::userPointerSubdir());
}

bool PointerPageController::installShaderPack(const QString& sourceUrl)
{
    const auto result = ShaderPackInstaller::install(sourceUrl, userShaderDirectoryPath());
    if (result != ShaderPackInstaller::Result::Success) {
        const QString message = ShaderPackInstaller::errorMessage(result);
        qCWarning(lcConfig) << "installShaderPack (pointer):" << message << "— source:" << sourceUrl;
        Q_EMIT toastRequested(message);
        return false;
    }
    // The registry's file watcher rescans on its own; shaderEffectsChanged
    // re-emits from the registry when the new pack lands.
    return true;
}

void PointerPageController::openUserShaderDirectory()
{
    const QString dir = userShaderDirectoryPath();
    if (!QDir().mkpath(dir)) {
        qCWarning(lcConfig) << "openUserShaderDirectory (pointer): mkpath failed for" << dir;
        Q_EMIT toastRequested(PhosphorI18n::tr("Could not create the user shader directory."));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

QVariantList PointerPageController::shaderEffectUsages(const QString& effectId) const
{
    if (!m_settings || effectId.isEmpty()) {
        return {};
    }
    const PhosphorPointerShaders::PointerProfile profile = m_settings->pointerChain();
    QVariantList out;
    for (int i = 0; i < profile.layers.size(); ++i) {
        if (profile.layers.at(i).effectId != effectId) {
            continue;
        }
        QVariantMap entry;
        // There is one chain, so the layer's index is its identity. Two layers
        // of the same pack are a legitimate look here, which is exactly why the
        // usage rows have to name positions rather than collapse to one entry.
        entry.insert(QLatin1String("path"), QString::number(i));
        entry.insert(QLatin1String("label"),
                     PhosphorI18n::tr("Pointer layer %1", "@item a position in the pointer chain").arg(i + 1));
        out.append(entry);
    }
    // Already in chain order, which is the order the user sees on the page, so
    // no re-sort: an alphabetical pass would scramble "layer 10" above
    // "layer 2".
    return out;
}

} // namespace PlasmaZones
