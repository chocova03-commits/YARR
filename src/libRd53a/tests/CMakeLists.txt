add_executable(testRd53a
        test_config_json.cpp
)
target_link_libraries(testRd53a PUBLIC Yarr)
target_link_libraries(testRd53a PRIVATE mainTest)

add_test(NAME testRd53a COMMAND testRd53a)
post_build_debug_executable(testRd53a)

install(TARGETS testRd53a
    EXPORT YarrTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}  # For DLLs on Windows
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )
