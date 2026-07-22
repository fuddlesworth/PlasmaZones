# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# PlasmaZones Unit Tests

# Helper macro for simple tests (Qt6::Test + Qt6::Core + plasmazones_core).
#
# IMPORTANT: This macro hands you Qt6::Test + Qt6::Core ONLY. If your test
# source includes a D-Bus header, a Wayland-client header, or any other Qt
# component not in that minimal pair, use a hand-rolled `add_executable` +
# `target_link_libraries` with an explicit `target_link_libraries(... PRIVATE
# Qt6::DBus ...)` instead. The macro will SEEM to work because plasmazones_core's
# INTERFACE_LINK_LIBRARIES pulls Qt6::DBus transitively today — but that's
# coincidental, not contractual. A future refactor that drops Qt6::DBus from
# plasmazones_core's interface link would silently break every test that
# relied on the transitive pull.
macro(p_add_test _name _src)
    add_executable(${_name} ${_src})
    target_link_libraries(${_name} PRIVATE Qt6::Test Qt6::Core plasmazones_core)
    add_test(NAME ${_name} COMMAND ${_name})
endmacro()

# ═══════════════════════════════════════════════════════════════════════════════
# core/ - Window Identity Tests
# ═══════════════════════════════════════════════════════════════════════════════
p_add_test(test_window_identity core/windowtracking/test_window_identity.cpp)
p_add_test(test_window_registry core/windowtracking/test_window_registry.cpp)
p_add_test(test_virtual_desktop_per_screen core/screens/test_virtual_desktop_per_screen.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# core/ - Window Tracking Tests (snap/unsnap, floating)
# ═══════════════════════════════════════════════════════════════════════════════
p_add_test(test_window_tracking_snap core/windowtracking/test_window_tracking_snap.cpp)
p_add_test(test_window_tracking_float core/windowtracking/test_window_tracking_float.cpp)
p_add_test(test_window_placement_store core/windowtracking/test_window_placement_store.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# core/ - Session Persistence Tests
# ═══════════════════════════════════════════════════════════════════════════════
p_add_test(test_session_persistence core/session/test_session_persistence.cpp)
p_add_test(test_session_persistence_layout core/session/test_session_persistence_layout.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# core/ - QML i18n Context Substitution Tests
# ═══════════════════════════════════════════════════════════════════════════════
p_add_test(test_localized_context_substitution core/test_localized_context_substitution.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# core/ - Window Tracking Service Tests (lifecycle, session, queries)
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_wts_lifecycle core/windowtracking/test_wts_lifecycle.cpp)
target_link_libraries(test_wts_lifecycle PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_wts_lifecycle COMMAND test_wts_lifecycle)

add_executable(test_wts_session core/windowtracking/test_wts_session.cpp)
target_link_libraries(test_wts_session PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_wts_session COMMAND test_wts_session)

add_executable(test_wts_queries core/windowtracking/test_wts_queries.cpp)
target_link_libraries(test_wts_queries PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_wts_queries COMMAND test_wts_queries)

add_executable(test_daemongeometryresolver_inset core/geometry/test_daemongeometryresolver_inset.cpp)
target_link_libraries(test_daemongeometryresolver_inset PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_daemongeometryresolver_inset COMMAND test_daemongeometryresolver_inset)

add_executable(test_wts_clearfloat core/windowtracking/test_wts_clearfloat.cpp)
target_link_libraries(test_wts_clearfloat PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_wts_clearfloat COMMAND test_wts_clearfloat)

add_executable(test_wts_virtual_migration core/windowtracking/test_wts_virtual_migration.cpp)
target_link_libraries(test_wts_virtual_migration PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_wts_virtual_migration COMMAND test_wts_virtual_migration)

add_executable(test_wts_crossmode_float core/windowtracking/test_wts_crossmode_float.cpp)
target_link_libraries(test_wts_crossmode_float PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_wts_crossmode_float COMMAND test_wts_crossmode_float)

add_executable(test_wts_registry_integration core/windowtracking/test_wts_registry_integration.cpp)
target_link_libraries(test_wts_registry_integration PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_wts_registry_integration COMMAND test_wts_registry_integration)

# DirtyMask invariants for delta-persistence (Phase 3 of refactor/dbus-performance).
add_executable(test_wts_dirty_mask core/windowtracking/test_wts_dirty_mask.cpp)
target_link_libraries(test_wts_dirty_mask PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_wts_dirty_mask COMMAND test_wts_dirty_mask)

# ═══════════════════════════════════════════════════════════════════════════════
# core/ - SnapEngine Tests
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_snap_engine core/snap/test_snap_engine.cpp helpers/SnapEngineTestFixture.h)
target_link_libraries(test_snap_engine PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_snap_engine COMMAND test_snap_engine)

# Restore-resolution half of the SnapEngine suite (capture placement, window
# restore, fallback unfloat geometry, snap-to-empty gating). The fixture header
# is listed as a source so AUTOMOC scans its Q_OBJECT classes.
add_executable(test_snap_engine_restore core/snap/test_snap_engine_restore.cpp helpers/SnapEngineTestFixture.h)
target_link_libraries(test_snap_engine_restore PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_snap_engine_restore COMMAND test_snap_engine_restore)

# Exclusion half of the SnapEngine suite (exclude wiring, size exclusion,
# entry-zone crossing).
add_executable(test_snap_engine_exclude core/snap/test_snap_engine_exclude.cpp helpers/SnapEngineTestFixture.h)
target_link_libraries(test_snap_engine_exclude PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_snap_engine_exclude COMMAND test_snap_engine_exclude)

# End-to-end per-monitor snap acceptance (Discussion #724): cross-monitor unfloat
# determinism, threaded-screen authority, per-screen independence, migration completeness.
add_executable(test_snap_per_monitor core/snap/test_snap_per_monitor.cpp)
target_link_libraries(test_snap_per_monitor PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_snap_per_monitor COMMAND test_snap_per_monitor)

# resolveFallbackUnfloatGeometry on-success coverage. QTEST_MAIN (not GUILESS) so a
# QGuiApplication + offscreen primary screen exists and zoneGeometry() resolves —
# the geometry-dependent path test_snap_engine.cpp (guiless) cannot reach.
add_executable(test_snap_unfloat_fallback core/snap/test_snap_unfloat_fallback.cpp)
# Qt6::Gui is explicit (not transitive via plasmazones_core) so QTEST_MAIN
# resolves to QGuiApplication — the offscreen primary screen it provides is what
# makes zoneGeometry() resolve. Relying on the transitive pull would silently
# degrade to QCoreApplication (null primaryScreen) if core dropped Gui PUBLIC.
target_link_libraries(test_snap_unfloat_fallback PRIVATE Qt6::Test Qt6::Core Qt6::Gui Qt6::DBus plasmazones_core)
add_test(NAME test_snap_unfloat_fallback COMMAND test_snap_unfloat_fallback)

# ═══════════════════════════════════════════════════════════════════════════════
# core/ - PlacementEngineBase Tests (LGPL shared engine base: settings injection + base prune)
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_placement_engine_base core/test_placement_engine_base.cpp)
target_link_libraries(test_placement_engine_base PRIVATE Qt6::Test Qt6::Core PhosphorEngine)
add_test(NAME test_placement_engine_base COMMAND test_placement_engine_base)

# ═══════════════════════════════════════════════════════════════════════════════
# autotile/ - Tiling Algorithm Tests (split by algorithm family)
# ═══════════════════════════════════════════════════════════════════════════════
p_add_test(test_tiling_algo_masterstack autotile/algorithms/test_tiling_algo_masterstack.cpp)
target_compile_definitions(test_tiling_algo_masterstack PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
p_add_test(test_tiling_algo_spiral_monocle autotile/algorithms/test_tiling_algo_spiral_monocle.cpp)
target_compile_definitions(test_tiling_algo_spiral_monocle PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
p_add_test(test_tiling_algo_grid_threecolumn autotile/algorithms/test_tiling_algo_grid_threecolumn.cpp)
target_compile_definitions(test_tiling_algo_grid_threecolumn PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
p_add_test(test_tiling_algo_wide_centered autotile/algorithms/test_tiling_algo_wide_centered.cpp)
target_compile_definitions(test_tiling_algo_wide_centered PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
p_add_test(test_tiling_algo_cascade_stair_spread autotile/algorithms/test_tiling_algo_cascade_stair_spread.cpp)
target_compile_definitions(test_tiling_algo_cascade_stair_spread PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
p_add_test(test_tiling_algo_lshape autotile/algorithms/test_tiling_algo_lshape.cpp)
target_compile_definitions(test_tiling_algo_lshape PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
p_add_test(test_tiling_algo_deck_zen autotile/algorithms/test_tiling_algo_deck_zen.cpp)
target_compile_definitions(test_tiling_algo_deck_zen PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
p_add_test(test_tiling_algo_edge_cases autotile/algorithms/test_tiling_algo_edge_cases.cpp)
target_compile_definitions(test_tiling_algo_edge_cases PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
p_add_test(test_tiling_algo_bsp autotile/algorithms/test_tiling_algo_bsp.cpp)
target_compile_definitions(test_tiling_algo_bsp PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")

# ═══════════════════════════════════════════════════════════════════════════════
# core/ - PhosphorLayoutApi LayoutSourceBundle + FactoryContext
# ═══════════════════════════════════════════════════════════════════════════════
# Covers the PR #343 plugin-discovery API directly: FactoryContext
# set/get, bundle lifecycle, duplicate-name handling, priority sort, and
# the ~LayoutSourceBundle "no spurious emit" guarantee.
p_add_test(test_layout_source_bundle core/layout/test_layout_source_bundle.cpp)
p_add_test(test_layout_preview_serialize core/layout/test_layout_preview_serialize.cpp)
p_add_test(test_unified_layout_order core/layout/test_unified_layout_order.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# autotile/ - Algorithm Registry Tests
# ═══════════════════════════════════════════════════════════════════════════════
p_add_test(test_algorithm_registry autotile/algorithms/test_algorithm_registry.cpp)
target_compile_definitions(test_algorithm_registry PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
p_add_test(test_algorithm_registry_custom autotile/algorithms/test_algorithm_registry_custom.cpp)
target_compile_definitions(test_algorithm_registry_custom PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
p_add_test(test_split_tree autotile/state/test_split_tree.cpp)
target_compile_definitions(test_split_tree PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
p_add_test(test_scripted_algorithm_loader autotile/algorithms/test_scripted_algorithm_loader.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# scripting/ - PhosphorScripting (embedded Luau host)
# ═══════════════════════════════════════════════════════════════════════════════
# Manual target: a library unit test that links only PhosphorScripting, not the
# app core (plasmazones_core).
add_executable(test_luau_engine scripting/test_luau_engine.cpp)
set_target_properties(test_luau_engine PROPERTIES AUTOMOC ON)
target_link_libraries(test_luau_engine PRIVATE Qt6::Test Qt6::Core PhosphorScripting::PhosphorScripting)
add_test(NAME test_luau_engine COMMAND test_luau_engine)

# Shared pluau helpers (guardArea/stripLayout/clamp/…): loads the real pluau.luau
# prelude into a sandboxed VM and exercises each helper directly.
add_executable(test_pluau_helpers scripting/test_pluau_helpers.cpp)
set_target_properties(test_pluau_helpers PROPERTIES AUTOMOC ON)
target_compile_definitions(test_pluau_helpers PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
target_link_libraries(test_pluau_helpers PRIVATE Qt6::Test Qt6::Core PhosphorScripting::PhosphorScripting)
add_test(NAME test_pluau_helpers COMMAND test_pluau_helpers)

add_executable(test_luau_tile_algorithm scripting/test_luau_tile_algorithm.cpp)
set_target_properties(test_luau_tile_algorithm PROPERTIES AUTOMOC ON)
target_compile_definitions(test_luau_tile_algorithm PRIVATE "P_LUAU_TEST_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/scripting\"")
target_link_libraries(test_luau_tile_algorithm PRIVATE Qt6::Test Qt6::Core PhosphorTiles::PhosphorTiles)
add_test(NAME test_luau_tile_algorithm COMMAND test_luau_tile_algorithm)

# Golden parity harness: ported Luau algorithms vs the committed golden fixture
# (originally captured from the JS engine's byte-identical output).
add_executable(test_luau_parity scripting/test_luau_parity.cpp)
set_target_properties(test_luau_parity PROPERTIES AUTOMOC ON)
target_compile_definitions(test_luau_parity PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
target_link_libraries(test_luau_parity PRIVATE Qt6::Test Qt6::Core PhosphorTiles::PhosphorTiles)
add_test(NAME test_luau_parity COMMAND test_luau_parity)

# Aligned resize-aware grid: loads the real bundled algorithm and proves the
# interactive-resize scripting hook + persistent state bag work end-to-end.
add_executable(test_luau_aligned_grid scripting/test_luau_aligned_grid.cpp)
set_target_properties(test_luau_aligned_grid PROPERTIES AUTOMOC ON)
target_compile_definitions(test_luau_aligned_grid PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
target_link_libraries(test_luau_aligned_grid PRIVATE Qt6::Test Qt6::Core PhosphorTiles::PhosphorTiles)
add_test(NAME test_luau_aligned_grid COMMAND test_luau_aligned_grid)

add_executable(test_luau_ratio_reflow scripting/test_luau_ratio_reflow.cpp)
set_target_properties(test_luau_ratio_reflow PROPERTIES AUTOMOC ON)
target_compile_definitions(test_luau_ratio_reflow PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
target_link_libraries(test_luau_ratio_reflow PRIVATE Qt6::Test Qt6::Core PhosphorTiles::PhosphorTiles)
add_test(NAME test_luau_ratio_reflow COMMAND test_luau_ratio_reflow)

add_executable(test_luau_theater scripting/test_luau_theater.cpp)
set_target_properties(test_luau_theater PROPERTIES AUTOMOC ON)
target_compile_definitions(test_luau_theater PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
target_link_libraries(test_luau_theater PRIVATE Qt6::Test Qt6::Core PhosphorTiles::PhosphorTiles)
add_test(NAME test_luau_theater COMMAND test_luau_theater)

# ═══════════════════════════════════════════════════════════════════════════════
# autotile/ - Autotile Engine Tests (core, master, minsize, overflow)
# ═══════════════════════════════════════════════════════════════════════════════
p_add_test(test_autotile_engine_core autotile/engine/test_autotile_engine_core.cpp)
target_compile_definitions(test_autotile_engine_core PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
p_add_test(test_autotile_engine_master autotile/engine/test_autotile_engine_master.cpp)
target_compile_definitions(test_autotile_engine_master PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
# helpers/AutotileFakes.h declares a Q_OBJECT fake — listing it as a source is
# what makes AUTOMOC generate its metaobject (a merely #included header is not
# scanned), and the IAutotileSettings qobject_cast needs it.
target_sources(test_autotile_engine_master PRIVATE helpers/AutotileFakes.h)
p_add_test(test_autotile_engine_minsize autotile/engine/test_autotile_engine_minsize.cpp)
p_add_test(test_autotile_engine_overflow autotile/engine/test_autotile_engine_overflow.cpp)
p_add_test(test_autotile_engine_retry autotile/engine/test_autotile_engine_retry.cpp)
p_add_test(test_autotile_engine_order_cycling autotile/engine/test_autotile_engine_order_cycling.cpp)
target_compile_definitions(test_autotile_engine_order_cycling PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
p_add_test(test_autotile_engine_class_mutation autotile/engine/test_autotile_engine_class_mutation.cpp)
p_add_test(test_autotile_drag_insert autotile/behavior/test_autotile_drag_insert.cpp)
target_compile_definitions(test_autotile_drag_insert PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
p_add_test(test_autotile_handoff autotile/behavior/test_autotile_handoff.cpp)
target_compile_definitions(test_autotile_handoff PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
# Producer-side validator guard - ensures the JSON emitted by
# AutotileEngine::applyTiling round-trips through TileRequestEntry
# parsing and passes validationError(). Would have caught PR #326 C1.
p_add_test(test_autotile_tile_request_validation autotile/behavior/test_autotile_tile_request_validation.cpp)
target_link_libraries(test_autotile_tile_request_validation PRIVATE PhosphorCompositor::PhosphorCompositor)

# ═══════════════════════════════════════════════════════════════════════════════
# autotile/ - Autotile + Virtual Screen Integration Tests
# ═══════════════════════════════════════════════════════════════════════════════
p_add_test(test_autotile_per_screen_desktop autotile/behavior/test_autotile_per_screen_desktop.cpp)

# Accepting a virtual screen ID needs the manager to resolve real virtual
# geometry, which needs a backing physical screen — compile in the
# FakeScreenProvider (like the cross-surface navigation tests).
add_executable(test_autotile_virtual
               autotile/behavior/test_autotile_virtual.cpp
               ${CMAKE_SOURCE_DIR}/libs/phosphor-screens/tests/FakeScreenProvider.cpp)
target_include_directories(test_autotile_virtual
               PRIVATE ${CMAKE_SOURCE_DIR}/libs/phosphor-screens/tests)
target_link_libraries(test_autotile_virtual PRIVATE Qt6::Test Qt6::Core plasmazones_core)
add_test(NAME test_autotile_virtual COMMAND test_autotile_virtual)
target_compile_definitions(test_autotile_virtual PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")

# ═══════════════════════════════════════════════════════════════════════════════
# autotile/ - Autotile Engine Extended Tests (startup, float, algoswitch,
#             script-state stash, overflow)
# ═══════════════════════════════════════════════════════════════════════════════
p_add_test(test_autotile_ext_startup_float autotile/behavior/test_autotile_ext_startup_float.cpp)
p_add_test(test_autotile_ext_algoswitch autotile/behavior/test_autotile_ext_algoswitch.cpp)
target_compile_definitions(test_autotile_ext_algoswitch PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
p_add_test(test_autotile_script_state_stash autotile/state/test_autotile_script_state_stash.cpp)
target_compile_definitions(test_autotile_script_state_stash PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
# The split-tree case needs a screen with real geometry: the restore hangs off
# recalculateLayout, which bails before it without one. Same fake provider
# test_autotile_virtual uses.
target_sources(test_autotile_script_state_stash
               PRIVATE ${CMAKE_SOURCE_DIR}/libs/phosphor-screens/tests/FakeScreenProvider.cpp)
target_include_directories(test_autotile_script_state_stash
               PRIVATE ${CMAKE_SOURCE_DIR}/libs/phosphor-screens/tests)
p_add_test(test_autotile_ext_overflow autotile/behavior/test_autotile_ext_overflow.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# autotile/ - Tiling State Tests (windows, config, serialization)
# ═══════════════════════════════════════════════════════════════════════════════
p_add_test(test_tiling_state_windows autotile/state/test_tiling_state_windows.cpp)
p_add_test(test_tiling_state_config autotile/state/test_tiling_state_config.cpp)
p_add_test(test_tiling_state_core autotile/state/test_tiling_state_core.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# autotile/ - Overflow Manager Tests
# ═══════════════════════════════════════════════════════════════════════════════
p_add_test(test_overflow_manager autotile/engine/test_overflow_manager.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# core/ - Geometry Utils Tests (minsizes, overlaps, BSP)
# ═══════════════════════════════════════════════════════════════════════════════
p_add_test(test_geometry_utils_minsizes core/geometry/test_geometry_utils_minsizes.cpp)
p_add_test(test_geometry_utils_overlaps core/geometry/test_geometry_utils_overlaps.cpp)
p_add_test(test_geometry_utils_bsp core/geometry/test_geometry_utils_bsp.cpp)
p_add_test(test_geometry_utils_clamp core/geometry/test_geometry_utils_clamp.cpp)
p_add_test(test_geometry_utils_serialization core/geometry/test_geometry_utils_serialization.cpp)
p_add_test(test_spatial_adjacency core/zones/test_spatial_adjacency.cpp)
p_add_test(test_virtualscreen_swapper core/screens/test_virtualscreen_swapper.cpp)
p_add_test(test_screen_mode_router core/screens/test_screen_mode_router.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# config/ - Settings Tests (core, validation, per-screen)
# ═══════════════════════════════════════════════════════════════════════════════
p_add_test(test_settings_core config/settings/test_settings_core.cpp)

# Companion test executable: animation Profile JSON-blob storage,
# per-field signal discrimination, aggregate-setter merge semantics.
p_add_test(test_settings_animation_profile config/settings/test_settings_animation_profile.cpp)

# Companion test executable: ShaderProfileTree persistence + prune,
# autoAssignAllLayouts master toggle.
p_add_test(test_settings_shader_tree config/settings/test_settings_shader_tree.cpp)

# Companion test executable: DecorationProfileTree persistence + JSON facade
# (the decoration analogue of test_settings_shader_tree). PhosphorSurface
# symbols resolve transitively via plasmazones_core (which links
# PhosphorSurface PUBLIC for Settings::decorationProfileTree()).
p_add_test(test_settings_decoration_tree config/settings/test_settings_decoration_tree.cpp)

# Pins runtime palette tracking for system-colors mode (needs Qt6::Gui for
# QGuiApplication palette events; p_add_test links Core only).
add_executable(test_settings_system_palette_tracking config/settings/test_settings_system_palette_tracking.cpp)
target_link_libraries(test_settings_system_palette_tracking PRIVATE Qt6::Test Qt6::Core Qt6::Gui plasmazones_core)
add_test(NAME test_settings_system_palette_tracking COMMAND test_settings_system_palette_tracking)

# Companion test executable: per-page Discard/Reset primitives — committed
# baseline capture, isKeyModified, discardKeys (revert-to-baseline) and
# resetKeys (revert-to-default) with NOTIFY re-emission.
p_add_test(test_settings_pagereset config/settings/test_settings_pagereset.cpp)

p_add_test(test_settings_validation config/settings/test_settings_validation.cpp)

p_add_test(test_settings_perscreen config/settings/test_settings_perscreen.cpp)

p_add_test(test_settings_disable_per_mode config/settings/test_settings_disable_per_mode.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# config/ - Virtual Screen Settings Tests (save/load round-trip, staged flow)
# ═══════════════════════════════════════════════════════════════════════════════
p_add_test(test_settings_virtualscreen config/settings/test_settings_virtualscreen.cpp)

p_add_test(test_virtualscreen_staged config/test_virtualscreen_staged.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# config/ - ConfigDefaults Tests (standalone, no external dependency)
# ═══════════════════════════════════════════════════════════════════════════════
p_add_test(test_configdefaults config/test_configdefaults.cpp)

p_add_test(test_configbackend_json config/test_configbackend_json.cpp)

p_add_test(test_configmigration config/migrations/test_configmigration.cpp)
p_add_test(test_configmigration_v2tov3 config/migrations/test_configmigration_v2tov3.cpp)
p_add_test(test_migration_v1_animation config/migrations/test_migration_v1_animation.cpp)
p_add_test(test_layoutsettings_relocation config/migrations/test_layoutsettings_relocation.cpp)

# Binds the schema's declared choices to their user-facing labels.
add_executable(test_settingsvaluelabels config/settings/test_settingsvaluelabels.cpp)
target_link_libraries(test_settingsvaluelabels
    PRIVATE Qt6::Test Qt6::Core plasmazones_core)
target_compile_definitions(test_settingsvaluelabels PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
add_test(NAME test_settingsvaluelabels COMMAND test_settingsvaluelabels)

# v3 → v4 window-rule consolidation migration.
add_executable(test_migration_v3_to_v4 config/migrations/test_migration_v3_to_v4.cpp)
target_link_libraries(test_migration_v3_to_v4
    PRIVATE Qt6::Test Qt6::Core plasmazones_core PhosphorRules::PhosphorRules)
add_test(NAME test_migration_v3_to_v4 COMMAND test_migration_v3_to_v4)

# v3 → v4 animation app-rule and animation-exclusion folds.
add_executable(test_migration_v3_to_v4_animations config/migrations/test_migration_v3_to_v4_animations.cpp)
target_link_libraries(test_migration_v3_to_v4_animations
    PRIVATE Qt6::Test Qt6::Core plasmazones_core PhosphorRules::PhosphorRules)
add_test(NAME test_migration_v3_to_v4_animations COMMAND test_migration_v3_to_v4_animations)

# v3 → v4 snapping-exclusion and zone-rename folds.
add_executable(test_migration_v3_to_v4_exclusions config/migrations/test_migration_v3_to_v4_exclusions.cpp)
target_link_libraries(test_migration_v3_to_v4_exclusions
    PRIVATE Qt6::Test Qt6::Core plasmazones_core PhosphorRules::PhosphorRules)
add_test(NAME test_migration_v3_to_v4_exclusions COMMAND test_migration_v3_to_v4_exclusions)

# v4 → v5 per-mode appearance + gaps → unified Windows/Gaps config-group migration.
add_executable(test_migration_v4_to_v5 config/migrations/test_migration_v4_to_v5.cpp)
target_link_libraries(test_migration_v4_to_v5
    PRIVATE Qt6::Test Qt6::Core plasmazones_core PhosphorRules::PhosphorRules)
add_test(NAME test_migration_v4_to_v5 COMMAND test_migration_v4_to_v5)

# Retired provider-default rule pruned from rules.json (finalizeV4Conversion cleanup).
add_executable(test_provider_default_cleanup config/test_provider_default_cleanup.cpp)
target_link_libraries(test_provider_default_cleanup
    PRIVATE Qt6::Test Qt6::Core plasmazones_core PhosphorRules::PhosphorRules)
add_test(NAME test_provider_default_cleanup COMMAND test_provider_default_cleanup)

# RuleStore — daemon-side rules.json persistence + mutation API.
add_executable(test_rule_store config/test_rule_store.cpp)
target_link_libraries(test_rule_store
    PRIVATE Qt6::Test Qt6::Core plasmazones_core PhosphorRules::PhosphorRules)
add_test(NAME test_rule_store COMMAND test_rule_store)

# ═══════════════════════════════════════════════════════════════════════════════
# daemon/ - Daemon-level Integration Tests (animation profile publication, etc.)
# ═══════════════════════════════════════════════════════════════════════════════
# Daemon-level publication contract: ProfileLoader registers user JSON
# under its owner tag, Settings publishes to the registry's `Global`
# path under the empty/direct owner, and a loader-side `Global.json`
# collision is silently skipped (Phase 1b inversion: Settings wins on
# settings-driven paths). Mirrors the daemon's setupAnimationProfiles
# without constructing a real Daemon (which requires D-Bus + KWin
# binding - out of scope for unit tests).
add_executable(test_animation_profile_publication daemon/test_animation_profile_publication.cpp)
target_link_libraries(test_animation_profile_publication
    PRIVATE Qt6::Test Qt6::Core plasmazones_core PhosphorAnimation::PhosphorAnimation)
add_test(NAME test_animation_profile_publication COMMAND test_animation_profile_publication)

# Startup Wayland-display guard (main.cpp). Pins resolveWaylandSocketPath()
# so an empty/unset WAYLAND_DISPLAY resolves to Qt's default "wayland-0" and
# the guard exits cleanly instead of letting QGuiApplication qFatal() →
# SIGABRT → core dump. Header-only logic, so links Qt6::Test + Qt6::Core
# only (no plasmazones_core).
add_executable(test_wayland_session_check daemon/test_wayland_session_check.cpp)
target_link_libraries(test_wayland_session_check PRIVATE Qt6::Test Qt6::Core)
add_test(NAME test_wayland_session_check COMMAND test_wayland_session_check)

# Phase-5 SurfaceAnimator scope-prefix policy. Pins down the
# per-instance scope construction in osd.cpp / selector.cpp /
# snapassist.cpp so a divergence from the PhosphorRoles base scopePrefix
# doesn't silently fall back to the empty default config in release
# builds. Replaces the debug-only Q_ASSERT_X in createZoneSelectorWindow
# with a release-built check that covers every overlay role.
add_executable(test_overlay_scope_prefixes daemon/test_overlay_scope_prefixes.cpp)
target_link_libraries(test_overlay_scope_prefixes
    PRIVATE Qt6::Test Qt6::Core Qt6::Gui Qt6::Quick PhosphorLayer::PhosphorLayer
            PhosphorShellPatterns::PhosphorShellPatterns
            PhosphorAnimation::PhosphorAnimation
            PhosphorOverlay::PhosphorOverlay)
add_test(NAME test_overlay_scope_prefixes COMMAND test_overlay_scope_prefixes)

# Snap-assist thumbnail provider: bounded LRU cache, monotonic URL
# generation, eviction-tied URL state. Compiles the daemon source
# directly because the provider lives in the plasmazonesd executable, not
# plasmazones_core.
add_executable(test_snap_assist_thumbnail_provider
    daemon/test_snap_assist_thumbnail_provider.cpp
    ${CMAKE_SOURCE_DIR}/src/daemon/rendering/snapassistthumbnailprovider.cpp
)
# PhosphorProtocol pulls in ServiceConstants.h's
# SnapAssistThumbnailCacheCapacity that the provider's CacheCapacity now
# resolves against - header-only constant, no .so coupling beyond the
# include path.
target_link_libraries(test_snap_assist_thumbnail_provider
    PRIVATE Qt6::Test Qt6::Core Qt6::Gui Qt6::Quick PhosphorProtocol::PhosphorProtocol)
add_test(NAME test_snap_assist_thumbnail_provider COMMAND test_snap_assist_thumbnail_provider)

# ═══════════════════════════════════════════════════════════════════════════════
# core/ - Layout Tests (core, zones)
# ═══════════════════════════════════════════════════════════════════════════════
p_add_test(test_layout_core core/layout/test_layout_core.cpp)
p_add_test(test_layout_zones core/layout/test_layout_zones.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# core/ - LayoutManager Tests (persistence, cycling, assignment)
# ═══════════════════════════════════════════════════════════════════════════════
p_add_test(test_layoutmanager_persistence core/layout/test_layoutmanager_persistence.cpp)
p_add_test(test_layoutmanager_cycling core/layout/test_layoutmanager_cycling.cpp)
p_add_test(test_layoutmanager_assignment core/layout/test_layoutmanager_assignment.cpp)
p_add_test(test_layoutmanager_default_synthesis core/layout/test_layoutmanager_default_synthesis.cpp)
# The shared fixture header holds a Q_OBJECT base class; AUTOMOC only scans
# headers listed as target sources, so both targets must list it explicitly.
target_sources(test_layoutmanager_assignment PRIVATE core/layout/LayoutManagerAssignmentFixture.h)
target_sources(test_layoutmanager_default_synthesis PRIVATE core/layout/LayoutManagerAssignmentFixture.h)
p_add_test(test_layout_registry_cache core/layout/test_layout_registry_cache.cpp)

# The editor's save payload is schema-gated at the D-Bus boundary, so the shape
# EditorController::saveLayout emits has to satisfy the layout schema. Pins both
# serialized forms of aspectRatioClass (editor int, canonical string from
# Layout::toJson) against the schema that gates createLayoutFromJson/updateLayout.
p_add_test(test_layout_schema_editor_shape core/layout/test_layout_schema_editor_shape.cpp)

# exportLayout / importLayout report their outcome rather than logging it and
# returning void, and the export writes atomically so a failure leaves the
# user's chosen destination alone.
p_add_test(test_layoutregistry_transfer core/layout/test_layoutregistry_transfer.cpp)

# Regression guard for the in-process dual-RuleStore data-loss race
# where Settings used to construct its own store pointed at the same file
# as the daemon's. Pins the share-via-borrow-ctor wiring in daemon.cpp.
p_add_test(test_daemon_dual_store_race core/test_daemon_dual_store_race.cpp)

# Cascade-fidelity proof for the window-rule context model — the priority
# formula reproduces the legacy zone-Assignment cascade.
add_executable(test_rule_cascade_fidelity core/test_rule_cascade_fidelity.cpp)
target_link_libraries(test_rule_cascade_fidelity
    PRIVATE Qt6::Test Qt6::Core PhosphorRules::PhosphorRules PhosphorZones::PhosphorZones plasmazones_core)
add_test(NAME test_rule_cascade_fidelity COMMAND test_rule_cascade_fidelity)

# Context-domain half of the cascade proof (gaps, orientation, active layout,
# tiling params, overlay, lock, per-mode gap rules).
add_executable(test_rule_cascade_context core/test_rule_cascade_context.cpp)
target_link_libraries(test_rule_cascade_context
    PRIVATE Qt6::Test Qt6::Core PhosphorRules::PhosphorRules PhosphorZones::PhosphorZones plasmazones_core)
add_test(NAME test_rule_cascade_context COMMAND test_rule_cascade_context)

# ═══════════════════════════════════════════════════════════════════════════════
# core/ - Virtual Screen Tests (ID utilities, data model, PhosphorScreens::ScreenManager)
# ═══════════════════════════════════════════════════════════════════════════════
p_add_test(test_virtual_screen core/screens/test_virtual_screen.cpp)
p_add_test(test_virtual_screen_manager core/screens/test_virtual_screen_manager.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# core/ - Snap Assist Virtual Screen Tests (screensMatch, buildOccupiedZoneSet)
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_snap_assist_virtual core/snap/test_snap_assist_virtual.cpp)
target_link_libraries(test_snap_assist_virtual PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_snap_assist_virtual COMMAND test_snap_assist_virtual)

# ═══════════════════════════════════════════════════════════════════════════════
# core/ - LayerSurface + computeLayerSize Tests
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_layersurface core/wayland/test_layersurface.cpp)
target_link_libraries(test_layersurface PRIVATE Qt6::Test Qt6::Core Qt6::Gui Qt6::WaylandClientPrivate Wayland::Client plasmazones_core)
# PhosphorWayland private QPA headers + generated protocol headers
target_include_directories(test_layersurface PRIVATE
    ${CMAKE_BINARY_DIR}/src                                      # plasmazones_export.h
    ${CMAKE_SOURCE_DIR}/libs/phosphor-wayland/src                  # private QPA headers
    ${CMAKE_BINARY_DIR}/libs/phosphor-wayland                      # phosphorwayland_export.h
    ${CMAKE_BINARY_DIR}/libs/phosphor-wayland/generated-protocols  # wlr-layer-shell protocol header
)
add_test(NAME test_layersurface COMMAND test_layersurface)

# ═══════════════════════════════════════════════════════════════════════════════
# core/ - SinglePixelBuffer Tests
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_singlepixelbuffer core/wayland/test_singlepixelbuffer.cpp)
target_link_libraries(test_singlepixelbuffer PRIVATE Qt6::Test Qt6::Core Qt6::Gui PhosphorWayland::PhosphorWayland)
add_test(NAME test_singlepixelbuffer COMMAND test_singlepixelbuffer)

# ═══════════════════════════════════════════════════════════════════════════════
# core/ - IdleNotifier Tests
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_idlenotifier core/wayland/test_idlenotifier.cpp)
target_link_libraries(test_idlenotifier PRIVATE Qt6::Test Qt6::Core Qt6::Gui PhosphorWayland::PhosphorWayland)
add_test(NAME test_idlenotifier COMMAND test_idlenotifier)

# ═══════════════════════════════════════════════════════════════════════════════
# core/ - ToplevelDrag Tests
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_topleveldrag core/wayland/test_topleveldrag.cpp)
target_link_libraries(test_topleveldrag PRIVATE Qt6::Test Qt6::Core Qt6::Gui PhosphorWayland::PhosphorWayland)
add_test(NAME test_topleveldrag COMMAND test_topleveldrag)

# ═══════════════════════════════════════════════════════════════════════════════
# core/ - ForeignToplevel Tests (wlr-foreign-toplevel-management offscreen)
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_foreigntoplevel core/wayland/test_foreigntoplevel.cpp)
target_link_libraries(test_foreigntoplevel PRIVATE Qt6::Test Qt6::Core Qt6::Gui PhosphorWayland::PhosphorWayland)
add_test(NAME test_foreigntoplevel COMMAND test_foreigntoplevel)

# ═══════════════════════════════════════════════════════════════════════════════
# core/ - Support Report Tests (redaction, generation)
# ═══════════════════════════════════════════════════════════════════════════════
p_add_test(test_support_report core/test_support_report.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# core/ - Zone Detection Tests (layout, service)
# ═══════════════════════════════════════════════════════════════════════════════
p_add_test(test_zone_detection_layout core/zones/test_zone_detection_layout.cpp)
p_add_test(test_zone_detector_overlap core/zones/test_zone_detector_overlap.cpp)

add_executable(test_zone_detection_service core/zones/test_zone_detection_service.cpp)
target_link_libraries(test_zone_detection_service PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_zone_detection_service COMMAND test_zone_detection_service)

# ═══════════════════════════════════════════════════════════════════════════════
# autotile/ - Per-Screen Config Tests
# ═══════════════════════════════════════════════════════════════════════════════
p_add_test(test_per_screen_config autotile/engine/test_per_screen_config.cpp)
target_compile_definitions(test_per_screen_config PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")

# ═══════════════════════════════════════════════════════════════════════════════
# autotile/ - Navigation Controller Tests
# ═══════════════════════════════════════════════════════════════════════════════
p_add_test(test_navigation_controller autotile/navigation/test_navigation_controller.cpp)
p_add_test(test_snap_cross_surface snap/test_snap_cross_surface.cpp)
p_add_test(test_snap_state_class_mutation snap/test_snap_state_class_mutation.cpp)

# Cross-surface navigation exercises cross-output handoff, which needs real
# output geometry — compile in the FakeScreenProvider (like the WTA tests).
add_executable(test_navigation_cross_surface
               autotile/navigation/test_navigation_cross_surface.cpp
               # Q_OBJECT fake — listed so AUTOMOC generates its metaobject.
               helpers/AutotileFakes.h
               ${CMAKE_SOURCE_DIR}/libs/phosphor-screens/tests/FakeScreenProvider.cpp)
target_include_directories(test_navigation_cross_surface
               PRIVATE ${CMAKE_SOURCE_DIR}/libs/phosphor-screens/tests)
target_link_libraries(test_navigation_cross_surface PRIVATE Qt6::Test Qt6::Core plasmazones_core)
add_test(NAME test_navigation_cross_surface COMMAND test_navigation_cross_surface)

# Real-retile cross-output test: drives the actual tile pipeline, so it needs
# the FakeScreenProvider AND the bundled Luau algorithms (P_SOURCE_DIR).
add_executable(test_navigation_retile
               autotile/navigation/test_navigation_retile.cpp
               ${CMAKE_SOURCE_DIR}/libs/phosphor-screens/tests/FakeScreenProvider.cpp)
target_include_directories(test_navigation_retile
               PRIVATE ${CMAKE_SOURCE_DIR}/libs/phosphor-screens/tests)
target_compile_definitions(test_navigation_retile PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
target_link_libraries(test_navigation_retile PRIVATE Qt6::Test Qt6::Core plasmazones_core)
add_test(NAME test_navigation_retile COMMAND test_navigation_retile)

add_executable(test_autotile_focus_retile
               autotile/behavior/test_autotile_focus_retile.cpp
               ${CMAKE_SOURCE_DIR}/libs/phosphor-screens/tests/FakeScreenProvider.cpp)
target_include_directories(test_autotile_focus_retile
               PRIVATE ${CMAKE_SOURCE_DIR}/libs/phosphor-screens/tests)
target_compile_definitions(test_autotile_focus_retile PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
target_link_libraries(test_autotile_focus_retile PRIVATE Qt6::Test Qt6::Core plasmazones_core)
add_test(NAME test_autotile_focus_retile COMMAND test_autotile_focus_retile)

# ═══════════════════════════════════════════════════════════════════════════════
# autotile/ - Engine Settings Tests (replaces deleted test_settings_bridge)
# ═══════════════════════════════════════════════════════════════════════════════
p_add_test(test_engine_settings autotile/engine/test_engine_settings.cpp)
target_compile_definitions(test_engine_settings PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")

# ═══════════════════════════════════════════════════════════════════════════════
# ui/ - Zone Shader Item Tests - need Qt6::Quick + plasmazones_rendering
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_zone_shader_item ui/zones/test_zone_shader_item.cpp)
target_link_libraries(test_zone_shader_item PRIVATE Qt6::Test Qt6::Core Qt6::Quick Qt6::GuiPrivate plasmazones_rendering)
add_test(NAME test_zone_shader_item COMMAND test_zone_shader_item)

# Pins the shared ZonePreview's per-zone vs whole-card highlight split. The
# zone selector's hit-tested `selectedZoneIndex` is only observable if the
# singled-out zone renders differently from its siblings, while the picker / OSD
# consumers rely on card-level state lighting every zone. Both halves live in
# one expression, so a regression in either is silent at runtime.
add_executable(test_zone_preview_highlight ui/zones/test_zone_preview_highlight.cpp)
target_link_libraries(test_zone_preview_highlight PRIVATE Qt6::Test Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick plasmazones_shared_qmlplugin)
add_test(NAME test_zone_preview_highlight COMMAND test_zone_preview_highlight)

# Component-load canary for the shared QML module: "X is not a type" at app
# runtime hides the nested cause; this fails with the real error instead.
# plasmazones_rendering supplies ZoneShaderItem, which the test registers under
# "PlasmaZones 1.0" (mirroring the daemon/editor composition roots) so
# ZoneShaderRenderer's `import PlasmaZones` resolves.
add_executable(test_shared_module_load ui/test_shared_module_load.cpp)
target_link_libraries(test_shared_module_load PRIVATE Qt6::Test Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick plasmazones_shared_qmlplugin plasmazones_rendering)
add_test(NAME test_shared_module_load COMMAND test_shared_module_load)

# Pins the C++/QML contract the zone selector's cursor hit test depends on:
# zone delegates discoverable by objectName, exposing their model index, with
# rendered geometry that tracks LayoutCard's aspect-ratio letterboxing. All of
# it fails silently at runtime — the highlight just stops following the cursor.
add_executable(test_zone_selector_hit_geometry ui/zones/test_zone_selector_hit_geometry.cpp)
target_link_libraries(test_zone_selector_hit_geometry PRIVATE Qt6::Test Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick plasmazones_shared_qmlplugin plasmazones_core)
add_test(NAME test_zone_selector_hit_geometry COMMAND test_zone_selector_hit_geometry)

add_executable(test_surface_shader_item ui/zones/test_surface_shader_item.cpp)
target_link_libraries(test_surface_shader_item PRIVATE Qt6::Test Qt6::Core Qt6::Gui Qt6::Quick Qt6::GuiPrivate plasmazones_rendering)
add_test(NAME test_surface_shader_item COMMAND test_surface_shader_item)

# Pure-data byte-layout test for the zone UBO extension. No GPU / scene graph
# required - guards against silent C++/GLSL UBO drift after a struct refactor.
add_executable(test_zone_uniform_extension ui/zones/test_zone_uniform_extension.cpp)
target_link_libraries(test_zone_uniform_extension PRIVATE Qt6::Test Qt6::Core Qt6::GuiPrivate plasmazones_rendering)
add_test(NAME test_zone_uniform_extension COMMAND test_zone_uniform_extension)

# Bake-check the VERTEX stage of every daemon-eligible animation pack (plus
# shared/animation.vert) against ShaderCompiler so a qsb-incompatible source
# change (e.g. accidentally re-introducing default-block uniforms in
# #version 450) fails CI instead of silently breaking the daemon's
# overlay-surface animation path. Pinned by the canonical UBO declaration in
# data/animations/shared/animation_uniforms.glsl. Compositor-only packs are
# excluded (their source is kwin classic-GL by design) and covered by
# test_animation_shader_kwin_bake; fragment-stage coverage lives in
# test_animation_shader_preamble_bake.
add_executable(test_animation_shader_bake ui/shaders/test_animation_shader_bake.cpp)
target_link_libraries(test_animation_shader_bake PRIVATE Qt6::Test Qt6::Core Qt6::GuiPrivate Qt6::ShaderToolsPrivate PhosphorAnimation::PhosphorAnimation PhosphorRendering::PhosphorRendering)
target_compile_definitions(test_animation_shader_bake PRIVATE "PLASMAZONES_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
add_test(NAME test_animation_shader_bake COMMAND test_animation_shader_bake)

# Pins ShaderIncludeResolver's #line bracketing + source-string legend so a
# glslang / driver diagnostic maps back to the author's file:line instead of a
# line in the flattened include blob.
add_executable(test_shader_include_resolver ui/shaders/test_shader_include_resolver.cpp)
target_link_libraries(test_shader_include_resolver PRIVATE Qt6::Test Qt6::Core Qt6::Gui PhosphorShaders::PhosphorShaders)
add_test(NAME test_shader_include_resolver COMMAND test_shader_include_resolver)

# Pins the T1.4 entry-point harness: main()/entry-function detection
# (comment-stripped, definition-not-call) and composeEntryPoint dispatch
# (author main() wins, else wrap the first matching entry, else pass through).
add_executable(test_shader_entry_point ui/shaders/test_shader_entry_point.cpp)
target_link_libraries(test_shader_entry_point PRIVATE Qt6::Test Qt6::Core Qt6::Gui PhosphorShaders::PhosphorShaders)
target_compile_definitions(test_shader_entry_point PRIVATE "PLASMAZONES_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
add_test(NAME test_shader_entry_point COMMAND test_shader_entry_point)

# End-to-end: an entry-only zone shader (author writes only pZone/pImage)
# assembles through the harness scaffold and bakes against the real zone UBO,
# and a traditional main() pack is passed through unchanged (T1.4 scope B).
add_executable(test_zone_entry_scaffold ui/zones/test_zone_entry_scaffold.cpp)
target_link_libraries(test_zone_entry_scaffold PRIVATE Qt6::Test Qt6::Core Qt6::GuiPrivate Qt6::ShaderToolsPrivate plasmazones_rendering PhosphorRendering::PhosphorRendering PhosphorShaders::PhosphorShaders)
target_compile_definitions(test_zone_entry_scaffold PRIVATE "PLASMAZONES_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
add_test(NAME test_zone_entry_scaffold COMMAND test_zone_entry_scaffold)

# Pins buildParamPreamble's generated `#define p_<id> <accessor>` block:
# declaration-order auto-slotting (must match the lane the runtime uploads to),
# independent scalar/color/image pools, and skip-don't-break on bad input.
add_executable(test_shader_param_preamble ui/shaders/test_shader_param_preamble.cpp)
target_link_libraries(test_shader_param_preamble PRIVATE Qt6::Test Qt6::Core Qt6::Gui PhosphorShaders::PhosphorShaders)
add_test(NAME test_shader_param_preamble COMMAND test_shader_param_preamble)

# End-to-end: every daemon-eligible built-in animation shader must still bake
# once its generated `#define p_<id> ...` preamble is spliced in — proving the
# generated accessors compile against the real animation UBO. Compositor-only
# packs are excluded here; test_animation_shader_kwin_bake covers them.
add_executable(test_animation_shader_preamble_bake ui/shaders/test_animation_shader_preamble_bake.cpp)
target_link_libraries(test_animation_shader_preamble_bake PRIVATE Qt6::Test Qt6::Core Qt6::GuiPrivate Qt6::ShaderToolsPrivate PhosphorAnimation::PhosphorAnimation PhosphorRendering::PhosphorRendering PhosphorShaders::PhosphorShaders)
target_compile_definitions(test_animation_shader_preamble_bake PRIVATE "PLASMAZONES_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
add_test(NAME test_animation_shader_preamble_bake COMMAND test_animation_shader_preamble_bake)

# Bakes the KWIN-PATH variant (#define PLASMAZONES_KWIN) of every animation pack
# through a real offscreen OpenGL context — the branch qsb/SPIR-V rejects and the
# daemon preamble bake therefore never compiles. Catches GLSL errors in the KWin
# branch (getFromColor/getToColor, surfaceColor's kwin flip, customColors/iFrame
# usage). Skips when no desktop-GL >= 4.5 context is available (headless CI).
add_executable(test_animation_shader_kwin_bake ui/shaders/test_animation_shader_kwin_bake.cpp)
target_link_libraries(test_animation_shader_kwin_bake PRIVATE Qt6::Test Qt6::Core Qt6::Gui PhosphorAnimation::PhosphorAnimation PhosphorShaders::PhosphorShaders)
target_compile_definitions(test_animation_shader_kwin_bake PRIVATE "PLASMAZONES_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
add_test(NAME test_animation_shader_kwin_bake COMMAND test_animation_shader_kwin_bake)

# End-to-end: an entry-only animation shader (author writes only pTransition,
# or pIn+pOut) assembles through the harness scaffold and bakes against the
# real animation UBO; lone pIn falls through; traditional main() passes (T1.5).
add_executable(test_animation_entry_scaffold ui/shaders/test_animation_entry_scaffold.cpp)
target_link_libraries(test_animation_entry_scaffold PRIVATE Qt6::Test Qt6::Core Qt6::GuiPrivate Qt6::ShaderToolsPrivate PhosphorAnimation::PhosphorAnimation PhosphorRendering::PhosphorRendering PhosphorShaders::PhosphorShaders)
target_compile_definitions(test_animation_entry_scaffold PRIVATE "PLASMAZONES_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
add_test(NAME test_animation_entry_scaffold COMMAND test_animation_entry_scaffold)

# Cross-checks that every animation shader's `#define <paramId>
# customParams[N].xyzw` (or customColors[N]) line agrees with the
# allocation order an in-order walk of metadata.json's `parameters[]`
# array would produce - i.e. the same allocation rule
# AnimationShaderRegistry::translateAnimationParams uses at runtime.
# Catches author-side wiring drift at CI time so a misordered pack
# can't ship and silently deliver wrong values.
add_executable(test_animation_shader_param_wiring ui/shaders/test_animation_shader_param_wiring.cpp)
target_link_libraries(test_animation_shader_param_wiring PRIVATE Qt6::Test Qt6::Core)
target_compile_definitions(test_animation_shader_param_wiring PRIVATE "PLASMAZONES_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
add_test(NAME test_animation_shader_param_wiring COMMAND test_animation_shader_param_wiring)

# Pure-geometry test for the kwin-effect's anchor-uniform branch
# (ShaderInternal::computeAnchorUniforms) — pins anchor-extent vs
# surface-extent (fboExtent: "surface") iResolution/iAnchorPosInFbo math.
add_executable(test_anchor_uniforms ui/effect/test_anchor_uniforms.cpp)
target_link_libraries(test_anchor_uniforms PRIVATE Qt6::Test Qt6::Core Qt6::Gui PhosphorAnimation::PhosphorAnimation)
# Resolve <plasmazoneseffect/shader_internal.h> without a brittle relative
# path, and let CMake track the header as a dependency of the test.
target_include_directories(test_anchor_uniforms PRIVATE ${CMAKE_SOURCE_DIR}/kwin-effect)
add_test(NAME test_anchor_uniforms COMMAND test_anchor_uniforms)

# Pure-timing test for the compositor's shader clock
# (ShaderInternal::resolveTransitionLifetimeMs / easeProgress) — pins the
# duration envelope (the only bound on a hand-edited profile or an undamped
# spring), the stateless/stateful split, the dt cap, the peek contract, and the
# deliberate overshoot passthrough. Same header-only reach as the anchor test.
add_executable(test_shader_timing ui/effect/test_shader_timing.cpp)
target_link_libraries(test_shader_timing PRIVATE Qt6::Test Qt6::Core Qt6::Gui PhosphorAnimation::PhosphorAnimation)
target_include_directories(test_shader_timing PRIVATE ${CMAKE_SOURCE_DIR}/kwin-effect)
add_test(NAME test_shader_timing COMMAND test_shader_timing)

# Pure-geometry test for per-VS wallpaper cropping (PR #333). Pins the C++
# cover-fit math against hand-worked expected rects so it can't silently
# drift from the GLSL wallpaperUv helper it mirrors.
add_executable(test_wallpaper_crop ui/test_wallpaper_crop.cpp)
target_link_libraries(test_wallpaper_crop PRIVATE Qt6::Test Qt6::Core Qt6::Gui PhosphorShaders::PhosphorShaders)
add_test(NAME test_wallpaper_crop COMMAND test_wallpaper_crop)

# ═══════════════════════════════════════════════════════════════════════════════
# ui/ - Overlay Helpers Tests - need Qt6::Quick + QGuiApplication
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_overlay_helpers ui/test_overlay_helpers.cpp)
target_link_libraries(test_overlay_helpers PRIVATE Qt6::Test Qt6::Core Qt6::Quick plasmazones_core)
add_test(NAME test_overlay_helpers COMMAND test_overlay_helpers)

# ═══════════════════════════════════════════════════════════════════════════════
# dbus/ - D-Bus API Tests (convenience, schema, compositor bridge)
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_wta_convenience dbus/test_wta_convenience.cpp
               ${CMAKE_SOURCE_DIR}/libs/phosphor-screens/tests/FakeScreenProvider.cpp)
target_include_directories(test_wta_convenience PRIVATE ${CMAKE_SOURCE_DIR}/libs/phosphor-screens/tests)
target_link_libraries(test_wta_convenience PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_wta_convenience COMMAND test_wta_convenience)

add_executable(test_wta_capture_guards dbus/test_wta_capture_guards.cpp
               ${CMAKE_SOURCE_DIR}/libs/phosphor-screens/tests/FakeScreenProvider.cpp)
target_include_directories(test_wta_capture_guards PRIVATE ${CMAKE_SOURCE_DIR}/libs/phosphor-screens/tests)
target_link_libraries(test_wta_capture_guards PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_wta_capture_guards COMMAND test_wta_capture_guards)

add_executable(test_wta_reactive_metadata dbus/test_wta_reactive_metadata.cpp)
target_link_libraries(test_wta_reactive_metadata PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_wta_reactive_metadata COMMAND test_wta_reactive_metadata)

add_executable(test_settings_schema dbus/test_settings_schema.cpp)
target_link_libraries(test_settings_schema PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_settings_schema COMMAND test_settings_schema)

# Regression guard for PR #324: the editor startup batches 8 gap/overlay
# keys into one SettingsAdaptor::getSettings call. If the daemon-side
# batch drops known keys or propagates unknown keys, the editor silently
# falls back to defaults.
add_executable(test_settings_adaptor_batch dbus/test_settings_adaptor_batch.cpp)
target_link_libraries(test_settings_adaptor_batch PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_settings_adaptor_batch COMMAND test_settings_adaptor_batch)

add_executable(test_compositor_bridge dbus/test_compositor_bridge.cpp)
target_link_libraries(test_compositor_bridge PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_compositor_bridge COMMAND test_compositor_bridge)

# XML ↔ adaptor contract tripwire: the dbus/*.xml files are the installed
# public contract while the adaptors are handwritten — this compares each
# adaptor's staticMetaObject (bus-exposed slots/invokables, signals,
# properties) against its XML so drift fails CI instead of shipping.
add_executable(test_dbus_contract_sync dbus/test_dbus_contract_sync.cpp)
target_compile_definitions(test_dbus_contract_sync
                           PRIVATE PLASMAZONES_DBUS_XML_DIR="${CMAKE_SOURCE_DIR}/dbus")
target_link_libraries(test_dbus_contract_sync PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_dbus_contract_sync COMMAND test_dbus_contract_sync)

# Every setting the KWin effect fetches must be present in SettingsAdaptor's
# hand-maintained getter registry. getSetting resolves keys through that map, NOT
# through Qt property reflection, so a forgotten REGISTER_*_SETTING line ships a
# feature that is dead on the effect side while every other layer looks complete.
# That has happened; this is the tripwire. The expected keys are scraped from the
# effect's own source rather than duplicated, so the test cannot drift.
add_executable(test_settings_registry_contract dbus/test_settings_registry_contract.cpp)
target_compile_definitions(test_settings_registry_contract
                           PRIVATE PLASMAZONES_EFFECT_SRC_DIR="${CMAKE_SOURCE_DIR}/kwin-effect"
                                   PLASMAZONES_EDITOR_SRC_DIR="${CMAKE_SOURCE_DIR}/src/editor"
                                   PLASMAZONES_PROTOCOL_INC_DIR="${CMAKE_SOURCE_DIR}/libs/phosphor-protocol/include")
# PhosphorAnimation: the fixture builds a real PhosphorProfileRegistry, because
# initializeRegistry() gates the motionProfileTree getter behind a non-null one — with
# a null registry the test would report a correctly-registered key as missing.
target_link_libraries(test_settings_registry_contract
                      PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core
                              PhosphorAnimation::PhosphorAnimation)
add_test(NAME test_settings_registry_contract COMMAND test_settings_registry_contract)

# End-to-end adaptor routing test: verifies typed-struct slot dispatch works
# over a real QDBusConnection. Regression guard for the 2026-04-10 resnap
# crash (moc recorded unqualified type names, metatype lookup failed).
add_executable(test_dbus_adaptor_routing dbus/test_dbus_adaptor_routing.cpp)
target_link_libraries(test_dbus_adaptor_routing PRIVATE Qt6::Test Qt6::Core Qt6::DBus
                                                          PhosphorCompositor::PhosphorCompositor)
add_test(NAME test_dbus_adaptor_routing COMMAND test_dbus_adaptor_routing)

# Panel-ready gate for AutotileAdaptor windowOpened queue. Regression guard
# for the stale-VS-geometry race: entries arriving before the first panel
# D-Bus query must be deferred until PhosphorScreens::ScreenManager::panelGeometryReady fires.
add_executable(test_autotile_adaptor_panel_gate dbus/test_autotile_adaptor_panel_gate.cpp)
target_link_libraries(test_autotile_adaptor_panel_gate PRIVATE Qt6::Test Qt6::Core Qt6::DBus
                                                                 plasmazones_core)
add_test(NAME test_autotile_adaptor_panel_gate COMMAND test_autotile_adaptor_panel_gate)

# Truth-table guard for WindowDragAdaptor::computeDragPolicy - pins the
# (snap, autotile, context) matrix so the daemon's drag routing decision
# can't drift relative to the compositor plugin's deleted local caches.
# Regression guard for #310.
add_executable(test_drag_policy dbus/test_drag_policy.cpp)
target_link_libraries(test_drag_policy PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_drag_policy COMMAND test_drag_policy)

# Truth-table guard for resolveActivationActive() - pins the per-tick
# overlay-active decision (toggle latch + deactivation override + the
# always-active gate that scopes deactivation to its UI surface, #249).
add_executable(test_drag_activation dbus/test_drag_activation.cpp)
target_link_libraries(test_drag_activation PRIVATE Qt6::Test Qt6::Core plasmazones_core)
add_test(NAME test_drag_activation COMMAND test_drag_activation)

# Round-trip guard for the AlwaysActive sentinel handling shared by
# TriggerUtils and SnappingBehaviorController (#249). Pins the cap-aware
# merge that prevents the sentinel from being silently truncated when the
# user already holds MAX activation triggers - without this, the
# always-active toggle would quietly fail. Compiles the controller +
# triggerutils sources directly because they live in the plasmazones-settings
# executable rather than plasmazones_core.
add_executable(test_snapping_behavior_controller
    settings/pages/test_snapping_behavior_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/pages/snappingbehaviorcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/utils/triggerutils.cpp
)
target_link_libraries(test_snapping_behavior_controller PRIVATE Qt6::Test Qt6::Core plasmazones_core
    PhosphorControl::PhosphorControl)
add_test(NAME test_snapping_behavior_controller COMMAND test_snapping_behavior_controller)

# Pure string helpers behind AlgorithmService::createNewAlgorithm. Compiled
# directly because they live in the plasmazones-settings executable rather than
# plasmazones_core. Split by surface: the template splice / metadata rewrite
# here, the shapes it refuses in _rejects, the blank scaffold next door. They
# share no fixture beyond a copyright line.
#
# All three targets compile algorithmscaffold.cpp, which reads AutotileConstants.h's
# ScriptedDefaultMaxWindows and DefaultSplitRatio so a generated algorithm
# matches the engine's own fallbacks - header-only constants, no .so coupling
# beyond the include path. Linking PhosphorTiles rather than hand-listing paths
# lets its interface carry the transitive PhosphorLayoutApi include it needs.
add_executable(test_algorithm_scaffold
    settings/services/test_algorithm_scaffold.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/services/algorithmscaffold.cpp
)
target_link_libraries(test_algorithm_scaffold PRIVATE Qt6::Test Qt6::Core PhosphorTiles::PhosphorTiles)
target_compile_definitions(test_algorithm_scaffold PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
add_test(NAME test_algorithm_scaffold COMMAND test_algorithm_scaffold)

# The one AlgorithmService surface that feeds the scaffold's
# already-sanitized-caller contract from the translation catalog: the duplicate
# name suffix. Drives the real service against a real loader + registry over an
# isolated XDG root, so it needs the service and scaffold TUs (both live in the
# plasmazones-settings executable) plus PhosphorTiles for the Luau loader.
add_executable(test_algorithm_copy_suffix
    settings/services/test_algorithm_copy_suffix.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/services/algorithmservice.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/services/algorithmscaffold.cpp
)
target_link_libraries(test_algorithm_copy_suffix PRIVATE Qt6::Test Qt6::Core plasmazones_core
    PhosphorTiles::PhosphorTiles)
add_test(NAME test_algorithm_copy_suffix COMMAND test_algorithm_copy_suffix)

add_executable(test_algorithm_scaffold_rejects
    settings/services/test_algorithm_scaffold_rejects.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/services/algorithmscaffold.cpp
)
target_link_libraries(test_algorithm_scaffold_rejects PRIVATE Qt6::Test Qt6::Core PhosphorTiles::PhosphorTiles)
add_test(NAME test_algorithm_scaffold_rejects COMMAND test_algorithm_scaffold_rejects)

add_executable(test_algorithm_blank_scaffold
    settings/services/test_algorithm_blank_scaffold.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/services/algorithmscaffold.cpp
)
target_link_libraries(test_algorithm_blank_scaffold PRIVATE Qt6::Test Qt6::Core PhosphorTiles::PhosphorTiles)
add_test(NAME test_algorithm_blank_scaffold COMMAND test_algorithm_blank_scaffold)

# clampName is header-only in src/core/constants.h, so there is no source to
# compile in beyond the test itself. plasmazones_core supplies LayoutAdaptor
# (src/dbus/layoutadaptor.cpp), whose duplicateLayout clamp the test drives, and
# carries the include chain constants.h pulls (PhosphorEngine, PhosphorLayoutApi,
# PhosphorZones). PhosphorZones is named for the LayoutRegistry and Layout
# symbols the duplicate cases construct — duplicateNameSuffix() alone would need
# no link, being inline in the header. Qt6::DBus is LayoutAdaptor's own base
# class (QDBusAbstractAdaptor); the test calls the slot directly, never a bus.
add_executable(test_layoutadaptor_nameclamp
    settings/test_layoutadaptor_nameclamp.cpp
)
target_link_libraries(test_layoutadaptor_nameclamp PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core
    PhosphorZones::PhosphorZones)
add_test(NAME test_layoutadaptor_nameclamp COMMAND test_layoutadaptor_nameclamp)

add_executable(test_window_appearance_controller
    settings/pages/test_window_appearance_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/pages/windowappearancecontroller.cpp
)
target_link_libraries(test_window_appearance_controller PRIVATE Qt6::Test Qt6::Core plasmazones_core
    PhosphorControl::PhosphorControl PhosphorRules::PhosphorRules)
add_test(NAME test_window_appearance_controller COMMAND test_window_appearance_controller)

add_executable(test_tiling_algorithm_controller
    settings/pages/test_tiling_algorithm_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/pages/tilingalgorithmcontroller.cpp
)
target_link_libraries(test_tiling_algorithm_controller PRIVATE Qt6::Test Qt6::Core plasmazones_core
    PhosphorControl::PhosphorControl PhosphorRules::PhosphorRules)
target_compile_definitions(test_tiling_algorithm_controller PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
add_test(NAME test_tiling_algorithm_controller COMMAND test_tiling_algorithm_controller)

# Animations-controller sources shared by the four test executables below
# (page controller / motion sets / presets / shader overrides). One list so a
# new controller source file can't silently miss one of the four.
set(_animations_ctrl_SRCS
    ${CMAKE_SOURCE_DIR}/src/settings/pages/animationspagecontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/pages/animationspagecontroller_shaders.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/pages/animationspagecontroller_overrides.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/pages/animationspagecontroller_paths.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/services/shaderpackinstaller.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/stores/animationpresetlibrary.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/stores/shadersetstore.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/services/motionsetdomain.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/utils/animationfileutils.cpp
)

# Phase 0 of the animation-settings UI rework: AnimationsPageController
# round-trips per-event override files in `<userProfilesDir>/<path>.json`,
# walks the ProfilePaths inheritance chain for resolvedProfile(), and
# surfaces the built-in taxonomy as section-grouped lists for the QML
# drilldown. Compiles the controller source directly (it lives in the
# plasmazones-settings executable, not plasmazones_core).
add_executable(test_animations_page_controller
    settings/pages/test_animations_page_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/pages/animationpagescope.cpp
    ${_animations_ctrl_SRCS}
)
# simpleScopeCoversEverySimplePageCard reads AnimationsSimplePage.qml from the
# source tree to prove the C++ scope roots still cover every card the page hosts.
target_compile_definitions(test_animations_page_controller
    PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\""
)
target_link_libraries(test_animations_page_controller
    PRIVATE
        Qt6::Test
        Qt6::Core
        Qt6::Gui
        PhosphorAnimation::PhosphorAnimation
        PhosphorControl::PhosphorControl
        plasmazones_core
)
add_test(NAME test_animations_page_controller COMMAND test_animations_page_controller)

# Companion test executable: motion-set + pending-changes coverage.
add_executable(test_animations_motion_sets
    settings/pages/test_animations_motion_sets.cpp
    ${_animations_ctrl_SRCS}
)
target_link_libraries(test_animations_motion_sets
    PRIVATE
        Qt6::Test
        Qt6::Core
        Qt6::Gui
        PhosphorAnimation::PhosphorAnimation
        PhosphorControl::PhosphorControl
        plasmazones_core
)
add_test(NAME test_animations_motion_sets COMMAND test_animations_motion_sets)

# Companion test executable: the user-preset library (AnimationPresetLibrary).
# Split from test_animations_motion_sets because presets and sets are separate
# sub-services of the controller.
add_executable(test_animations_presets
    settings/pages/test_animations_presets.cpp
    ${_animations_ctrl_SRCS}
)
target_link_libraries(test_animations_presets
    PRIVATE
        Qt6::Test
        Qt6::Core
        Qt6::Gui
        PhosphorAnimation::PhosphorAnimation
        PhosphorControl::PhosphorControl
        plasmazones_core
)
add_test(NAME test_animations_presets COMMAND test_animations_presets)

# Companion test executable: shader-override end-to-end coverage.
add_executable(test_animations_shader_overrides
    settings/pages/test_animations_shader_overrides.cpp
    ${_animations_ctrl_SRCS}
)
target_link_libraries(test_animations_shader_overrides
    PRIVATE
        Qt6::Test
        Qt6::Core
        Qt6::Gui
        PhosphorAnimation::PhosphorAnimation
        PhosphorControl::PhosphorControl
        plasmazones_core
)
target_compile_definitions(test_animations_shader_overrides PRIVATE "PLASMAZONES_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
add_test(NAME test_animations_shader_overrides COMMAND test_animations_shader_overrides)

# Decoration page (per-surface DecorationProfileTree editor): reader /
# mutator coverage for DecorationPageController — chain resolution,
# override engage/clear, per-pack parameter merge/prune, subtree ops and
# profilesChanged reload propagation. Compiles the controller sources
# directly (they live in the plasmazones-settings executable, not
# plasmazones_core); the split _paths / _browser / _sets TUs carry
# parentChain(), the shader-browser bridge and the set-store wiring.
#
# One source list, shared by the page-controller test and the decoration-set
# test below, so a new controller TU can't silently miss one of the two.
set(_decoration_ctrl_SRCS
    ${CMAKE_SOURCE_DIR}/src/settings/pages/decorationpagecontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/pages/decorationpagecontroller_paths.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/pages/decorationpagecontroller_browser.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/pages/decorationpagecontroller_sets.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/stores/shadersetstore.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/utils/animationfileutils.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/services/shaderpackinstaller.cpp
)

# decorationpagecontroller_sets.cpp carries the same GCC-only LTO
# suppression it gets in src/settings/CMakeLists.txt (see the full
# rationale there): source-file properties are directory-scoped, so the
# settings dir's suppression does not reach these consuming targets,
# and a Release+LTO build would re-emit the GCC 16 std::optional
# maybe-uninitialized false positive from the test build.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(${CMAKE_SOURCE_DIR}/src/settings/pages/decorationpagecontroller_sets.cpp PROPERTIES
        COMPILE_OPTIONS "-fno-lto;-Wno-maybe-uninitialized")
endif()
add_executable(test_decorationpagecontroller
    settings/pages/test_decorationpagecontroller.cpp
    ${_decoration_ctrl_SRCS}
)
target_link_libraries(test_decorationpagecontroller
    PRIVATE
        Qt6::Test
        Qt6::Core
        Qt6::Gui
        PhosphorSurface::PhosphorSurface
        PhosphorControl::PhosphorControl
        plasmazones_core
)
add_test(NAME test_decorationpagecontroller COMMAND test_decorationpagecontroller)

# Companion test executable: decoration-set CRUD over the shared
# ShaderSetStore (save / list / apply / remove / update / export / import,
# the active-flag containment semantics, and the format-version gate). Mirrors
# the motion side's test_animations_motion_sets.
add_executable(test_decoration_sets
    settings/stores/test_decoration_sets.cpp
    ${_decoration_ctrl_SRCS}
)
target_link_libraries(test_decoration_sets
    PRIVATE
        Qt6::Test
        Qt6::Core
        Qt6::Gui
        PhosphorSurface::PhosphorSurface
        PhosphorControl::PhosphorControl
        plasmazones_core
)
add_test(NAME test_decoration_sets COMMAND test_decoration_sets)

# The refusal half of the decoration-set suite. It shares the sandbox guard and
# the payload builders in helpers/DecorationSetHelpers.h with the round-trip
# half.
add_executable(test_decoration_sets_validation
    settings/stores/test_decoration_sets_validation.cpp
    ${_decoration_ctrl_SRCS}
)
target_link_libraries(test_decoration_sets_validation
    PRIVATE
        Qt6::Test
        Qt6::Core
        Qt6::Gui
        PhosphorSurface::PhosphorSurface
        PhosphorControl::PhosphorControl
        plasmazones_core
)
add_test(NAME test_decoration_sets_validation COMMAND test_decoration_sets_validation)

# Settings profiles: the ProfileStore delta / inheritance-resolution engine
# behind ProfilePageController::bridge. Driven with stub config closures, so it
# links only the store itself (no PhosphorConfig::Store needed).
add_executable(test_profilestore
    settings/stores/test_profilestore.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/stores/profilestore.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/stores/profilestore_diff.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/stores/profilestore_revert.cpp
)
target_link_libraries(test_profilestore
    PRIVATE
        Qt6::Test
        Qt6::Core
        Qt6::Gui
        plasmazones_core
        PhosphorRules::PhosphorRules
)
add_test(NAME test_profilestore COMMAND test_profilestore)

# Rules page (Phase 4 of the window-rule refactor): RuleModel
# derives SectionRole / summaries from a rule's shape; RuleController
# stages CRUD by UUID and builds the monitor overview. Compiles the model /
# controller sources directly (they live in the plasmazones-settings
# executable, not plasmazones_core).
add_executable(test_rule_model
    settings/rules/test_rule_model.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/rules/rulemodel.cpp
    # rulemodel's action summary now shares its boolean polarity phrasing with
    # the editor via RuleAuthoring::boolActionStateLabel.
    ${CMAKE_SOURCE_DIR}/src/settings/rules/ruleauthoring.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/rules/ruleauthoring_actions.cpp
)
target_link_libraries(test_rule_model
    PRIVATE
        Qt6::Test
        Qt6::Core
        PhosphorRules::PhosphorRules
        plasmazones_core
)
add_test(NAME test_rule_model COMMAND test_rule_model)

add_executable(test_rule_controller
    settings/rules/test_rule_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/rules/rulemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/rules/rulecontroller.cpp
    # Read-only / projection methods (sections, rulesSnapshot, monitorOverview,
    # author surfaces, validation). The controller's class definition spans
    # this TU and rulecontroller.cpp.
    ${CMAKE_SOURCE_DIR}/src/settings/rules/rulecontroller_views.cpp
    # Label-lookup setters + wiring-completion gate. The controller's class
    # definition spans this TU and rulecontroller.cpp.
    ${CMAKE_SOURCE_DIR}/src/settings/rules/rulecontroller_lookups.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/rules/rulecontroller_baseline.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/rules/ruleauthoring.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/rules/ruleauthoring_actions.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/rules/ruletemplates.cpp
)
target_link_libraries(test_rule_controller
    PRIVATE
        Qt6::Test
        Qt6::Core
        Qt6::DBus
        PhosphorRules::PhosphorRules
        PhosphorControl::PhosphorControl
        plasmazones_core
)
add_test(NAME test_rule_controller COMMAND test_rule_controller)

# Overview/vocabulary half of the controller suite (monitorOverview projections,
# vocabulary tokens, templates, action metadata). Same source set as
# test_rule_controller; the controller's class definition spans several TUs.
add_executable(test_rule_controller_overview
    settings/rules/test_rule_controller_overview.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/rules/rulemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/rules/rulecontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/rules/rulecontroller_views.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/rules/rulecontroller_lookups.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/rules/rulecontroller_baseline.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/rules/ruleauthoring.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/rules/ruleauthoring_actions.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/rules/ruletemplates.cpp
)
target_link_libraries(test_rule_controller_overview
    PRIVATE
        Qt6::Test
        Qt6::Core
        Qt6::DBus
        PhosphorRules::PhosphorRules
        PhosphorControl::PhosphorControl
        plasmazones_core
)
add_test(NAME test_rule_controller_overview COMMAND test_rule_controller_overview)

# Validation methods on D-Bus struct types - pins the accept/reject table
# for validationError() so cross-field invariants (DragOutcome action range,
# ApplySnap zoneId, TileRequestEntry tiled/floating size semantics, etc.)
# don't drift as producer code evolves.
add_executable(test_dbus_validation dbus/test_dbus_validation.cpp)
target_link_libraries(test_dbus_validation PRIVATE Qt6::Test Qt6::Core Qt6::DBus PhosphorCompositor::PhosphorCompositor)
add_test(NAME test_dbus_validation COMMAND test_dbus_validation)

# LayoutAdaptor signal contract test (Phase 1.2 of refactor/dbus-performance).
# Pins the rule: property mutations emit layoutChanged only, NEVER
# layoutListChanged - the list hasn't changed.
add_executable(test_layout_adaptor_signals dbus/test_layout_adaptor_signals.cpp)
target_link_libraries(test_layout_adaptor_signals PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core
                      PhosphorTiles::PhosphorTiles)
add_test(NAME test_layout_adaptor_signals COMMAND test_layout_adaptor_signals)

# Micro-benchmarks for D-Bus adaptor hot paths. Drives the
# refactor/dbus-performance branch (SettingsAdaptor value-equality guard,
# LayoutAdaptor signal split, JSON cache reuse). Run with
# `ctest -R bench_dbus_adaptors` to capture numbers against
# docs/perf/dbus-baseline.md. NOTE: add_test() puts it in the directory
# TESTS property, so a bare `ctest` run DOES include it (and its verbose
# benchmark output); use `ctest -LE bench` to exclude it — the LABELS
# property below exists exactly for that.
add_executable(bench_dbus_adaptors dbus/bench_dbus_adaptors.cpp)
target_link_libraries(bench_dbus_adaptors PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME bench_dbus_adaptors COMMAND bench_dbus_adaptors)
# LABELS=bench lets CI skip the verbose benchmark output via
# `ctest -LE bench` while still letting `ctest -L bench` select it
# explicitly for the perf-regression run.
set_tests_properties(bench_dbus_adaptors PROPERTIES LABELS "bench")

# ═══════════════════════════════════════════════════════════════════════════════
# compositor-common/ - Shared Library Types Tests
#   test_wire_types: D-Bus wire type signature/roundtrip tests
#   test_floating_cache: FloatingCache + ZoneCache + TriggerParser tests
#   test_autotile_state: AutotileStateHelpers + WindowId tests
#   test_decoration_manager: DecorationManager ownership/veto behavioral spec
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_wire_types compositor-common/test_wire_types.cpp)
target_link_libraries(test_wire_types PRIVATE Qt6::Test PhosphorCompositor::PhosphorCompositor
                                              PhosphorProtocol::PhosphorProtocol)
add_test(NAME test_wire_types COMMAND test_wire_types)

add_executable(test_floating_cache compositor-common/test_floating_cache.cpp)
target_link_libraries(test_floating_cache PRIVATE Qt6::Test PhosphorCompositor::PhosphorCompositor)
add_test(NAME test_floating_cache COMMAND test_floating_cache)

add_executable(test_autotile_state compositor-common/test_autotile_state.cpp)
target_link_libraries(test_autotile_state PRIVATE Qt6::Test PhosphorCompositor::PhosphorCompositor
                                                  PhosphorIdentity::PhosphorIdentity)
add_test(NAME test_autotile_state COMMAND test_autotile_state)

add_executable(test_decoration_manager compositor-common/test_decoration_manager.cpp)
target_link_libraries(test_decoration_manager PRIVATE Qt6::Test PhosphorCompositor::PhosphorCompositor)
add_test(NAME test_decoration_manager COMMAND test_decoration_manager)

# ═══════════════════════════════════════════════════════════════════════════════
# editor/ - Zone z-order
# ═══════════════════════════════════════════════════════════════════════════════
# zOrder is the zone's index in the list, and EditorWindow.qml stacks zones at
# zoneBaseZ + zOrder while DividerManager derives its own z from the zone count.
# A hole or a tie in the zOrder run therefore mis-paints the canvas and breaks
# divider hit-testing, silently. This pins density across every mutation, plus
# the contract that undoing a delete returns the zone to its original height
# rather than to the top. ZoneManager lives in the editor executable rather than
# a library, so its TUs are compiled into the test directly.
add_executable(test_zone_zorder editor/test_zone_zorder.cpp
               ${CMAKE_SOURCE_DIR}/src/editor/services/ZoneManager.cpp
               ${CMAKE_SOURCE_DIR}/src/editor/services/ZoneAutoFiller.cpp
               ${CMAKE_SOURCE_DIR}/src/editor/services/zonemanager/divider.cpp
               ${CMAKE_SOURCE_DIR}/src/editor/services/zonemanager/zorder.cpp
               ${CMAKE_SOURCE_DIR}/src/editor/services/zonemanager/serialization.cpp
               ${CMAKE_SOURCE_DIR}/src/editor/undo/commands/BaseZoneCommand.cpp
               ${CMAKE_SOURCE_DIR}/src/editor/undo/commands/DeleteZoneCommand.cpp)
# Qt6::Gui is explicit: DeleteZoneCommand derives from QUndoCommand. It arrives
# transitively via plasmazones_core today, but that is not a contract.
target_link_libraries(test_zone_zorder PRIVATE Qt6::Test Qt6::Core Qt6::Gui plasmazones_core)
add_test(NAME test_zone_zorder COMMAND test_zone_zorder)

# The editor's D-Bus client has to read the daemon's ANSWER, not just the reply
# type: updateLayout is declared bool and a refusal comes back as an ordinary
# ReplyMessage, which EditorController::saveLayout then treats as a landed write
# (clean undo stack, no unsaved-changes prompt, work lost on close). The client
# hard-binds the daemon's bus name, so the stub registry has to claim it — the
# shared TEST_LAUNCHER's private bus (see the D-Bus isolation block at the
# bottom) guarantees the name is unowned; when the launcher is unavailable the
# test skips itself if a real daemon already owns the name. DBusLayoutService
# lives in the editor executable rather than a library, so its TU is compiled
# into the test directly. ILayoutService.h declares its Q_OBJECT base entirely
# in the header, so it is listed explicitly: AUTOMOC has to see it to emit the
# base's metaobject.
add_executable(test_dbus_layout_service_replies editor/test_dbus_layout_service_replies.cpp
               ${CMAKE_SOURCE_DIR}/src/editor/services/DBusLayoutService.cpp
               ${CMAKE_SOURCE_DIR}/src/editor/services/ILayoutService.h)
target_link_libraries(test_dbus_layout_service_replies
                      PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core
                              PhosphorProtocol::PhosphorProtocol)
add_test(NAME test_dbus_layout_service_replies COMMAND test_dbus_layout_service_replies)

# Decoration page→root scoping helpers (decorationpagescope.{h,cpp}): the pure
# dispatch/prefix/diff logic behind SettingsController's per-page decoration
# Reset/Discard/dirty branches, compiled directly (it lives in the
# plasmazones-settings executable). Pure QString + DecorationProfileTree — no
# controller construction needed.
add_executable(test_decoration_page_scope
    settings/pages/test_decoration_page_scope.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/pages/decorationpagescope.cpp
)

target_include_directories(test_decoration_page_scope PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_decoration_page_scope
    PRIVATE
        Qt6::Test
        Qt6::Core
        Qt6::Gui
        PhosphorSurface::PhosphorSurface
)
add_test(NAME test_decoration_page_scope COMMAND test_decoration_page_scope)

# Animation page→event-root scoping — the same split, same rationale (pure
# QString + ShaderProfileTree, no controller construction). Pins the one
# include/exclude carve-out in the taxonomy (Window Motion vs Dragging), and
# that the condensed simple page scopes to its own cards rather than the whole
# tree.
add_executable(test_animation_page_scope
    settings/pages/test_animation_page_scope.cpp
    ${CMAKE_SOURCE_DIR}/src/settings/pages/animationpagescope.cpp
)
target_include_directories(test_animation_page_scope PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_animation_page_scope
    PRIVATE
        Qt6::Test
        Qt6::Core
        Qt6::Gui
        PhosphorAnimation::PhosphorAnimation
)
add_test(NAME test_animation_page_scope COMMAND test_animation_page_scope)

# Search-catalogue advanced-only flags vs the QML that declares the tier. Reads
# both trees as TEXT (no Qt Quick engine, no page construction), so it needs
# only the source dir — see the file comment for why the flag has to be
# mirrored at all.
add_executable(test_search_catalog_tiers
    settings/test_search_catalog_tiers.cpp
)
target_compile_definitions(test_search_catalog_tiers PRIVATE "P_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
target_link_libraries(test_search_catalog_tiers
    PRIVATE
        Qt6::Test
        Qt6::Core
)
add_test(NAME test_search_catalog_tiers COMMAND test_search_catalog_tiers)

# GPU-gated orientation guard for the per-render-target NDC Y-flip: renders a
# two-stage surface-decoration chain (border + shadow via ShaderEffectSource
# taps, mirroring SurfaceDecoration.qml) on a forced OpenGL scene graph and
# asserts the sampled card content is upright. OPT-IN: it only runs with
# PLASMAZONES_GPU_TESTS=1 in the environment, because it must present on the
# LIVE session (its custom main drops the forced offscreen QPA — an offscreen
# GL grab returns a uniform placeholder and proves nothing) and mapping a real
# window D-Bus-activates the installed plasmazonesd via the KWin effect on a
# developer machine, while CI containers pass the display/GL gates yet never
# get a ready shader pipeline. Without the opt-in it exits as Skipped.
add_executable(test_surface_decoration_orientation
    rendering/test_surface_decoration_orientation.cpp
)
target_compile_definitions(test_surface_decoration_orientation PRIVATE
    PLASMAZONES_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_surface_decoration_orientation
    PRIVATE
        Qt6::Test
        Qt6::DBus
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        plasmazones_rendering
        PhosphorSurface::PhosphorSurface
        PhosphorProtocol::PhosphorProtocol
)
add_test(NAME test_surface_decoration_orientation COMMAND test_surface_decoration_orientation)
# Non-opted-in and headless runs exit with 77 from the test's custom main,
# which ctest reports as SKIPPED rather than Passed — honest status for the
# GPU gate. In-test QSKIPs still exit 0 via qExec (green, Qt's contract).
set_tests_properties(test_surface_decoration_orientation PROPERTIES SKIP_RETURN_CODE 77)

# ═══════════════════════════════════════════════════════════════════════════════
# Headless environment + config isolation
# Apply a shared environment to every test in this directory so new tests added
# above automatically inherit it - a hand-maintained list rotted and left tests
# aborting in CI when the author forgot to append.
#
#   QT_QPA_PLATFORM=offscreen — headless rendering.
#   XDG_{CONFIG,DATA,STATE,CACHE}_HOME — redirect QStandardPaths into a throwaway
#     build-tree directory so tests that exercise config-writing paths
#     (LayoutManager assignments, mode toggles, the Rule store) never
#     mutate the developer's real ~/.config/plasmazones. Without this, running
#     ctest wrote sample-screen rules (e.g. "eDP-1") into the live config.
#
# IMPORTANT: get_property(... PROPERTY TESTS) reads the test list at CMake
# configure time, which means tests added BELOW this line are NOT covered.
# Always add new test targets ABOVE this block — never after it.
# ═══════════════════════════════════════════════════════════════════════════════
set(_pz_test_home "${CMAKE_BINARY_DIR}/test-xdg")
file(MAKE_DIRECTORY
    "${_pz_test_home}/config"
    "${_pz_test_home}/data"
    "${_pz_test_home}/state"
    "${_pz_test_home}/cache")
get_property(_all_tests DIRECTORY . PROPERTY TESTS)
set_tests_properties(${_all_tests} PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${_pz_test_home}/config;XDG_DATA_HOME=${_pz_test_home}/data;XDG_STATE_HOME=${_pz_test_home}/state;XDG_CACHE_HOME=${_pz_test_home}/cache")

# D-Bus isolation: run every test on its own PRIVATE session bus. Several
# tests compile client sources whose async calls target the daemon's real
# well-known name (org.plasmazones) with QDBus's default auto-start flag —
# on a developer machine the installed org.plasmazones.service activation
# file turns each such call into a spawn of the installed plasmazonesd,
# left running after ctest exits. The custom bus config is essential: a
# stock dbus-run-session bus still reads /usr/share/dbus-1/services, so the
# installed daemon would be activated on the private bus too (as a child of
# the private dbus-daemon — it then leaks into the session AND holds the
# test's stdout pipe open, hanging ctest after the test passes). The config
# declares no service directories, so activation is impossible and daemon
# calls fail fast with ServiceUnknown; tests that register their own bus
# names (adaptor routing, layout-service replies) get a clean bus where the
# name is never already owned. Uses the TEST_LAUNCHER target property
# (CMake 3.29+); when dbus-run-session or the CMake version is missing,
# tests run directly on whatever bus the environment provides — the
# pre-isolation status quo.
find_program(_pz_dbus_run_session dbus-run-session)
if(_pz_dbus_run_session AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.29)
    foreach(_pz_test IN LISTS _all_tests)
        # Every add_test in this directory uses NAME == target; the guard
        # keeps a future non-target test entry from breaking the configure.
        if(TARGET ${_pz_test})
            set_target_properties(${_pz_test} PROPERTIES
                TEST_LAUNCHER
                "${_pz_dbus_run_session};--config-file=${CMAKE_CURRENT_SOURCE_DIR}/test-session-bus.conf;--")
        endif()
    endforeach()
else()
    message(STATUS "PlasmaZones: dbus-run-session or CMake >= 3.29 unavailable — "
                   "tests will use the ambient session bus")
endif()
# ─── ABOVE THIS LINE: ADD NEW TESTS — BELOW THIS LINE: env wiring only ───
