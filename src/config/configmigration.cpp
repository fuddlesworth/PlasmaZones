// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "configmigration.h"

#include "configbackends.h"
#include "configdefaults.h"
#include "perscreenresolver.h"

#include <PhosphorConfig/MigrationRunner.h>
#include <PhosphorConfig/Schema.h>

#include <QColor>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLatin1String>
#include <array>
#include <atomic>

namespace PlasmaZones {

// ─── PhosphorConfig schema synthesis ────────────────────────────────────────
// A minimal schema with only the migration chain populated. PZ's migration
// runner only needs the version, the version key, and the migration steps —
// group/key declarations belong to the Settings layer (future work).

namespace {
PhosphorConfig::Schema makeMigrationSchema()
{
    PhosphorConfig::Schema s;
    s.version = ConfigSchemaVersion;
    s.versionKey = ConfigKeys::versionKey();
    s.migrations = {
        {1, &ConfigMigration::migrateV1ToV2},
        // {2, &ConfigMigration::migrateV2ToV3},
    };
    return s;
}
} // namespace

std::span<const MigrationStep> ConfigMigration::migrationSteps()
{
    // Kept for callers/tests that want a flat list of PZ-native steps. Built
    // once lazily; the underlying function pointers never change at runtime.
    static const std::array<MigrationStep, 1> s_steps{{
        {1, &ConfigMigration::migrateV1ToV2},
    }};
    return {s_steps.data(), s_steps.size()};
}

// ── Migration chain runner (delegates to PhosphorConfig::MigrationRunner) ──

void ConfigMigration::runMigrationChainInMemory(QJsonObject& root)
{
    const PhosphorConfig::Schema schema = makeMigrationSchema();
    PhosphorConfig::MigrationRunner(schema).runInMemory(root);
}

bool ConfigMigration::runMigrationChain(const QString& jsonPath)
{
    const PhosphorConfig::Schema schema = makeMigrationSchema();
    return PhosphorConfig::MigrationRunner(schema).runOnFile(jsonPath);
}

// ── ensureJsonConfig ────────────────────────────────────────────────────────

// Process-level "already migrated" flag. Set by ensureJsonConfig() on its
// first successful return, cleared by resetMigrationGuardForTesting(). See
// the docstring on ConfigMigration::ensureJsonConfig() for why this exists.
// File-scope so both ensureJsonConfig() and resetMigrationGuardForTesting()
// see the same atomic.
namespace {
std::atomic<bool> s_migrated{false};
} // namespace

bool ConfigMigration::ensureJsonConfig()
{
    // Process-level guard: migration is a one-shot operation. Once we've
    // confirmed the config is at the current schema version (or successfully
    // migrated it), skip the full file read + JSON parse on subsequent calls.
    // The editor reaches this function in its ctor path before the QML engine
    // starts, so shaving the disk read here keeps the startup hot path tight.
    // Uses acquire/release so a second caller observing `true` also observes
    // every write the first caller made (the atomic rename in
    // runMigrationChain / migrateIniToJson).
    if (s_migrated.load(std::memory_order_acquire)) {
        return true;
    }

    const bool ok = ensureJsonConfigImpl();
    if (ok) {
        s_migrated.store(true, std::memory_order_release);
    }
    return ok;
}

bool ConfigMigration::ensureJsonConfigImpl()
{
    const QString jsonPath = ConfigDefaults::configFilePath();
    if (QFile::exists(jsonPath)) {
        QFile f(jsonPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QByteArray data = f.readAll();
            if (!data.trimmed().isEmpty()) {
                QJsonParseError err;
                QJsonDocument doc = QJsonDocument::fromJson(data, &err);
                if (err.error == QJsonParseError::NoError && doc.isObject()) {
                    const int version = doc.object().value(ConfigKeys::versionKey()).toInt(0);
                    if (version < ConfigSchemaVersion) {
                        return runMigrationChain(jsonPath);
                    }
                    return true; // Already at current version
                }
            }
        }
        // Corrupt or empty JSON — check if INI backup exists for re-migration.
        const QString iniPath = ConfigDefaults::legacyConfigFilePath();
        if (!QFile::exists(iniPath)) {
            const QString corruptBak = jsonPath + QStringLiteral(".corrupt.bak");
            if (QFile::exists(corruptBak)) {
                QFile::remove(corruptBak);
            }
            QFile::rename(jsonPath, corruptBak);
            qWarning(
                "ConfigMigration: corrupt JSON config moved to %s — no INI to re-migrate from, "
                "using defaults",
                qPrintable(corruptBak));
            return true;
        }
        QFile::remove(jsonPath);
    }

    const QString iniPath = ConfigDefaults::legacyConfigFilePath();
    if (!QFile::exists(iniPath)) {
        return true; // Fresh install — no old config to migrate
    }

    qInfo("ConfigMigration: migrating %s → %s", qPrintable(iniPath), qPrintable(jsonPath));

    if (!migrateIniToJson(iniPath, jsonPath)) {
        qWarning("ConfigMigration: migration failed — old config preserved at %s", qPrintable(iniPath));
        return false;
    }

    const QString bakPath = iniPath + QStringLiteral(".bak");
    if (QFile::exists(bakPath)) {
        QFile::remove(bakPath);
    }
    if (!QFile::rename(iniPath, bakPath)) {
        qWarning("ConfigMigration: could not rename %s to %s", qPrintable(iniPath), qPrintable(bakPath));
    }

    qInfo("ConfigMigration: migration complete");
    return true;
}

void ConfigMigration::resetMigrationGuardForTesting()
{
    s_migrated.store(false, std::memory_order_release);
}

// ── INI → JSON ──────────────────────────────────────────────────────────────

bool ConfigMigration::migrateIniToJson(const QString& iniPath, const QString& jsonPath)
{
    const QMap<QString, QVariant> flatMap = PhosphorConfig::QSettingsBackend::readConfigFromDisk(iniPath);
    if (flatMap.isEmpty()) {
        qInfo("ConfigMigration: old config is empty — writing minimal JSON to complete migration");
    }

    QJsonObject root = iniMapToJson(flatMap);
    // INI migration produces v1 format; the chain upgrades to current version.
    root[ConfigKeys::versionKey()] = 1;
    runMigrationChainInMemory(root);

    return PhosphorConfig::JsonBackend::writeJsonAtomically(jsonPath, root);
}

QJsonObject ConfigMigration::iniMapToJson(const QMap<QString, QVariant>& flatMap)
{
    QJsonObject root;

    const QString renderingGroup = ConfigDefaults::renderingGroup();
    const QString generalGroup = ConfigDefaults::generalGroup();
    // Hardcoded v1 key name — the INI file uses "RenderingBackend"
    const QString renderingKey = QStringLiteral("RenderingBackend");
    const QString PerScreenKeyStr = PerScreenPathResolver::perScreenKey();

    for (auto it = flatMap.constBegin(); it != flatMap.constEnd(); ++it) {
        const QString& flatKey = it.key();
        const QVariant& value = it.value();

        const int slashIdx = flatKey.indexOf(QLatin1Char('/'));
        if (slashIdx < 0) {
            // Root-level INI key (ungrouped). Route RenderingBackend to its own group.
            if (flatKey == renderingKey) {
                QJsonObject rendering = root.value(renderingGroup).toObject();
                rendering[flatKey] = convertValue(value);
                root[renderingGroup] = rendering;
            } else {
                QJsonObject general = root.value(generalGroup).toObject();
                general[flatKey] = convertValue(value);
                root[generalGroup] = general;
            }
            continue;
        }

        const QString groupPart = flatKey.left(slashIdx);
        const QString keyPart = flatKey.mid(slashIdx + 1);

        // QSettings maps ungrouped keys AND [General]-grouped keys identically,
        // so RenderingBackend may appear as either a root key (handled above) or
        // as "General/RenderingBackend". Route it to the Rendering group either way.
        if (groupPart == generalGroup && keyPart == renderingKey) {
            QJsonObject rendering = root.value(renderingGroup).toObject();
            rendering[keyPart] = convertValue(value);
            root[renderingGroup] = rendering;
            continue;
        }

        // Check for known per-screen group patterns: ZoneSelector:*, AutotileScreen:*, SnappingScreen:*
        // Other colon-containing groups (e.g., Assignment:ScreenId:Desktop:1) are regular groups.
        if (PerScreenPathResolver::isPerScreenPrefix(groupPart)) {
            const int colonIdx = groupPart.indexOf(QLatin1Char(':'));
            const QString prefix = groupPart.left(colonIdx);
            const QString screenId = groupPart.mid(colonIdx + 1);
            const QString category = PerScreenPathResolver::prefixToCategory(prefix);

            QJsonObject perScreen = root.value(PerScreenKeyStr).toObject();
            QJsonObject cat = perScreen.value(category).toObject();
            QJsonObject screen = cat.value(screenId).toObject();
            screen[keyPart] = convertValue(value);
            cat[screenId] = screen;
            perScreen[category] = cat;
            root[PerScreenKeyStr] = perScreen;
        } else {
            // Regular group: Group/Key
            QJsonObject groupObj = root.value(groupPart).toObject();
            groupObj[keyPart] = convertValue(value);
            root[groupPart] = groupObj;
        }
    }

    return root;
}

QJsonValue ConfigMigration::convertValue(const QVariant& value)
{
    const QString s = value.toString();

    // Already a typed bool from INI reader
    if (value.typeId() == QMetaType::Bool) {
        return QJsonValue(value.toBool());
    }

    // Type detection priority (order matters):
    //   1. Boolean strings ("true"/"false")
    //   2. JSON arrays/objects (trigger lists, per-algorithm settings)
    //   3. Comma-separated integers 0-255 → color hex (r,g,b or r,g,b,a)
    //   4. Plain integers
    //   5. Doubles (only if contains '.' to avoid "0" → 0.0)
    //   6. Fallback: keep as string

    // Boolean strings
    if (s.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0) {
        return QJsonValue(true);
    }
    if (s.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0) {
        return QJsonValue(false);
    }

    // Try JSON (triggers, per-algorithm settings)
    if (!s.isEmpty() && (s.front() == QLatin1Char('[') || s.front() == QLatin1Char('{'))) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(s.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError) {
            if (doc.isArray()) {
                return QJsonValue(doc.array());
            }
            if (doc.isObject()) {
                return QJsonValue(doc.object());
            }
        }
    }

    // Color in r,g,b or r,g,b,a format → convert to hex.
    // Assumption: no PlasmaZones config value is a comma-separated list of integers
    // in the 0-255 range that isn't a color. If one is added, this heuristic would
    // need a key-based allowlist to avoid false positives.
    if (s.contains(QLatin1Char(','))) {
        const QStringList parts = s.split(QLatin1Char(','));
        if (parts.size() >= 3 && parts.size() <= 4) {
            bool allNumeric = true;
            QList<int> components;
            for (const QString& part : parts) {
                bool ok = false;
                int v = part.trimmed().toInt(&ok);
                if (!ok || v < 0 || v > 255) {
                    allNumeric = false;
                    break;
                }
                components.append(v);
            }
            if (allNumeric && components.size() >= 3) {
                int a = (components.size() >= 4) ? components[3] : 255;
                QColor c(components[0], components[1], components[2], a);
                return QJsonValue(c.name(QColor::HexArgb));
            }
        }
        // Not a color — might be a comma-separated list, keep as string
        return QJsonValue(s);
    }

    // Try integer
    {
        bool ok = false;
        int intVal = s.toInt(&ok);
        if (ok) {
            return QJsonValue(intVal);
        }
    }

    // Try double (but only if it has a decimal point to avoid converting "0" → 0.0)
    if (s.contains(QLatin1Char('.'))) {
        bool ok = false;
        double dblVal = s.toDouble(&ok);
        if (ok) {
            return QJsonValue(dblVal);
        }
    }

    // Default: keep as string
    return QJsonValue(s);
}

// ── Schema migration: v1 → v2 ───────────────────────────────────────────────
// Restructures flat groups (Activation, Display, etc.) into nested dot-path
// hierarchy (Snapping.Behavior.ZoneSpan, Tiling.Gaps, etc.).

namespace {
// Helper: move a key from one JSON object to another, renaming it.
void moveKey(const QJsonObject& src, const QString& srcKey, QJsonObject& dst, const QString& dstKey)
{
    if (src.contains(srcKey)) {
        dst[dstKey] = src.value(srcKey);
    }
}
} // anonymous namespace

void ConfigMigration::migrateV1ToV2(QJsonObject& root)
{
    // ── Read all v1 groups (using ConfigKeys v1 accessors) ────────────────
    const QJsonObject v1Activation = root.value(ConfigKeys::v1ActivationGroup()).toObject();
    const QJsonObject v1Display = root.value(ConfigKeys::v1DisplayGroup()).toObject();
    const QJsonObject v1Appearance = root.value(ConfigKeys::v1AppearanceGroup()).toObject();
    const QJsonObject v1Zones = root.value(ConfigKeys::v1ZonesGroup()).toObject();
    const QJsonObject v1Behavior = root.value(ConfigKeys::v1BehaviorGroup()).toObject();
    const QJsonObject v1Exclusions = root.value(ConfigKeys::v1ExclusionsGroup()).toObject();
    const QJsonObject v1ZoneSelector = root.value(ConfigKeys::v1ZoneSelectorGroup()).toObject();
    const QJsonObject v1Autotiling = root.value(ConfigKeys::v1AutotilingGroup()).toObject();
    const QJsonObject v1AutotileShortcuts = root.value(ConfigKeys::v1AutotileShortcutsGroup()).toObject();
    const QJsonObject v1Animations = root.value(ConfigKeys::v1AnimationsGroup()).toObject();
    const QJsonObject v1GlobalShortcuts = root.value(ConfigKeys::v1GlobalShortcutsGroup()).toObject();
    const QJsonObject v1Editor = root.value(ConfigKeys::v1EditorGroup()).toObject();
    const QJsonObject v1Ordering = root.value(ConfigKeys::v1OrderingGroup()).toObject();
    const QJsonObject v1Rendering = root.value(ConfigKeys::v1RenderingGroup()).toObject();
    const QJsonObject v1Shaders = root.value(ConfigKeys::v1ShadersGroup()).toObject();

    // ── Remove all v1 groups ────────────────────────────────────────────────
    const QString v1Groups[] = {
        ConfigKeys::v1ActivationGroup(),   ConfigKeys::v1DisplayGroup(),         ConfigKeys::v1AppearanceGroup(),
        ConfigKeys::v1ZonesGroup(),        ConfigKeys::v1BehaviorGroup(),        ConfigKeys::v1ExclusionsGroup(),
        ConfigKeys::v1ZoneSelectorGroup(), ConfigKeys::v1AutotilingGroup(),      ConfigKeys::v1AutotileShortcutsGroup(),
        ConfigKeys::v1AnimationsGroup(),   ConfigKeys::v1GlobalShortcutsGroup(), ConfigKeys::v1EditorGroup(),
        ConfigKeys::v1OrderingGroup(),     ConfigKeys::v1RenderingGroup(),       ConfigKeys::v1ShadersGroup(),
    };
    for (const auto& key : v1Groups) {
        root.remove(key);
    }

    // ── Snapping (top-level) ────────────────────────────────────────────────
    QJsonObject snappingTop;
    moveKey(v1Activation, QLatin1String("SnappingEnabled"), snappingTop, QLatin1String("Enabled"));

    // ── Snapping.Behavior ───────────────────────────────────────────────────
    QJsonObject snappingBehavior;
    moveKey(v1Activation, QLatin1String("DragActivationTriggers"), snappingBehavior, QLatin1String("Triggers"));
    moveKey(v1Activation, QLatin1String("ToggleActivation"), snappingBehavior, QLatin1String("ToggleActivation"));

    // Snapping.Behavior.ZoneSpan
    QJsonObject zoneSpan;
    moveKey(v1Activation, QLatin1String("ZoneSpanEnabled"), zoneSpan, QLatin1String("Enabled"));
    moveKey(v1Activation, QLatin1String("ZoneSpanModifier"), zoneSpan, QLatin1String("Modifier"));
    moveKey(v1Activation, QLatin1String("ZoneSpanTriggers"), zoneSpan, QLatin1String("Triggers"));

    // Snapping.Behavior.SnapAssist
    QJsonObject snapAssist;
    moveKey(v1Activation, QLatin1String("SnapAssistFeatureEnabled"), snapAssist, QLatin1String("FeatureEnabled"));
    moveKey(v1Activation, QLatin1String("SnapAssistEnabled"), snapAssist, QLatin1String("Enabled"));
    moveKey(v1Activation, QLatin1String("SnapAssistTriggers"), snapAssist, QLatin1String("Triggers"));

    // Snapping.Behavior.Display
    QJsonObject snappingDisplay;
    moveKey(v1Display, QLatin1String("ShowOnAllMonitors"), snappingDisplay, QLatin1String("ShowOnAllMonitors"));
    moveKey(v1Display, QLatin1String("DisabledMonitors"), snappingDisplay, QLatin1String("DisabledMonitors"));
    moveKey(v1Display, QLatin1String("DisabledDesktops"), snappingDisplay, QLatin1String("DisabledDesktops"));
    moveKey(v1Display, QLatin1String("DisabledActivities"), snappingDisplay, QLatin1String("DisabledActivities"));
    moveKey(v1Behavior, QLatin1String("FilterLayoutsByAspectRatio"), snappingDisplay,
            QLatin1String("FilterByAspectRatio"));

    // Snapping.Behavior.WindowHandling
    QJsonObject windowHandling;
    moveKey(v1Behavior, QLatin1String("KeepOnResolutionChange"), windowHandling,
            QLatin1String("KeepOnResolutionChange"));
    moveKey(v1Behavior, QLatin1String("MoveNewToLastZone"), windowHandling, QLatin1String("MoveNewToLastZone"));
    moveKey(v1Behavior, QLatin1String("RestoreSizeOnUnsnap"), windowHandling, QLatin1String("RestoreOnUnsnap"));
    moveKey(v1Behavior, QLatin1String("StickyWindowHandling"), windowHandling, QLatin1String("StickyWindowHandling"));
    moveKey(v1Behavior, QLatin1String("RestoreWindowsToZonesOnLogin"), windowHandling, QLatin1String("RestoreOnLogin"));
    moveKey(v1Behavior, QLatin1String("DefaultLayoutId"), windowHandling, QLatin1String("DefaultLayoutId"));

    // Assemble Snapping.Behavior
    if (!zoneSpan.isEmpty())
        snappingBehavior[QLatin1String("ZoneSpan")] = zoneSpan;
    if (!snapAssist.isEmpty())
        snappingBehavior[QLatin1String("SnapAssist")] = snapAssist;
    if (!snappingDisplay.isEmpty())
        snappingBehavior[QLatin1String("Display")] = snappingDisplay;
    if (!windowHandling.isEmpty())
        snappingBehavior[QLatin1String("WindowHandling")] = windowHandling;

    // ── Snapping.Appearance ─────────────────────────────────────────────────
    QJsonObject sColors;
    moveKey(v1Appearance, QLatin1String("UseSystemColors"), sColors, QLatin1String("UseSystem"));
    moveKey(v1Appearance, QLatin1String("HighlightColor"), sColors, QLatin1String("Highlight"));
    moveKey(v1Appearance, QLatin1String("InactiveColor"), sColors, QLatin1String("Inactive"));
    moveKey(v1Appearance, QLatin1String("BorderColor"), sColors, QLatin1String("Border"));

    QJsonObject sOpacity;
    moveKey(v1Appearance, QLatin1String("ActiveOpacity"), sOpacity, QLatin1String("Active"));
    moveKey(v1Appearance, QLatin1String("InactiveOpacity"), sOpacity, QLatin1String("Inactive"));

    QJsonObject sBorder;
    moveKey(v1Appearance, QLatin1String("BorderWidth"), sBorder, QLatin1String("Width"));
    moveKey(v1Appearance, QLatin1String("BorderRadius"), sBorder, QLatin1String("Radius"));

    QJsonObject sLabels;
    moveKey(v1Appearance, QLatin1String("LabelFontColor"), sLabels, QLatin1String("FontColor"));
    moveKey(v1Appearance, QLatin1String("LabelFontFamily"), sLabels, QLatin1String("FontFamily"));
    moveKey(v1Appearance, QLatin1String("LabelFontSizeScale"), sLabels, QLatin1String("FontSizeScale"));
    moveKey(v1Appearance, QLatin1String("LabelFontWeight"), sLabels, QLatin1String("FontWeight"));
    moveKey(v1Appearance, QLatin1String("LabelFontItalic"), sLabels, QLatin1String("FontItalic"));
    moveKey(v1Appearance, QLatin1String("LabelFontUnderline"), sLabels, QLatin1String("FontUnderline"));
    moveKey(v1Appearance, QLatin1String("LabelFontStrikeout"), sLabels, QLatin1String("FontStrikeout"));

    QJsonObject snappingAppearance;
    if (!sColors.isEmpty())
        snappingAppearance[QLatin1String("Colors")] = sColors;
    if (!sOpacity.isEmpty())
        snappingAppearance[QLatin1String("Opacity")] = sOpacity;
    if (!sBorder.isEmpty())
        snappingAppearance[QLatin1String("Border")] = sBorder;
    if (!sLabels.isEmpty())
        snappingAppearance[QLatin1String("Labels")] = sLabels;

    // ── Snapping.Effects ────────────────────────────────────────────────────
    QJsonObject effects;
    moveKey(v1Appearance, QLatin1String("EnableBlur"), effects, QLatin1String("Blur"));
    moveKey(v1Display, QLatin1String("ShowNumbers"), effects, QLatin1String("ShowNumbers"));
    moveKey(v1Display, QLatin1String("FlashOnSwitch"), effects, QLatin1String("FlashOnSwitch"));
    moveKey(v1Display, QLatin1String("ShowOsdOnLayoutSwitch"), effects, QLatin1String("OsdOnLayoutSwitch"));
    moveKey(v1Display, QLatin1String("ShowNavigationOsd"), effects, QLatin1String("NavigationOsd"));
    moveKey(v1Display, QLatin1String("OsdStyle"), effects, QLatin1String("OsdStyle"));
    moveKey(v1Display, QLatin1String("OverlayDisplayMode"), effects, QLatin1String("OverlayDisplayMode"));

    // ── Snapping.ZoneSelector (keys mostly unchanged) ───────────────────────
    // v1 ZoneSelector keys don't have prefixes, so they stay the same
    QJsonObject zoneSelector = v1ZoneSelector;

    // ── Snapping.Gaps ───────────────────────────────────────────────────────
    QJsonObject snappingGaps;
    moveKey(v1Zones, QLatin1String("Padding"), snappingGaps, QLatin1String("Inner"));
    moveKey(v1Zones, QLatin1String("OuterGap"), snappingGaps, QLatin1String("Outer"));
    moveKey(v1Zones, QLatin1String("UsePerSideOuterGap"), snappingGaps, QLatin1String("UsePerSide"));
    moveKey(v1Zones, QLatin1String("OuterGapTop"), snappingGaps, QLatin1String("Top"));
    moveKey(v1Zones, QLatin1String("OuterGapBottom"), snappingGaps, QLatin1String("Bottom"));
    moveKey(v1Zones, QLatin1String("OuterGapLeft"), snappingGaps, QLatin1String("Left"));
    moveKey(v1Zones, QLatin1String("OuterGapRight"), snappingGaps, QLatin1String("Right"));
    moveKey(v1Zones, QLatin1String("AdjacentThreshold"), snappingGaps, QLatin1String("AdjacentThreshold"));

    // ── Assemble Snapping ───────────────────────────────────────────────────
    QJsonObject snapping = snappingTop;
    if (!snappingBehavior.isEmpty())
        snapping[QLatin1String("Behavior")] = snappingBehavior;
    if (!snappingAppearance.isEmpty())
        snapping[QLatin1String("Appearance")] = snappingAppearance;
    if (!effects.isEmpty())
        snapping[QLatin1String("Effects")] = effects;
    if (!zoneSelector.isEmpty())
        snapping[QLatin1String("ZoneSelector")] = zoneSelector;
    if (!snappingGaps.isEmpty())
        snapping[QLatin1String("Gaps")] = snappingGaps;
    if (!snapping.isEmpty())
        root[QLatin1String("Snapping")] = snapping;

    // ── Performance ─────────────────────────────────────────────────────────
    QJsonObject performance;
    moveKey(v1Zones, QLatin1String("PollIntervalMs"), performance, QLatin1String("PollIntervalMs"));
    moveKey(v1Zones, QLatin1String("MinimumZoneSizePx"), performance, QLatin1String("MinimumZoneSizePx"));
    moveKey(v1Zones, QLatin1String("MinimumZoneDisplaySizePx"), performance, QLatin1String("MinimumZoneDisplaySizePx"));
    if (!performance.isEmpty())
        root[QLatin1String("Performance")] = performance;

    // ── Tiling ──────────────────────────────────────────────────────────────
    QJsonObject tilingTop;
    moveKey(v1Autotiling, QLatin1String("AutotileEnabled"), tilingTop, QLatin1String("Enabled"));

    QJsonObject tilingAlgo;
    moveKey(v1Autotiling, QLatin1String("DefaultAutotileAlgorithm"), tilingAlgo, QLatin1String("Default"));
    moveKey(v1Autotiling, QLatin1String("AutotileSplitRatio"), tilingAlgo, QLatin1String("SplitRatio"));
    moveKey(v1Autotiling, QLatin1String("AutotileSplitRatioStep"), tilingAlgo, QLatin1String("SplitRatioStep"));
    moveKey(v1Autotiling, QLatin1String("AutotileMasterCount"), tilingAlgo, QLatin1String("MasterCount"));
    moveKey(v1Autotiling, QLatin1String("AutotileMaxWindows"), tilingAlgo, QLatin1String("MaxWindows"));
    moveKey(v1Autotiling, QLatin1String("AutotilePerAlgorithmSettings"), tilingAlgo,
            QLatin1String("PerAlgorithmSettings"));

    QJsonObject tilingBehavior;
    moveKey(v1Autotiling, QLatin1String("AutotileInsertPosition"), tilingBehavior, QLatin1String("InsertPosition"));
    moveKey(v1Autotiling, QLatin1String("AutotileFocusNewWindows"), tilingBehavior, QLatin1String("FocusNewWindows"));
    moveKey(v1Autotiling, QLatin1String("AutotileFocusFollowsMouse"), tilingBehavior,
            QLatin1String("FocusFollowsMouse"));
    moveKey(v1Autotiling, QLatin1String("AutotileRespectMinimumSize"), tilingBehavior,
            QLatin1String("RespectMinimumSize"));
    moveKey(v1Autotiling, QLatin1String("AutotileStickyWindowHandling"), tilingBehavior,
            QLatin1String("StickyWindowHandling"));
    moveKey(v1Autotiling, QLatin1String("LockedScreens"), tilingBehavior, QLatin1String("LockedScreens"));

    QJsonObject tColors;
    moveKey(v1Autotiling, QLatin1String("AutotileUseSystemBorderColors"), tColors, QLatin1String("UseSystem"));
    moveKey(v1Autotiling, QLatin1String("AutotileBorderColor"), tColors, QLatin1String("Active"));
    moveKey(v1Autotiling, QLatin1String("AutotileInactiveBorderColor"), tColors, QLatin1String("Inactive"));

    QJsonObject tDecorations;
    moveKey(v1Autotiling, QLatin1String("AutotileHideTitleBars"), tDecorations, QLatin1String("HideTitleBars"));

    QJsonObject tBorders;
    moveKey(v1Autotiling, QLatin1String("AutotileShowBorder"), tBorders, QLatin1String("ShowBorder"));
    moveKey(v1Autotiling, QLatin1String("AutotileBorderWidth"), tBorders, QLatin1String("Width"));
    moveKey(v1Autotiling, QLatin1String("AutotileBorderRadius"), tBorders, QLatin1String("Radius"));

    QJsonObject tilingAppearance;
    if (!tColors.isEmpty())
        tilingAppearance[QLatin1String("Colors")] = tColors;
    if (!tDecorations.isEmpty())
        tilingAppearance[QLatin1String("Decorations")] = tDecorations;
    if (!tBorders.isEmpty())
        tilingAppearance[QLatin1String("Borders")] = tBorders;

    QJsonObject tilingGaps;
    moveKey(v1Autotiling, QLatin1String("AutotileInnerGap"), tilingGaps, QLatin1String("Inner"));
    moveKey(v1Autotiling, QLatin1String("AutotileOuterGap"), tilingGaps, QLatin1String("Outer"));
    moveKey(v1Autotiling, QLatin1String("AutotileUsePerSideOuterGap"), tilingGaps, QLatin1String("UsePerSide"));
    moveKey(v1Autotiling, QLatin1String("AutotileOuterGapTop"), tilingGaps, QLatin1String("Top"));
    moveKey(v1Autotiling, QLatin1String("AutotileOuterGapBottom"), tilingGaps, QLatin1String("Bottom"));
    moveKey(v1Autotiling, QLatin1String("AutotileOuterGapLeft"), tilingGaps, QLatin1String("Left"));
    moveKey(v1Autotiling, QLatin1String("AutotileOuterGapRight"), tilingGaps, QLatin1String("Right"));
    moveKey(v1Autotiling, QLatin1String("AutotileSmartGaps"), tilingGaps, QLatin1String("SmartGaps"));

    QJsonObject tiling = tilingTop;
    if (!tilingAlgo.isEmpty())
        tiling[QLatin1String("Algorithm")] = tilingAlgo;
    if (!tilingBehavior.isEmpty())
        tiling[QLatin1String("Behavior")] = tilingBehavior;
    if (!tilingAppearance.isEmpty())
        tiling[QLatin1String("Appearance")] = tilingAppearance;
    if (!tilingGaps.isEmpty())
        tiling[QLatin1String("Gaps")] = tilingGaps;
    if (!tiling.isEmpty())
        root[QLatin1String("Tiling")] = tiling;

    // ── Exclusions (key renames) ────────────────────────────────────────────
    QJsonObject exclusions;
    moveKey(v1Exclusions, QLatin1String("ExcludeTransientWindows"), exclusions, QLatin1String("TransientWindows"));
    moveKey(v1Exclusions, QLatin1String("MinimumWindowWidth"), exclusions, QLatin1String("MinimumWindowWidth"));
    moveKey(v1Exclusions, QLatin1String("MinimumWindowHeight"), exclusions, QLatin1String("MinimumWindowHeight"));
    moveKey(v1Exclusions, QLatin1String("Applications"), exclusions, QLatin1String("Applications"));
    moveKey(v1Exclusions, QLatin1String("WindowClasses"), exclusions, QLatin1String("WindowClasses"));
    if (!exclusions.isEmpty())
        root[QLatin1String("Exclusions")] = exclusions;

    // ── Rendering (key rename) ──────────────────────────────────────────────
    QJsonObject rendering;
    moveKey(v1Rendering, QLatin1String("RenderingBackend"), rendering, QLatin1String("Backend"));
    if (!rendering.isEmpty())
        root[QLatin1String("Rendering")] = rendering;

    // ── Shaders (key renames) ───────────────────────────────────────────────
    QJsonObject shaders;
    moveKey(v1Shaders, QLatin1String("EnableShaderEffects"), shaders, QLatin1String("Enabled"));
    moveKey(v1Shaders, QLatin1String("ShaderFrameRate"), shaders, QLatin1String("FrameRate"));
    moveKey(v1Shaders, QLatin1String("EnableAudioVisualizer"), shaders, QLatin1String("AudioVisualizer"));
    moveKey(v1Shaders, QLatin1String("AudioSpectrumBarCount"), shaders, QLatin1String("AudioSpectrumBarCount"));
    if (!shaders.isEmpty())
        root[QLatin1String("Shaders")] = shaders;

    // ── Animations (key renames) ────────────────────────────────────────────
    QJsonObject animations;
    moveKey(v1Animations, QLatin1String("AnimationsEnabled"), animations, QLatin1String("Enabled"));
    moveKey(v1Animations, QLatin1String("AnimationDuration"), animations, QLatin1String("Duration"));
    moveKey(v1Animations, QLatin1String("AnimationEasingCurve"), animations, QLatin1String("EasingCurve"));
    moveKey(v1Animations, QLatin1String("AnimationMinDistance"), animations, QLatin1String("MinDistance"));
    moveKey(v1Animations, QLatin1String("AnimationSequenceMode"), animations, QLatin1String("SequenceMode"));
    moveKey(v1Animations, QLatin1String("AnimationStaggerInterval"), animations, QLatin1String("StaggerInterval"));
    if (!animations.isEmpty())
        root[QLatin1String("Animations")] = animations;

    // ── Shortcuts.Global (drop "Shortcut" suffix from some keys) ────────────
    QJsonObject globalShortcuts;
    moveKey(v1GlobalShortcuts, QLatin1String("OpenEditorShortcut"), globalShortcuts, QLatin1String("OpenEditor"));
    moveKey(v1GlobalShortcuts, QLatin1String("OpenSettingsShortcut"), globalShortcuts, QLatin1String("OpenSettings"));
    moveKey(v1GlobalShortcuts, QLatin1String("PreviousLayoutShortcut"), globalShortcuts,
            QLatin1String("PreviousLayout"));
    moveKey(v1GlobalShortcuts, QLatin1String("NextLayoutShortcut"), globalShortcuts, QLatin1String("NextLayout"));
    for (int i = 1; i <= 9; ++i) {
        moveKey(v1GlobalShortcuts, QStringLiteral("QuickLayout%1Shortcut").arg(i), globalShortcuts,
                QStringLiteral("QuickLayout%1").arg(i));
    }
    // Navigation keys — same names in v1 and v2
    for (const auto& key :
         {"MoveWindowLeft", "MoveWindowRight", "MoveWindowUp", "MoveWindowDown", "FocusZoneLeft", "FocusZoneRight",
          "FocusZoneUp", "FocusZoneDown", "PushToEmptyZone", "RestoreWindowSize", "ToggleWindowFloat", "SwapWindowLeft",
          "SwapWindowRight", "SwapWindowUp", "SwapWindowDown", "RotateWindowsClockwise",
          "RotateWindowsCounterclockwise", "CycleWindowForward", "CycleWindowBackward"}) {
        moveKey(v1GlobalShortcuts, QLatin1String(key), globalShortcuts, QLatin1String(key));
    }
    for (int i = 1; i <= 9; ++i) {
        const QString key = QStringLiteral("SnapToZone%1").arg(i);
        moveKey(v1GlobalShortcuts, key, globalShortcuts, key);
    }
    moveKey(v1GlobalShortcuts, QLatin1String("ResnapToNewLayoutShortcut"), globalShortcuts,
            QLatin1String("ResnapToNewLayout"));
    moveKey(v1GlobalShortcuts, QLatin1String("SnapAllWindowsShortcut"), globalShortcuts,
            QLatin1String("SnapAllWindows"));
    moveKey(v1GlobalShortcuts, QLatin1String("LayoutPickerShortcut"), globalShortcuts, QLatin1String("LayoutPicker"));
    moveKey(v1GlobalShortcuts, QLatin1String("ToggleLayoutLockShortcut"), globalShortcuts,
            QLatin1String("ToggleLayoutLock"));

    // ── Shortcuts.Tiling (drop "Shortcut" suffix) ───────────────────────────
    QJsonObject tilingShortcuts;
    moveKey(v1AutotileShortcuts, QLatin1String("ToggleShortcut"), tilingShortcuts, QLatin1String("Toggle"));
    moveKey(v1AutotileShortcuts, QLatin1String("FocusMasterShortcut"), tilingShortcuts, QLatin1String("FocusMaster"));
    moveKey(v1AutotileShortcuts, QLatin1String("SwapMasterShortcut"), tilingShortcuts, QLatin1String("SwapMaster"));
    moveKey(v1AutotileShortcuts, QLatin1String("IncMasterRatioShortcut"), tilingShortcuts,
            QLatin1String("IncMasterRatio"));
    moveKey(v1AutotileShortcuts, QLatin1String("DecMasterRatioShortcut"), tilingShortcuts,
            QLatin1String("DecMasterRatio"));
    moveKey(v1AutotileShortcuts, QLatin1String("IncMasterCountShortcut"), tilingShortcuts,
            QLatin1String("IncMasterCount"));
    moveKey(v1AutotileShortcuts, QLatin1String("DecMasterCountShortcut"), tilingShortcuts,
            QLatin1String("DecMasterCount"));
    moveKey(v1AutotileShortcuts, QLatin1String("RetileShortcut"), tilingShortcuts, QLatin1String("Retile"));

    QJsonObject shortcuts;
    if (!globalShortcuts.isEmpty())
        shortcuts[QLatin1String("Global")] = globalShortcuts;
    if (!tilingShortcuts.isEmpty())
        shortcuts[QLatin1String("Tiling")] = tilingShortcuts;
    if (!shortcuts.isEmpty())
        root[QLatin1String("Shortcuts")] = shortcuts;

    // ── Editor (split into sub-groups) ──────────────────────────────────────
    QJsonObject editorShortcuts;
    moveKey(v1Editor, QLatin1String("EditorDuplicateShortcut"), editorShortcuts, QLatin1String("Duplicate"));
    moveKey(v1Editor, QLatin1String("EditorSplitHorizontalShortcut"), editorShortcuts,
            QLatin1String("SplitHorizontal"));
    moveKey(v1Editor, QLatin1String("EditorSplitVerticalShortcut"), editorShortcuts, QLatin1String("SplitVertical"));
    moveKey(v1Editor, QLatin1String("EditorFillShortcut"), editorShortcuts, QLatin1String("Fill"));

    QJsonObject editorSnapping;
    moveKey(v1Editor, QLatin1String("GridSnappingEnabled"), editorSnapping, QLatin1String("GridEnabled"));
    moveKey(v1Editor, QLatin1String("EdgeSnappingEnabled"), editorSnapping, QLatin1String("EdgeEnabled"));
    moveKey(v1Editor, QLatin1String("SnapIntervalX"), editorSnapping, QLatin1String("IntervalX"));
    moveKey(v1Editor, QLatin1String("SnapIntervalY"), editorSnapping, QLatin1String("IntervalY"));
    // If per-axis intervals don't exist, propagate the unified SnapInterval to both.
    // Without this, configs that only set SnapInterval (no per-axis override) would
    // lose their value because the v2 load code reads IntervalX/IntervalY directly.
    if (!editorSnapping.contains(QLatin1String("IntervalX"))) {
        moveKey(v1Editor, QLatin1String("SnapInterval"), editorSnapping, QLatin1String("IntervalX"));
    }
    if (!editorSnapping.contains(QLatin1String("IntervalY"))) {
        moveKey(v1Editor, QLatin1String("SnapInterval"), editorSnapping, QLatin1String("IntervalY"));
    }
    moveKey(v1Editor, QLatin1String("SnapOverrideModifier"), editorSnapping, QLatin1String("OverrideModifier"));

    QJsonObject editorFillOnDrop;
    moveKey(v1Editor, QLatin1String("FillOnDropEnabled"), editorFillOnDrop, QLatin1String("Enabled"));
    moveKey(v1Editor, QLatin1String("FillOnDropModifier"), editorFillOnDrop, QLatin1String("Modifier"));

    QJsonObject editor;
    if (!editorShortcuts.isEmpty())
        editor[QLatin1String("Shortcuts")] = editorShortcuts;
    if (!editorSnapping.isEmpty())
        editor[QLatin1String("Snapping")] = editorSnapping;
    if (!editorFillOnDrop.isEmpty())
        editor[QLatin1String("FillOnDrop")] = editorFillOnDrop;
    if (!editor.isEmpty())
        root[QLatin1String("Editor")] = editor;

    // ── Ordering (keys unchanged) ───────────────────────────────────────────
    if (!v1Ordering.isEmpty())
        root[QLatin1String("Ordering")] = v1Ordering;

    // ── Extract WindowTracking (ephemeral session state) to session.json ──
    // In v2, session state lives in its own file to avoid write contention
    // between user preferences (config.json) and high-frequency window
    // tracking saves (session.json).
    const QString wtGroup = ConfigKeys::windowTrackingGroup();
    if (root.contains(wtGroup)) {
        QJsonObject sessionRoot;
        sessionRoot[wtGroup] = root.value(wtGroup);
        const QString sessionPath = ConfigDefaults::sessionFilePath();
        if (!PhosphorConfig::JsonBackend::writeJsonAtomically(sessionPath, sessionRoot)) {
            qWarning("ConfigMigration: failed to write session state to %s", qPrintable(sessionPath));
        }
        root.remove(wtGroup);
    }

    // ── Extract Assignment/QuickLayouts to assignments.json ─────────────────
    // LayoutManager owns its own persistence file, separate from config.json.
    // Note: LayoutManager::loadAssignments() has a runtime migration fallback
    // for users already on v2 whose Assignment:* groups were never extracted
    // by this path (e.g. upgraded between the v2 stamp and this split).
    {
        QJsonObject assignRoot;
        const QString assignPrefix = ConfigDefaults::assignmentGroupPrefix();
        QStringList keysToRemove;
        for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
            if (it.key().startsWith(assignPrefix)) {
                assignRoot[it.key()] = it.value();
                keysToRemove.append(it.key());
            }
        }
        const QString quickLayoutsKey = ConfigDefaults::quickLayoutsGroup();
        if (root.contains(quickLayoutsKey)) {
            assignRoot[quickLayoutsKey] = root.value(quickLayoutsKey);
            keysToRemove.append(quickLayoutsKey);
        }
        // ModeTracking is NOT extracted to assignments.json — it is consumed
        // by LayoutManager::loadAssignments() directly from config.json and
        // deleted after application.  Extracting it here would leave dead data
        // in assignments.json that nothing reads.
        const QString modeTrackingKey = ConfigDefaults::modeTrackingGroup();
        if (root.contains(modeTrackingKey)) {
            keysToRemove.append(modeTrackingKey);
        }
        if (!assignRoot.isEmpty()) {
            const QString assignPath = ConfigDefaults::assignmentsFilePath();
            if (!PhosphorConfig::JsonBackend::writeJsonAtomically(assignPath, assignRoot)) {
                qWarning("ConfigMigration: failed to write assignments to %s", qPrintable(assignPath));
            }
        }
        for (const QString& key : keysToRemove) {
            root.remove(key);
        }
    }

    // ── Bump version ────────────────────────────────────────────────────────
    // Stamp literal 2, not ConfigSchemaVersion — prevents future version bumps
    // (e.g. to 3) from making this step stamp 3 and skipping a v2→v3 migration.
    root[ConfigKeys::versionKey()] = 2;
}

} // namespace PlasmaZones
