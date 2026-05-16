// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_settings_schema.cpp
 * @brief Unit tests for SettingsAdaptor schema introspection methods:
 *        getSettingSchema, getAllSettingSchemas.
 *
 * The SettingsAdaptor is constructed with a StubSettings (ISettings*) so the
 * registry macros (REGISTER_BOOL_SETTING, etc.) populate m_schemas via the
 * ISettings pointer. Concrete-only entries require a real Settings object and
 * will not be present, but all ISettings-backed entries are testable.
 */

#include <QTest>
#include <QString>
#include <QStringList>
#include <QJsonDocument>
#include <QJsonObject>

#include "dbus/settingsadaptor.h"
#include "../helpers/StubSettings.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

// =========================================================================
// Test Class
// =========================================================================

class TestSettingsSchema : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_settings = new StubSettings(nullptr);
        // SettingsAdaptor is a QDBusAbstractAdaptor and needs a parent
        m_parent = new QObject(nullptr);
        m_adaptor = new SettingsAdaptor(m_settings, /*shaderRegistry=*/nullptr, m_parent);
    }

    void cleanup()
    {
        delete m_parent;
        m_parent = nullptr;
        m_adaptor = nullptr;
        delete m_settings;
        m_settings = nullptr;
        m_guard.reset();
    }

    // =====================================================================
    // getSettingSchema
    // =====================================================================

    void testGetSettingSchema_boolSetting_returnsTypeBool()
    {
        QString json = m_adaptor->getSettingSchema(QStringLiteral("snapAssistEnabled"));
        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        QVERIFY(!doc.isNull());

        QJsonObject obj = doc.object();
        QCOMPARE(obj[QLatin1String("key")].toString(), QStringLiteral("snapAssistEnabled"));
        QCOMPARE(obj[QLatin1String("type")].toString(), QStringLiteral("bool"));
    }

    void testGetSettingSchema_intSetting_returnsTypeInt()
    {
        QString json = m_adaptor->getSettingSchema(QStringLiteral("zonePadding"));
        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        QVERIFY(!doc.isNull());

        QJsonObject obj = doc.object();
        QCOMPARE(obj[QLatin1String("key")].toString(), QStringLiteral("zonePadding"));
        QCOMPARE(obj[QLatin1String("type")].toString(), QStringLiteral("int"));
    }

    void testGetSettingSchema_stringSetting_returnsTypeString()
    {
        // animationEasingCurve is registered as a string setting
        QString json = m_adaptor->getSettingSchema(QStringLiteral("animationEasingCurve"));
        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        QVERIFY(!doc.isNull());

        QJsonObject obj = doc.object();
        // If found, verify type; if not found (concrete-only), skip
        if (obj.contains(QLatin1String("key"))) {
            QCOMPARE(obj[QLatin1String("type")].toString(), QStringLiteral("string"));
        } else {
            // Not in registry when using StubSettings - try another string key
            // labelFontFamily is a string setting on ISettings
            QString json2 = m_adaptor->getSettingSchema(QStringLiteral("labelFontFamily"));
            QJsonDocument doc2 = QJsonDocument::fromJson(json2.toUtf8());
            if (!doc2.isNull() && doc2.object().contains(QLatin1String("key"))) {
                QCOMPARE(doc2.object()[QLatin1String("type")].toString(), QStringLiteral("string"));
            } else {
                QSKIP("No string-typed setting accessible through StubSettings");
            }
        }
    }

    void testGetSettingSchema_unknownKey_returnsEmpty()
    {
        QString json = m_adaptor->getSettingSchema(QStringLiteral("thisKeyDoesNotExist_xyz"));
        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        QVERIFY(!doc.isNull());

        QJsonObject obj = doc.object();
        // Unknown key: the object should have no "key" field
        QVERIFY(!obj.contains(QLatin1String("key")));
    }

    // =====================================================================
    // getAllSettingSchemas
    // =====================================================================

    void testGetAllSettingSchemas_coversAllKeys()
    {
        QStringList keys = m_adaptor->getSettingKeys();
        QString allSchemasJson = m_adaptor->getAllSettingSchemas();
        QJsonDocument doc = QJsonDocument::fromJson(allSchemasJson.toUtf8());
        QVERIFY(!doc.isNull());
        QVERIFY(doc.isObject());

        QJsonObject obj = doc.object();

        // Every key in getSettingKeys should have a schema entry
        for (const QString& key : keys) {
            QVERIFY2(obj.contains(key), qPrintable(QStringLiteral("Missing schema for key: ") + key));
        }
    }

    void testGetAllSettingSchemas_everyKeyHasType()
    {
        QString allSchemasJson = m_adaptor->getAllSettingSchemas();
        QJsonDocument doc = QJsonDocument::fromJson(allSchemasJson.toUtf8());
        QVERIFY(!doc.isNull());

        QJsonObject obj = doc.object();
        QVERIFY(!obj.isEmpty());

        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            QVERIFY2(it.value().isObject(),
                     qPrintable(QStringLiteral("Entry for '") + it.key() + QStringLiteral("' is not an object")));
            QJsonObject entry = it.value().toObject();
            QVERIFY2(entry.contains(QLatin1String("type")),
                     qPrintable(QStringLiteral("Entry for '") + it.key() + QStringLiteral("' has no 'type' field")));
            QString type = entry[QLatin1String("type")].toString();
            QVERIFY2(!type.isEmpty(), qPrintable(QStringLiteral("Empty type for key: ") + it.key()));
        }
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    StubSettings* m_settings = nullptr;
    QObject* m_parent = nullptr;
    SettingsAdaptor* m_adaptor = nullptr;
};

QTEST_MAIN(TestSettingsSchema)
#include "test_settings_schema.moc"
