// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "configdefaults.h"

#include "configkeys.h"

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorLayoutApi/LayoutId.h>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
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

QString ConfigDefaults::rulesFilePath()
{
    return configDir() + QStringLiteral("/plasmazones/rules.json");
}

QString ConfigDefaults::profilesDir()
{
    return configDir() + QStringLiteral("/plasmazones/profiles");
}

QString ConfigDefaults::quickLayoutsFilePath()
{
    return configDir() + QStringLiteral("/plasmazones/quicklayouts.json");
}

QString ConfigDefaults::layoutSettingsFilePath()
{
    return configDir() + QStringLiteral("/plasmazones/layout-settings.json");
}

QString ConfigDefaults::layoutsDirPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QLatin1Char('/')
        + ConfigKeys::layoutsSubdir();
}

QJsonObject ConfigDefaults::defaultLayoutVisibilitySettings()
{
    // Out-of-the-box the picker shows a curated subset; the rest start hidden
    // (users re-show any via the eye toggle). Keys mirror layout-settings.json
    // exactly. The "hiddenFromSelector" key and "autotile:" prefix match
    // PhosphorZones::ZoneJsonKeys::HiddenFromSelector and
    // PhosphorLayout::LayoutId::AutotilePrefix — kept as literals here so the
    // config layer takes no phosphor-zones / phosphor-layout-api dependency.
    const QJsonObject hidden{{QStringLiteral("hiddenFromSelector"), true}};

    // Non-curated standard snapping layouts, by bundled layout UUID. "Wide" is
    // curated (visible by default), so the standard set shows an even eight:
    // BSP, Columns (2), Columns (3), Focus, Grid (2x2), Master + Stack, Rows (2),
    // and Wide.
    static const QStringList layoutIds{
        QStringLiteral("{b8669c74-947a-4551-ba8b-79b6444439e8}"), // Fibonacci
        QStringLiteral("{a40ad8ca-2d60-4418-92cc-01b83420918e}"), // Grid (3x2)
        QStringLiteral("{0c9585bc-ecae-4e87-a6b8-9d34e9b791f2}"), // Priority Grid
        QStringLiteral("{a11899b1-f0e3-4425-9363-acb71726c566}"), // Split Focus
    };

    // Non-curated tiling algorithms (visible set: aligned-grid, bsp, columns,
    // deck, dwindle-memory, master-stack, monocle, three-column). The plain
    // "grid" is hidden by default — "aligned-grid" supersedes it as the curated
    // grid (equal cells, but resize-aware), so only one grid shows out of the box.
    static const QStringList algorithmIds{
        QStringLiteral("cascade"),
        QStringLiteral("centered-master"),
        QStringLiteral("cluster"),
        QStringLiteral("corner-master"),
        QStringLiteral("dwindle"),
        QStringLiteral("floating-center"),
        QStringLiteral("focus-sidebar"),
        QStringLiteral("grid"),
        QStringLiteral("horizontal-deck"),
        QStringLiteral("paper"),
        QStringLiteral("quadrant-priority"),
        QStringLiteral("rows"),
        QStringLiteral("spiral"),
        QStringLiteral("spread"),
        QStringLiteral("stair"),
        QStringLiteral("tatami"),
        QStringLiteral("theater"),
        QStringLiteral("wide"),
        QStringLiteral("zen"),
    };

    QJsonObject out;
    for (const QString& id : layoutIds) {
        out.insert(id, hidden);
    }
    for (const QString& id : algorithmIds) {
        out.insert(PhosphorLayout::LayoutId::makeAutotileId(id), hidden);
    }
    return out;
}

QString ConfigDefaults::legacyConfigFilePath()
{
    return configDir() + QStringLiteral("/plasmazonesrc");
}

ConfigDefaults::RenderingBootConfig ConfigDefaults::readRenderingConfigFromDisk()
{
    // Lightweight read — only needs the Rendering group, so avoid constructing
    // a full backend (which parses the entire file and tracks group state), and
    // parse the file once for both keys. This runs before QCoreApplication
    // exists on the daemon startup path.
    RenderingBootConfig out{renderingBackend(), gpuDevice()};
    bool backendFound = false;
    const QString jsonPath = configFilePath();
    if (QFile::exists(jsonPath)) {
        QFile f(jsonPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                const QJsonObject rendering = doc.object().value(renderingGroup()).toObject();
                if (rendering.contains(backendKey())) {
                    out.backend = normalizeRenderingBackend(rendering.value(backendKey()).toString(renderingBackend()));
                    backendFound = true;
                }
                if (rendering.contains(gpuKey())) {
                    out.gpuDevice = normalizeGpuDevice(rendering.value(gpuKey()).toString(gpuDevice()));
                }
            }
        }
    }

    // Backend fallback: read from the legacy INI when the JSON is absent,
    // unparseable, or doesn't carry the key — in practice the pre-migration
    // window (this function is called very early, before ensureJsonConfig(),
    // and a successful migration renames the INI away). No INI fallback for
    // the GPU pin: Rendering.Gpu postdates the v1 INI format.
    if (!backendFound) {
        const QString iniPath = legacyConfigFilePath();
        if (QFile::exists(iniPath)) {
            QSettings cfg(iniPath, QSettings::IniFormat);
            // v1 INI key name — the INI file predates the v2 rename to "Backend".
            // Routed through the frozen `Legacy::v1RenderingBackendKey()` accessor
            // shared with `migrateIniToJson` / `migrateV1ToV2` so a future rename
            // of the literal can't drift one consumer behind the others.
            const QString raw = cfg.value(ConfigKeys::Legacy::v1RenderingBackendKey(), renderingBackend()).toString();
            out.backend = normalizeRenderingBackend(raw);
        }
    }

    return out;
}

QString ConfigDefaultsShaders::normalizeGpuDevice(const QString& raw)
{
    const QString normalized = raw.toLower().trimmed();
    if (normalized.isEmpty() || normalized == gpuDevice()) {
        return gpuDevice();
    }
    // Exactly four hex digits per field: sysfs prints PCI ids as 0x%04x and
    // GpuDeviceList emits them 0x-stripped, so a shorter form never comes from
    // a legitimate producer. anchoredPattern (\A...\z) keeps the anchoring
    // independent of the trim above (PCRE '$' alone would still match before a
    // trailing newline).
    static const QRegularExpression pciPair(
        QRegularExpression::anchoredPattern(QStringLiteral("[0-9a-f]{4}:[0-9a-f]{4}")));
    return pciPair.match(normalized).hasMatch() ? normalized : gpuDevice();
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
