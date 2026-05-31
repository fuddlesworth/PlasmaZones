// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTiles/LuauTileAlgorithm.h>
#include <PhosphorTiles/SplitTree.h>
#include <PhosphorTiles/TilingState.h>

#include <PhosphorScripting/LuauEngine.h>
#include <PhosphorScripting/LuauWatchdog.h>

#include <PhosphorLayoutApi/AlgorithmMetadata.h>

#include "tileslogging.h"

#include <QFile>
#include <QFileInfo>

#include <algorithm>

using PhosphorScripting::LuauEngine;

namespace PhosphorTiles {

using namespace AutotileDefaults;

namespace {

constexpr qint64 MaxScriptSizeBytes = 1024 * 1024; // 1 MB

ScriptedHelpers::CustomParamDef parseCustomParam(const QVariantMap& m)
{
    ScriptedHelpers::CustomParamDef d;
    d.name = m.value(QStringLiteral("name")).toString();
    d.type = m.value(QStringLiteral("type")).toString();
    d.defaultValue = m.value(QStringLiteral("default"));
    d.description = m.value(QStringLiteral("description")).toString();
    const bool hasMin = m.contains(QStringLiteral("min"));
    const bool hasMax = m.contains(QStringLiteral("max"));
    if (hasMin) {
        d.minValue = m.value(QStringLiteral("min")).toDouble();
    }
    if (hasMax) {
        d.maxValue = m.value(QStringLiteral("max")).toDouble();
    }
    // A malformed script can declare an inverted range (min > max); a UI control
    // built from min > max is undefined, so normalise it when both are given.
    if (hasMin && hasMax && d.minValue > d.maxValue) {
        const double t = d.minValue;
        d.minValue = d.maxValue;
        d.maxValue = t;
    }
    d.enumOptions = m.value(QStringLiteral("options")).toStringList();
    return d;
}

ScriptedHelpers::ScriptMetadata parseMetadata(const QVariantMap& m)
{
    ScriptedHelpers::ScriptMetadata md;
    md.name = m.value(QStringLiteral("name")).toString();
    md.description = m.value(QStringLiteral("description")).toString();
    md.id = m.value(QStringLiteral("id")).toString();
    md.supportsMasterCount = m.value(QStringLiteral("supportsMasterCount")).toBool();
    md.supportsSplitRatio = m.value(QStringLiteral("supportsSplitRatio")).toBool();
    md.supportsMemory = m.value(QStringLiteral("supportsMemory")).toBool();
    md.producesOverlappingZones = m.value(QStringLiteral("producesOverlappingZones")).toBool();
    md.centerLayout = m.value(QStringLiteral("centerLayout")).toBool();
    if (m.contains(QStringLiteral("supportsMinSizes"))) {
        md.supportsMinSizes = m.value(QStringLiteral("supportsMinSizes")).toBool();
    }
    if (m.contains(QStringLiteral("defaultSplitRatio"))) {
        md.defaultSplitRatio = m.value(QStringLiteral("defaultSplitRatio")).toDouble();
    }
    if (m.contains(QStringLiteral("defaultMaxWindows"))) {
        md.defaultMaxWindows = m.value(QStringLiteral("defaultMaxWindows")).toInt();
    }
    if (m.contains(QStringLiteral("minimumWindows"))) {
        md.minimumWindows = m.value(QStringLiteral("minimumWindows")).toInt();
    }
    if (m.contains(QStringLiteral("masterZoneIndex"))) {
        md.masterZoneIndex = m.value(QStringLiteral("masterZoneIndex")).toInt();
    }
    const QString znd = m.value(QStringLiteral("zoneNumberDisplay")).toString();
    if (!znd.isEmpty()) {
        md.zoneNumberDisplay = PhosphorLayout::zoneNumberDisplayFromString(znd);
    }
    const QVariantList cps = m.value(QStringLiteral("customParams")).toList();
    for (const QVariant& cp : cps) {
        md.customParams.append(parseCustomParam(cp.toMap()));
    }
    return md;
}

// Lua array-of-{x,y,width,height} → QRects (capped at MaxZones).
QVector<QRect> variantToRects(const QVariant& v, const QString& scriptId)
{
    QVector<QRect> zones;
    if (v.typeId() != QMetaType::QVariantList) {
        qCWarning(PhosphorTiles::lcTilesLib)
            << "LuauTileAlgorithm: tile() did not return an array, script=" << scriptId;
        return zones;
    }
    const QVariantList list = v.toList();
    const int cap = std::min<int>(static_cast<int>(list.size()), MaxZones);
    zones.reserve(cap);
    for (int i = 0; i < cap; ++i) {
        // Always emit one rect per entry to keep the zone count aligned with the
        // window count: skipping a malformed/empty entry here would leave a
        // window unplaced. A non-map entry, missing coords, or NaN coerce to a
        // zero/empty QRect, which clampZonesToArea() detects and replaces with
        // the full area (with a warning) — the geometry safety net lives there.
        const QVariantMap z = list.at(i).toMap();
        zones.append(QRect(z.value(QStringLiteral("x")).toInt(), z.value(QStringLiteral("y")).toInt(),
                           z.value(QStringLiteral("width")).toInt(), z.value(QStringLiteral("height")).toInt()));
    }
    return zones;
}

// Read-only deep copy of the split tree for memory-aware scripts.
QVariantMap splitNodeToVariant(const SplitNode* node, int depth)
{
    QVariantMap m;
    if (!node || depth > MaxRuntimeTreeDepth) {
        return m;
    }
    m[QStringLiteral("splitRatio")] = node->splitRatio;
    m[QStringLiteral("splitHorizontal")] = node->splitHorizontal;
    m[QStringLiteral("windowId")] = node->windowId;
    m[QStringLiteral("isLeaf")] = node->isLeaf();
    if (node->first) {
        m[QStringLiteral("first")] = splitNodeToVariant(node->first.get(), depth + 1);
    }
    if (node->second) {
        m[QStringLiteral("second")] = splitNodeToVariant(node->second.get(), depth + 1);
    }
    return m;
}

} // namespace

LuauTileAlgorithm::LuauTileAlgorithm(const QString& filePath, std::shared_ptr<PhosphorScripting::LuauWatchdog> watchdog,
                                     QObject* parent)
    : TilingAlgorithm(parent)
    , m_watchdog(std::move(watchdog))
{
    m_engine = std::make_unique<LuauEngine>(m_watchdog);
    loadScript(filePath);
}

LuauTileAlgorithm::~LuauTileAlgorithm() = default;

bool LuauTileAlgorithm::isValid() const
{
    return m_valid;
}

QString LuauTileAlgorithm::filePath() const
{
    return m_filePath;
}

QString LuauTileAlgorithm::id() const
{
    return m_metadata.id;
}

void LuauTileAlgorithm::setUserScript(bool isUser)
{
    m_isUserScript = isUser;
}

bool LuauTileAlgorithm::loadScript(const QString& filePath)
{
    m_filePath = filePath;
    m_scriptId = QStringLiteral("script:") + QFileInfo(filePath).completeBaseName();
    m_valid = false;
    m_metadata = ScriptedHelpers::ScriptMetadata{};

    // Release any module anchored by a prior load so a re-load doesn't leak the
    // previous registry ref (today loadScript runs once, from the ctor).
    if (m_module >= 0) {
        m_engine->releaseModule(m_module);
        m_module = -1;
    }

    QString error;
    if (!m_engine->init(&error)) {
        qCWarning(PhosphorTiles::lcTilesLib) << "LuauTileAlgorithm: engine init failed:" << error;
        return false;
    }

    // Inject + freeze the pz standard library before loading the (untrusted) script.
    QFile preludeFile(QStringLiteral(":/pz/pz.luau"));
    if (!preludeFile.open(QIODevice::ReadOnly)) {
        qCCritical(PhosphorTiles::lcTilesLib) << "LuauTileAlgorithm: missing bundled pz.luau prelude";
        return false;
    }
    if (!m_engine->runPrelude(QStringLiteral("pz"), preludeFile.readAll(), &error)) {
        qCWarning(PhosphorTiles::lcTilesLib) << "LuauTileAlgorithm: pz prelude failed:" << error;
        return false;
    }
    m_engine->sandbox();

    QFile scriptFile(filePath);
    if (!scriptFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(PhosphorTiles::lcTilesLib) << "LuauTileAlgorithm: failed to open file=" << filePath;
        return false;
    }
    if (scriptFile.size() > MaxScriptSizeBytes) {
        qCWarning(PhosphorTiles::lcTilesLib) << "LuauTileAlgorithm: script too large:" << filePath;
        return false;
    }

    m_module = m_engine->loadModule(filePath, scriptFile.readAll(), &error);
    if (m_module < 0) {
        qCWarning(PhosphorTiles::lcTilesLib) << "LuauTileAlgorithm: load failed file=" << filePath << ":" << error;
        return false;
    }
    if (!m_engine->hasFunction(m_module, QStringLiteral("tile"))) {
        qCWarning(PhosphorTiles::lcTilesLib) << "LuauTileAlgorithm: module has no tile() function, file=" << filePath;
        return false;
    }

    m_valid = true;
    cacheMetadataAndOverrides();
    qCInfo(PhosphorTiles::lcTilesLib) << "LuauTileAlgorithm: loaded script=" << m_scriptId << "file=" << filePath;
    return true;
}

void LuauTileAlgorithm::cacheMetadataAndOverrides()
{
    const QVariant md = m_engine->moduleField(m_module, QStringLiteral("metadata"));
    if (md.typeId() == QMetaType::QVariantMap) {
        m_metadata = parseMetadata(md.toMap());
    }

    // Seed caches from metadata (with unset → base default).
    m_cachedMasterZoneIndex = m_metadata.masterZoneIndex;
    m_cachedSupportsMasterCount = m_metadata.supportsMasterCount;
    m_cachedSupportsSplitRatio = m_metadata.supportsSplitRatio;
    m_cachedProducesOverlappingZones = m_metadata.producesOverlappingZones;
    m_cachedCenterLayout = m_metadata.centerLayout;
    m_cachedMinimumWindows = (m_metadata.minimumWindows > 0) ? m_metadata.minimumWindows : 1;
    m_cachedDefaultMaxWindows = (m_metadata.defaultMaxWindows > 0) ? m_metadata.defaultMaxWindows : 6;
    m_cachedDefaultSplitRatio = (m_metadata.defaultSplitRatio > 0.0) ? m_metadata.defaultSplitRatio : DefaultSplitRatio;

    // Optional override functions: called once at load, result cached (the
    // established three-tier resolution). Each is guarded by
    // the watchdog so a misbehaving override cannot hang load.
    // A present-but-failing override (runtime error or watchdog timeout) is
    // distinct from an absent one: log it so a misbehaving override is greppable
    // instead of silently masquerading as "no override declared".
    auto warnIfFailed = [this](const QString& fn, LuauEngine::CallStatus status) {
        if (status != LuauEngine::CallStatus::Ok) {
            qCWarning(PhosphorTiles::lcTilesLib)
                << "LuauTileAlgorithm: override" << fn << "failed (status" << static_cast<int>(status)
                << "), using fallback, script=" << m_scriptId;
        }
    };
    auto resolveInt = [this, &warnIfFailed](const QString& fn, int fallback) -> int {
        if (!m_engine->hasFunction(m_module, fn)) {
            return fallback;
        }
        const auto out = m_engine->callModule(m_module, fn, {}, ScriptWatchdogTimeoutMs);
        warnIfFailed(fn, out.status);
        return out.status == LuauEngine::CallStatus::Ok ? out.result.toInt() : fallback;
    };
    auto resolveBool = [this, &warnIfFailed](const QString& fn, bool fallback) -> bool {
        if (!m_engine->hasFunction(m_module, fn)) {
            return fallback;
        }
        const auto out = m_engine->callModule(m_module, fn, {}, ScriptWatchdogTimeoutMs);
        warnIfFailed(fn, out.status);
        return out.status == LuauEngine::CallStatus::Ok ? out.result.toBool() : fallback;
    };
    auto resolveReal = [this, &warnIfFailed](const QString& fn, qreal fallback) -> qreal {
        if (!m_engine->hasFunction(m_module, fn)) {
            return fallback;
        }
        const auto out = m_engine->callModule(m_module, fn, {}, ScriptWatchdogTimeoutMs);
        warnIfFailed(fn, out.status);
        return out.status == LuauEngine::CallStatus::Ok ? out.result.toDouble() : fallback;
    };

    m_cachedMasterZoneIndex = resolveInt(QStringLiteral("masterZoneIndex"), m_cachedMasterZoneIndex);
    m_cachedSupportsMasterCount = resolveBool(QStringLiteral("supportsMasterCount"), m_cachedSupportsMasterCount);
    m_cachedSupportsSplitRatio = resolveBool(QStringLiteral("supportsSplitRatio"), m_cachedSupportsSplitRatio);
    m_cachedProducesOverlappingZones =
        resolveBool(QStringLiteral("producesOverlappingZones"), m_cachedProducesOverlappingZones);
    m_cachedCenterLayout = resolveBool(QStringLiteral("centerLayout"), m_cachedCenterLayout);
    m_cachedMinimumWindows = std::clamp(resolveInt(QStringLiteral("minimumWindows"), m_cachedMinimumWindows),
                                        MinMetadataWindows, MaxMetadataWindows);
    m_cachedDefaultMaxWindows = std::clamp(resolveInt(QStringLiteral("defaultMaxWindows"), m_cachedDefaultMaxWindows),
                                           MinMetadataWindows, MaxMetadataWindows);
    m_cachedDefaultSplitRatio = std::clamp(resolveReal(QStringLiteral("defaultSplitRatio"), m_cachedDefaultSplitRatio),
                                           MinSplitRatio, MaxSplitRatio);

    m_hasOnWindowAdded = m_engine->hasFunction(m_module, QStringLiteral("onWindowAdded"));
    m_hasOnWindowRemoved = m_engine->hasFunction(m_module, QStringLiteral("onWindowRemoved"));
}

QString LuauTileAlgorithm::name() const
{
    if (!m_metadata.name.isEmpty()) {
        return m_metadata.name;
    }
    QString fallback = m_scriptId;
    if (fallback.startsWith(QLatin1String("script:"))) {
        fallback = fallback.mid(QStringLiteral("script:").size());
    }
    if (!fallback.isEmpty()) {
        fallback[0] = fallback[0].toUpper();
        return fallback;
    }
    return QStringLiteral("Scripted");
}

QString LuauTileAlgorithm::description() const
{
    return m_metadata.description.isEmpty() ? QStringLiteral("User-provided scripted tiling algorithm")
                                            : m_metadata.description;
}

int LuauTileAlgorithm::masterZoneIndex() const
{
    return m_cachedMasterZoneIndex;
}

bool LuauTileAlgorithm::supportsMasterCount() const
{
    return m_cachedSupportsMasterCount;
}

bool LuauTileAlgorithm::supportsSplitRatio() const
{
    return m_cachedSupportsSplitRatio;
}

qreal LuauTileAlgorithm::defaultSplitRatio() const
{
    return m_cachedDefaultSplitRatio;
}

int LuauTileAlgorithm::minimumWindows() const
{
    return m_cachedMinimumWindows;
}

int LuauTileAlgorithm::defaultMaxWindows() const
{
    return m_cachedDefaultMaxWindows;
}

bool LuauTileAlgorithm::producesOverlappingZones() const
{
    return m_cachedProducesOverlappingZones;
}

bool LuauTileAlgorithm::supportsMinSizes() const noexcept
{
    return m_metadata.supportsMinSizes;
}

bool LuauTileAlgorithm::supportsMemory() const noexcept
{
    return m_metadata.supportsMemory;
}

QString LuauTileAlgorithm::zoneNumberDisplay() const noexcept
{
    if (m_metadata.zoneNumberDisplay != PhosphorLayout::ZoneNumberDisplay::RendererDecides) {
        return PhosphorLayout::zoneNumberDisplayToString(m_metadata.zoneNumberDisplay);
    }
    return TilingAlgorithm::zoneNumberDisplay();
}

bool LuauTileAlgorithm::centerLayout() const
{
    return m_cachedCenterLayout;
}

bool LuauTileAlgorithm::isScripted() const noexcept
{
    return true;
}

bool LuauTileAlgorithm::isUserScript() const noexcept
{
    return m_isUserScript;
}

QVariantMap LuauTileAlgorithm::buildContext(const TilingParams& params, const QRect& area) const
{
    QVariantMap ctx;
    // New ergonomic names (count/gap) plus the original names
    // (windowCount/innerGap) the faithfully-ported bundled algorithms use.
    ctx[QStringLiteral("count")] = params.windowCount;
    ctx[QStringLiteral("windowCount")] = params.windowCount;
    ctx[QStringLiteral("gap")] = std::max(0, params.innerGap);
    ctx[QStringLiteral("innerGap")] = std::max(0, params.innerGap);

    QVariantMap areaMap;
    areaMap[QStringLiteral("x")] = area.x();
    areaMap[QStringLiteral("y")] = area.y();
    areaMap[QStringLiteral("width")] = area.width();
    areaMap[QStringLiteral("height")] = area.height();
    ctx[QStringLiteral("area")] = areaMap;

    if (params.state) {
        ctx[QStringLiteral("masterCount")] = params.state->masterCount();
        ctx[QStringLiteral("splitRatio")] = std::clamp(params.state->splitRatio(), MinSplitRatio, MaxSplitRatio);
    } else {
        ctx[QStringLiteral("masterCount")] = DefaultMasterCount;
        ctx[QStringLiteral("splitRatio")] = DefaultSplitRatio;
    }

    QVariantList minSizes;
    const int minCap = std::min<int>(static_cast<int>(params.minSizes.size()), MaxZones);
    for (int i = 0; i < minCap; ++i) {
        QVariantMap s;
        s[QStringLiteral("w")] = params.minSizes[i].width();
        s[QStringLiteral("h")] = params.minSizes[i].height();
        minSizes.append(s);
    }
    ctx[QStringLiteral("minSizes")] = minSizes;

    if (params.state && params.state->splitTree() && !params.state->splitTree()->isEmpty()) {
        QVariantMap tree = splitNodeToVariant(params.state->splitTree()->root(), 0);
        tree[QStringLiteral("leafCount")] = params.state->splitTree()->leafCount();
        ctx[QStringLiteral("tree")] = tree;
    }

    if (!params.windowInfos.isEmpty()) {
        QVariantList windows;
        const int wcap = std::min<int>(static_cast<int>(params.windowInfos.size()), MaxZones);
        for (int i = 0; i < wcap; ++i) {
            QVariantMap w;
            w[QStringLiteral("appId")] = params.windowInfos[i].appId;
            w[QStringLiteral("focused")] = params.windowInfos[i].focused;
            windows.append(w);
        }
        ctx[QStringLiteral("windows")] = windows;
    }
    ctx[QStringLiteral("focusedIndex")] = params.focusedIndex;

    if (!params.screenInfo.id.isEmpty()) {
        QVariantMap screen;
        screen[QStringLiteral("id")] = params.screenInfo.id;
        screen[QStringLiteral("portrait")] = params.screenInfo.portrait;
        screen[QStringLiteral("aspectRatio")] = params.screenInfo.aspectRatio;
        ctx[QStringLiteral("screen")] = screen;
    }

    if (!params.customParams.isEmpty()) {
        ctx[QStringLiteral("custom")] = params.customParams;
    }
    return ctx;
}

QVector<QRect> LuauTileAlgorithm::calculateZones(const TilingParams& params) const
{
    if (!m_valid || params.windowCount <= 0 || !params.screenGeometry.isValid()) {
        return {};
    }

    const QRect area = innerRect(params.screenGeometry, params.outerGaps);
    if (area.isEmpty()) {
        return {};
    }

    // Degenerate screen: fill with stacked full-area zones (matches prior engine).
    if (area.width() < MinRectSizePx || area.height() < MinRectSizePx) {
        QVector<QRect> zones;
        zones.reserve(params.windowCount);
        for (int i = 0; i < params.windowCount; ++i) {
            zones.append(area);
        }
        return zones;
    }
    if (params.windowCount == 1) {
        return {area};
    }

    const QVariantMap ctx = buildContext(params, area);
    const auto out = m_engine->callModule(m_module, QStringLiteral("tile"), {ctx}, ScriptWatchdogTimeoutMs);
    if (out.status != LuauEngine::CallStatus::Ok) {
        qCWarning(PhosphorTiles::lcTilesLib)
            << "LuauTileAlgorithm: tile() failed script=" << m_scriptId << ":" << out.message;
        return {};
    }

    const QVector<QRect> zones = variantToRects(out.result, m_scriptId);
    return ScriptedHelpers::clampZonesToArea(zones, area, m_scriptId);
}

void LuauTileAlgorithm::prepareTilingState(TilingState* state) const
{
    if (!m_metadata.supportsMemory) {
        return;
    }
    if (!state || state->splitTree()) {
        return;
    }

    const qreal currentRatio = state->splitRatio();
    const qreal defRatio = defaultSplitRatio();
    if (currentRatio > defRatio + AutotileDefaults::SplitRatioHysteresis
        || currentRatio < defRatio - AutotileDefaults::SplitRatioHysteresis) {
        state->setSplitRatio(defRatio);
    }

    const QStringList tiledWindows = state->tiledWindows();
    if (tiledWindows.size() <= 1) {
        return;
    }

    const int maxWindows = qMin(static_cast<int>(tiledWindows.size()), AutotileDefaults::MaxZones);
    const qreal ratio = state->splitRatio();
    auto newTree = std::make_unique<SplitTree>();
    for (int i = 0; i < maxWindows; ++i) {
        newTree->insertAtEnd(tiledWindows[i], ratio);
    }
    state->setSplitTree(std::move(newTree));
}

QVariantMap LuauTileAlgorithm::buildStateMap(const TilingState* state, bool includeCountAfterRemoval) const
{
    QVariantMap st;
    st[QStringLiteral("windowCount")] = state->tiledWindowCount();
    st[QStringLiteral("masterCount")] = state->masterCount();
    st[QStringLiteral("splitRatio")] = std::clamp(state->splitRatio(), MinSplitRatio, MaxSplitRatio);

    int focusedIndex = -1;
    const QVector<WindowInfo> infos = buildWindowInfos(state, state->tiledWindowCount(), appIdResolver(), focusedIndex);
    QVariantList windows;
    for (const WindowInfo& info : infos) {
        QVariantMap w;
        w[QStringLiteral("appId")] = info.appId;
        w[QStringLiteral("focused")] = info.focused;
        windows.append(w);
    }
    st[QStringLiteral("windows")] = windows;
    st[QStringLiteral("focusedIndex")] = focusedIndex;

    if (includeCountAfterRemoval) {
        st[QStringLiteral("countAfterRemoval")] = qMax(0, state->tiledWindowCount() - 1);
    }
    return st;
}

bool LuauTileAlgorithm::supportsLifecycleHooks() const noexcept
{
    return m_hasOnWindowAdded || m_hasOnWindowRemoved;
}

void LuauTileAlgorithm::onWindowAdded(TilingState* state, int windowIndex)
{
    if (!m_hasOnWindowAdded || !state) {
        return;
    }
    const QVariantMap st = buildStateMap(state, false);
    m_engine->callModule(m_module, QStringLiteral("onWindowAdded"), {st, windowIndex}, ScriptWatchdogTimeoutMs);
}

void LuauTileAlgorithm::onWindowRemoved(TilingState* state, int windowIndex)
{
    if (!m_hasOnWindowRemoved || !state) {
        return;
    }
    const QVariantMap st = buildStateMap(state, true);
    m_engine->callModule(m_module, QStringLiteral("onWindowRemoved"), {st, windowIndex}, ScriptWatchdogTimeoutMs);
}

bool LuauTileAlgorithm::supportsCustomParams() const noexcept
{
    return !m_metadata.customParams.isEmpty();
}

QVariantList LuauTileAlgorithm::customParamDefList() const
{
    QVariantList result;
    for (const ScriptedHelpers::CustomParamDef& def : m_metadata.customParams) {
        result.append(def.toVariantMap());
    }
    return result;
}

bool LuauTileAlgorithm::hasCustomParam(const QString& name) const
{
    return std::any_of(m_metadata.customParams.cbegin(), m_metadata.customParams.cend(),
                       [&name](const ScriptedHelpers::CustomParamDef& def) {
                           return def.name == name;
                       });
}

} // namespace PhosphorTiles
