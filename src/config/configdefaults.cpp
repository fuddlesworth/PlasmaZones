// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "configdefaults.h"

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Profile.h>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>

namespace PlasmaZones {

static QString configDir()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (dir.isEmpty()) {
        dir = QDir::homePath() + QStringLiteral("/.config");
    }
    return dir;
}

QString ConfigDefaults::configFilePath()
{
    return configDir() + QStringLiteral("/plasmazones/config.json");
}

QString ConfigDefaults::sessionFilePath()
{
    return configDir() + QStringLiteral("/plasmazones/session.json");
}

QString ConfigDefaults::windowRulesFilePath()
{
    return configDir() + QStringLiteral("/plasmazones/windowrules.json");
}

QString ConfigDefaults::quickLayoutsFilePath()
{
    return configDir() + QStringLiteral("/plasmazones/quicklayouts.json");
}

QString ConfigDefaults::layoutSettingsFilePath()
{
    return configDir() + QStringLiteral("/plasmazones/layout-settings.json");
}

QString ConfigDefaults::legacyConfigFilePath()
{
    return configDir() + QStringLiteral("/plasmazonesrc");
}

QString ConfigDefaults::readRenderingBackendFromDisk()
{
    // Lightweight read — only needs one key, so avoid constructing a full backend
    // (which parses the entire file and tracks group state).  This runs before
    // QCoreApplication exists on the daemon startup path.
    const QString jsonPath = configFilePath();
    if (QFile::exists(jsonPath)) {
        QFile f(jsonPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                const QJsonObject rendering = doc.object().value(renderingGroup()).toObject();
                if (rendering.contains(backendKey())) {
                    return normalizeRenderingBackend(rendering.value(backendKey()).toString(renderingBackend()));
                }
            }
        }
    }

    // Fallback: read from the legacy INI when the JSON is absent, unparseable,
    // or doesn't carry the key — in practice the pre-migration window (this
    // function is called very early, before ensureJsonConfig(), and a
    // successful migration renames the INI away).
    const QString iniPath = legacyConfigFilePath();
    if (QFile::exists(iniPath)) {
        QSettings cfg(iniPath, QSettings::IniFormat);
        // v1 INI key name — the INI file predates the v2 rename to "Backend".
        // Routed through the frozen `Legacy::v1RenderingBackendKey()` accessor
        // shared with `migrateIniToJson` / `migrateV1ToV2` so a future rename
        // of the literal can't drift one consumer behind the others.
        const QString raw = cfg.value(ConfigKeys::Legacy::v1RenderingBackendKey(), renderingBackend()).toString();
        return normalizeRenderingBackend(raw);
    }

    return renderingBackend();
}

QVariantMap ConfigDefaults::animationProfile(const PhosphorAnimation::CurveRegistry& registry)
{
    // Curve is resolved through the CurveRegistry so the wire format
    // ("0.33,1.00,0.68,1.00") maps to the same shared_ptr<const Curve>
    // the runtime uses.
    PhosphorAnimation::Profile p;
    p.curve = registry.create(animationEasingCurve());
    p.duration = static_cast<qreal>(animationDuration());
    p.minDistance = animationMinDistance();
    p.sequenceMode = static_cast<PhosphorAnimation::SequenceMode>(animationSequenceMode());
    p.staggerInterval = animationStaggerInterval();
    return p.toJson().toVariantMap();
}

} // namespace PlasmaZones
