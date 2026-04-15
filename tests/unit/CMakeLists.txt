# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# PlasmaZones Unit Tests

# Helper macro for simple tests (Qt6::Test + Qt6::Core + plasmazones_core)
macro(pz_add_test _name _src)
    add_executable(${_name} ${_src})
    target_link_libraries(${_name} PRIVATE Qt6::Test Qt6::Core plasmazones_core)
    add_test(NAME ${_name} COMMAND ${_name})
endmacro()

# ═══════════════════════════════════════════════════════════════════════════════
# core/ — Window Identity Tests
# ═══════════════════════════════════════════════════════════════════════════════
pz_add_test(test_window_identity core/test_window_identity.cpp)
pz_add_test(test_window_registry core/test_window_registry.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# core/ — Window Tracking Tests (snap/unsnap, floating)
# ═══════════════════════════════════════════════════════════════════════════════
pz_add_test(test_window_tracking_snap core/test_window_tracking_snap.cpp)
pz_add_test(test_window_tracking_float core/test_window_tracking_float.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# core/ — Session Persistence Tests
# ═══════════════════════════════════════════════════════════════════════════════
pz_add_test(test_session_persistence core/test_session_persistence.cpp)
pz_add_test(test_session_persistence_layout core/test_session_persistence_layout.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# core/ — Window Tracking Service Tests (lifecycle, session, queries)
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_wts_lifecycle core/test_wts_lifecycle.cpp)
target_link_libraries(test_wts_lifecycle PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_wts_lifecycle COMMAND test_wts_lifecycle)

add_executable(test_wts_session core/test_wts_session.cpp)
target_link_libraries(test_wts_session PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_wts_session COMMAND test_wts_session)

add_executable(test_wts_queries core/test_wts_queries.cpp)
target_link_libraries(test_wts_queries PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_wts_queries COMMAND test_wts_queries)

add_executable(test_wts_clearfloat core/test_wts_clearfloat.cpp)
target_link_libraries(test_wts_clearfloat PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_wts_clearfloat COMMAND test_wts_clearfloat)

add_executable(test_wts_virtual_migration core/test_wts_virtual_migration.cpp)
target_link_libraries(test_wts_virtual_migration PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_wts_virtual_migration COMMAND test_wts_virtual_migration)

add_executable(test_wts_crossmode_float core/test_wts_crossmode_float.cpp)
target_link_libraries(test_wts_crossmode_float PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_wts_crossmode_float COMMAND test_wts_crossmode_float)

add_executable(test_wts_registry_integration core/test_wts_registry_integration.cpp)
target_link_libraries(test_wts_registry_integration PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_wts_registry_integration COMMAND test_wts_registry_integration)

# DirtyMask invariants for delta-persistence (Phase 3 of refactor/dbus-performance).
add_executable(test_wts_dirty_mask core/test_wts_dirty_mask.cpp)
target_link_libraries(test_wts_dirty_mask PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_wts_dirty_mask COMMAND test_wts_dirty_mask)

# ═══════════════════════════════════════════════════════════════════════════════
# core/ — SnapEngine Tests
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_snap_engine core/test_snap_engine.cpp)
target_link_libraries(test_snap_engine PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_snap_engine COMMAND test_snap_engine)

# ═══════════════════════════════════════════════════════════════════════════════
# autotile/ — Tiling Algorithm Tests (split by algorithm family)
# ═══════════════════════════════════════════════════════════════════════════════
pz_add_test(test_tiling_algo_masterstack autotile/test_tiling_algo_masterstack.cpp)
target_compile_definitions(test_tiling_algo_masterstack PRIVATE "PZ_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
pz_add_test(test_tiling_algo_spiral_monocle autotile/test_tiling_algo_spiral_monocle.cpp)
target_compile_definitions(test_tiling_algo_spiral_monocle PRIVATE "PZ_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
pz_add_test(test_tiling_algo_grid_threecolumn autotile/test_tiling_algo_grid_threecolumn.cpp)
target_compile_definitions(test_tiling_algo_grid_threecolumn PRIVATE "PZ_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
pz_add_test(test_tiling_algo_wide_centered autotile/test_tiling_algo_wide_centered.cpp)
target_compile_definitions(test_tiling_algo_wide_centered PRIVATE "PZ_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
pz_add_test(test_tiling_algo_cascade_stair_spread autotile/test_tiling_algo_cascade_stair_spread.cpp)
target_compile_definitions(test_tiling_algo_cascade_stair_spread PRIVATE "PZ_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
pz_add_test(test_tiling_algo_lshape autotile/test_tiling_algo_lshape.cpp)
target_compile_definitions(test_tiling_algo_lshape PRIVATE "PZ_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
pz_add_test(test_tiling_algo_deck_zen autotile/test_tiling_algo_deck_zen.cpp)
target_compile_definitions(test_tiling_algo_deck_zen PRIVATE "PZ_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
pz_add_test(test_tiling_algo_edge_cases autotile/test_tiling_algo_edge_cases.cpp)
target_compile_definitions(test_tiling_algo_edge_cases PRIVATE "PZ_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
pz_add_test(test_tiling_algo_bsp autotile/test_tiling_algo_bsp.cpp)
target_compile_definitions(test_tiling_algo_bsp PRIVATE "PZ_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")

# ═══════════════════════════════════════════════════════════════════════════════
# autotile/ — Algorithm Registry Tests
# ═══════════════════════════════════════════════════════════════════════════════
pz_add_test(test_algorithm_registry autotile/test_algorithm_registry.cpp)
target_compile_definitions(test_algorithm_registry PRIVATE "PZ_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
pz_add_test(test_algorithm_registry_custom autotile/test_algorithm_registry_custom.cpp)
target_compile_definitions(test_algorithm_registry_custom PRIVATE "PZ_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
pz_add_test(test_scripted_algorithm autotile/test_scripted_algorithm.cpp)
target_compile_definitions(test_scripted_algorithm PRIVATE "PZ_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
pz_add_test(test_split_tree autotile/test_split_tree.cpp)
target_compile_definitions(test_split_tree PRIVATE "PZ_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
pz_add_test(test_scripted_algorithm_loader autotile/test_scripted_algorithm_loader.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# autotile/ — Autotile Engine Tests (core, master, minsize, overflow)
# ═══════════════════════════════════════════════════════════════════════════════
pz_add_test(test_autotile_engine_core autotile/test_autotile_engine_core.cpp)
target_compile_definitions(test_autotile_engine_core PRIVATE "PZ_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
pz_add_test(test_autotile_engine_master autotile/test_autotile_engine_master.cpp)
target_compile_definitions(test_autotile_engine_master PRIVATE "PZ_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
pz_add_test(test_autotile_engine_minsize autotile/test_autotile_engine_minsize.cpp)
pz_add_test(test_autotile_engine_overflow autotile/test_autotile_engine_overflow.cpp)
pz_add_test(test_autotile_engine_retry autotile/test_autotile_engine_retry.cpp)
pz_add_test(test_autotile_engine_order_cycling autotile/test_autotile_engine_order_cycling.cpp)
target_compile_definitions(test_autotile_engine_order_cycling PRIVATE "PZ_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
pz_add_test(test_autotile_engine_class_mutation autotile/test_autotile_engine_class_mutation.cpp)
pz_add_test(test_autotile_drag_insert autotile/test_autotile_drag_insert.cpp)
target_compile_definitions(test_autotile_drag_insert PRIVATE "PZ_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
# Producer-side validator guard — ensures the JSON emitted by
# AutotileEngine::applyTiling round-trips through TileRequestEntry
# parsing and passes validationError(). Would have caught PR #326 C1.
pz_add_test(test_autotile_tile_request_validation autotile/test_autotile_tile_request_validation.cpp)
target_link_libraries(test_autotile_tile_request_validation PRIVATE plasmazones_compositor_common)

# ═══════════════════════════════════════════════════════════════════════════════
# autotile/ — Autotile + Virtual Screen Integration Tests
# ═══════════════════════════════════════════════════════════════════════════════
pz_add_test(test_autotile_virtual autotile/test_autotile_virtual.cpp)
target_compile_definitions(test_autotile_virtual PRIVATE "PZ_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")

# ═══════════════════════════════════════════════════════════════════════════════
# autotile/ — Autotile Engine Extended Tests (startup, float, algoswitch, overflow)
# ═══════════════════════════════════════════════════════════════════════════════
pz_add_test(test_autotile_ext_startup_float autotile/test_autotile_ext_startup_float.cpp)
pz_add_test(test_autotile_ext_algoswitch autotile/test_autotile_ext_algoswitch.cpp)
target_compile_definitions(test_autotile_ext_algoswitch PRIVATE "PZ_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
pz_add_test(test_autotile_ext_overflow autotile/test_autotile_ext_overflow.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# autotile/ — Tiling State Tests (windows, config, serialization)
# ═══════════════════════════════════════════════════════════════════════════════
pz_add_test(test_tiling_state_windows autotile/test_tiling_state_windows.cpp)
pz_add_test(test_tiling_state_config autotile/test_tiling_state_config.cpp)
pz_add_test(test_tiling_state_serial autotile/test_tiling_state_serial.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# autotile/ — Overflow Manager Tests
# ═══════════════════════════════════════════════════════════════════════════════
pz_add_test(test_overflow_manager autotile/test_overflow_manager.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# core/ — Geometry Utils Tests (minsizes, overlaps, BSP)
# ═══════════════════════════════════════════════════════════════════════════════
pz_add_test(test_geometry_utils_minsizes core/test_geometry_utils_minsizes.cpp)
pz_add_test(test_geometry_utils_overlaps core/test_geometry_utils_overlaps.cpp)
pz_add_test(test_geometry_utils_bsp core/test_geometry_utils_bsp.cpp)
pz_add_test(test_geometry_utils_serialization core/test_geometry_utils_serialization.cpp)
pz_add_test(test_spatial_adjacency core/test_spatial_adjacency.cpp)
pz_add_test(test_virtualscreen_swapper core/test_virtualscreen_swapper.cpp)
pz_add_test(test_screen_mode_router core/test_screen_mode_router.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# config/ — Settings Tests (core, validation, per-screen)
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_settings_core config/test_settings_core.cpp)
target_link_libraries(test_settings_core PRIVATE Qt6::Test Qt6::Core plasmazones_core)
add_test(NAME test_settings_core COMMAND test_settings_core)

add_executable(test_settings_validation config/test_settings_validation.cpp)
target_link_libraries(test_settings_validation PRIVATE Qt6::Test Qt6::Core plasmazones_core)
add_test(NAME test_settings_validation COMMAND test_settings_validation)

add_executable(test_settings_perscreen config/test_settings_perscreen.cpp)
target_link_libraries(test_settings_perscreen PRIVATE Qt6::Test Qt6::Core plasmazones_core)
add_test(NAME test_settings_perscreen COMMAND test_settings_perscreen)

# ═══════════════════════════════════════════════════════════════════════════════
# config/ — Virtual Screen Settings Tests (save/load round-trip, staged flow)
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_settings_virtualscreen config/test_settings_virtualscreen.cpp)
target_link_libraries(test_settings_virtualscreen PRIVATE Qt6::Test Qt6::Core plasmazones_core)
add_test(NAME test_settings_virtualscreen COMMAND test_settings_virtualscreen)

pz_add_test(test_virtualscreen_staged config/test_virtualscreen_staged.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# config/ — ConfigDefaults Tests (standalone, no external dependency)
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_configdefaults config/test_configdefaults.cpp)
target_link_libraries(test_configdefaults PRIVATE Qt6::Test Qt6::Core plasmazones_core)
add_test(NAME test_configdefaults COMMAND test_configdefaults)

add_executable(test_configbackend_json config/test_configbackend_json.cpp)
target_link_libraries(test_configbackend_json PRIVATE Qt6::Test Qt6::Core plasmazones_core)
add_test(NAME test_configbackend_json COMMAND test_configbackend_json)

add_executable(test_configmigration config/test_configmigration.cpp)
target_link_libraries(test_configmigration PRIVATE Qt6::Test Qt6::Core plasmazones_core)
add_test(NAME test_configmigration COMMAND test_configmigration)

# ═══════════════════════════════════════════════════════════════════════════════
# core/ — Layout Tests (core, zones)
# ═══════════════════════════════════════════════════════════════════════════════
pz_add_test(test_layout_core core/test_layout_core.cpp)
pz_add_test(test_layout_zones core/test_layout_zones.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# core/ — LayoutManager Tests (persistence, cycling, assignment)
# ═══════════════════════════════════════════════════════════════════════════════
pz_add_test(test_layoutmanager_persistence core/test_layoutmanager_persistence.cpp)
pz_add_test(test_layoutmanager_cycling core/test_layoutmanager_cycling.cpp)
pz_add_test(test_layoutmanager_assignment core/test_layoutmanager_assignment.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# core/ — Virtual Screen Tests (ID utilities, data model, ScreenManager)
# ═══════════════════════════════════════════════════════════════════════════════
pz_add_test(test_virtual_screen core/test_virtual_screen.cpp)
pz_add_test(test_virtual_screen_manager core/test_virtual_screen_manager.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# core/ — Snap Assist Virtual Screen Tests (screensMatch, buildOccupiedZoneSet)
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_snap_assist_virtual core/test_snap_assist_virtual.cpp)
target_link_libraries(test_snap_assist_virtual PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_snap_assist_virtual COMMAND test_snap_assist_virtual)

# ═══════════════════════════════════════════════════════════════════════════════
# core/ — LayerSurface + computeLayerSize Tests
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_layersurface core/test_layersurface.cpp)
target_link_libraries(test_layersurface PRIVATE Qt6::Test Qt6::Core Qt6::Gui Qt6::WaylandClientPrivate Wayland::Client plasmazones_core)
# QPA integration header includes generated protocol headers and the export header
target_include_directories(test_layersurface PRIVATE
    ${CMAKE_BINARY_DIR}/src            # plasmazones_export.h
    ${CMAKE_BINARY_DIR}/src/generated-protocols  # wlr-layer-shell protocol header
)
add_test(NAME test_layersurface COMMAND test_layersurface)

# ═══════════════════════════════════════════════════════════════════════════════
# core/ — Support Report Tests (redaction, generation)
# ═══════════════════════════════════════════════════════════════════════════════
pz_add_test(test_support_report core/test_support_report.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# core/ — Zone Detection Tests (layout, service)
# ═══════════════════════════════════════════════════════════════════════════════
pz_add_test(test_zone_detection_layout core/test_zone_detection_layout.cpp)
pz_add_test(test_zone_detector_overlap core/test_zone_detector_overlap.cpp)

add_executable(test_zone_detection_service core/test_zone_detection_service.cpp)
target_link_libraries(test_zone_detection_service PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_zone_detection_service COMMAND test_zone_detection_service)

# ═══════════════════════════════════════════════════════════════════════════════
# autotile/ — Per-Screen Config Tests
# ═══════════════════════════════════════════════════════════════════════════════
pz_add_test(test_per_screen_config autotile/test_per_screen_config.cpp)
target_compile_definitions(test_per_screen_config PRIVATE "PZ_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")

# ═══════════════════════════════════════════════════════════════════════════════
# autotile/ — Navigation Controller Tests
# ═══════════════════════════════════════════════════════════════════════════════
pz_add_test(test_navigation_controller autotile/test_navigation_controller.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# autotile/ — Settings Bridge Tests
# ═══════════════════════════════════════════════════════════════════════════════
pz_add_test(test_settings_bridge autotile/test_settings_bridge.cpp)
target_compile_definitions(test_settings_bridge PRIVATE "PZ_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")

# ═══════════════════════════════════════════════════════════════════════════════
# ui/ — Zone Shader Item Tests — need Qt6::Quick + plasmazones_rendering
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_zone_shader_item ui/test_zone_shader_item.cpp)
target_link_libraries(test_zone_shader_item PRIVATE Qt6::Test Qt6::Core Qt6::Quick plasmazones_rendering)
add_test(NAME test_zone_shader_item COMMAND test_zone_shader_item)

# ═══════════════════════════════════════════════════════════════════════════════
# ui/ — Overlay Helpers Tests — need Qt6::Quick + QGuiApplication
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_overlay_helpers ui/test_overlay_helpers.cpp)
target_link_libraries(test_overlay_helpers PRIVATE Qt6::Test Qt6::Core Qt6::Quick plasmazones_core)
add_test(NAME test_overlay_helpers COMMAND test_overlay_helpers)

# ═══════════════════════════════════════════════════════════════════════════════
# dbus/ — D-Bus API Tests (convenience, schema, compositor bridge)
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_wta_convenience dbus/test_wta_convenience.cpp)
target_link_libraries(test_wta_convenience PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_wta_convenience COMMAND test_wta_convenience)

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

# End-to-end adaptor routing test: verifies typed-struct slot dispatch works
# over a real QDBusConnection. Regression guard for the 2026-04-10 resnap
# crash (moc recorded unqualified type names, metatype lookup failed).
add_executable(test_dbus_adaptor_routing dbus/test_dbus_adaptor_routing.cpp)
target_link_libraries(test_dbus_adaptor_routing PRIVATE Qt6::Test Qt6::Core Qt6::DBus
                                                          plasmazones_compositor_common)
add_test(NAME test_dbus_adaptor_routing COMMAND test_dbus_adaptor_routing)

# Panel-ready gate for AutotileAdaptor windowOpened queue. Regression guard
# for the stale-VS-geometry race: entries arriving before the first panel
# D-Bus query must be deferred until ScreenManager::panelGeometryReady fires.
add_executable(test_autotile_adaptor_panel_gate dbus/test_autotile_adaptor_panel_gate.cpp)
target_link_libraries(test_autotile_adaptor_panel_gate PRIVATE Qt6::Test Qt6::Core Qt6::DBus
                                                                 plasmazones_core)
add_test(NAME test_autotile_adaptor_panel_gate COMMAND test_autotile_adaptor_panel_gate)

# Truth-table guard for WindowDragAdaptor::computeDragPolicy — pins the
# (snap, autotile, context) matrix so the daemon's drag routing decision
# can't drift relative to the compositor plugin's deleted local caches.
# Regression guard for #310.
add_executable(test_drag_policy dbus/test_drag_policy.cpp)
target_link_libraries(test_drag_policy PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_drag_policy COMMAND test_drag_policy)

# Validation methods on D-Bus struct types — pins the accept/reject table
# for validationError() so cross-field invariants (DragOutcome action range,
# ApplySnap zoneId, TileRequestEntry tiled/floating size semantics, etc.)
# don't drift as producer code evolves.
add_executable(test_dbus_validation dbus/test_dbus_validation.cpp)
target_link_libraries(test_dbus_validation PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_compositor_common)
add_test(NAME test_dbus_validation COMMAND test_dbus_validation)

# LayoutAdaptor signal contract test (Phase 1.2 of refactor/dbus-performance).
# Pins the rule: property mutations emit layoutChanged only, NEVER
# layoutListChanged — the list hasn't changed.
add_executable(test_layout_adaptor_signals dbus/test_layout_adaptor_signals.cpp)
target_link_libraries(test_layout_adaptor_signals PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_layout_adaptor_signals COMMAND test_layout_adaptor_signals)

# Micro-benchmarks for D-Bus adaptor hot paths. Drives the
# refactor/dbus-performance branch (SettingsAdaptor value-equality guard,
# LayoutAdaptor signal split, JSON cache reuse). Run with
# `ctest -R bench_dbus_adaptors` to capture numbers against
# docs/perf/dbus-baseline.md. Not in _all_tests by default because
# benchmark output is verbose; it is still added via add_test() so
# `ctest -R bench` can select it.
add_executable(bench_dbus_adaptors dbus/bench_dbus_adaptors.cpp)
target_link_libraries(bench_dbus_adaptors PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME bench_dbus_adaptors COMMAND bench_dbus_adaptors)
# LABELS=bench lets CI skip the verbose benchmark output via
# `ctest -LE bench` while still letting `ctest -L bench` select it
# explicitly for the perf-regression run.
set_tests_properties(bench_dbus_adaptors PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "bench")

# ═══════════════════════════════════════════════════════════════════════════════
# compositor-common/ — Shared Library Types Tests (D-Bus types, FloatingCache,
#                      TriggerParser, WindowIdUtils)
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_compositor_common compositor-common/test_compositor_common.cpp)
target_link_libraries(test_compositor_common PRIVATE Qt6::Test plasmazones_compositor_common)
add_test(NAME test_compositor_common COMMAND test_compositor_common)

# ═══════════════════════════════════════════════════════════════════════════════
# Headless environment support
# Set QT_QPA_PLATFORM=offscreen so tests run in CI containers without a display
# ═══════════════════════════════════════════════════════════════════════════════
set(_all_tests
    test_window_identity
    test_window_tracking_snap test_window_tracking_float
    test_session_persistence test_session_persistence_layout
    test_wts_lifecycle test_wts_session test_wts_queries test_wts_dirty_mask
    test_tiling_algo_masterstack test_tiling_algo_spiral_monocle
    test_tiling_algo_grid_threecolumn test_tiling_algo_wide_centered
    test_tiling_algo_cascade_stair_spread
    test_tiling_algo_lshape test_tiling_algo_deck_zen test_tiling_algo_edge_cases
    test_tiling_algo_bsp
    test_algorithm_registry test_algorithm_registry_custom
    test_scripted_algorithm test_split_tree test_scripted_algorithm_loader
    test_autotile_engine_core test_autotile_engine_master
    test_autotile_engine_minsize test_autotile_engine_overflow
    test_autotile_engine_retry
    test_autotile_engine_order_cycling
    test_autotile_drag_insert
    test_autotile_tile_request_validation
    test_autotile_ext_startup_float test_autotile_ext_algoswitch
    test_autotile_ext_overflow
    test_tiling_state_windows test_tiling_state_config test_tiling_state_serial
    test_overflow_manager
    test_geometry_utils_minsizes test_geometry_utils_overlaps test_geometry_utils_bsp
    test_geometry_utils_serialization test_wts_clearfloat test_wts_virtual_migration test_wts_crossmode_float test_snap_engine
    test_settings_core test_settings_validation test_settings_perscreen
    test_settings_virtualscreen test_virtualscreen_staged
    test_configdefaults test_configbackend_json test_configmigration
    test_layout_core test_layout_zones
    test_layoutmanager_persistence test_layoutmanager_cycling test_layoutmanager_assignment
    test_virtual_screen test_virtual_screen_manager test_snap_assist_virtual
    test_layersurface
    test_support_report
    test_zone_detection_layout test_zone_detector_overlap test_zone_detection_service
    test_per_screen_config test_navigation_controller test_settings_bridge
    test_zone_shader_item test_overlay_helpers
    test_wta_convenience test_settings_schema test_settings_adaptor_batch test_compositor_bridge
    test_dbus_adaptor_routing
    test_autotile_adaptor_panel_gate
    test_drag_policy
    test_dbus_validation
    test_layout_adaptor_signals
    test_compositor_common
)
set_tests_properties(${_all_tests} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
