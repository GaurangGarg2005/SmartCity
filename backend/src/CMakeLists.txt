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

if(WIN32)
  target_link_libraries(smartcity_server PRIVATE ws2_32 mswsock)
endif()

if(WIN32)
  set(COPY_OR_SYMLINK copy_directory)
else()
  set(COPY_OR_SYMLINK create_symlink)
endif()

add_custom_command(TARGET smartcity_server POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E ${COPY_OR_SYMLINK}
    "${CMAKE_SOURCE_DIR}/../frontend"
    "$<TARGET_FILE_DIR:smartcity_server>/frontend"
  COMMAND ${CMAKE_COMMAND} -E ${COPY_OR_SYMLINK}
    "${CMAKE_SOURCE_DIR}/../data"
    "$<TARGET_FILE_DIR:smartcity_server>/data"
  COMMENT "Copying or symlinking frontend/ and data/ next to binary"
)
