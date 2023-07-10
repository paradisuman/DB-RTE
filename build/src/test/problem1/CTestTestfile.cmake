# CMake generated Testfile for 
# Source directory: /home/pa/db/db2023-runtimeterror/src/test/problem1
# Build directory: /home/pa/db/db2023-runtimeterror/build/src/test/problem1
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[test_buffer_pool_manager]=] "/home/pa/db/db2023-runtimeterror/build/bin/test_buffer_pool_manager")
set_tests_properties([=[test_buffer_pool_manager]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/pa/db/db2023-runtimeterror/src/test/problem1/CMakeLists.txt;39;add_test;/home/pa/db/db2023-runtimeterror/src/test/problem1/CMakeLists.txt;0;")
add_test([=[test_lru_replacer]=] "/home/pa/db/db2023-runtimeterror/build/bin/test_lru_replacer")
set_tests_properties([=[test_lru_replacer]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/pa/db/db2023-runtimeterror/src/test/problem1/CMakeLists.txt;44;add_test;/home/pa/db/db2023-runtimeterror/src/test/problem1/CMakeLists.txt;0;")
add_test([=[test_buffer_pool_manager_concurrency]=] "/home/pa/db/db2023-runtimeterror/build/bin/test_buffer_pool_manager_concurrency")
set_tests_properties([=[test_buffer_pool_manager_concurrency]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/pa/db/db2023-runtimeterror/src/test/problem1/CMakeLists.txt;49;add_test;/home/pa/db/db2023-runtimeterror/src/test/problem1/CMakeLists.txt;0;")
add_test([=[test_storage]=] "/home/pa/db/db2023-runtimeterror/build/bin/test_storage")
set_tests_properties([=[test_storage]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/pa/db/db2023-runtimeterror/src/test/problem1/CMakeLists.txt;54;add_test;/home/pa/db/db2023-runtimeterror/src/test/problem1/CMakeLists.txt;0;")
add_test([=[test_record_manager]=] "/home/pa/db/db2023-runtimeterror/build/bin/test_record_manager")
set_tests_properties([=[test_record_manager]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/pa/db/db2023-runtimeterror/src/test/problem1/CMakeLists.txt;59;add_test;/home/pa/db/db2023-runtimeterror/src/test/problem1/CMakeLists.txt;0;")
