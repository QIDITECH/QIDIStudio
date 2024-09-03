# Add individual tools as executables in separate directories
add_subdirectory(qds_gcode_checker)

install(TARGETS qds_gcode_checker RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}")
