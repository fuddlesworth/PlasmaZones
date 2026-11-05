// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_shaderpresetbridge.cpp
 * @brief Preset CRUD: the ShaderPresetBridge behind every editor's
 *        `presetBridge`.
 *
 * The library half of the feature (parse, resolve, the loader and the store)
 * is covered by libs/phosphor-shaders/tests/test_shaderpresets.cpp. This file
 * covers the WRITE half, which is this class and nothing else.
 *
 * Pinned behaviour:
 *   - save / list / update / rename / delete round-trip through real files
 *   - the id is a minted UUID with no braces, and a rename does NOT move it,
 *     which is what keeps assignments pointing at a renamed preset working
 *   - a pack-declared preset is offered, refused for every write, and can be
 *     duplicated into an editable one
 *   - the refusals: empty pack, unusable name, a preset deleted underneath us,
 *     and a record whose file is already gone (which is a SUCCESSFUL delete)
 *   - the parameter map is bounded on the way to disk
 *   - effectiveParams is the registry's own merge, clamp included
 *   - presetsChanged is relayed for this bridge's family only
 *
 * Every case runs against a QTemporaryDir preset root, so nothing here touches
 * the developer's own presets.
 */

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>
#include <QMetaObject>
#include <QRegularExpression>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include <PhosphorShaders/ShaderPreset.h>
#include <PhosphorShaders/ShaderPresetRegistry.h>
#include <PhosphorShaders/ShaderPresetStore.h>

#include "settings/stores/shaderpresetbridge.h"

using namespace PlasmaZones;
using namespace PhosphorShaders;

namespace {
constexpr ShaderFamily kFamily = ShaderFamily::Animation;
const QString kPack = QStringLiteral("dissolve");

bool writeFile(const QString& path, const QString& body)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    return file.write(body.toUtf8()) == body.toUtf8().size();
}

/// The row for @p presetId in @p rows, or an empty map when it is not listed.
QVariantMap rowFor(const QVariantList& rows, const QString& presetId)
{
    for (const QVariant& row : rows) {
        const QVariantMap map = row.toMap();
        if (map.value(QStringLiteral("id")).toString() == presetId) {
            return map;
        }
    }
    return {};
}
} // namespace

class TestShaderPresetBridge : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void savesAndListsAPreset();
    void theIdIsAUuidWithoutBracesAndIsTheFileStem();
    void refusesASaveWithNoPack();
    void refusesAnUnusableName();
    void nameRulesRejectControlAndFormatCharacters();
    void updateReplacesTheParameters();
    void renameKeepsTheIdAndTheFile();
    void renameWritesBackToTheFileTheRecordCameFrom();
    void anUnusableDeclaredIdFallsBackToTheFileStem();
    void aSourcePathOutsideTheDirectoryFallsBackToTheIdPath();
    void deleteRemovesTheFile();
    void deleteOfAnAlreadyGoneFileSucceeds();
    void writesRefuseAPresetDeletedUnderneathUs();
    void packDeclaredPresetsAreReadOnlyAndRefuseEveryWrite();
    void duplicateMakesAnEditableCopyOfAPackPreset();
    void theParameterMapIsBoundedOnTheWayToDisk();
    void effectiveParamsIsTheRegistryMerge();
    void presetsChangedIsRelayedForThisFamilyOnly();
    void everyPresetBridgeCallFromTheSettingsQmlIsReachable();

private:
    /// Parse the preset file @p presetId was written to.
    QJsonObject readPresetFile(const QString& presetId) const;
    /// Parse the preset file at @p path, for a file not named after its id.
    static QJsonObject readPresetFileNamed(const QString& path);

    std::unique_ptr<QTemporaryDir> m_root;
    std::unique_ptr<ShaderPresetStore> m_store;
    std::unique_ptr<ShaderPresetBridge> m_bridge;
};

void TestShaderPresetBridge::init()
{
    m_root = std::make_unique<QTemporaryDir>();
    QVERIFY(m_root->isValid());
    m_store = std::make_unique<ShaderPresetStore>();
    m_store->load(m_root->path());
    m_bridge = std::make_unique<ShaderPresetBridge>(*m_store, kFamily);
}

void TestShaderPresetBridge::cleanup()
{
    // The bridge holds a connection into the store's registry, so it goes first.
    m_bridge.reset();
    m_store.reset();
    m_root.reset();
}

QJsonObject TestShaderPresetBridge::readPresetFileNamed(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

QJsonObject TestShaderPresetBridge::readPresetFile(const QString& presetId) const
{
    const QString path = m_bridge->presetDirectory() + QLatin1Char('/') + presetId + QStringLiteral(".json");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

void TestShaderPresetBridge::savesAndListsAPreset()
{
    const QVariantMap params{{QStringLiteral("speed"), 1.4}, {QStringLiteral("glow"), 0.25}};
    const QString id = m_bridge->savePreset(kPack, QStringLiteral("Neon Pulse"), params);
    QVERIFY(!id.isEmpty());

    // Listed, and the write is visible WITHOUT waiting for the watcher: commit
    // rescans synchronously because the caller is about to select what it saved.
    const QVariantMap row = rowFor(m_bridge->presetsFor(kPack), id);
    QCOMPARE(row.value(QStringLiteral("name")).toString(), QStringLiteral("Neon Pulse"));
    QVERIFY(!row.value(QStringLiteral("readOnly")).toBool());

    QCOMPARE(m_bridge->presetParams(kPack, id), params);
    // And it is on disk, under this family's directory.
    QVERIFY(QFileInfo::exists(m_bridge->presetDirectory() + QLatin1Char('/') + id + QStringLiteral(".json")));
}

void TestShaderPresetBridge::theIdIsAUuidWithoutBracesAndIsTheFileStem()
{
    const QString id = m_bridge->savePreset(kPack, QStringLiteral("Soft"), {{QStringLiteral("speed"), 1.0}});
    QVERIFY(!id.isEmpty());
    // WithoutBraces, per the convention for an id that is also a path
    // component — and it makes the stem equal the declared id, so the loader's
    // stem fallback and the `id` field cannot disagree.
    QVERIFY(!id.contains(QLatin1Char('{')));
    QVERIFY(!id.contains(QLatin1Char('}')));
    QCOMPARE(QUuid::fromString(id).isNull(), false);
    QCOMPARE(readPresetFile(id).value(QLatin1String("id")).toString(), id);
}

void TestShaderPresetBridge::refusesASaveWithNoPack()
{
    QSignalSpy failed(m_bridge.get(), &ShaderPresetBridge::presetWriteFailed);
    QVERIFY(m_bridge->savePreset(QString(), QStringLiteral("Soft"), {{QStringLiteral("speed"), 1.0}}).isEmpty());
    QCOMPARE(failed.count(), 1);
    QVERIFY(!failed.at(0).at(0).toString().isEmpty());
}

void TestShaderPresetBridge::refusesAnUnusableName()
{
    QSignalSpy failed(m_bridge.get(), &ShaderPresetBridge::presetWriteFailed);
    QVERIFY(m_bridge->savePreset(kPack, QStringLiteral("   "), {{QStringLiteral("speed"), 1.0}}).isEmpty());
    QCOMPARE(failed.count(), 1);
    QVERIFY(m_bridge->presetsFor(kPack).isEmpty());
}

void TestShaderPresetBridge::nameRulesRejectControlAndFormatCharacters()
{
    // The predicate a rename dialog gates its Ok button on, because an
    // AcceptRole button dismisses the dialog before the refusal is known.
    QVERIFY(m_bridge->canUsePresetName(QStringLiteral("Soft")));
    QVERIFY(m_bridge->canUsePresetName(QStringLiteral("  Soft  "))); // trimmed
    QVERIFY(!m_bridge->canUsePresetName(QString()));
    QVERIFY(!m_bridge->canUsePresetName(QStringLiteral("\t ")));
    QVERIFY(!m_bridge->canUsePresetName(QStringLiteral("Two\nLines")));
    // Built rather than spelled: a literal RIGHT-TO-LEFT OVERRIDE in the source
    // would reverse the rest of this line for anyone reading the file, which is
    // the very thing the predicate exists to keep out of a combo row.
    QVERIFY(!m_bridge->canUsePresetName(QStringLiteral("Soft") + QChar(0x202E) + QStringLiteral("flip")));
    QVERIFY(!m_bridge->canUsePresetName(QString(200, QLatin1Char('x'))));

    // Names are NOT unique: the id is the identity, and two presets called
    // "Soft" are a legitimate intermediate state.
    const QString first = m_bridge->savePreset(kPack, QStringLiteral("Soft"), {{QStringLiteral("speed"), 1.0}});
    const QString second = m_bridge->savePreset(kPack, QStringLiteral("Soft"), {{QStringLiteral("speed"), 2.0}});
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());
    QVERIFY(first != second);
}

void TestShaderPresetBridge::updateReplacesTheParameters()
{
    const QString id = m_bridge->savePreset(kPack, QStringLiteral("Soft"),
                                            {{QStringLiteral("speed"), 1.0}, {QStringLiteral("glow"), 0.2}});
    QVERIFY(!id.isEmpty());

    // REPLACES rather than merges: the editor hands over the whole live map, so
    // a parameter the user cleared must not survive from the previous write.
    QVERIFY(m_bridge->updatePreset(id, {{QStringLiteral("speed"), 3.0}}));
    const QVariantMap params = m_bridge->presetParams(kPack, id);
    QCOMPARE(params.size(), 1);
    QCOMPARE(params.value(QStringLiteral("speed")).toDouble(), 3.0);
    // The name is untouched, and so is the id, so every assignment still resolves.
    QCOMPARE(rowFor(m_bridge->presetsFor(kPack), id).value(QStringLiteral("name")).toString(), QStringLiteral("Soft"));
}

void TestShaderPresetBridge::renameKeepsTheIdAndTheFile()
{
    const QString id = m_bridge->savePreset(kPack, QStringLiteral("Soft"), {{QStringLiteral("speed"), 1.0}});
    QVERIFY(!id.isEmpty());
    const QString path = m_bridge->presetDirectory() + QLatin1Char('/') + id + QStringLiteral(".json");

    QVERIFY(m_bridge->renamePreset(id, QStringLiteral("  Softer  ")));
    // Trimmed, and the identity did not move: same id, same file, same params.
    QCOMPARE(rowFor(m_bridge->presetsFor(kPack), id).value(QStringLiteral("name")).toString(),
             QStringLiteral("Softer"));
    QVERIFY(QFileInfo::exists(path));
    QCOMPARE(m_bridge->presetParams(kPack, id).value(QStringLiteral("speed")).toDouble(), 1.0);
    QCOMPARE(m_bridge->presetsFor(kPack).size(), 1);

    QSignalSpy failed(m_bridge.get(), &ShaderPresetBridge::presetWriteFailed);
    QVERIFY(!m_bridge->renamePreset(id, QString()));
    QCOMPARE(failed.count(), 1);
}

void TestShaderPresetBridge::renameWritesBackToTheFileTheRecordCameFrom()
{
    // A hand-written file may be named anything: the loader takes the id from
    // the `id` field and falls back to the stem only when the field is absent.
    // Deriving the path from the id alone wrote a SECOND file and left the
    // original in place, after which a rescan saw two files claiming one id.
    const QString dir = m_bridge->presetDirectory();
    QVERIFY(QDir().mkpath(dir));
    QVERIFY(writeFile(dir + QStringLiteral("/my-favourite.json"), QStringLiteral(R"({
        "id": "hand-written", "name": "Mine", "packId": "dissolve",
        "params": { "speed": 1.0 }
    })")));
    m_store->rescanNow(kFamily);
    QCOMPARE(m_bridge->presetsFor(kPack).size(), 1);

    QVERIFY(m_bridge->renamePreset(QStringLiteral("hand-written"), QStringLiteral("Renamed")));
    QCOMPARE(m_bridge->presetsFor(kPack).size(), 1);
    QVERIFY(!QFileInfo::exists(dir + QStringLiteral("/hand-written.json")));

    QFile file(dir + QStringLiteral("/my-favourite.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(QJsonDocument::fromJson(file.readAll()).object().value(QLatin1String("name")).toString(),
             QStringLiteral("Renamed"));
}

void TestShaderPresetBridge::anUnusableDeclaredIdFallsBackToTheFileStem()
{
    // commit() concatenates the id into a filesystem path, so an id carrying a
    // parent-directory hop must never reach it. What the loader does is NOT refuse the
    // file: `ShaderPreset::fromJson` falls back to the filename STEM, which is a real
    // path component by construction and so cannot escape, and the preset stays
    // loadable instead of vanishing over a bad `id` field. That fallback is the
    // behaviour under test here, because it is what makes the escaping id unreachable.
    const QString dir = m_bridge->presetDirectory();
    QVERIFY(QDir().mkpath(dir));
    QVERIFY(writeFile(dir + QStringLiteral("/escaping.json"), QStringLiteral(R"({
        "id": "../../../../escaped", "name": "Hostile", "packId": "dissolve",
        "params": { "speed": 1.0 }
    })")));
    m_store->rescanNow(kFamily);

    // Loaded, under the STEM rather than the declared id.
    const QVariantList rows = m_bridge->presetsFor(kPack);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.constFirst().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("escaping"));

    // A rename therefore writes by the safe stem, in place, and nothing lands outside
    // the preset directory.
    QVERIFY(m_bridge->renamePreset(QStringLiteral("escaping"), QStringLiteral("Renamed")));
    QCOMPARE(readPresetFileNamed(dir + QStringLiteral("/escaping.json")).value(QLatin1String("name")).toString(),
             QStringLiteral("Renamed"));
    QVERIFY(!QFileInfo::exists(QDir(dir).filePath(QStringLiteral("../../../../escaped.json"))));

    // And the bridge's OWN re-check refuses such an id directly, rather than trusting
    // the loader's fallback to be the only door. Deleting that guard leaves this
    // assertion failing.
    QSignalSpy failed(m_bridge.get(), &ShaderPresetBridge::presetWriteFailed);
    QVERIFY(!m_bridge->renamePreset(QStringLiteral("../../../../escaped"), QStringLiteral("Again")));
    QVERIFY(failed.count() >= 1);
}

void TestShaderPresetBridge::aSourcePathOutsideTheDirectoryFallsBackToTheIdPath()
{
    // commit() writes back to the record's own sourcePath so a hand-named file is
    // updated in place rather than duplicated. That path is loader-stamped, but a
    // SYMLINK in the preset directory pointing out of it would otherwise let QSaveFile
    // follow it, so the write is containment-checked and falls back to the id path.
    // Nothing exercised that fallback.
    const QString dir = m_bridge->presetDirectory();
    QVERIFY(QDir().mkpath(dir));

    QTemporaryDir outside;
    QVERIFY(outside.isValid());
    const QString realFile = outside.filePath(QStringLiteral("planted.json"));
    QVERIFY(writeFile(realFile, QStringLiteral(R"({
        "id": "linked", "name": "Linked", "packId": "dissolve",
        "params": { "speed": 1.0 }
    })")));
    // A symlink INSIDE the preset directory whose target is outside it. The loader
    // follows it and stamps the canonical outside path as sourcePath.
    const QString link = dir + QStringLiteral("/linked.json");
    if (!QFile::link(realFile, link)) {
        QSKIP("the filesystem refused a symlink");
    }
    m_store->rescanNow(kFamily);
    QCOMPARE(m_bridge->presetsFor(kPack).size(), 1);

    // REFUSED, and that refusal is what this test found. The containment check on
    // sourcePath correctly declines to write there, but falling back to the id path is
    // not by itself safe: the id path IS the symlink here, and QSaveFile follows it, so
    // the write went through the same link the check had just rejected. The guard
    // refuses a symlinked target whichever path it landed on.
    QSignalSpy failed(m_bridge.get(), &ShaderPresetBridge::presetWriteFailed);
    QVERIFY(!m_bridge->renamePreset(QStringLiteral("linked"), QStringLiteral("Renamed")));
    QCOMPARE(failed.count(), 1);

    // The planted file outside the preset directory is untouched.
    QFile planted(realFile);
    QVERIFY(planted.open(QIODevice::ReadOnly));
    QCOMPARE(QJsonDocument::fromJson(planted.readAll()).object().value(QLatin1String("name")).toString(),
             QStringLiteral("Linked"));
}

void TestShaderPresetBridge::deleteRemovesTheFile()
{
    const QString id = m_bridge->savePreset(kPack, QStringLiteral("Soft"), {{QStringLiteral("speed"), 1.0}});
    QVERIFY(!id.isEmpty());
    const QString path = m_bridge->presetDirectory() + QLatin1Char('/') + id + QStringLiteral(".json");

    QVERIFY(m_bridge->deletePreset(id));
    QVERIFY(!QFileInfo::exists(path));
    QVERIFY(m_bridge->presetsFor(kPack).isEmpty());

    // Gone from the registry too, so a second delete of the same id is a
    // refusal rather than a crash.
    QVERIFY(!m_bridge->deletePreset(id));
}

void TestShaderPresetBridge::deleteOfAnAlreadyGoneFileSucceeds()
{
    // Another window, or a text editor, already removed it. The end state is
    // exactly what the user asked for, so reporting failure told them something
    // went wrong and skipped the rescan, leaving the stale row until the
    // watcher fired.
    const QString id = m_bridge->savePreset(kPack, QStringLiteral("Soft"), {{QStringLiteral("speed"), 1.0}});
    QVERIFY(!id.isEmpty());
    QVERIFY(QFile::remove(m_bridge->presetDirectory() + QLatin1Char('/') + id + QStringLiteral(".json")));

    QSignalSpy failed(m_bridge.get(), &ShaderPresetBridge::presetWriteFailed);
    QVERIFY(m_bridge->deletePreset(id));
    QCOMPARE(failed.count(), 0);
    // And the row is gone now rather than when the watcher gets round to it.
    QVERIFY(m_bridge->presetsFor(kPack).isEmpty());
}

void TestShaderPresetBridge::writesRefuseAPresetDeletedUnderneathUs()
{
    // The other half of the same race. This store still HOLDS the record, so an
    // update or rename would have written the file back out, undoing another
    // window's delete while keeping the id — so every assignment snapped back.
    const QString id = m_bridge->savePreset(kPack, QStringLiteral("Soft"), {{QStringLiteral("speed"), 1.0}});
    QVERIFY(!id.isEmpty());
    const QString path = m_bridge->presetDirectory() + QLatin1Char('/') + id + QStringLiteral(".json");
    QVERIFY(QFile::remove(path));

    QSignalSpy failed(m_bridge.get(), &ShaderPresetBridge::presetWriteFailed);
    QVERIFY(!m_bridge->updatePreset(id, {{QStringLiteral("speed"), 9.0}}));
    QVERIFY(!m_bridge->renamePreset(id, QStringLiteral("Softer")));
    QCOMPARE(failed.count(), 2);
    QVERIFY(!QFileInfo::exists(path));
}

void TestShaderPresetBridge::packDeclaredPresetsAreReadOnlyAndRefuseEveryWrite()
{
    m_store->registry().setPackPresets(kFamily, kPack,
                                       PackPresets{{QStringLiteral("Shipped"), {{QStringLiteral("speed"), 2.0}}}});

    const QVariantMap row = rowFor(m_bridge->presetsFor(kPack), QStringLiteral("Shipped"));
    QVERIFY(row.value(QStringLiteral("readOnly")).toBool());

    QSignalSpy failed(m_bridge.get(), &ShaderPresetBridge::presetWriteFailed);
    QVERIFY(!m_bridge->updatePreset(QStringLiteral("Shipped"), {{QStringLiteral("speed"), 9.0}}));
    QVERIFY(!m_bridge->renamePreset(QStringLiteral("Shipped"), QStringLiteral("Mine")));
    QVERIFY(!m_bridge->deletePreset(QStringLiteral("Shipped")));
    QCOMPARE(failed.count(), 3);
    // Unchanged, and still offered.
    QCOMPARE(m_bridge->presetParams(kPack, QStringLiteral("Shipped")).value(QStringLiteral("speed")).toDouble(), 2.0);
}

void TestShaderPresetBridge::duplicateMakesAnEditableCopyOfAPackPreset()
{
    m_store->registry().setPackPresets(kFamily, kPack,
                                       PackPresets{{QStringLiteral("Shipped"), {{QStringLiteral("speed"), 2.0}}}});

    const QString copy = m_bridge->duplicatePreset(kPack, QStringLiteral("Shipped"), QStringLiteral("My Shipped"));
    QVERIFY(!copy.isEmpty());
    QVERIFY(copy != QStringLiteral("Shipped"));

    const QVariantMap row = rowFor(m_bridge->presetsFor(kPack), copy);
    QCOMPARE(row.value(QStringLiteral("name")).toString(), QStringLiteral("My Shipped"));
    QVERIFY(!row.value(QStringLiteral("readOnly")).toBool());
    QCOMPARE(m_bridge->presetParams(kPack, copy).value(QStringLiteral("speed")).toDouble(), 2.0);
    // The copy is editable, which is the whole point of offering it.
    QVERIFY(m_bridge->updatePreset(copy, {{QStringLiteral("speed"), 5.0}}));

    QSignalSpy failed(m_bridge.get(), &ShaderPresetBridge::presetWriteFailed);
    QVERIFY(m_bridge->duplicatePreset(kPack, QStringLiteral("nope"), QStringLiteral("X")).isEmpty());
    QCOMPARE(failed.count(), 1);
}

void TestShaderPresetBridge::theParameterMapIsBoundedOnTheWayToDisk()
{
    // A preset file is read back and merged into an assignment's effective
    // values, so this is the same input boundary the assignment writers bound
    // their maps at, for the same reason: persisted close to verbatim and
    // copied back in without validation on read.
    QVariantMap params;
    for (int i = 0; i < 200; ++i) {
        params.insert(QStringLiteral("p%1").arg(i), i);
    }
    params.insert(QStringLiteral("nested"), QVariantMap{{QStringLiteral("a"), 1}});
    params.insert(QStringLiteral("listy"), QVariantList{1, 2});
    params.insert(QStringLiteral("huge"), QString(4000, QLatin1Char('x')));

    const QString id = m_bridge->savePreset(kPack, QStringLiteral("Fat"), params);
    QVERIFY(!id.isEmpty());

    const QVariantMap stored = m_bridge->presetParams(kPack, id);
    // EXACTLY the cap, not merely under it: `<=` is satisfied by a cap of 1.
    QCOMPARE(stored.size(), 64);
    QVERIFY(!stored.contains(QStringLiteral("nested")));
    QVERIFY(!stored.contains(QStringLiteral("listy")));
    QVERIFY(!stored.contains(QStringLiteral("huge")));
}

void TestShaderPresetBridge::effectiveParamsIsTheRegistryMerge()
{
    // The ONE definition of the merge. Three QML sites had each reimplemented
    // the overlay in JavaScript, so a preview could disagree with the renderer
    // and none of them clamped.
    m_store->registry().setPackPresetsForFamily(
        kFamily,
        {{kPack,
          PackPresets{{QStringLiteral("Shipped"), {{QStringLiteral("speed"), 2.0}, {QStringLiteral("glow"), 0.5}}}}}},
        {{kPack, PresetValueBounds{{QStringLiteral("speed"), PresetValueRange(QVariant(0.0), QVariant(3.0))}}}});

    const QVariantMap deltas{{QStringLiteral("glow"), 0.9}};
    const QVariantMap effective = m_bridge->effectiveParams(kPack, QStringLiteral("Shipped"), deltas);
    QCOMPARE(effective, m_store->registry().resolveParams(kFamily, kPack, QStringLiteral("Shipped"), deltas));
    QCOMPARE(effective.value(QStringLiteral("speed")).toDouble(), 2.0);
    QCOMPARE(effective.value(QStringLiteral("glow")).toDouble(), 0.9);

    // Clamped on the way out, which the JS versions silently were not.
    const QVariantMap clamped =
        m_bridge->effectiveParams(kPack, QStringLiteral("Shipped"), {{QStringLiteral("speed"), 99.0}});
    QCOMPARE(clamped.value(QStringLiteral("speed")).toDouble(), 3.0);

    // No preset named is the deltas alone, not an empty map.
    QCOMPARE(m_bridge->effectiveParams(kPack, QString(), deltas), deltas);
}

void TestShaderPresetBridge::presetsChangedIsRelayedForThisFamilyOnly()
{
    // Relayed rather than re-derived, so a preset edited in another process
    // reaches an open settings window. A decoration editor has no use for a
    // pointer preset changing, which is why the relay filters.
    QSignalSpy changed(m_bridge.get(), &ShaderPresetBridge::presetsChanged);

    m_store->registry().setPackPresets(ShaderFamily::Pointer, kPack,
                                       PackPresets{{QStringLiteral("Other"), {{QStringLiteral("speed"), 1.0}}}});
    QCOMPARE(changed.count(), 0);

    m_store->registry().setPackPresets(kFamily, kPack,
                                       PackPresets{{QStringLiteral("Shipped"), {{QStringLiteral("speed"), 1.0}}}});
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.at(0).at(0).toString(), kPack);
}

void TestShaderPresetBridge::everyPresetBridgeCallFromTheSettingsQmlIsReachable()
{
    // Every `presetBridge.<name>` the settings QML calls must exist on this
    // class. A renamed or mistyped invokable is otherwise a silent runtime
    // TypeError and a preset control that does nothing, not a build failure —
    // and this surface is reached from five hosts across three pages, so the
    // one that breaks is not necessarily the one being edited.
    //
    // Shaped after the decoration preview's own previewController guard, and
    // swept rather than listed for the same reason given there: a hardcoded
    // file list rots, and a new editor that binds the bridge would simply go
    // unchecked, which is the failure the guard exists to prevent one level up.
    const QString qmlRoot = QStringLiteral(P_SOURCE_DIR "/src/settings/qml");

    // Comments stripped before scraping, so a commented-out call cannot stand
    // in for a real one and a `//` inside a string cannot swallow the rest of a
    // line carrying one.
    static const QRegularExpression blockCommentRe(QStringLiteral("/\\*.*?\\*/"),
                                                   QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression lineCommentRe(QStringLiteral("(?<![:\"'])//[^\n]*"));
    // Every spelling a host reaches a preset bridge by, not just the literal
    // `presetBridge`. Three real call sites went unchecked under that one name —
    // `_shaderPresetBridge` (ActionShaderParamsEditor), `_bridge` (ActionPresetEditor)
    // and a local in OverlayShaderAssignmentCard, since renamed to `presetBridge` so it
    // is swept rather than special-cased — and a missed site is exactly the silent
    // runtime TypeError this guard exists to catch.
    //
    // A receiver-name alternation rather than "any identifier": scraping every `x.y(`
    // in the QML tree collects hundreds of names this bridge has no business answering
    // (CurvePresets.curveLabel, a springPresets array's .length) and the assertion
    // becomes noise. A host that invents a new spelling adds it here, a one-line change
    // the failure message points at.
    static const QRegularExpression callRe(
        QStringLiteral("\\b(?:presetBridge|_bridge|_shaderPresetBridge|animationPresets|overlayPresets"
                       "|decorationPresets|pointerPresets)\\.([A-Za-z_][A-Za-z0-9_]*)"));

    QSet<QString> used;
    QDirIterator sweep(qmlRoot, QStringList{QStringLiteral("*.qml")}, QDir::Files, QDirIterator::Subdirectories);
    while (sweep.hasNext()) {
        const QString path = sweep.next();
        QFile f(path);
        QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(QStringLiteral("cannot read ") + path));
        QString src = QString::fromUtf8(f.readAll());
        src.remove(blockCommentRe);
        src.remove(lineCommentRe);
        auto it = callRe.globalMatch(src);
        while (it.hasNext()) {
            used.insert(it.next().captured(1));
        }
    }
    QVERIFY2(!used.isEmpty(), "scraped no preset-bridge call names — the QML tree or every receiver name moved");

    const QMetaObject* meta = m_bridge->metaObject();
    QStringList unreachable;
    for (const QString& name : used) {
        const QByteArray raw = name.toUtf8();
        if (meta->indexOfProperty(raw.constData()) >= 0) {
            continue;
        }
        bool found = false;
        for (int i = 0; i < meta->methodCount() && !found; ++i) {
            found = meta->method(i).name() == raw;
        }
        if (!found) {
            unreachable.append(name);
        }
    }
    QVERIFY2(unreachable.isEmpty(),
             qPrintable(QStringLiteral("the settings QML calls these on presetBridge, but the bridge lacks them: %1")
                            .arg(unreachable.join(QStringLiteral(", ")))));
}

QTEST_MAIN(TestShaderPresetBridge)
#include "test_shaderpresetbridge.moc"
