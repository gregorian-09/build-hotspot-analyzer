cmake_minimum_required(VERSION 3.31)
find_package(GTest CONFIG REQUIRED)

file(GLOB_RECURSE UNIT_TEST_SOURCES "unit/*.cpp")

if(UNIT_TEST_SOURCES)
    add_executable(unit_tests ${UNIT_TEST_SOURCES})

    target_link_libraries(unit_tests
            PRIVATE
            bha_core
            GTest::gtest
            GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(unit_tests
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()

file(GLOB_RECURSE INTEGRATION_TEST_SOURCES "integration/*.cpp")

if(INTEGRATION_TEST_SOURCES)
    add_executable(integration_tests ${INTEGRATION_TEST_SOURCES})

    target_link_libraries(integration_tests
            PRIVATE
            bha_core
            GTest::gtest
            GTest::gtest_main
    )

    gtest_discover_tests(integration_tests
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()