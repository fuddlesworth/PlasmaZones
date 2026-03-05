// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_settings_validation.cpp
 * @brief Unit tests for Settings validation helpers and exclusion matching
 *
 * Split from test_settings.cpp. Tests cover:
 * 1. readValidatedInt -- out-of-range returns default
 * 2. readValidatedColor -- invalid color returns default
 * 3. parseTriggerListJson -- invalid JSON, max trigger cap
 * 4. Window exclusion matching (case-insensitive substring)
 */

#include <QTest>
#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>
#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>

#include "../../src/config/settings.h"
#include "../../src/config/configdefaults.h"
#include "../../src/core/constants.h"
#include "helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestSettingsValidation : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // =========================================================================
    // readValidatedInt
    // =========================================================================

    /**
     * readValidatedInt must return the default when the stored value is out of range.
     * We test this indirectly by writing an out-of-range value and loading.
     */
    void testReadValidatedInt_outOfRange_returnsDefault()
    {
        IsolatedConfigGuard guard;

        {
            auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
            KConfigGroup zones = config->group(QStringLiteral("Zones"));
            zones.writeEntry(QLatin1String("Padding"), 999); // max is 50
            config->sync();
        }

        Settings settings;
        QCOMPARE(settings.zonePadding(), ConfigDefaults::zonePadding());
    }

    // =========================================================================
    // readValidatedColor
    // =========================================================================

    /**
     * readValidatedColor must return the default when the config has an invalid color.
     * We test by writing gibberish as a color string.
     */
    void testReadValidatedColor_invalidColor_returnsDefault()
    {
        IsolatedConfigGuard guard;

        {
            auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
            KConfigGroup appearance = config->group(QStringLiteral("Appearance"));
            appearance.writeEntry(QLatin1String("HighlightColor"), QStringLiteral("not-a-color"));
            config->sync();
        }

        Settings settings;
        // The color should be valid (either default or system color)
        QVERIFY2(settings.highlightColor().isValid(), "Invalid color in config must fall back to a valid default");
    }

    // =========================================================================
    // parseTriggerListJson
    // =========================================================================

    /**
     * parseTriggerListJson must return nullopt for invalid JSON.
     */
    void testParseTriggerListJson_invalidJson_returnsNullopt()
    {
        IsolatedConfigGuard guard;

        // parseTriggerListJson is a static method, test it through config
        {
            auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
            KConfigGroup activation = config->group(QStringLiteral("Activation"));
            // Write invalid JSON as the trigger list
            activation.writeEntry(QLatin1String("DragActivationTriggers"), QStringLiteral("{broken json["));
            // Provide a legacy fallback modifier
            activation.writeEntry(QLatin1String("DragActivationModifier"), 3); // Alt
            config->sync();
        }

        Settings settings;

        QVariantList triggers = settings.dragActivationTriggers();
        // Should have fallen back to legacy migration
        QVERIFY(!triggers.isEmpty());
        QVariantMap first = triggers.first().toMap();
        QCOMPARE(first.value(QStringLiteral("modifier")).toInt(), 3);
    }

    /**
     * parseTriggerListJson must cap the list at MaxTriggersPerAction (4).
     */
    void testParseTriggerListJson_capsAtMaxTriggers()
    {
        IsolatedConfigGuard guard;

        // Build a JSON array with 6 triggers (above MaxTriggersPerAction=4)
        QJsonArray arr;
        for (int i = 0; i < 6; ++i) {
            QJsonObject obj;
            obj[QLatin1String("modifier")] = i;
            obj[QLatin1String("mouseButton")] = 0;
            arr.append(obj);
        }
        QString json = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));

        {
            auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
            KConfigGroup activation = config->group(QStringLiteral("Activation"));
            activation.writeEntry(QLatin1String("DragActivationTriggers"), json);
            config->sync();
        }

        Settings settings;

        QVERIFY2(settings.dragActivationTriggers().size() <= Settings::MaxTriggersPerAction,
                 "Trigger list must be capped at MaxTriggersPerAction");
    }

    // =========================================================================
    // Window exclusion matching
    // =========================================================================

    /**
     * isWindowExcluded must do case-insensitive substring matching for both
     * app name and window class.
     */
    void testIsWindowExcluded_caseInsensitiveSubstring()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        settings.setExcludedApplications(QStringList{QStringLiteral("Firefox")});
        settings.setExcludedWindowClasses(QStringList{QStringLiteral("konsole")});

        // Case-insensitive substring match on app name
        QVERIFY(settings.isWindowExcluded(QStringLiteral("org.mozilla.firefox"), QString()));
        QVERIFY(settings.isWindowExcluded(QStringLiteral("FIREFOX-ESR"), QString()));

        // Case-insensitive substring match on window class
        QVERIFY(settings.isWindowExcluded(QString(), QStringLiteral("org.kde.Konsole")));
        QVERIFY(settings.isWindowExcluded(QString(), QStringLiteral("KONSOLE")));

        // Non-matching
        QVERIFY(!settings.isWindowExcluded(QStringLiteral("chrome"), QStringLiteral("chromium")));
    }
};

QTEST_MAIN(TestSettingsValidation)
#include "test_settings_validation.moc"
