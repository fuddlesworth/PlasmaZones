// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorConfig/QSettingsBackend.h>

#include <QColor>
#include <QDebug>
#include <QFile>
#include <QHash>
#include <QMap>
#include <QMutex>
#include <QTextStream>

namespace PhosphorConfig {

// ═══════════════════════════════════════════════════════════════════════════════
// KConfig-compatible INI format
//
// QSettings::IniFormat wraps string values in quotes and escapes internal
// quotes with backslash. KConfig's INI format does NOT quote values. Since
// config files may be shared with KConfig-aware consumers (e.g. KDE's KCM
// subsystem), we register a custom QSettings format that reads standard INI
// but writes plain Key=Value pairs.
// ═══════════════════════════════════════════════════════════════════════════════

static bool readKConfigIni(QIODevice& device, QSettings::SettingsMap& map)
{
    QString currentGroup;
    QTextStream in(&device);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';'))) {
            continue;
        }

        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            currentGroup = line.mid(1, line.length() - 2);
            continue;
        }

        int eqIdx = line.indexOf(QLatin1Char('='));
        if (eqIdx < 0) {
            continue;
        }

        QString key = line.left(eqIdx).trimmed();
        QString value = line.mid(eqIdx + 1); // Preserve leading spaces in values

        QString fullKey = currentGroup.isEmpty() ? key : (currentGroup + QLatin1Char('/') + key);

        // Store typed booleans so toBool() works without re-parsing the string.
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

    if (groups.contains(QString())) {
        const auto& entries = groups[QString()];
        for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
            out << it.key() << '=' << it.value() << '\n';
        }
        if (!entries.isEmpty()) {
            out << '\n';
        }
    }

    for (auto git = groups.constBegin(); git != groups.constEnd(); ++git) {
        if (git.key().isEmpty()) {
            continue;
        }
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
    static QSettings::Format fmt = QSettings::registerFormat(QStringLiteral("rc"), readKConfigIni, writeKConfigIni);
    return fmt;
}

// ─── QSettingsGroup ─────────────────────────────────────────────────────────

QSettingsGroup::QSettingsGroup(QSettings* settings, QString groupName, QSettingsBackend* backend)
    : m_settings(settings)
    , m_group(std::move(groupName))
    , m_backend(backend)
{
    Q_ASSERT_X(m_settings->group().isEmpty(), "PhosphorConfig::QSettingsGroup",
               "Another group is still active — destroy it before creating a new one");
    m_settings->beginGroup(m_group);
    if (m_backend) {
        const int count = m_backend->activeGroupCount();
        if (count != 0) {
            qWarning(
                "PhosphorConfig::QSettingsGroup: creating group '%s' while %d other group(s) still active — "
                "concurrent writes to the same backend may lose data",
                qPrintable(m_group), count);
        }
        m_backend->incActiveGroupCount();
    }
}

QSettingsGroup::~QSettingsGroup()
{
    m_settings->endGroup();
    if (m_backend) {
        m_backend->decActiveGroupCount();
    }
}

QString QSettingsGroup::readString(const QString& key, const QString& defaultValue) const
{
    return m_settings->value(key, defaultValue).toString();
}

int QSettingsGroup::readInt(const QString& key, int defaultValue) const
{
    QVariant val = m_settings->value(key);
    if (!val.isValid()) {
        return defaultValue;
    }
    bool ok = false;
    int result = val.toInt(&ok);
    return ok ? result : defaultValue;
}

bool QSettingsGroup::readBool(const QString& key, bool defaultValue) const
{
    QVariant val = m_settings->value(key);
    if (!val.isValid()) {
        return defaultValue;
    }
    if (val.typeId() == QMetaType::Bool) {
        return val.toBool();
    }
    const QString s = val.toString().toLower().trimmed();
    if (s == QLatin1String("true") || s == QLatin1String("1") || s == QLatin1String("yes")
        || s == QLatin1String("on")) {
        return true;
    }
    if (s == QLatin1String("false") || s == QLatin1String("0") || s == QLatin1String("no")
        || s == QLatin1String("off")) {
        return false;
    }
    return defaultValue;
}

double QSettingsGroup::readDouble(const QString& key, double defaultValue) const
{
    QVariant val = m_settings->value(key);
    if (!val.isValid()) {
        return defaultValue;
    }
    bool ok = false;
    double result = val.toDouble(&ok);
    return ok ? result : defaultValue;
}

QColor QSettingsGroup::readColor(const QString& key, const QColor& defaultValue) const
{
    QVariant val = m_settings->value(key);
    if (!val.isValid()) {
        return defaultValue;
    }

    QString str = val.toString();
    if (str.startsWith(QLatin1Char('#'))) {
        QColor c(str);
        return c.isValid() ? c : defaultValue;
    }

    QStringList parts = str.split(QLatin1Char(','));
    if (parts.size() >= 3) {
        bool okR = false, okG = false, okB = false;
        int r = qBound(0, parts[0].trimmed().toInt(&okR), 255);
        int g = qBound(0, parts[1].trimmed().toInt(&okG), 255);
        int b = qBound(0, parts[2].trimmed().toInt(&okB), 255);
        if (!okR || !okG || !okB) {
            return defaultValue;
        }
        int a = 255;
        if (parts.size() >= 4) {
            bool okA = false;
            a = qBound(0, parts[3].trimmed().toInt(&okA), 255);
            if (!okA) {
                return defaultValue;
            }
        }
        return QColor(r, g, b, a);
    }
    return defaultValue;
}

void QSettingsGroup::writeString(const QString& key, const QString& value)
{
    m_settings->setValue(key, value);
}

void QSettingsGroup::writeInt(const QString& key, int value)
{
    m_settings->setValue(key, value);
}

void QSettingsGroup::writeBool(const QString& key, bool value)
{
    m_settings->setValue(key, value);
}

void QSettingsGroup::writeDouble(const QString& key, double value)
{
    m_settings->setValue(key, value);
}

void QSettingsGroup::writeColor(const QString& key, const QColor& value)
{
    m_settings->setValue(
        key, QStringLiteral("%1,%2,%3,%4").arg(value.red()).arg(value.green()).arg(value.blue()).arg(value.alpha()));
}

bool QSettingsGroup::hasKey(const QString& key) const
{
    return m_settings->contains(key);
}

void QSettingsGroup::deleteKey(const QString& key)
{
    m_settings->remove(key);
}

// ─── Instance tracking (debug builds) ───────────────────────────────────────
// Warns when >1 backend is created for the same config file. The design
// relies on exactly one QSettings per file so Qt's QConfFile cache observes
// consistent state; a second instance silently reintroduces cache bugs.

#ifndef NDEBUG
using InstanceCountMap = QHash<QString, int>;
Q_GLOBAL_STATIC(QMutex, s_instanceMutex)
Q_GLOBAL_STATIC(InstanceCountMap, s_instanceCounts)
#endif

// ─── QSettingsBackend ───────────────────────────────────────────────────────

QSettingsBackend::QSettingsBackend(const QString& filePath)
    : m_filePath(filePath)
    , m_settings(std::make_unique<QSettings>(filePath, kconfigIniFormat()))
{
#ifndef NDEBUG
    QMutexLocker lock(s_instanceMutex());
    int& count = (*s_instanceCounts())[m_filePath];
    ++count;
    if (count > 1) {
        qWarning("PhosphorConfig::QSettingsBackend: %d instances for \"%s\" — single-instance invariant violated",
                 count, qPrintable(m_filePath));
    }
#endif
}

QSettingsBackend::~QSettingsBackend()
{
#ifndef NDEBUG
    QMutexLocker lock(s_instanceMutex());
    int& count = (*s_instanceCounts())[m_filePath];
    --count;
    if (count <= 0) {
        s_instanceCounts()->remove(m_filePath);
    }
#endif
}

std::unique_ptr<IGroup> QSettingsBackend::group(const QString& name)
{
    return std::make_unique<QSettingsGroup>(m_settings.get(), name, this);
}

void QSettingsBackend::reparseConfiguration()
{
    Q_ASSERT_X(m_activeGroupCount == 0, "PhosphorConfig::QSettingsBackend::reparseConfiguration",
               "Cannot reparse while QSettingsGroup instances are alive");
    m_settings.reset();
    m_settings = std::make_unique<QSettings>(m_filePath, kconfigIniFormat());
}

void QSettingsBackend::sync()
{
    m_settings->sync();
    if (m_settings->status() != QSettings::NoError) {
        qWarning("PhosphorConfig::QSettingsBackend: sync() failed (status=%d) for %s",
                 static_cast<int>(m_settings->status()), qPrintable(m_filePath));
    }
}

void QSettingsBackend::deleteGroup(const QString& name)
{
    m_settings->remove(name);
}

QString QSettingsBackend::readRootString(const QString& key, const QString& defaultValue) const
{
    return m_settings->value(key, defaultValue).toString();
}

void QSettingsBackend::writeRootString(const QString& key, const QString& value)
{
    m_settings->setValue(key, value);
}

void QSettingsBackend::removeRootKey(const QString& key)
{
    m_settings->remove(key);
}

QStringList QSettingsBackend::groupList() const
{
    return m_settings->childGroups();
}

void QSettingsBackend::incActiveGroupCount()
{
    ++m_activeGroupCount;
}

void QSettingsBackend::decActiveGroupCount()
{
    --m_activeGroupCount;
}

int QSettingsBackend::activeGroupCount() const
{
    return m_activeGroupCount;
}

QMap<QString, QVariant> QSettingsBackend::readConfigFromDisk(const QString& filePath)
{
    QSettings::SettingsMap map;
    QFile f(filePath);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        readKConfigIni(f, map);
    }
    return map;
}

} // namespace PhosphorConfig
