// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "newpackagedialog.h"
#include "shaderpackageio.h"

#include <QFontDatabase>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWidget>
#include <QVBoxLayout>

#include <KLocalizedContext>
#include <KLocalizedString>

namespace PlasmaZones {

// Preset definitions with gradient colors for the template cards.
struct PresetDef {
    const char* title;
    const char* subtitle;
    const char* iconName;
    const char* color1;
    const char* color2;
    int features; // ShaderFeatures as int
};

static const PresetDef s_presets[] = {
    {"Minimal",
     "Clean gradient with zone masking",
     "color-gradient",
     "#818cf8", "#6366f1",
     0},
    {"Audio Visualizer",
     "Spectrum-reactive colors and bars",
     "audio-volume-high",
     "#34d399", "#10b981",
     static_cast<int>(ShaderFeature::AudioReactive)},
    {"Multipass Feedback",
     "Two-pass buffer chain for trails and flow",
     "view-split-left-right",
     "#fb923c", "#f97316",
     static_cast<int>(ShaderFeature::Multipass)},
    {"Wallpaper Overlay",
     "Blend the desktop wallpaper with FX",
     "preferences-desktop-wallpaper",
     "#fb7185", "#f43f5e",
     static_cast<int>(ShaderFeature::Wallpaper)},
    {"Multipass + Audio",
     "Feedback loops driven by audio input",
     "media-playlist-shuffle",
     "#a78bfa", "#8b5cf6",
     static_cast<int>(ShaderFeature::Multipass | ShaderFeature::AudioReactive)},
    {"Kitchen Sink",
     "Every feature enabled \u2014 maximum freedom",
     "applications-all",
     "#f472b6", "#ec4899",
     static_cast<int>(ShaderFeature::Multipass | ShaderFeature::AudioReactive | ShaderFeature::Wallpaper)},
};

NewPackageDialog::NewPackageDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("New Shader Package"));
    setMinimumSize(760, 540);
    resize(780, 580);
    setupUi();
}

NewPackageDialog::~NewPackageDialog() = default;

QString NewPackageDialog::shaderName() const
{
    auto* root = m_quickWidget->rootObject();
    if (!root) {
        return {};
    }
    return root->property("shaderName").toString().trimmed();
}

QString NewPackageDialog::shaderId() const
{
    return ShaderPackageIO::sanitizeId(shaderName());
}

ShaderFeatures NewPackageDialog::selectedFeatures() const
{
    auto* root = m_quickWidget->rootObject();
    if (!root) {
        return ShaderFeature::None;
    }
    return ShaderFeatures(root->property("selectedFeatures").toInt());
}

QString NewPackageDialog::category() const
{
    auto* root = m_quickWidget->rootObject();
    return root ? root->property("shaderCategory").toString() : QStringLiteral("Custom");
}

QString NewPackageDialog::description() const
{
    auto* root = m_quickWidget->rootObject();
    return root ? root->property("shaderDescription").toString().trimmed() : QString();
}

QString NewPackageDialog::author() const
{
    auto* root = m_quickWidget->rootObject();
    return root ? root->property("shaderAuthor").toString().trimmed() : QString();
}

QString NewPackageDialog::sanitizeId(const QString& name) const
{
    return ShaderPackageIO::sanitizeId(name);
}

QString NewPackageDialog::fixedFontFamily() const
{
    return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
}

void NewPackageDialog::setupUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Build preset data for QML
    QVariantList presetList;
    for (const auto& p : s_presets) {
        QVariantMap map;
        map[QStringLiteral("title")] = i18n(p.title);
        map[QStringLiteral("subtitle")] = i18n(p.subtitle);
        map[QStringLiteral("iconName")] = QString::fromLatin1(p.iconName);
        map[QStringLiteral("color1")] = QString::fromLatin1(p.color1);
        map[QStringLiteral("color2")] = QString::fromLatin1(p.color2);
        map[QStringLiteral("features")] = p.features;
        presetList.append(map);
    }

    m_quickWidget = new QQuickWidget(this);
    m_quickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_quickWidget->setClearColor(Qt::transparent);

    // Enable i18n() in QML
    m_quickWidget->engine()->rootContext()->setContextObject(
        new KLocalizedContext(m_quickWidget->engine()));

    // Set context properties BEFORE loading QML so presets are
    // available during initial binding (avoids undefined color errors)
    m_quickWidget->engine()->rootContext()->setContextProperty(
        QStringLiteral("wizard"), this);
    m_quickWidget->engine()->rootContext()->setContextProperty(
        QStringLiteral("initialPresets"), presetList);

    // Expose feature flag values so QML doesn't hardcode magic numbers
    const int fMultipass = static_cast<int>(ShaderFeature::Multipass);
    const int fAudio = static_cast<int>(ShaderFeature::AudioReactive);
    const int fWallpaper = static_cast<int>(ShaderFeature::Wallpaper);
    m_quickWidget->engine()->rootContext()->setContextProperty(QStringLiteral("FeatureMultipass"), fMultipass);
    m_quickWidget->engine()->rootContext()->setContextProperty(QStringLiteral("FeatureAudio"), fAudio);
    m_quickWidget->engine()->rootContext()->setContextProperty(QStringLiteral("FeatureWallpaper"), fWallpaper);

    // Parameter definitions per feature — single source of truth for QML manifest display
    QVariantList featureParams;
    auto addParam = [&](int feature, const QString& name, const QString& type, const QString& range) {
        QVariantMap p;
        p[QStringLiteral("feature")] = feature;
        p[QStringLiteral("name")] = name;
        p[QStringLiteral("type")] = type;
        p[QStringLiteral("range")] = range;
        featureParams.append(p);
    };
    addParam(fMultipass, i18n("Speed"), QStringLiteral("float"), QStringLiteral("0 .. 5"));
    addParam(fAudio, i18n("Reactivity"), QStringLiteral("float"), QStringLiteral("0 .. 3"));
    addParam(fAudio, i18n("Bass Boost"), QStringLiteral("float"), QStringLiteral("0 .. 5"));
    addParam(fAudio, i18n("Color Shift"), QStringLiteral("color"), QString());
    addParam(fWallpaper, i18n("Blend"), QStringLiteral("float"), QStringLiteral("0 .. 1"));
    addParam(fWallpaper, i18n("Tint"), QStringLiteral("color"), QString());
    m_quickWidget->engine()->rootContext()->setContextProperty(QStringLiteral("featureParamDefs"), featureParams);
    m_quickWidget->engine()->rootContext()->setContextProperty(
        QStringLiteral("categoryList"), QStringList{
            QStringLiteral("Custom"),
            QStringLiteral("Audio Visualizer"),
            QStringLiteral("3D"),
            QStringLiteral("Cyberpunk"),
            QStringLiteral("Energy"),
            QStringLiteral("Organic"),
            QStringLiteral("Branded"),
        });

    m_quickWidget->setSource(QUrl(QStringLiteral("qrc:/qml/NewPackageWizard.qml")));

    if (m_quickWidget->status() == QQuickWidget::Error) {
        const auto errors = m_quickWidget->errors();
        for (const auto& error : errors) {
            qWarning() << "NewPackageWizard QML error:" << error.toString();
        }
    }

    layout->addWidget(m_quickWidget);
}

} // namespace PlasmaZones
