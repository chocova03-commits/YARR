add_executable(testItsdaqFW
               test_packet_queue.cpp
)

target_link_libraries(testItsdaqFW PUBLIC Yarr)
target_link_libraries(testItsdaqFW PRIVATE mainTest)

add_test(NAME testItsdaqFW COMMAND testItsdaqFW)
post_build_debug_executable(testItsdaqFW)

install(TARGETS testItsdaqFW
    EXPORT YarrTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}  # For DLLs on Windows
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )
