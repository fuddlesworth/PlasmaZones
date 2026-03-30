// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "translationloader.h"
#include "logging.h"

#include <QCoreApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

namespace PlasmaZones {

void loadTranslations(QCoreApplication* app)
{
    Q_ASSERT(app && "loadTranslations() must be called after QCoreApplication is created");
    if (!app)
        return;

    auto* translator = new QTranslator(app);
    const QLocale locale;

    // Search paths in priority order:
    // 1. Alongside the binary: ../share/plasmazones/translations/ (installed)
    // 2. Build tree root: ../ from bin/ (qt_add_lrelease puts .qm in build root)
    // 3. Build tree: ../translations/ (manual lrelease)

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList searchDirs = {
        appDir + QStringLiteral("/../share/plasmazones/translations"),
        appDir + QStringLiteral("/.."),
        appDir + QStringLiteral("/../translations"),
    };

    for (const QString& dir : searchDirs) {
        if (translator->load(locale, QStringLiteral("plasmazones"), QStringLiteral("_"), dir)) {
            app->installTranslator(translator);
            qCDebug(lcCore) << "Loaded translations from" << dir << "for locale" << locale.name();
            return;
        }
    }

    qCDebug(lcCore) << "No translations found for locale" << locale.name();
}

} // namespace PlasmaZones
