cmake_minimum_required(VERSION 3.20)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../)
link_libraries(gtest_main)

enable_testing()

# test_buffer_pool_manager
file(GLOB SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(test_add ${SOURCES})
target_link_libraries(test_add index system)
add_test(NAME test_add COMMAND test_add)