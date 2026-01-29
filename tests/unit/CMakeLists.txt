# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# PlasmaZones Unit Tests

# ═══════════════════════════════════════════════════════════════════════════════
# Window Identity Tests
# Tests for extractStableId() and window identity collision scenarios
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_window_identity
    test_window_identity.cpp
)

target_link_libraries(test_window_identity
    PRIVATE
        Qt6::Test
        Qt6::Core
        plasmazones_core
)

add_test(NAME WindowIdentity COMMAND test_window_identity)

# ═══════════════════════════════════════════════════════════════════════════════
# Window Tracking Tests
# Tests for WindowTrackingAdaptor behavior including session persistence
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_window_tracking
    test_window_tracking.cpp
)

target_link_libraries(test_window_tracking
    PRIVATE
        Qt6::Test
        Qt6::Core
        Qt6::DBus
        plasmazones_core
)

add_test(NAME WindowTracking COMMAND test_window_tracking)

# ═══════════════════════════════════════════════════════════════════════════════
# Session Persistence Tests
# Tests for cross-session window restoration scenarios
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_session_persistence
    test_session_persistence.cpp
)

target_link_libraries(test_session_persistence
    PRIVATE
        Qt6::Test
        Qt6::Core
        Qt6::DBus
        plasmazones_core
)

add_test(NAME SessionPersistence COMMAND test_session_persistence)

# ═══════════════════════════════════════════════════════════════════════════════
# Tiling Algorithm Tests
# Tests for MasterStack, Columns, BSP algorithms and gap application
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_tiling_algorithms
    test_tiling_algorithms.cpp
)

target_link_libraries(test_tiling_algorithms
    PRIVATE
        Qt6::Test
        Qt6::Core
        plasmazones_core
)

add_test(NAME TilingAlgorithms COMMAND test_tiling_algorithms)

# ═══════════════════════════════════════════════════════════════════════════════
# Algorithm Registry Tests
# Tests for AlgorithmRegistry singleton and algorithm discovery
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_algorithm_registry
    test_algorithm_registry.cpp
)

target_link_libraries(test_algorithm_registry
    PRIVATE
        Qt6::Test
        Qt6::Core
        plasmazones_core
)

add_test(NAME AlgorithmRegistry COMMAND test_algorithm_registry)

# ═══════════════════════════════════════════════════════════════════════════════
# AutotileEngine Tests
# Tests for AutotileEngine core functionality
# ═══════════════════════════════════════════════════════════════════════════════
add_executable(test_autotile_engine
    test_autotile_engine.cpp
)

target_link_libraries(test_autotile_engine
    PRIVATE
        Qt6::Test
        Qt6::Core
        plasmazones_core
)

add_test(NAME AutotileEngine COMMAND test_autotile_engine)
