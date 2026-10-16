add_executable(smartcity_tests
  test_main.cpp
  test_dijkstra.cpp
  test_snap.cpp
  test_polyline.cpp
  test_simulation.cpp
)

target_include_directories(smartcity_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_link_libraries(smartcity_tests PRIVATE
  Catch2::Catch2WithMain
  nlohmann_json::nlohmann_json
  Threads::Threads
)

# Also link the modules under test
target_sources(smartcity_tests PRIVATE
  ../src/graph/graph.cpp
  ../src/graph/spatial_grid.cpp
  ../src/routing/dijkstra.cpp
  ../src/routing/alternatives.cpp
  ../src/routing/maneuver.cpp
  ../src/routing/polyline.cpp
  ../src/geo/snap.cpp
  ../src/sim/simulator.cpp
  ../src/sim/predictor.cpp
  ../src/traffic/weight_updater.cpp
  ../src/util/config.cpp
  ../src/util/logger.cpp
)

include(Catch)
catch_discover_tests(smartcity_tests)
