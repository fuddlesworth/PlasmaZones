// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#include <PhosphorTheme/AppearanceStore.h>
#include <PhosphorTheme/ShellPalette.h>
#include <QFile>
#include <QImage>
#include <cmath>
#include <limits>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using PhosphorTheme::AppearanceStore;
class TestAppearanceStore : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void trayPreferencesPersistAcrossPresetsAndPreviewRollback()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("appearance.json"));
        AppearanceStore store(path);
        QCOMPARE(store.values().value(QStringLiteral("trayIcons")).toString(), QStringLiteral("symbolic"));
        QCOMPARE(store.values().value(QStringLiteral("trayLimit")).toInt(), 2);
        QVERIFY(store.values().value(QStringLiteral("trayAttention")).toBool());
        QVERIFY(store.values().value(QStringLiteral("trayOrder")).toList().isEmpty());
        QVERIFY(store.values().value(QStringLiteral("trayVisibility")).toMap().isEmpty());
        const auto cloud = QStringLiteral("id:Nextcloud"),
                   fallback = QStringLiteral("fallback:música 🎵|/StatusNotifierItem");
        const QVariantMap preferences{{QStringLiteral("trayIcons"), QStringLiteral("color")},
                                      {QStringLiteral("trayLimit"), 4},
                                      {QStringLiteral("trayAttention"), false},
                                      {QStringLiteral("trayOrder"), QVariantList{fallback, cloud}},
                                      {QStringLiteral("trayVisibility"),
                                       QVariantMap{{cloud, QStringLiteral("hidden")},
                                                   {fallback, QStringLiteral("pinned")},
                                                   {QStringLiteral("id:Steam"), QStringLiteral("auto")},
                                                   {QStringLiteral("id:Discord"), QStringLiteral("overflow")}}}};
        for (auto it = preferences.cbegin(); it != preferences.cend(); ++it)
            QVERIFY(store.setValue(it.key(), it.value()));
        for (const auto& preset : {QStringLiteral("phosphor"), QStringLiteral("paper"), QStringLiteral("ember")}) {
            QVERIFY(store.applyPreset(preset));
            for (auto it = preferences.cbegin(); it != preferences.cend(); ++it)
                QCOMPARE(store.values().value(it.key()), it.value());
        }
        const auto saved = store.values();
        QCOMPARE(AppearanceStore(path).values(), saved);
        const auto exported = QUrl::fromLocalFile(dir.filePath(QStringLiteral("tray.json")));
        QVERIFY(store.exportPreset(exported));
        AppearanceStore imported(dir.filePath(QStringLiteral("imported.json")));
        QVERIFY(imported.importPreset(exported));
        QCOMPARE(imported.values(), saved);

        QVERIFY(store.beginPreview());
        QVERIFY(store.setValue(QStringLiteral("trayLimit"), 0));
        QVERIFY(store.setValue(QStringLiteral("trayVisibility"), QVariantMap{{cloud, QStringLiteral("pinned")}}));
        QCOMPARE(AppearanceStore(path).values(), saved);
        QCOMPARE(AppearanceStore::effectiveValues(path), store.values());
        store.revertPreview();
        QCOMPARE(store.values(), saved);
        QVERIFY(store.setValue(QStringLiteral("trayOrder"), QVariantList{}));
        store.endPreview();
        QCOMPARE(store.values(), saved);
        QCOMPARE(AppearanceStore(path).values(), saved);
    }
    void trayPreferencesRejectInvalidValues_data()
    {
        QTest::addColumn<QString>("key");
        QTest::addColumn<QVariant>("value");
        QTest::newRow("unknown-icons") << QStringLiteral("trayIcons") << QVariant(QStringLiteral("full-color"));
        QTest::newRow("icons-type") << QStringLiteral("trayIcons") << QVariant(1);
        QTest::newRow("attention-type") << QStringLiteral("trayAttention") << QVariant(QStringLiteral("true"));
        QTest::newRow("negative-limit") << QStringLiteral("trayLimit") << QVariant(-1);
        QTest::newRow("large-limit") << QStringLiteral("trayLimit") << QVariant(5);
        QTest::newRow("fractional-limit") << QStringLiteral("trayLimit") << QVariant(2.5);
        QTest::newRow("text-limit") << QStringLiteral("trayLimit") << QVariant(QStringLiteral("2"));
        QTest::newRow("bool-limit") << QStringLiteral("trayLimit") << QVariant(true);
        QTest::newRow("nan-limit") << QStringLiteral("trayLimit") << QVariant(std::numeric_limits<double>::quiet_NaN());
        QTest::newRow("infinite-limit") << QStringLiteral("trayLimit")
                                        << QVariant(std::numeric_limits<double>::infinity());
        QTest::newRow("order-type") << QStringLiteral("trayOrder") << QVariant(QStringLiteral("id:app"));
        QTest::newRow("order-entry-type") << QStringLiteral("trayOrder") << QVariant(QVariantList{1});
        QTest::newRow("duplicate-order") << QStringLiteral("trayOrder")
                                         << QVariant(QVariantList{QStringLiteral("id:app"), QStringLiteral("id:app")});
        QTest::newRow("empty-order-key") << QStringLiteral("trayOrder") << QVariant(QVariantList{QString()});
        QTest::newRow("control-order-key")
            << QStringLiteral("trayOrder") << QVariant(QVariantList{QStringLiteral("id:bad\napp")});
        QTest::newRow("long-order-key") << QStringLiteral("trayOrder") << QVariant(QVariantList{QString(513, u'a')});
        QTest::newRow("visibility-type") << QStringLiteral("trayVisibility") << QVariant(QVariantList{});
        QTest::newRow("policy-type") << QStringLiteral("trayVisibility")
                                     << QVariant(QVariantMap{{QStringLiteral("id:app"), true}});
        QTest::newRow("unknown-policy") << QStringLiteral("trayVisibility")
                                        << QVariant(QVariantMap{{QStringLiteral("id:app"), QStringLiteral("never")}});
        QTest::newRow("empty-policy-key")
            << QStringLiteral("trayVisibility") << QVariant(QVariantMap{{QString(), QStringLiteral("hidden")}});
        QTest::newRow("control-policy-key")
            << QStringLiteral("trayVisibility")
            << QVariant(QVariantMap{{QStringLiteral("id:bad\tapp"), QStringLiteral("hidden")}});
        QTest::newRow("long-policy-key") << QStringLiteral("trayVisibility")
                                         << QVariant(QVariantMap{{QString(513, u'a'), QStringLiteral("hidden")}});
    }
    void trayPreferencesRejectInvalidValues()
    {
        QFETCH(QString, key);
        QFETCH(QVariant, value);
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("appearance.json"));
        AppearanceStore store(path);
        QVERIFY(store.setValue(QStringLiteral("trayLimit"), 1));
        const auto before = store.values();
        QVERIFY(!store.setValue(key, value));
        QCOMPARE(store.values(), before);
        QCOMPARE(AppearanceStore(path).values(), before);
    }
    void trayPreferenceBoundsCountUniqueApps()
    {
        QVariantMap result;
        QVERIFY(AppearanceStore::validate({{QStringLiteral("trayLimit"), 0.0}}, result));
        QVERIFY(AppearanceStore::validate({{QStringLiteral("trayLimit"), qlonglong(4)}}, result));
        const auto longest = QString(512, u'a');
        QVERIFY(AppearanceStore::validate(
            {{QStringLiteral("trayOrder"), QVariantList{longest}},
             {QStringLiteral("trayVisibility"), QVariantMap{{longest, QStringLiteral("hidden")}}}},
            result));
        QVariantList order;
        QVariantMap visibility;
        for (int i = 0; i < 256; ++i) {
            const auto key = QStringLiteral("id:app-%1").arg(i);
            order.append(key);
            visibility[key] = QStringLiteral("auto");
        }
        QVERIFY(AppearanceStore::validate(
            {{QStringLiteral("trayOrder"), order}, {QStringLiteral("trayVisibility"), visibility}}, result));
        const auto extra = QStringLiteral("id:another-app");
        QVERIFY(!AppearanceStore::validate(
            {{QStringLiteral("trayOrder"), order},
             {QStringLiteral("trayVisibility"), QVariantMap{{extra, QStringLiteral("hidden")}}}},
            result));
        order.append(extra);
        visibility[extra] = QStringLiteral("hidden");
        QVERIFY(!AppearanceStore::validate({{QStringLiteral("trayOrder"), order}}, result));
        QVERIFY(!AppearanceStore::validate({{QStringLiteral("trayVisibility"), visibility}}, result));
    }
    void statsPreferencesValidateAndPersist()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("appearance.json"));
        AppearanceStore store(path);
        const QVariantList metrics{QStringLiteral("memory"), QStringLiteral("network")};
        QVERIFY(store.setValue(QStringLiteral("statsMetrics"), metrics));
        QVERIFY(store.setValue(QStringLiteral("statsStyle"), QStringLiteral("meters")));
        QVERIFY(store.setValue(QStringLiteral("statsMemoryUnit"), QStringLiteral("used")));
        QVERIFY(store.setValue(QStringLiteral("statsInterval"), 5));
        const auto valid = store.values();
        for (const auto& invalid : QList<QVariantList>{
                 {},
                 {QStringLiteral("cpu"), QStringLiteral("cpu")},
                 {QStringLiteral("invalid")},
                 {QStringLiteral("cpu"), QStringLiteral("gpu"), QStringLiteral("memory"), QStringLiteral("network")}}) {
            QVERIFY(!store.setValue(QStringLiteral("statsMetrics"), invalid));
        }
        QVERIFY(!store.setValue(QStringLiteral("statsStyle"), QStringLiteral("invalid")));
        QVERIFY(!store.setValue(QStringLiteral("statsMemoryUnit"), QStringLiteral("invalid")));
        QVERIFY(!store.setValue(QStringLiteral("statsInterval"), 0));
        QVERIFY(!store.setValue(QStringLiteral("statsInterval"), 3));
        QVERIFY(!store.setValue(QStringLiteral("statsInterval"), QStringLiteral("2")));
        QCOMPARE(store.values(), valid);
        QCOMPARE(AppearanceStore(path).values(), valid);
        QVERIFY(store.beginPreview());
        QVERIFY(store.setValue(QStringLiteral("statsStyle"), QStringLiteral("numbers")));
        QCOMPARE(AppearanceStore(path).values(), valid);
        store.endPreview();
        QCOMPARE(store.values(), valid);
        QVERIFY(store.applyPreset(QStringLiteral("ember")));
        for (const auto& key : {QStringLiteral("statsMetrics"), QStringLiteral("statsStyle"),
                                QStringLiteral("statsMemoryUnit"), QStringLiteral("statsInterval")})
            QCOMPARE(store.values().value(key), valid.value(key));
    }
    void decorationPreviewIsOwnedAndNeverReplacesSavedSettings()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("appearance.json"));
        AppearanceStore store(path);
        QVERIFY(store.applyPreset(QStringLiteral("phosphor")));
        const auto saved = store.values();
        QVERIFY(store.beginPreview());
        QVERIFY(store.applyPreset(QStringLiteral("ember")));
        bool active = false;
        QCOMPARE(AppearanceStore::effectiveValues(path, &active), store.values());
        QVERIFY(active);
        QCOMPARE(AppearanceStore(path).values(), saved);
        AppearanceStore competing(path);
        QVERIFY(!competing.beginPreview());
        store.revertPreview();
        QCOMPARE(AppearanceStore::effectiveValues(path), saved);
        QVERIFY(store.applyPreset(QStringLiteral("paper")));
        QVERIFY(store.applyPreview());
        const auto applied = store.values();
        QVERIFY(store.applyPreset(QStringLiteral("ember")));
        store.endPreview();
        QCOMPARE(AppearanceStore::effectiveValues(path, &active), applied);
        QVERIFY(!active);
        QVERIFY(!QFileInfo::exists(path + QStringLiteral(".preview")));
        // An orphan draft, left by a crashed producer, must never win on startup.
        QVERIFY(QFile::copy(path, path + QStringLiteral(".preview")));
        QVERIFY(store.applyPreset(QStringLiteral("phosphor")));
        QCOMPARE(AppearanceStore::effectiveValues(path, &active), store.values());
        QVERIFY(!active);
    }
    void previewIsOneAtomicLook()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("appearance.json"));
        AppearanceStore store(path);
        QVERIFY(store.applyPreset(QStringLiteral("phosphor")));
        const auto original = store.values();
        const auto image = dir.filePath(QStringLiteral("wallpaper.png"));
        QImage sample(16, 16, QImage::Format_RGB32);
        sample.fill(Qt::green);
        QVERIFY(sample.save(image));
        store.beginPreview();
        QVERIFY(store.applyPreset(QStringLiteral("paper")));
        QVERIFY(store.setWallpaper(image, QStringLiteral("DP-1"), QStringLiteral("fit")));
        QVERIFY(store.dirty());
        AppearanceStore during(path);
        QCOMPARE(during.values(), original);
        store.beginPreview(); // reload/open does not replace the original snapshot
        store.revertPreview();
        QCOMPARE(store.values(), original);
        QVERIFY(!store.dirty());
        QVERIFY(store.setWallpaper(image, QString(), QStringLiteral("fill")));
        QVERIFY(store.setValue(QStringLiteral("textScale"), 110));
        QVERIFY(store.applyPreview());
        const auto applied = store.values();
        QVERIFY(!store.dirty());
        QVERIFY(store.setValue(QStringLiteral("barInset"), 24));
        store.endPreview();
        QCOMPARE(store.values(), applied);
        QVERIFY(!store.editing());
        AppearanceStore restored(path);
        QCOMPARE(restored.values(), applied);
    }
    void failedApplyRetainsEditableDraft()
    {
        QTemporaryDir dir;
        AppearanceStore store(dir.path());
        const auto original = store.values();
        store.beginPreview();
        QVERIFY(store.applyPreset(QStringLiteral("ember")));
        QVERIFY(!store.applyPreview());
        QVERIFY(store.dirty());
        QVERIFY(!store.error().isEmpty());
        store.endPreview();
        QCOMPARE(store.values(), original);
    }
    void wallpaperPaletteChangesEveryRoleAndKeepsContrast()
    {
        const auto luminance = [](const QColor& color) {
            const auto c = [](double v) {
                return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4);
            };
            return .2126 * c(color.redF()) + .7152 * c(color.greenF()) + .0722 * c(color.blueF());
        };
        for (const auto& material : {QStringLiteral("glass"), QStringLiteral("solid"), QStringLiteral("light")}) {
            auto values = AppearanceStore::defaults();
            values[QStringLiteral("palette")] = QStringLiteral("wallpaper");
            values[QStringLiteral("material")] = material;
            const auto purple = PhosphorTheme::ShellPalette::fromSettings(values);
            for (const auto& seed : {QStringLiteral("#9bb897"), QStringLiteral("#d3906c"), QStringLiteral("#000000"),
                                     QStringLiteral("#ffffff"), QStringLiteral("#00ff00")}) {
                values[QStringLiteral("wallpaperColors")] = QVariantList{seed, seed, seed, seed};
                const auto palette = PhosphorTheme::ShellPalette::fromSettings(values);
                QVERIFY(palette.surface != purple.surface);
                QVERIFY(palette.card != purple.card);
                QVERIFY(palette.recess != purple.recess);
                for (const auto& foreground : QList<QColor>{palette.text, palette.muted} + palette.stops) {
                    for (const auto& background : {palette.surface, palette.card, palette.recess}) {
                        const auto a = luminance(foreground), b = luminance(background);
                        QVERIFY((std::max(a, b) + .05) / (std::min(a, b) + .05) >= 4.49);
                    }
                }
            }
        }
    }
    void newSettingsRejectMalformedImports()
    {
        QTemporaryDir dir;
        AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        QVERIFY(!store.setValue(QStringLiteral("accentIndex"), 4));
        QVERIFY(!store.setValue(QStringLiteral("textScale"), 400));
        QVERIFY(!store.setValue(QStringLiteral("wallpaperColors"), QVariantList{QStringLiteral("invalid")}));
        QVERIFY(!store.setValue(QStringLiteral("wallpapers"),
                                QVariantMap{{QStringLiteral("DP-1"),
                                             QVariantMap{{QStringLiteral("path"), QStringLiteral("relative.png")},
                                                         {QStringLiteral("fit"), QStringLiteral("fill")}}}}));
        QVariantMap oversized;
        for (int i = 0; i < 32; ++i)
            oversized[QString::number(i)] = QVariantMap{{QStringLiteral("path"), QString(u'/' + QString(4000, u'a'))},
                                                        {QStringLiteral("fit"), QStringLiteral("fill")}};
        QVERIFY(!store.setValue(QStringLiteral("wallpapers"), oversized));
    }

    void notificationPreferencesSurvivePresetAndReload()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("appearance.json"));
        AppearanceStore store(path);
        QVERIFY(store.setValue(QStringLiteral("notificationGrouping"), QStringLiteral("time")));
        QVERIFY(store.setValue(QStringLiteral("notificationPreviews"), false));
        QVERIFY(!store.setValue(QStringLiteral("notificationGrouping"), QStringLiteral("invalid")));
        QVERIFY(store.applyPreset(QStringLiteral("ember")));
        AppearanceStore loaded(path);
        QCOMPARE(loaded.values().value(QStringLiteral("notificationGrouping")).toString(), QStringLiteral("time"));
        QCOMPARE(loaded.values().value(QStringLiteral("notificationPreviews")).toBool(), false);
    }
    void sharedPalettePreservesApprovedColors()
    {
        QTemporaryDir dir;
        AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        const QList<QColor> backgrounds{QColor(QStringLiteral("#101d32")), QColor(QStringLiteral("#eef0f7")),
                                        QColor(QStringLiteral("#272620"))};
        const QList<QString> presets{QStringLiteral("phosphor"), QStringLiteral("paper"), QStringLiteral("ember")};
        for (int i = 0; i < presets.size(); ++i) {
            QVERIFY(store.applyPreset(presets[i]));
            const auto palette = PhosphorTheme::ShellPalette::fromSettings(store.values());
            QCOMPARE(palette.surface.name(), backgrounds[i].name());
            QCOMPARE(store.palette(), palette.toVariant());
            QCOMPARE(palette.windowColor(0), palette.stops[0]);
            QCOMPARE(palette.windowColor(1), palette.stops[2]);
            QCOMPARE(palette.windowColor(2), palette.stops[3]);
            QCOMPARE(palette.windowColor(3), palette.stops[1]);
            QCOMPARE(palette.windowColor(7), palette.windowColor(3));
            QVERIFY(palette.text != palette.surface);
        }
    }
    void presetNamesReflectStyleEdits()
    {
        QTemporaryDir dir;
        AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        for (const auto& preset : {QStringLiteral("phosphor"), QStringLiteral("paper"), QStringLiteral("ember")}) {
            QVERIFY(store.applyPreset(preset));
            QCOMPARE(store.currentPreset(), preset);
            QVERIFY(store.setValue(QStringLiteral("presentation"), QStringLiteral("stage")));
            QCOMPARE(store.currentPreset(), preset);
            QVERIFY(store.setValue(QStringLiteral("radius"), 29));
            QCOMPARE(store.currentPreset(), QStringLiteral("custom"));
        }
    }

    void persistsAndRoundTrips()
    {
        QTemporaryDir dir;
        AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        QVERIFY(store.applyPreset(QStringLiteral("ember")));
        QVERIFY(store.setValue(QStringLiteral("visualizer"), QStringLiteral("halo")));
        const auto exported = QUrl::fromLocalFile(dir.filePath(QStringLiteral("export.json")));
        QVERIFY(store.exportPreset(exported));
        AppearanceStore reloaded(dir.filePath(QStringLiteral("appearance.json")));
        QCOMPARE(reloaded.values(), store.values());
        QVERIFY(store.applyPreset(QStringLiteral("paper")));
        QVERIFY(store.importPreset(exported));
        QCOMPARE(store.values(), reloaded.values());
    }
    void lockPrivacyChoicesPersistAcrossPresets()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("appearance.json"));
        AppearanceStore store(path);
        QCOMPARE(store.values().value(QStringLiteral("lockMedia")).toBool(), false);
        QCOMPARE(store.values().value(QStringLiteral("lockNotifications")).toBool(), true);
        QVERIFY(store.setValue(QStringLiteral("lockLayout"), QStringLiteral("centered")));
        QVERIFY(store.setValue(QStringLiteral("lockMedia"), true));
        QVERIFY(store.setValue(QStringLiteral("lockNotifications"), false));
        QVERIFY(store.applyPreset(QStringLiteral("paper")));
        QCOMPARE(store.values().value(QStringLiteral("lockLayout")).toString(), QStringLiteral("centered"));
        QCOMPARE(store.values().value(QStringLiteral("lockMedia")).toBool(), true);
        QCOMPARE(store.values().value(QStringLiteral("lockNotifications")).toBool(), false);
        AppearanceStore restored(path);
        QCOMPARE(restored.values(), store.values());
        QVERIFY(!store.setValue(QStringLiteral("lockLayout"), QStringLiteral("unknown")));
        QVERIFY(!store.setValue(QStringLiteral("lockMedia"), QStringLiteral("true")));
    }
    void rejectsInvalidWithoutChangingMemoryOrDisk()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("appearance.json"));
        AppearanceStore store(path);
        QVERIFY(store.applyPreset(QStringLiteral("phosphor")));
        const auto before = store.values();
        QVERIFY(!store.setValue(QStringLiteral("radius"), 10000));
        QVERIFY(!store.setValue(QStringLiteral("radius"), 9.5));
        QVERIFY(!store.setValue(QStringLiteral("motion"), QStringLiteral("false")));
        QVERIFY(!store.setValue(QStringLiteral("edge"), QStringLiteral("sideways")));
        QVERIFY(!store.setValue(QStringLiteral("unknown"), true));
        QCOMPARE(store.values(), before);
        AppearanceStore reloaded(path);
        QCOMPARE(reloaded.values(), before);
        QFile invalid(dir.filePath(QStringLiteral("invalid.json")));
        QVERIFY(invalid.open(QIODevice::WriteOnly));
        invalid.write("{\"version\":2,\"settings\":{}}");
        invalid.close();
        QVERIFY(!store.importPreset(QUrl::fromLocalFile(invalid.fileName())));
        QCOMPARE(store.values(), before);
    }
    void geometryNotificationOnlyForLayoutChanges()
    {
        QTemporaryDir dir;
        AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        QSignalSpy layout(&store, &AppearanceStore::geometryChanged);
        QSignalSpy changed(&store, &AppearanceStore::changed);
        QVERIFY(store.setValue(QStringLiteral("radius"), 12));
        QCOMPARE(layout.count(), 0);
        QVERIFY(store.setValue(QStringLiteral("radius"), 12));
        QCOMPARE(changed.count(), 1);
        QVERIFY(store.setValue(QStringLiteral("density"), QStringLiteral("compact")));
        QCOMPARE(layout.count(), 1);
    }
    void layoutAndFontsSurvivePresetsAndRoundTrip()
    {
        QTemporaryDir dir;
        AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        QVERIFY(store.moveWidget(QStringLiteral("clock"), QStringLiteral("left"), 0));
        QVERIFY(store.moveWidget(QStringLiteral("power"), QString()));
        QVERIFY(store.setValue(QStringLiteral("uiFont"), QStringLiteral("Noto Sans")));
        QVERIFY(store.setValue(QStringLiteral("monoFont"), QStringLiteral("monospace")));
        QVERIFY(store.setValue(QStringLiteral("presentation"), QStringLiteral("stage")));
        const auto layout = store.values().value(QStringLiteral("barLayout"));
        QVERIFY(store.applyPreset(QStringLiteral("ember")));
        QCOMPARE(store.values().value(QStringLiteral("barLayout")), layout);
        QCOMPARE(store.values().value(QStringLiteral("uiFont")).toString(), QStringLiteral("Noto Sans"));
        QCOMPARE(store.values().value(QStringLiteral("presentation")).toString(), QStringLiteral("stage"));
        const auto url = QUrl::fromLocalFile(dir.filePath(QStringLiteral("preset.json")));
        QVERIFY(store.exportPreset(url));
        AppearanceStore restored(dir.filePath(QStringLiteral("second.json")));
        QVERIFY(restored.importPreset(url));
        QCOMPARE(restored.values(), store.values());
        QVERIFY(store.resetBarLayout());
        QCOMPARE(store.values().value(QStringLiteral("barLayout")),
                 AppearanceStore::defaults().value(QStringLiteral("barLayout")));
    }
    void layoutBoundaryRejectsDuplicatesAndMalformedIds()
    {
        QTemporaryDir dir;
        AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        const auto before = store.values();
        auto layout = before.value(QStringLiteral("barLayout")).toMap();
        layout[QStringLiteral("center")] = QVariantList{QVariantList{QStringLiteral("clock"), QStringLiteral("clock")}};
        QVERIFY(!store.setValue(QStringLiteral("barLayout"), layout));
        QVERIFY(!store.moveWidget(QStringLiteral("bad id"), QStringLiteral("left")));
        QVERIFY(!store.moveWidget(QStringLiteral("clock"), QStringLiteral("above")));
        QVERIFY(!store.setValue(QStringLiteral("uiFont"), QStringLiteral("bad\nfont")));
        QVERIFY(!store.setValue(QStringLiteral("monoFont"), 42));
        QCOMPARE(store.values(), before);
    }
    void reorderCountsWidgetsAcrossGroups()
    {
        QTemporaryDir dir;
        AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        auto layout = AppearanceStore::defaults().value(QStringLiteral("barLayout")).toMap();
        layout[QStringLiteral("left")] =
            QVariantList{QVariantList{QStringLiteral("focusedapp")}, QVariantList{QStringLiteral("media")}};
        QVERIFY(store.setValue(QStringLiteral("barLayout"), layout));
        QVERIFY(store.moveWidget(QStringLiteral("placementmap"), QStringLiteral("left"), 1));
        QVariantList flattened;
        for (const auto& group :
             store.values().value(QStringLiteral("barLayout")).toMap().value(QStringLiteral("left")).toList())
            flattened += group.toList();
        QCOMPARE(flattened,
                 (QVariantList{QStringLiteral("focusedapp"), QStringLiteral("placementmap"), QStringLiteral("media")}));
    }
    void failedWriteKeepsCurrentAppearance()
    {
        QTemporaryDir dir;
        AppearanceStore store(dir.path()); // a directory cannot be replaced by a preset
        const auto before = store.values();
        QVERIFY(!store.applyPreset(QStringLiteral("ember")));
        QCOMPARE(store.values(), before);
        QVERIFY(!store.error().isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestAppearanceStore)
#include "test_appearancestore.moc"
