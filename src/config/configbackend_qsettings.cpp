// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "configbackend_qsettings.h"
#include "configdefaults.h"
#include <QColor>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// KConfig-compatible INI format for QSettings
//
// QSettings::IniFormat wraps string values in quotes and escapes internal
// quotes with backslash. KConfig's INI format does NOT quote values.
// Since KConfigXT (used by KCM/ConfigDefaults) reads the same file,
// we must write in KConfig's format: Key=Value (no quotes, no escaping).
//
// We register a custom QSettings format via registerFormat() that reads
// the standard INI format but writes without quoting.
// ═══════════════════════════════════════════════════════════════════════════════

static bool readKConfigIni(QIODevice& device, QSettings::SettingsMap& map)
{
    QString currentGroup;
    QTextStream in(&device);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';')))
            continue;

        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            currentGroup = line.mid(1, line.length() - 2);
            continue;
        }

        int eqIdx = line.indexOf(QLatin1Char('='));
        if (eqIdx < 0)
            continue;

        QString key = line.left(eqIdx).trimmed();
        QString value = line.mid(eqIdx + 1); // Don't trim — preserve leading spaces in values

        // KConfig uses [$d] and [$i] suffixes on group names — strip for lookup
        // but they won't appear in our config since we don't use those features

        QString fullKey = currentGroup.isEmpty() ? key : (currentGroup + QLatin1Char('/') + key);

        // Store typed QVariants so toBool()/toInt()/toDouble() work correctly.
        // QVariant(QString("false")).toBool() returns true (non-empty string),
        // but QVariant(false).toBool() returns false — we need the latter.
        if (value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0) {
            map.insert(fullKey, true);
        } else if (value.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0) {
            map.insert(fullKey, false);
        } else {
            map.insert(fullKey, value);
        }
    }
    return true;
}

static bool writeKConfigIni(QIODevice& device, const QSettings::SettingsMap& map)
{
    // Group entries by their group prefix
    QMap<QString, QMap<QString, QString>> groups;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        int slashIdx = it.key().indexOf(QLatin1Char('/'));
        if (slashIdx < 0) {
            groups[QString()].insert(it.key(), it.value().toString());
        } else {
            QString group = it.key().left(slashIdx);
            QString key = it.key().mid(slashIdx + 1);
            groups[group].insert(key, it.value().toString());
        }
    }

    QTextStream out(&device);

    // Write ungrouped keys first (rare, but handle it)
    if (groups.contains(QString())) {
        const auto& entries = groups[QString()];
        for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
            out << it.key() << '=' << it.value() << '\n';
        }
        if (!entries.isEmpty())
            out << '\n';
    }

    // Write grouped entries — no quoting, no escaping (KConfig compatible)
    for (auto git = groups.constBegin(); git != groups.constEnd(); ++git) {
        if (git.key().isEmpty())
            continue;
        out << '[' << git.key() << "]\n";
        for (auto it = git.value().constBegin(); it != git.value().constEnd(); ++it) {
            out << it.key() << '=' << it.value() << '\n';
        }
        out << '\n';
    }

    return true;
}

static QSettings::Format kconfigIniFormat()
{
    // Register once, cache the format ID
    static QSettings::Format fmt = QSettings::registerFormat(QStringLiteral("rc"), readKConfigIni, writeKConfigIni);
    return fmt;
}

// ── QSettingsConfigGroup ─────────────────────────────────────────────────────

QSettingsConfigGroup::QSettingsConfigGroup(QSettings* settings, const QString& groupName)
    : m_settings(settings)
    , m_group(groupName)
{
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
    QVariant val = m_settings->value(key);
    if (!val.isValid())
        return defaultValue;
    bool ok = false;
    int result = val.toInt(&ok);
    return ok ? result : defaultValue;
}

bool QSettingsConfigGroup::readBool(const QString& key, bool defaultValue) const
{
    QVariant val = m_settings->value(key);
    if (!val.isValid())
        return defaultValue;
    // QVariant::toBool() treats any non-empty string as true (including "false").
    // Our custom ini reader stores booleans as bool-typed QVariants, but imported
    // or hand-edited configs may store "true"/"false" strings. Handle both.
    if (val.typeId() == QMetaType::Bool)
        return val.toBool();
    const QString s = val.toString().toLower().trimmed();
    if (s == QLatin1String("true") || s == QLatin1String("1"))
        return true;
    if (s == QLatin1String("false") || s == QLatin1String("0"))
        return false;
    return defaultValue;
}

double QSettingsConfigGroup::readDouble(const QString& key, double defaultValue) const
{
    QVariant val = m_settings->value(key);
    if (!val.isValid())
        return defaultValue;
    bool ok = false;
    double result = val.toDouble(&ok);
    return ok ? result : defaultValue;
}

QColor QSettingsConfigGroup::readColor(const QString& key, const QColor& defaultValue) const
{
    QVariant val = m_settings->value(key);
    if (!val.isValid())
        return defaultValue;

    QString str = val.toString();
    if (str.startsWith(QLatin1Char('#'))) {
        QColor c(str);
        return c.isValid() ? c : defaultValue;
    }
    // KConfig comma format: "r,g,b" or "r,g,b,a"
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
    // KConfig-compatible: r,g,b,a (no quotes — handled by custom format)
    m_settings->setValue(
        key, QStringLiteral("%1,%2,%3,%4").arg(value.red()).arg(value.green()).arg(value.blue()).arg(value.alpha()));
}

bool QSettingsConfigGroup::hasKey(const QString& key) const
{
    return m_settings->contains(key);
}

void QSettingsConfigGroup::deleteKey(const QString& key)
{
    m_settings->remove(key);
}

// ── QSettingsConfigBackend ───────────────────────────────────────────────────

QSettingsConfigBackend::QSettingsConfigBackend(const QString& filePath)
    : m_filePath(filePath)
    , m_settings(std::make_unique<QSettings>(filePath, kconfigIniFormat()))
{
}

std::unique_ptr<QSettingsConfigGroup> QSettingsConfigBackend::group(const QString& name)
{
    return std::make_unique<QSettingsConfigGroup>(m_settings.get(), name);
}

void QSettingsConfigBackend::reparseConfiguration()
{
    // Destroy old QSettings BEFORE creating the new one. Qt's QConfFile
    // cache is keyed by file path — if the old object is still alive when
    // the new one is constructed, QConfFile::fromName() returns the cached
    // (stale) entry and the file is never re-read from disk.
    m_settings.reset();
    m_settings = std::make_unique<QSettings>(m_filePath, kconfigIniFormat());
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

// ── Static factory ───────────────────────────────────────────────────────────

std::unique_ptr<QSettingsConfigBackend> QSettingsConfigBackend::createDefault()
{
    return std::make_unique<QSettingsConfigBackend>(ConfigDefaults::configFilePath());
}

} // namespace PlasmaZones
