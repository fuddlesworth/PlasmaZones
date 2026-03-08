# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# PlasmaZones Unit Tests

# KConfigXT generated sources (needed by tests that use ConfigDefaults/PlasmaZonesConfig)
kconfig_add_kcfg_files(test_kcfg_SRCS
    ${CMAKE_SOURCE_DIR}/src/config/plasmazones.kcfgc
    GENERATE_MOC
)

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
pz_add_test(test_tiling_algo_spiral_monocle autotile/test_tiling_algo_spiral_monocle.cpp)
pz_add_test(test_tiling_algo_grid_threecolumn autotile/test_tiling_algo_grid_threecolumn.cpp)
pz_add_test(test_tiling_algo_wide_centered autotile/test_tiling_algo_wide_centered.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# autotile/ — Algorithm Registry Tests
# ═══════════════════════════════════════════════════════════════════════════════
pz_add_test(test_algorithm_registry autotile/test_algorithm_registry.cpp)
pz_add_test(test_algorithm_registry_custom autotile/test_algorithm_registry_custom.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# autotile/ — Autotile Engine Tests (core, master, minsize, overflow)
# ═══════════════════════════════════════════════════════════════════════════════
pz_add_test(test_autotile_engine_core autotile/test_autotile_engine_core.cpp)
pz_add_test(test_autotile_engine_master autotile/test_autotile_engine_master.cpp)
pz_add_test(test_autotile_engine_minsize autotile/test_autotile_engine_minsize.cpp)
pz_add_test(test_autotile_engine_overflow autotile/test_autotile_engine_overflow.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# autotile/ — Autotile Engine Extended Tests (startup, float, algoswitch, overflow)
# ═══════════════════════════════════════════════════════════════════════════════
pz_add_test(test_autotile_ext_startup_float autotile/test_autotile_ext_startup_float.cpp)
pz_add_test(test_autotile_ext_algoswitch autotile/test_autotile_ext_algoswitch.cpp)
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

# ═══════════════════════════════════════════════════════════════════════════════
# config/ — Settings Tests (core, validation, per-screen) — need kcfg + KF6::ConfigCore
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_settings_core config/test_settings_core.cpp ${test_kcfg_SRCS})
target_link_libraries(test_settings_core PRIVATE Qt6::Test Qt6::Core KF6::ConfigCore plasmazones_core)
add_test(NAME test_settings_core COMMAND test_settings_core)

add_executable(test_settings_validation config/test_settings_validation.cpp ${test_kcfg_SRCS})
target_link_libraries(test_settings_validation PRIVATE Qt6::Test Qt6::Core KF6::ConfigCore plasmazones_core)
add_test(NAME test_settings_validation COMMAND test_settings_validation)

add_executable(test_settings_perscreen config/test_settings_perscreen.cpp ${test_kcfg_SRCS})
target_link_libraries(test_settings_perscreen PRIVATE Qt6::Test Qt6::Core KF6::ConfigCore plasmazones_core)
add_test(NAME test_settings_perscreen COMMAND test_settings_perscreen)

# ═══════════════════════════════════════════════════════════════════════════════
# config/ — ConfigDefaults Tests — need kcfg + KF6::ConfigCore
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_configdefaults config/test_configdefaults.cpp ${test_kcfg_SRCS})
target_link_libraries(test_configdefaults PRIVATE Qt6::Test Qt6::Core KF6::ConfigCore plasmazones_core)
add_test(NAME test_configdefaults COMMAND test_configdefaults)

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
# core/ — Zone Detection Tests (layout, service)
# ═══════════════════════════════════════════════════════════════════════════════
pz_add_test(test_zone_detection_layout core/test_zone_detection_layout.cpp)

add_executable(test_zone_detection_service core/test_zone_detection_service.cpp)
target_link_libraries(test_zone_detection_service PRIVATE Qt6::Test Qt6::Core Qt6::DBus plasmazones_core)
add_test(NAME test_zone_detection_service COMMAND test_zone_detection_service)

# ═══════════════════════════════════════════════════════════════════════════════
# autotile/ — Per-Screen Config Tests
# ═══════════════════════════════════════════════════════════════════════════════
pz_add_test(test_per_screen_config autotile/test_per_screen_config.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# autotile/ — Navigation Controller Tests
# ═══════════════════════════════════════════════════════════════════════════════
pz_add_test(test_navigation_controller autotile/test_navigation_controller.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# autotile/ — Settings Bridge Tests
# ═══════════════════════════════════════════════════════════════════════════════
pz_add_test(test_settings_bridge autotile/test_settings_bridge.cpp)

# ═══════════════════════════════════════════════════════════════════════════════
# ui/ — Zone Shader Item Tests — need Qt6::Quick + plasmazones_rendering
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_zone_shader_item ui/test_zone_shader_item.cpp)
target_link_libraries(test_zone_shader_item PRIVATE Qt6::Test Qt6::Core Qt6::Quick plasmazones_rendering)
add_test(NAME test_zone_shader_item COMMAND test_zone_shader_item)

# ═══════════════════════════════════════════════════════════════════════════════
# ui/ — Overlay Helpers Tests — need Qt6::Quick + LayerShellQt + QGuiApplication
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_overlay_helpers ui/test_overlay_helpers.cpp)
target_link_libraries(test_overlay_helpers PRIVATE Qt6::Test Qt6::Core Qt6::Quick LayerShellQt::Interface plasmazones_core)
add_test(NAME test_overlay_helpers COMMAND test_overlay_helpers)

# ═══════════════════════════════════════════════════════════════════════════════
# Headless environment support
# Set QT_QPA_PLATFORM=offscreen so tests run in CI containers without a display
# ═══════════════════════════════════════════════════════════════════════════════
set(_all_tests
    test_window_identity
    test_window_tracking_snap test_window_tracking_float
    test_session_persistence test_session_persistence_layout
    test_wts_lifecycle test_wts_session test_wts_queries
    test_tiling_algo_masterstack test_tiling_algo_spiral_monocle
    test_tiling_algo_grid_threecolumn test_tiling_algo_wide_centered
    test_algorithm_registry test_algorithm_registry_custom
    test_autotile_engine_core test_autotile_engine_master
    test_autotile_engine_minsize test_autotile_engine_overflow
    test_autotile_ext_startup_float test_autotile_ext_algoswitch
    test_autotile_ext_overflow
    test_tiling_state_windows test_tiling_state_config test_tiling_state_serial
    test_overflow_manager
    test_geometry_utils_minsizes test_geometry_utils_overlaps test_geometry_utils_bsp
    test_geometry_utils_serialization test_wts_clearfloat test_snap_engine
    test_settings_core test_settings_validation test_settings_perscreen
    test_configdefaults
    test_layout_core test_layout_zones
    test_layoutmanager_persistence test_layoutmanager_cycling test_layoutmanager_assignment
    test_zone_detection_layout test_zone_detection_service
    test_per_screen_config test_navigation_controller test_settings_bridge
    test_zone_shader_item test_overlay_helpers
)
set_tests_properties(${_all_tests} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
