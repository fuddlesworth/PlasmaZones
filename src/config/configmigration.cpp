// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "configmigration.h"

#include "configbackends.h"
#include "configdefaults.h"
#include "perscreenresolver.h"

#include <PhosphorConfig/MigrationRunner.h>
#include <PhosphorConfig/Schema.h>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLatin1String>
#include <QLockFile>
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
        {2, &ConfigMigration::migrateV2ToV3},
    };
    return s;
}
} // namespace

std::span<const MigrationStep> ConfigMigration::migrationSteps()
{
    // Kept for callers/tests that want a flat list of PZ-native steps. Built
    // once lazily; the underlying function pointers never change at runtime.
    static const std::array<MigrationStep, 2> s_steps{{
        {1, &ConfigMigration::migrateV1ToV2},
        {2, &ConfigMigration::migrateV2ToV3},
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

    // Cross-process migration lock: PZ starts multiple processes (daemon,
    // settings, editor) in parallel at session start, each of which calls
    // ensureJsonConfig in its own ctor path. Without this lock, two
    // concurrent starts can race the read-migrate-write sequence of
    // migrateIniToJson / runMigrationChain — the later writer silently
    // overwrites the earlier migration's output.
    //
    // QLockFile is an advisory flock-style lock that cleans up automatically
    // when the owning process exits (including crashes, via stale-lock
    // detection). 5-second stale window covers the real-world migration
    // cost; a genuinely hung peer won't starve us forever.
    //
    // QLockFile requires the lock file's parent directory to exist —
    // ensureJsonConfigImpl below also needs the directory for any write it
    // performs, so create it up front. Silently fail the mkpath and let
    // tryLock report the lock-creation failure as the primary error.
    const QString jsonPath = ConfigDefaults::configFilePath();
    QDir().mkpath(QFileInfo(jsonPath).absolutePath());
    QLockFile lock(jsonPath + QStringLiteral(".migrate.lock"));
    lock.setStaleLockTime(5000);
    if (!lock.tryLock(5000)) {
        // Couldn't acquire the lock within the timeout. The peer may have
        // completed successfully (in which case our next call will see
        // the current schema version and return early) — fall through and
        // let ensureJsonConfigImpl re-check the on-disk state.
        qWarning("ConfigMigration: could not acquire migration lock within 5s — proceeding without lock");
    }

    // Re-check the migrated flag after acquiring the lock. Another process
    // may have just released it after stamping the current schema version;
    // re-reading the file lets us short-circuit without paying the parse
    // cost again.
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
        bool whitespaceOnly = false;
        bool corrupt = false;
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QByteArray data = f.readAll();
            if (data.trimmed().isEmpty()) {
                // Truncated / zero-byte files aren't corruption — treat them
                // like a fresh install (proceed to the INI / fresh-install
                // logic below) instead of archiving the empty file.
                whitespaceOnly = true;
            } else {
                QJsonParseError err;
                QJsonDocument doc = QJsonDocument::fromJson(data, &err);
                if (err.error == QJsonParseError::NoError && doc.isObject()) {
                    const int version = doc.object().value(ConfigKeys::versionKey()).toInt(0);
                    if (version < ConfigSchemaVersion) {
                        return runMigrationChain(jsonPath);
                    }
                    return true; // Already at current version
                }
                corrupt = true;
            }
        } else {
            // Open failed — log and treat as fresh-install fall-through.
            qWarning("ConfigMigration: could not open %s for reading: %s", qPrintable(jsonPath),
                     qPrintable(f.errorString()));
            return false;
        }

        // Real parse-error corruption — always back up to .corrupt.bak before
        // removing. Whether or not an INI exists to re-migrate from, the user
        // may want to recover hand-made edits from the corrupt JSON (a parse
        // error can be one stray character in an otherwise valid file).
        // Asymmetric "rename if no INI, silent rm if INI exists" was a trap.
        if (corrupt) {
            const QString corruptBak = jsonPath + QStringLiteral(".corrupt.bak");
            if (QFile::exists(corruptBak) && !QFile::remove(corruptBak)) {
                qWarning("ConfigMigration: failed to remove old %s — leaving corrupt JSON in place",
                         qPrintable(corruptBak));
                return false;
            }
            if (!QFile::rename(jsonPath, corruptBak)) {
                qWarning("ConfigMigration: failed to rename corrupt JSON to %s — leaving it in place",
                         qPrintable(corruptBak));
                return false;
            }
            const QString iniPath = ConfigDefaults::legacyConfigFilePath();
            if (!QFile::exists(iniPath)) {
                qWarning(
                    "ConfigMigration: corrupt JSON config moved to %s — no INI to re-migrate from, "
                    "using defaults",
                    qPrintable(corruptBak));
                return true;
            }
            qWarning("ConfigMigration: corrupt JSON config moved to %s — re-migrating from INI",
                     qPrintable(corruptBak));
            // Fall through to the INI migration path below.
        } else if (whitespaceOnly) {
            // Drop the empty file so the INI-or-fresh-install path below is clean.
            if (!QFile::remove(jsonPath)) {
                qWarning("ConfigMigration: failed to remove empty JSON %s", qPrintable(jsonPath));
                return false;
            }
        }
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
                rendering[flatKey] = convertValue(flatKey, value);
                root[renderingGroup] = rendering;
            } else {
                QJsonObject general = root.value(generalGroup).toObject();
                general[flatKey] = convertValue(flatKey, value);
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
            rendering[keyPart] = convertValue(keyPart, value);
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
            screen[keyPart] = convertValue(keyPart, value);
            cat[screenId] = screen;
            perScreen[category] = cat;
            root[PerScreenKeyStr] = perScreen;
        } else {
            // Regular group: Group/Key
            QJsonObject groupObj = root.value(groupPart).toObject();
            groupObj[keyPart] = convertValue(keyPart, value);
            root[groupPart] = groupObj;
        }
    }

    return root;
}

QJsonValue ConfigMigration::convertValue(const QString& keyName, const QVariant& value)
{
    const QString s = value.toString();

    // Already a typed bool from INI reader
    if (value.typeId() == QMetaType::Bool) {
        return QJsonValue(value.toBool());
    }

    // Type detection priority (order matters):
    //   1. Boolean strings ("true"/"false")
    //   2. JSON arrays/objects (trigger lists, per-algorithm settings)
    //   3. Comma-separated integers 0-255 on a color-shaped key → hex
    //      (content heuristic alone is ambiguous — a layout-order list like
    //      "1,2,3" also matches, so gate on the key name to avoid false
    //      positives)
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
    //
    // The old code applied this conversion to ANY comma-separated list of
    // 3–4 ints in 0..255, which corrupts settings whose wire format happens
    // to match — e.g. a layout-order like "1,2,3". Require the key to look
    // like a color key (ends with "Color") before converting.
    //
    // Every v1 color key in the PZ config follows this convention:
    //   HighlightColor / InactiveColor / BorderColor / LabelFontColor /
    //   AutotileBorderColor / AutotileInactiveBorderColor.
    if (keyName.endsWith(QLatin1String("Color")) && s.contains(QLatin1Char(','))) {
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
    }

    // Any other comma-containing value is kept verbatim — comma-separated
    // lists (layout order, exclusion apps, locked screens) round-trip as
    // strings and get parsed by their respective setters.
    if (s.contains(QLatin1Char(','))) {
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
    //
    // Atomicity: only mutate `root` after the side-effect write succeeds.
    // If the out-of-tree write fails, leaving the keys in `root` lets
    // `config.json` retain the data so the next startup's migration can
    // retry. A partial commit here would silently lose session state.
    bool allSideEffectsSucceeded = true;

    const QString wtGroup = ConfigKeys::windowTrackingGroup();
    if (root.contains(wtGroup)) {
        QJsonObject sessionRoot;
        sessionRoot[wtGroup] = root.value(wtGroup);
        const QString sessionPath = ConfigDefaults::sessionFilePath();
        if (PhosphorConfig::JsonBackend::writeJsonAtomically(sessionPath, sessionRoot)) {
            root.remove(wtGroup);
        } else {
            qWarning(
                "ConfigMigration: failed to write session state to %s — "
                "aborting migration so the next run can retry",
                qPrintable(sessionPath));
            allSideEffectsSucceeded = false;
        }
    }

    // ── Extract Assignment/QuickLayouts to assignments.json ─────────────────
    // PhosphorZones::LayoutRegistry owns its own persistence file, separate from config.json.
    // Note: PhosphorZones::LayoutRegistry::loadAssignments() has a runtime migration fallback
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
        // by PhosphorZones::LayoutRegistry::loadAssignments() directly from config.json and
        // deleted after application.  Extracting it here would leave dead data
        // in assignments.json that nothing reads.
        const QString modeTrackingKey = ConfigDefaults::modeTrackingGroup();
        if (root.contains(modeTrackingKey)) {
            keysToRemove.append(modeTrackingKey);
        }

        bool assignmentsWritten = true;
        if (!assignRoot.isEmpty()) {
            const QString assignPath = ConfigDefaults::assignmentsFilePath();
            if (!PhosphorConfig::JsonBackend::writeJsonAtomically(assignPath, assignRoot)) {
                qWarning(
                    "ConfigMigration: failed to write assignments to %s — "
                    "aborting migration so the next run can retry",
                    qPrintable(assignPath));
                assignmentsWritten = false;
                allSideEffectsSucceeded = false;
            }
        }
        // Only strip from root if the external file is committed (or there
        // was nothing to extract). ModeTracking is safe to drop unconditionally
        // — it's ephemeral and consumed in-memory only.
        if (assignmentsWritten) {
            for (const QString& key : keysToRemove) {
                root.remove(key);
            }
        } else if (root.contains(modeTrackingKey)) {
            // Still safe to drop ModeTracking even if assignments write failed.
            root.remove(modeTrackingKey);
        }
    }

    // ── Bump version ────────────────────────────────────────────────────────
    // Stamp literal 2, not ConfigSchemaVersion — prevents future version bumps
    // (e.g. to 3) from making this step stamp 3 and skipping a v2→v3 migration.
    //
    // Skip the bump when any side-effect write failed. MigrationRunner
    // detects the unbumped version, aborts the chain with a critical log,
    // and config.json is left untouched so the next startup retries.
    //
    // Note: the in-memory @p root has already been mutated extensively
    // above (v1 groups removed, dot-path hierarchy rebuilt) by the time
    // we get here. On a side-effect failure these mutations are silently
    // discarded by the caller — MigrationRunner::runOnFile sees that the
    // version key didn't advance and skips the disk write entirely, and
    // Store::Store's in-memory migration path likewise compares before vs.
    // after and skips the file rewrite. The on-disk file is therefore left
    // at v1 with the original layout, ready for the next startup to retry.
    if (allSideEffectsSucceeded) {
        root[ConfigKeys::versionKey()] = 2;
    }
}

// ── Schema migration: v2 → v3 ───────────────────────────────────────────────
// Splits the single "this monitor / desktop / activity is disabled in PlasmaZones"
// list into independent per-mode lists, and relocates them out of
// Snapping.Behavior.Display (which historically gated both modes despite the
// snapping-prefixed group name) into a mode-neutral Display group.
//
// Migration semantics: every entry in a v2 disabled list is copied into BOTH
// the snapping and autotile lists in v3. Rationale: v2 didn't distinguish
// modes, so the only safe interpretation of "the user disabled monitor X" is
// "the user wanted X off in PlasmaZones, period" — preserve that intent in
// both modes until the user explicitly re-enables one in the new UI.
//
// Side note: the v2 keys ShowOnAllMonitors and FilterByAspectRatio remain
// in Snapping.Behavior.Display untouched — only the three Disabled* keys move.

void ConfigMigration::migrateV2ToV3(QJsonObject& root)
{
    // Walk the canonical v2 dot-path Snapping.Behavior.Display by splitting
    // the group accessor on '.' — this keeps the migration in lockstep with
    // the schema instead of duplicating segment names as bare literals.
    // The accessor still resolves to the v2 group because that group lives
    // on past v3 (it continues to hold ShowOnAllMonitors and
    // FilterByAspectRatio); only the three Disabled* keys move out.
    const QStringList v2GroupSegments =
        ConfigKeys::snappingBehaviorDisplayGroup().split(QLatin1Char('.'), Qt::SkipEmptyParts);
    Q_ASSERT(v2GroupSegments.size() == 3);
    const QString& snappingSeg = v2GroupSegments[0];
    const QString& behaviorSeg = v2GroupSegments[1];
    const QString& displaySeg = v2GroupSegments[2];

    QJsonObject snapping = root.value(snappingSeg).toObject();
    QJsonObject behavior = snapping.value(behaviorSeg).toObject();
    QJsonObject v2Display = behavior.value(displaySeg).toObject();

    // takeKey: read the v2 string value at @p key, drop the key from @p obj
    // unconditionally if present (even when the value isn't a string — we
    // don't want a hand-edited array or null lingering past the migration
    // and looking like live v2 data on a v3-stamped config), and return
    // the string representation when one is available.
    auto takeKey = [](QJsonObject& obj, const QString& key) -> QString {
        const auto it = obj.find(key);
        if (it == obj.end()) {
            return {};
        }
        const QJsonValue v = it.value();
        QString result;
        if (v.isString()) {
            result = v.toString();
        }
        obj.erase(it);
        return result;
    };

    const QString v2Monitors = takeKey(v2Display, ConfigKeys::v2DisabledMonitorsKey());
    const QString v2Desktops = takeKey(v2Display, ConfigKeys::v2DisabledDesktopsKey());
    const QString v2Activities = takeKey(v2Display, ConfigKeys::v2DisabledActivitiesKey());

    // Write the duplicated lists into the new Display group. Skip empties so
    // a clean v2 config with no disabled entries doesn't grow noise keys.
    QJsonObject v3Display = root.value(ConfigKeys::displayGroup()).toObject();

    auto writeIfNonEmpty = [&v3Display](const QString& key, const QString& value) {
        if (!value.isEmpty()) {
            v3Display[key] = value;
        }
    };

    writeIfNonEmpty(ConfigKeys::snappingDisabledMonitorsKey(), v2Monitors);
    writeIfNonEmpty(ConfigKeys::autotileDisabledMonitorsKey(), v2Monitors);
    writeIfNonEmpty(ConfigKeys::snappingDisabledDesktopsKey(), v2Desktops);
    writeIfNonEmpty(ConfigKeys::autotileDisabledDesktopsKey(), v2Desktops);
    writeIfNonEmpty(ConfigKeys::snappingDisabledActivitiesKey(), v2Activities);
    writeIfNonEmpty(ConfigKeys::autotileDisabledActivitiesKey(), v2Activities);

    // Stitch the trimmed v2 Display object back into Snapping.Behavior, drop
    // the Display sub-object entirely if it became empty (no ShowOnAllMonitors
    // / FilterByAspectRatio either). Same for Snapping.Behavior itself.
    if (v2Display.isEmpty()) {
        behavior.remove(displaySeg);
    } else {
        behavior[displaySeg] = v2Display;
    }
    if (behavior.isEmpty()) {
        snapping.remove(behaviorSeg);
    } else {
        snapping[behaviorSeg] = behavior;
    }
    if (snapping.isEmpty()) {
        root.remove(snappingSeg);
    } else {
        root[snappingSeg] = snapping;
    }

    if (!v3Display.isEmpty()) {
        root[ConfigKeys::displayGroup()] = v3Display;
    }

    // Stamp literal 3 — see migrateV1ToV2 for why this isn't ConfigSchemaVersion.
    root[ConfigKeys::versionKey()] = 3;
}

} // namespace PlasmaZones
