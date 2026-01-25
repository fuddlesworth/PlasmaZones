// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TemplateService.h"
#include "TemplateStrategy.h"
#include "../../core/constants.h"

#include <QUuid>
#include <QLatin1String>
#include "../../core/logging.h"

using namespace PlasmaZones;

// Helper function for zone creation (shared by all strategies)
namespace {
[[maybe_unused]]
QVariantMap createZoneForTemplate(const QString& name, int number, qreal x, qreal y, qreal width, qreal height)
{
    QVariantMap zone;
    using namespace PlasmaZones::JsonKeys;
    zone[Id] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    zone[Name] = name;
    zone[ZoneNumber] = number;
    zone[X] = x;
    zone[Y] = y;
    zone[Width] = width;
    zone[Height] = height;
    zone[HighlightColor] = QString::fromLatin1(EditorConstants::DefaultHighlightColor);
    zone[InactiveColor] = QString::fromLatin1(EditorConstants::DefaultInactiveColor);
    zone[BorderColor] = QString::fromLatin1(EditorConstants::DefaultBorderColor);
    return zone;
}
}

// Template strategy implementations

QVariantList GridTemplateStrategy::createZones(int columns, int rows)
{
    QVariantList zones;
    if (columns < 1)
        columns = 2;
    if (rows < 1)
        rows = 2;

    qreal cellWidth = 1.0 / columns;
    qreal cellHeight = 1.0 / rows;
    int zoneNumber = 1;

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < columns; ++c) {
            int currentNum = zoneNumber++;
            zones.append(createZoneForTemplate(QStringLiteral("Zone %1").arg(currentNum), currentNum, c * cellWidth,
                                               r * cellHeight, cellWidth, cellHeight));
        }
    }
    return zones;
}

QVariantList ColumnsTemplateStrategy::createZones(int columns, int rows)
{
    Q_UNUSED(rows);
    QVariantList zones;
    if (columns < 1)
        columns = 2;

    qreal colWidth = 1.0 / columns;

    for (int i = 0; i < columns; ++i) {
        zones.append(
            createZoneForTemplate(QStringLiteral("Column %1").arg(i + 1), i + 1, i * colWidth, 0.0, colWidth, 1.0));
    }
    return zones;
}

QVariantList RowsTemplateStrategy::createZones(int columns, int rows)
{
    Q_UNUSED(columns);
    QVariantList zones;
    if (rows < 1)
        rows = 2;

    qreal rowHeight = 1.0 / rows;

    for (int i = 0; i < rows; ++i) {
        zones.append(
            createZoneForTemplate(QStringLiteral("Row %1").arg(i + 1), i + 1, 0.0, i * rowHeight, 1.0, rowHeight));
    }
    return zones;
}

QVariantList PriorityTemplateStrategy::createZones(int columns, int rows)
{
    Q_UNUSED(columns);
    Q_UNUSED(rows);
    QVariantList zones;
    using namespace PlasmaZones::Defaults;

    zones.append(createZoneForTemplate(QStringLiteral("Main"), 1, 0.0, 0.0, PriorityGridMainRatio, 1.0));
    zones.append(createZoneForTemplate(QStringLiteral("Secondary Top"), 2, PriorityGridMainRatio, 0.0,
                                       PriorityGridSecondaryRatio, 0.5));
    zones.append(createZoneForTemplate(QStringLiteral("Secondary Bottom"), 3, PriorityGridMainRatio, 0.5,
                                       PriorityGridSecondaryRatio, 0.5));

    return zones;
}

QVariantList FocusTemplateStrategy::createZones(int columns, int rows)
{
    Q_UNUSED(columns);
    Q_UNUSED(rows);
    QVariantList zones;
    using namespace PlasmaZones::Defaults;

    zones.append(createZoneForTemplate(QStringLiteral("Left Panel"), 1, 0.0, 0.0, FocusSideRatio, 1.0));
    zones.append(createZoneForTemplate(QStringLiteral("Center"), 2, FocusSideRatio, 0.0, FocusMainRatio, 1.0));
    zones.append(createZoneForTemplate(QStringLiteral("Right Panel"), 3, FocusSideRatio + FocusMainRatio, 0.0,
                                       FocusSideRatio, 1.0));

    return zones;
}

TemplateService::TemplateService(QObject* parent)
    : QObject(parent)
{
    // Register all template strategies
    m_strategies[QStringLiteral("grid")] = new GridTemplateStrategy(this);
    m_strategies[QStringLiteral("columns")] = new ColumnsTemplateStrategy(this);
    m_strategies[QStringLiteral("rows")] = new RowsTemplateStrategy(this);
    m_strategies[QStringLiteral("priority")] = new PriorityTemplateStrategy(this);
    m_strategies[QStringLiteral("focus")] = new FocusTemplateStrategy(this);
}

TemplateService::~TemplateService()
{
    // Strategies are QObjects with this as parent, so they'll be deleted automatically
}

QVariantList TemplateService::applyTemplate(const QString& templateType, int columns, int rows)
{
    if (templateType.isEmpty()) {
        qCWarning(lcEditor) << "Empty template type";
        return QVariantList();
    }

    TemplateStrategy* strategy = m_strategies.value(templateType, nullptr);
    if (!strategy) {
        qCWarning(lcEditor) << "Unknown template type:" << templateType;
        return QVariantList();
    }

    // Input validation
    if (columns < 1)
        columns = 2;
    if (rows < 1)
        rows = 2;

    return strategy->createZones(columns, rows);
}

QStringList TemplateService::availableTemplates() const
{
    return m_strategies.keys();
}
