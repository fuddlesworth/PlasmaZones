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

QString ConfigDefaults::assignmentsFilePath()
{
    return configDir() + QStringLiteral("/plasmazones/assignments.json");
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

    // Fallback: read from legacy INI file if JSON doesn't exist yet (migration
    // hasn't run — this function is called very early, before ensureJsonConfig()).
    const QString iniPath = legacyConfigFilePath();
    if (QFile::exists(iniPath)) {
        QSettings cfg(iniPath, QSettings::IniFormat);
        // Hardcoded v1 key name — the INI file predates the v2 rename to "Backend"
        const QString raw = cfg.value(QStringLiteral("RenderingBackend"), renderingBackend()).toString();
        return normalizeRenderingBackend(raw);
    }

    return renderingBackend();
}

QString ConfigDefaults::animationProfile(const PhosphorAnimation::CurveRegistry& registry)
{
    // Assemble the default animation Profile from the existing per-field
    // defaults and serialise via Profile::toJson. This preserves the
    // pre-migration feel — same duration / easing / minDistance etc.
    // — while persisting in the new Phase-4 single-blob format.
    //
    // Curve is looked up through CurveRegistry so the wire format
    // ("0.33,1.00,0.68,1.00" — cubic-bezier) resolves to the exact
    // same shared_ptr<const Curve> the AnimatedValue<T> runtime uses,
    // no divergent parse paths.
    PhosphorAnimation::Profile p;
    p.curve = registry.create(animationEasingCurve());
    p.duration = static_cast<qreal>(animationDuration());
    p.minDistance = animationMinDistance();
    p.sequenceMode = static_cast<PhosphorAnimation::SequenceMode>(animationSequenceMode());
    p.staggerInterval = animationStaggerInterval();
    return QString::fromUtf8(QJsonDocument(p.toJson()).toJson(QJsonDocument::Compact));
}

} // namespace PlasmaZones
