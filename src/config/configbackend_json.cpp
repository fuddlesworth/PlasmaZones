// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "configbackend_json.h"
#include "configdefaults.h"
#include <QColor>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <functional>
#include <limits>

namespace PlasmaZones {

namespace {
const QLatin1String PerScreenKeyStr(PerScreenKey);

/// Convert a QJsonValue to a QVariant suitable for flat-map consumption.
/// Arrays and objects are serialized to compact JSON strings so that
/// callers using QVariant::toString() get the expected JSON text.
/// Mirrors the logic in JsonConfigGroup::readString().
QVariant jsonValueToFlatVariant(const QJsonValue& val)
{
    if (val.isArray()) {
        return QString::fromUtf8(QJsonDocument(val.toArray()).toJson(QJsonDocument::Compact));
    }
    if (val.isObject()) {
        return QString::fromUtf8(QJsonDocument(val.toObject()).toJson(QJsonDocument::Compact));
    }
    return val.toVariant();
}
} // anonymous namespace

// ── JsonConfigGroup ─────────────────────────────────────────────────────────

JsonConfigGroup::JsonConfigGroup(QJsonObject& root, const QString& groupName, JsonConfigBackend* backend)
    : m_root(root)
    , m_groupName(groupName)
    , m_backend(backend)
{
    // JsonConfigBackend is single-threaded (used only from the main/GUI thread).
    // Warn in release builds to help diagnose lost-write bugs; assert in debug.
    const int count = m_backend->m_activeGroupCount;
    if (count != 0) {
        qWarning(
            "JsonConfigGroup: creating group '%s' while %d other group(s) still active — "
            "concurrent writes to the same backend may lose data",
            qPrintable(groupName), count);
    }
    Q_ASSERT_X(count == 0, "JsonConfigGroup::JsonConfigGroup",
               "Another JsonConfigGroup is still alive — destroy it first");
    ++m_backend->m_activeGroupCount;
}

JsonConfigGroup::~JsonConfigGroup()
{
    --m_backend->m_activeGroupCount;
}

bool JsonConfigGroup::isPerScreenGroup() const
{
    return PlasmaZones::isPerScreenPrefix(m_groupName);
}

JsonConfigGroup::PerScreenPath JsonConfigGroup::parsePerScreenGroup() const
{
    PerScreenPath path;
    const int colonIdx = m_groupName.indexOf(QLatin1Char(':'));
    if (colonIdx < 0) {
        return path;
    }

    const QString prefix = m_groupName.left(colonIdx);
    const QString screenId = m_groupName.mid(colonIdx + 1);
    if (screenId.isEmpty()) {
        return path;
    }
    path.screenId = screenId;
    path.category = PlasmaZones::prefixToCategory(prefix);
    return path;
}

QJsonObject JsonConfigGroup::groupObject() const
{
    if (isPerScreenGroup()) {
        const auto path = parsePerScreenGroup();
        if (path.screenId.isEmpty()) {
            // Malformed per-screen group name (e.g. "ZoneSelector:" with no screen ID).
            // Return empty object so reads get defaults and writes are no-ops.
            return {};
        }
        const QJsonObject perScreen = m_root.value(PerScreenKeyStr).toObject();
        const QJsonObject category = perScreen.value(path.category).toObject();
        return category.value(path.screenId).toObject();
    }
    // Dot-path navigation: "Snapping.Behavior.ZoneSpan" → root["Snapping"]["Behavior"]["ZoneSpan"]
    if (m_groupName.contains(QLatin1Char('.'))) {
        const QStringList segments = m_groupName.split(QLatin1Char('.'));
        QJsonObject current = m_root;
        for (const QString& seg : segments) {
            current = current.value(seg).toObject();
        }
        return current;
    }
    return m_root.value(m_groupName).toObject();
}

void JsonConfigGroup::setGroupObject(const QJsonObject& obj)
{
    if (isPerScreenGroup()) {
        const auto path = parsePerScreenGroup();
        if (path.screenId.isEmpty()) {
            // Malformed per-screen group name (e.g. "ZoneSelector:" with no screen ID).
            // Refuse to write — would insert an empty-string key into the JSON tree.
            qWarning("JsonConfigGroup: ignoring write to malformed per-screen group '%s'", qPrintable(m_groupName));
            return;
        }
        QJsonObject perScreen = m_root.value(PerScreenKeyStr).toObject();
        QJsonObject category = perScreen.value(path.category).toObject();
        category[path.screenId] = obj;
        perScreen[path.category] = category;
        m_root[PerScreenKeyStr] = perScreen;
    } else if (m_groupName.contains(QLatin1Char('.'))) {
        // Dot-path write: rebuild the nested chain from root.
        // e.g. "Snapping.Behavior.ZoneSpan" → root["Snapping"]["Behavior"]["ZoneSpan"] = obj
        const QStringList segments = m_groupName.split(QLatin1Char('.'));

        // Read existing objects at each level (except the leaf)
        QList<QJsonObject> chain;
        chain.reserve(segments.size());
        chain.append(m_root);
        for (int i = 0; i < segments.size() - 1; ++i) {
            chain.append(chain.last().value(segments[i]).toObject());
        }

        // Set the leaf, then write back up the chain
        QJsonObject current = obj;
        for (int i = segments.size() - 1; i >= 1; --i) {
            QJsonObject parent = chain[i];
            parent[segments[i]] = current;
            current = parent;
        }
        m_root[segments[0]] = current;
    } else {
        m_root[m_groupName] = obj;
    }
    m_backend->markDirty();
}

QString JsonConfigGroup::readString(const QString& key, const QString& defaultValue) const
{
    const QJsonObject obj = groupObject();
    if (!obj.contains(key)) {
        return defaultValue;
    }
    const QJsonValue val = obj.value(key);
    // If the value is a JSON array or object, return compact JSON string
    // so parseTriggerListJson() and similar callers work unchanged.
    if (val.isArray()) {
        return QString::fromUtf8(QJsonDocument(val.toArray()).toJson(QJsonDocument::Compact));
    }
    if (val.isObject()) {
        return QString::fromUtf8(QJsonDocument(val.toObject()).toJson(QJsonDocument::Compact));
    }
    return val.toString(defaultValue);
}

int JsonConfigGroup::readInt(const QString& key, int defaultValue) const
{
    const QJsonObject obj = groupObject();
    if (!obj.contains(key)) {
        return defaultValue;
    }
    const QJsonValue val = obj.value(key);
    if (val.isDouble()) {
        const double d = val.toDouble();
        if (d >= static_cast<double>(std::numeric_limits<int>::min())
            && d <= static_cast<double>(std::numeric_limits<int>::max())) {
            const int result = static_cast<int>(d);
            if (d != static_cast<double>(result)) {
                qWarning("JsonConfigGroup: key '%s' contains non-integer double %g, truncating to %d", qPrintable(key),
                         d, result);
            }
            return result;
        }
        qWarning("JsonConfigGroup: key '%s' contains double %g outside int range, using default %d", qPrintable(key), d,
                 defaultValue);
        return defaultValue;
    }
    // Handle string values (from hand-edited configs)
    if (val.isString()) {
        bool ok = false;
        const int result = val.toString().toInt(&ok);
        return ok ? result : defaultValue;
    }
    return defaultValue;
}

bool JsonConfigGroup::readBool(const QString& key, bool defaultValue) const
{
    const QJsonObject obj = groupObject();
    if (!obj.contains(key)) {
        return defaultValue;
    }
    const QJsonValue val = obj.value(key);
    if (val.isBool()) {
        return val.toBool();
    }
    // Handle string values for compatibility
    if (val.isString()) {
        const QString s = val.toString().toLower();
        if (s == QLatin1String("true") || s == QLatin1String("1") || s == QLatin1String("yes")
            || s == QLatin1String("on")) {
            return true;
        }
        if (s == QLatin1String("false") || s == QLatin1String("0") || s == QLatin1String("no")
            || s == QLatin1String("off")) {
            return false;
        }
    }
    // Handle numeric values: only 0 and 1 are valid boolean representations.
    // Other numbers (e.g. 0.5 from a hand-edited config) fall through to default.
    if (val.isDouble()) {
        const double d = val.toDouble();
        if (d == 0.0) {
            return false;
        }
        if (d == 1.0) {
            return true;
        }
    }
    return defaultValue;
}

double JsonConfigGroup::readDouble(const QString& key, double defaultValue) const
{
    const QJsonObject obj = groupObject();
    if (!obj.contains(key)) {
        return defaultValue;
    }
    const QJsonValue val = obj.value(key);
    if (val.isDouble()) {
        return val.toDouble();
    }
    if (val.isString()) {
        bool ok = false;
        const double result = val.toString().toDouble(&ok);
        return ok ? result : defaultValue;
    }
    return defaultValue;
}

QColor JsonConfigGroup::readColor(const QString& key, const QColor& defaultValue) const
{
    const QJsonObject obj = groupObject();
    if (!obj.contains(key)) {
        return defaultValue;
    }
    const QString s = obj.value(key).toString();
    if (s.isEmpty()) {
        return defaultValue;
    }

    // Try hex format: #rrggbb, #aarrggbb (HexArgb, as written by writeColor)
    if (s.startsWith(QLatin1Char('#'))) {
        QColor c(s);
        return c.isValid() ? c : defaultValue;
    }

    // KConfig comma format: r,g,b or r,g,b,a
    const QStringList parts = s.split(QLatin1Char(','));
    if (parts.size() >= 3) {
        bool okR = false, okG = false, okB = false;
        int r = parts[0].trimmed().toInt(&okR);
        int g = parts[1].trimmed().toInt(&okG);
        int b = parts[2].trimmed().toInt(&okB);
        if (okR && okG && okB) {
            int a = 255;
            if (parts.size() >= 4) {
                bool okA = false;
                a = parts[3].trimmed().toInt(&okA);
                if (!okA) {
                    a = 255;
                }
            }
            QColor c(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255), qBound(0, a, 255));
            return c.isValid() ? c : defaultValue;
        }
    }
    return defaultValue;
}

void JsonConfigGroup::writeString(const QString& key, const QString& value)
{
    QJsonObject obj = groupObject();
    // Detect JSON arrays/objects and store as native JSON rather than escaped strings.
    // This keeps trigger lists, per-algorithm settings, etc. as clean JSON in the file.
    // Known limitation: a plain string that happens to be valid JSON (e.g. '["x"]')
    // will be stored as a native array, not a string.  All current config values are
    // either plain text or intentional JSON, so this is safe.  If a future setting
    // needs to store arbitrary user strings that could look like JSON, add a writeRawString()
    // method that bypasses this heuristic.  "[Main Monitor]" fails parsing and stays as-is.
    if (!value.isEmpty() && (value.front() == QLatin1Char('[') || value.front() == QLatin1Char('{'))) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(value.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError) {
            if (doc.isArray()) {
                obj[key] = doc.array();
            } else if (doc.isObject()) {
                obj[key] = doc.object();
            } else {
                obj[key] = value;
            }
            setGroupObject(obj);
            return;
        }
    }
    obj[key] = value;
    setGroupObject(obj);
}

void JsonConfigGroup::writeInt(const QString& key, int value)
{
    QJsonObject obj = groupObject();
    obj[key] = value;
    setGroupObject(obj);
}

void JsonConfigGroup::writeBool(const QString& key, bool value)
{
    QJsonObject obj = groupObject();
    obj[key] = value;
    setGroupObject(obj);
}

void JsonConfigGroup::writeDouble(const QString& key, double value)
{
    QJsonObject obj = groupObject();
    obj[key] = value;
    setGroupObject(obj);
}

void JsonConfigGroup::writeColor(const QString& key, const QColor& value)
{
    QJsonObject obj = groupObject();
    // Store as #aarrggbb hex (Qt's HexArgb format)
    obj[key] = value.name(QColor::HexArgb);
    setGroupObject(obj);
}

bool JsonConfigGroup::hasKey(const QString& key) const
{
    return groupObject().contains(key);
}

void JsonConfigGroup::deleteKey(const QString& key)
{
    QJsonObject obj = groupObject();
    if (obj.contains(key)) {
        obj.remove(key);
        setGroupObject(obj);
    }
}

// ── JsonConfigBackend ───────────────────────────────────────────────────────

JsonConfigBackend::JsonConfigBackend(const QString& filePath)
    : m_filePath(filePath)
{
    loadFromDisk();
}

JsonConfigBackend::~JsonConfigBackend()
{
    Q_ASSERT_X(m_activeGroupCount == 0, "JsonConfigBackend::~JsonConfigBackend",
               "JsonConfigGroup still alive when backend is being destroyed");
}

void JsonConfigBackend::loadFromDisk()
{
    QFile f(m_filePath);
    if (!f.exists()) {
        m_root = QJsonObject();
        return;
    }
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning("JsonConfigBackend: failed to open %s for reading", qPrintable(m_filePath));
        m_root = QJsonObject();
        return;
    }
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning("JsonConfigBackend: JSON parse error in %s at offset %d: %s", qPrintable(m_filePath), err.offset,
                 qPrintable(err.errorString()));
        m_root = QJsonObject();
        return;
    }
    m_root = doc.object();
}

void JsonConfigBackend::markDirty()
{
    m_dirty = true;
}

std::unique_ptr<IConfigGroup> JsonConfigBackend::group(const QString& name)
{
    return std::make_unique<JsonConfigGroup>(m_root, name, this);
}

void JsonConfigBackend::reparseConfiguration()
{
    Q_ASSERT_X(m_activeGroupCount == 0, "JsonConfigBackend::reparseConfiguration",
               "Cannot reparse while JsonConfigGroup instances are alive");
    loadFromDisk();
    m_dirty = false;
}

void JsonConfigBackend::sync()
{
    if (!m_dirty) {
        return;
    }

    // Ensure _version is always present so future migration steps can
    // distinguish format revisions.  Fresh installs that never went through
    // migration would otherwise lack it.
    if (!m_root.contains(QLatin1String("_version"))) {
        m_root[QLatin1String("_version")] = ConfigSchemaVersion;
    }

    if (!writeJsonAtomically(m_filePath, m_root)) {
        return;
    }

    m_dirty = false;
}

bool JsonConfigBackend::writeJsonAtomically(const QString& filePath, const QJsonObject& root)
{
    // Ensure parent directory exists
    QDir dir = QFileInfo(filePath).absoluteDir();
    if (!dir.exists() && !dir.mkpath(QLatin1String("."))) {
        qWarning("JsonConfigBackend: failed to create directory %s", qPrintable(dir.absolutePath()));
        return false;
    }

    // Atomic write using QSaveFile (writes to temp, then renames on commit)
    QSaveFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning("JsonConfigBackend: failed to open %s for writing: %s", qPrintable(filePath),
                 qPrintable(f.errorString()));
        return false;
    }

    QJsonDocument doc(root);
    f.write(doc.toJson(QJsonDocument::Indented));

    if (!f.commit()) {
        qWarning("JsonConfigBackend: failed to commit write to %s: %s", qPrintable(filePath),
                 qPrintable(f.errorString()));
        return false;
    }

    return true;
}

void JsonConfigBackend::deleteGroup(const QString& name)
{
    if (PlasmaZones::isPerScreenPrefix(name)) {
        const int colonIdx = name.indexOf(QLatin1Char(':'));
        Q_ASSERT(colonIdx >= 0); // guaranteed by isPerScreenPrefix
        const QString prefix = name.left(colonIdx);
        const QString screenId = name.mid(colonIdx + 1);
        if (screenId.isEmpty()) {
            qWarning("JsonConfigBackend: ignoring deleteGroup for malformed per-screen name '%s'", qPrintable(name));
            return;
        }
        const QString category = PlasmaZones::prefixToCategory(prefix);

        QJsonObject perScreen = m_root.value(PerScreenKeyStr).toObject();
        QJsonObject cat = perScreen.value(category).toObject();
        cat.remove(screenId);
        if (cat.isEmpty()) {
            perScreen.remove(category);
        } else {
            perScreen[category] = cat;
        }
        if (perScreen.isEmpty()) {
            m_root.remove(PerScreenKeyStr);
        } else {
            m_root[PerScreenKeyStr] = perScreen;
        }
    } else if (name.contains(QLatin1Char('.'))) {
        // Dot-path delete: remove the leaf and clean up empty parents.
        // e.g. "Snapping.Behavior.ZoneSpan" removes ZoneSpan from Behavior,
        // then removes Behavior if empty, then Snapping if empty.
        const QStringList segments = name.split(QLatin1Char('.'));

        // Read existing objects at each level (chain[0] = root's child, etc.)
        QList<QJsonObject> chain;
        chain.reserve(segments.size());
        for (int i = 0; i < segments.size(); ++i) {
            const QJsonObject& parent = (i == 0) ? m_root : chain[i - 1];
            chain.append(parent.value(segments[i]).toObject());
        }

        // Remove the leaf key from its parent object.
        // For path "A.B.C", segments=["A","B","C"], chain=[root["A"], A["B"], B["C"]]
        // We want to remove "C" from chain[1] (the "B" object).
        // The leaf itself (chain.last()) is what gets deleted.
        // Its parent is chain[size-2] for size>=2, or m_root for size==1.
        if (segments.size() == 1) {
            m_root.remove(segments[0]);
        } else {
            // Remove leaf from its parent, then write back up
            int parentIdx = segments.size() - 2;
            QJsonObject parentObj = chain[parentIdx];
            parentObj.remove(segments.last());

            // Write back up the chain, pruning empty objects
            for (int i = parentIdx; i >= 1; --i) {
                QJsonObject grandparent = chain[i - 1];
                if (parentObj.isEmpty()) {
                    grandparent.remove(segments[i]);
                } else {
                    grandparent[segments[i]] = parentObj;
                }
                parentObj = grandparent;
            }
            // parentObj is now the updated top-level object for segments[0]
            if (parentObj.isEmpty()) {
                m_root.remove(segments[0]);
            } else {
                m_root[segments[0]] = parentObj;
            }
        }
    } else {
        m_root.remove(name);
    }
    markDirty();
}

QString JsonConfigBackend::readRootString(const QString& key, const QString& defaultValue) const
{
    // Root-level keys live under the General group (matching QSettings INI behavior)
    const QString generalKey = ConfigDefaults::generalGroup();
    const QJsonObject general = m_root.value(generalKey).toObject();
    if (general.contains(key)) {
        return general.value(key).toString(defaultValue);
    }
    return defaultValue;
}

void JsonConfigBackend::writeRootString(const QString& key, const QString& value)
{
    const QString generalKey = ConfigDefaults::generalGroup();
    QJsonObject general = m_root.value(generalKey).toObject();
    general[key] = value;
    m_root[generalKey] = general;
    markDirty();
}

void JsonConfigBackend::removeRootKey(const QString& key)
{
    const QString generalKey = ConfigDefaults::generalGroup();
    QJsonObject general = m_root.value(generalKey).toObject();
    if (general.contains(key)) {
        general.remove(key);
        if (general.isEmpty()) {
            m_root.remove(generalKey);
        } else {
            m_root[generalKey] = general;
        }
        markDirty();
    }
}

QStringList JsonConfigBackend::groupList() const
{
    QStringList groups;

    // Recursively enumerate nested object groups with dot-joined names.
    // e.g. {"Snapping": {"Behavior": {"ZoneSpan": {...}}}} produces:
    //   "Snapping", "Snapping.Behavior", "Snapping.Behavior.ZoneSpan"
    // Leaf groups (containing only scalar values) and intermediate groups
    // (containing both scalars and sub-objects) are both included.
    std::function<void(const QJsonObject&, const QString&)> enumerate = [&](const QJsonObject& obj,
                                                                            const QString& prefix) {
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            if (it.key() == PerScreenKeyStr || it.key() == QLatin1String("_version")) {
                continue;
            }
            if (it.value().isObject()) {
                const QString path = prefix.isEmpty() ? it.key() : (prefix + QLatin1Char('.') + it.key());
                groups.append(path);
                enumerate(it.value().toObject(), path);
            }
        }
    };
    enumerate(m_root, QString());

    // Flatten per-screen groups into Prefix:ScreenId format
    const QJsonObject perScreen = m_root.value(PerScreenKeyStr).toObject();
    for (auto catIt = perScreen.constBegin(); catIt != perScreen.constEnd(); ++catIt) {
        const QJsonObject category = catIt.value().toObject();
        const QString prefix = PlasmaZones::categoryToPrefix(catIt.key());
        for (auto screenIt = category.constBegin(); screenIt != category.constEnd(); ++screenIt) {
            groups.append(prefix + QLatin1Char(':') + screenIt.key());
        }
    }

    return groups;
}

QMap<QString, QVariant> readJsonConfigFromDisk(const QString& filePath)
{
    QMap<QString, QVariant> map;

    QFile f(filePath.isEmpty() ? ConfigDefaults::configFilePath() : filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return map;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        return map;
    }

    const QJsonObject root = doc.object();

    // Flatten nested JSON into "Group/Key" format
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        if (it.key() == QLatin1String("_version")) {
            continue;
        }
        if (it.value().isObject()) {
            if (it.key() == PerScreenKeyStr) {
                // Flatten per-screen: PerScreen/Category/ScreenId/Key → Category:ScreenId/Key
                const QJsonObject perScreen = it.value().toObject();
                for (auto catIt = perScreen.constBegin(); catIt != perScreen.constEnd(); ++catIt) {
                    const QString prefix = PlasmaZones::categoryToPrefix(catIt.key());
                    const QJsonObject category = catIt.value().toObject();
                    for (auto sIt = category.constBegin(); sIt != category.constEnd(); ++sIt) {
                        const QJsonObject screenObj = sIt.value().toObject();
                        const QString groupKey = prefix + QLatin1Char(':') + sIt.key();
                        for (auto kIt = screenObj.constBegin(); kIt != screenObj.constEnd(); ++kIt) {
                            map.insert(groupKey + QLatin1Char('/') + kIt.key(), jsonValueToFlatVariant(kIt.value()));
                        }
                    }
                }
            } else {
                // Regular group: Group/Key
                const QJsonObject groupObj = it.value().toObject();
                for (auto kIt = groupObj.constBegin(); kIt != groupObj.constEnd(); ++kIt) {
                    map.insert(it.key() + QLatin1Char('/') + kIt.key(), jsonValueToFlatVariant(kIt.value()));
                }
            }
        } else {
            // Root-level non-object key
            map.insert(it.key(), jsonValueToFlatVariant(it.value()));
        }
    }

    return map;
}

// ── Default backend factory ──────────────────────────────────────────────

std::unique_ptr<IConfigBackend> createDefaultConfigBackend()
{
    return std::make_unique<JsonConfigBackend>(ConfigDefaults::configFilePath());
}

} // namespace PlasmaZones
