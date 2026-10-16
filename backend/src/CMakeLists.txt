add_executable(smartcity_server
  main.cpp
  api/server.cpp
  graph/graph.cpp
  graph/spatial_grid.cpp
  routing/dijkstra.cpp
  routing/alternatives.cpp
  routing/maneuver.cpp
  routing/polyline.cpp
  geo/snap.cpp
  search/search_index.cpp
  sim/simulator.cpp
  sim/predictor.cpp
  traffic/weight_updater.cpp
  util/config.cpp
  util/logger.cpp
)

target_include_directories(smartcity_server PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(smartcity_server PRIVATE
  Crow::Crow
  nlohmann_json::nlohmann_json
  Threads::Threads
)

# Copy frontend and data directories next to the binary for local dev
add_custom_command(TARGET smartcity_server POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E create_symlink
    "${CMAKE_SOURCE_DIR}/../frontend"
    "$<TARGET_FILE_DIR:smartcity_server>/frontend"
  COMMAND ${CMAKE_COMMAND} -E create_symlink
    "${CMAKE_SOURCE_DIR}/../data"
    "$<TARGET_FILE_DIR:smartcity_server>/data"
  COMMENT "Symlinking frontend/ and data/ next to binary"
)
