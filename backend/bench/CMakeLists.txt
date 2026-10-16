add_executable(smartcity_bench
  bench_dijkstra.cpp
)

target_include_directories(smartcity_bench PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_link_libraries(smartcity_bench PRIVATE
  nlohmann_json::nlohmann_json
  Threads::Threads
)

target_sources(smartcity_bench PRIVATE
  ../src/graph/graph.cpp
  ../src/graph/spatial_grid.cpp
  ../src/routing/dijkstra.cpp
  ../src/routing/alternatives.cpp
  ../src/routing/maneuver.cpp
  ../src/routing/polyline.cpp
  ../src/geo/snap.cpp
  ../src/traffic/weight_updater.cpp
  ../src/util/config.cpp
  ../src/util/logger.cpp
)
