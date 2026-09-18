# Build-only integration, outside the simulator checkout. No simulator source changes.
function(meshroute_controller_sources)
  target_sources(meshroute_core_normal PRIVATE "${MESHROUTE_DIR}/lib/core/remote_client.cpp")
  target_sources(meshroute_core_gw PRIVATE "${MESHROUTE_DIR}/lib/core/remote_client.cpp")
endfunction()
cmake_language(DEFER CALL meshroute_controller_sources)
