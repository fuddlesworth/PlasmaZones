// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "configbackend_qsettings.h"
#include <QColor>
#include <QDir>
#include <QStandardPaths>

namespace PlasmaZones {

// ── QSettingsConfigGroup ─────────────────────────────────────────────────────

QSettingsConfigGroup::QSettingsConfigGroup(QSettings* settings, const QString& groupName)
    : m_settings(settings)
    , m_group(groupName)
{
    // QSettings groups are a stack — only one ConfigGroup should be active at a time.
    // Nested beginGroup would silently read from "OldGroup/NewGroup" instead of "NewGroup".
    Q_ASSERT_X(m_settings->group().isEmpty(), "QSettingsConfigGroup",
               "Another ConfigGroup is still active — destroy it before creating a new one");
    m_settings->beginGroup(m_group);
}

QSettingsConfigGroup::~QSettingsConfigGroup()
{
    m_settings->endGroup();
}

QString QSettingsConfigGroup::readString(const QString& key, const QString& defaultValue) const
{
    return m_settings->value(key, defaultValue).toString();
}

int QSettingsConfigGroup::readInt(const QString& key, int defaultValue) const
{
    return m_settings->value(key, defaultValue).toInt();
}

bool QSettingsConfigGroup::readBool(const QString& key, bool defaultValue) const
{
    return m_settings->value(key, defaultValue).toBool();
}

double QSettingsConfigGroup::readDouble(const QString& key, double defaultValue) const
{
    return m_settings->value(key, defaultValue).toDouble();
}

QColor QSettingsConfigGroup::readColor(const QString& key, const QColor& defaultValue) const
{
    QVariant val = m_settings->value(key);
    if (!val.isValid())
        return defaultValue;

    // KConfig stores colors as "r,g,b,a" or "#AARRGGBB"
    QString str = val.toString();
    if (str.startsWith(QLatin1Char('#'))) {
        QColor c(str);
        return c.isValid() ? c : defaultValue;
    }
    // Try KConfig comma format: "r,g,b" or "r,g,b,a"
    QStringList parts = str.split(QLatin1Char(','));
    if (parts.size() >= 3) {
        int r = parts[0].trimmed().toInt();
        int g = parts[1].trimmed().toInt();
        int b = parts[2].trimmed().toInt();
        int a = (parts.size() >= 4) ? parts[3].trimmed().toInt() : 255;
        return QColor(r, g, b, a);
    }
    return defaultValue;
}

void QSettingsConfigGroup::writeString(const QString& key, const QString& value)
{
    m_settings->setValue(key, value);
}

void QSettingsConfigGroup::writeInt(const QString& key, int value)
{
    m_settings->setValue(key, value);
}

void QSettingsConfigGroup::writeBool(const QString& key, bool value)
{
    m_settings->setValue(key, value);
}

void QSettingsConfigGroup::writeDouble(const QString& key, double value)
{
    m_settings->setValue(key, value);
}

void QSettingsConfigGroup::writeColor(const QString& key, const QColor& value)
{
    // Write in KConfig-compatible format: "r,g,b,a"
    m_settings->setValue(
        key, QStringLiteral("%1,%2,%3,%4").arg(value.red()).arg(value.green()).arg(value.blue()).arg(value.alpha()));
}

bool QSettingsConfigGroup::hasKey(const QString& key) const
{
    return m_settings->contains(key);
}

// ── QSettingsConfigBackend ───────────────────────────────────────────────────

QSettingsConfigBackend::QSettingsConfigBackend(const QString& filePath)
    : m_filePath(filePath)
    , m_settings(std::make_unique<QSettings>(filePath, QSettings::IniFormat))
{
}

std::unique_ptr<ConfigGroup> QSettingsConfigBackend::group(const QString& name)
{
    return std::make_unique<QSettingsConfigGroup>(m_settings.get(), name);
}

void QSettingsConfigBackend::reparseConfiguration()
{
    // QSettings caches values; re-create to force re-read from disk
    m_settings = std::make_unique<QSettings>(m_filePath, QSettings::IniFormat);
}

void QSettingsConfigBackend::sync()
{
    m_settings->sync();
    if (m_settings->status() != QSettings::NoError) {
        qWarning("QSettingsConfigBackend: sync() failed (status=%d) for %s", static_cast<int>(m_settings->status()),
                 qPrintable(m_filePath));
    }
}

void QSettingsConfigBackend::deleteGroup(const QString& name)
{
    m_settings->remove(name);
}

QStringList QSettingsConfigBackend::groupList() const
{
    return m_settings->childGroups();
}

// ── Factory ──────────────────────────────────────────────────────────────────

std::unique_ptr<IConfigBackend> createDefaultConfigBackend()
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (configDir.isEmpty()) {
        configDir = QDir::homePath() + QStringLiteral("/.config");
    }
    return std::make_unique<QSettingsConfigBackend>(configDir + QStringLiteral("/plasmazonesrc"));
}

} // namespace PlasmaZones
