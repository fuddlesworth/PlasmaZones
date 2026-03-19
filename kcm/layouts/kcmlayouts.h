// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <KQuickConfigModule>
#include <functional>
#include <memory>

namespace PlasmaZones {

class LayoutManager;
class ScreenHelper;
class Settings;

/**
 * @brief Layouts sub-KCM -- View, create, edit, import/export zone layouts
 */
class KCMLayouts : public KQuickConfigModule
{
    Q_OBJECT

    // Layout list (from LayoutManager)
    Q_PROPERTY(QVariantList layouts READ layouts NOTIFY layoutsChanged)
    Q_PROPERTY(QString layoutToSelect READ layoutToSelect NOTIFY layoutToSelectChanged)

    // Settings needed by LayoutsTab / LayoutGridDelegate / LayoutToolbar
    Q_PROPERTY(QString defaultLayoutId READ defaultLayoutId WRITE setDefaultLayoutId NOTIFY defaultLayoutIdChanged)
    Q_PROPERTY(bool autotileEnabled READ autotileEnabled NOTIFY autotileEnabledChanged)
    Q_PROPERTY(
        QString autotileAlgorithm READ autotileAlgorithm WRITE setAutotileAlgorithm NOTIFY autotileAlgorithmChanged)

    // Font properties for LayoutThumbnail zone number labels
    Q_PROPERTY(QString labelFontFamily READ labelFontFamily NOTIFY labelFontFamilyChanged)
    Q_PROPERTY(qreal labelFontSizeScale READ labelFontSizeScale NOTIFY labelFontSizeScaleChanged)
    Q_PROPERTY(int labelFontWeight READ labelFontWeight NOTIFY labelFontWeightChanged)
    Q_PROPERTY(bool labelFontItalic READ labelFontItalic NOTIFY labelFontItalicChanged)
    Q_PROPERTY(bool labelFontUnderline READ labelFontUnderline NOTIFY labelFontUnderlineChanged)
    Q_PROPERTY(bool labelFontStrikeout READ labelFontStrikeout NOTIFY labelFontStrikeoutChanged)

    // Screens (for editor launch targeting)
    Q_PROPERTY(QVariantList screens READ screens NOTIFY screensChanged)

public:
    KCMLayouts(QObject* parent, const KPluginMetaData& data);
    ~KCMLayouts() override;

    // Layout list
    QVariantList layouts() const;
    QString layoutToSelect() const;

    // Settings
    QString defaultLayoutId() const;
    void setDefaultLayoutId(const QString& layoutId);
    bool autotileEnabled() const;
    QString autotileAlgorithm() const;
    void setAutotileAlgorithm(const QString& algorithm);

    // Font (read-only — edited in Snapping KCM)
    QString labelFontFamily() const;
    qreal labelFontSizeScale() const;
    int labelFontWeight() const;
    bool labelFontItalic() const;
    bool labelFontUnderline() const;
    bool labelFontStrikeout() const;

    // Screens
    QVariantList screens() const;

    // Layout management Q_INVOKABLE methods
    Q_INVOKABLE void createNewLayout();
    Q_INVOKABLE void deleteLayout(const QString& layoutId);
    Q_INVOKABLE void duplicateLayout(const QString& layoutId);
    Q_INVOKABLE void importLayout(const QString& filePath);
    Q_INVOKABLE void exportLayout(const QString& layoutId, const QString& filePath);
    Q_INVOKABLE void editLayout(const QString& layoutId);
    Q_INVOKABLE void openEditor();
    Q_INVOKABLE void setLayoutHidden(const QString& layoutId, bool hidden);
    Q_INVOKABLE void setLayoutAutoAssign(const QString& layoutId, bool enabled);
    Q_INVOKABLE void openLayoutsFolder();
    Q_INVOKABLE void editLayoutOnScreen(const QString& layoutId, const QString& screenId);

public Q_SLOTS:
    void load() override;
    void save() override;
    void defaults() override;
    void refreshScreens();
    void onExternalSettingsChanged();

Q_SIGNALS:
    void layoutsChanged();
    void layoutToSelectChanged();
    void defaultLayoutIdChanged();
    void autotileEnabledChanged();
    void autotileAlgorithmChanged();
    void labelFontFamilyChanged();
    void labelFontSizeScaleChanged();
    void labelFontWeightChanged();
    void labelFontItalicChanged();
    void labelFontUnderlineChanged();
    void labelFontStrikeoutChanged();
    void screensChanged();

private:
    void emitAllChanged();
    QString currentScreenId() const;

    Settings* m_settings = nullptr;
    bool m_saving = false;
    bool m_ignoreNextSettingsChanged = false;
    std::unique_ptr<LayoutManager> m_layoutManager;
    std::unique_ptr<ScreenHelper> m_screenHelper;
};

} // namespace PlasmaZones
