# 1. 测试集 `LRUReplacerTest` 包含6个测试，全部通过。
# 2. 测试集 `BufferPoolManagerTest` 包含8个测试，其中2个失败。失败的测试为 `fetch_page` 和 `IsDirty`。
#    - `fetch_page` 失败原因有4个:
#      - 第一个失败，期望值为NULL，但实际结果是地址 `0x55de2128c010`。
#      - 第二个失败，期望值为NULL，但实际结果是地址 `0x55de21289ff0`。
#      - 第三个失败，期望值为1，但实际结果是 false。
#      - 第四个失败，期望 `(nullptr) != (new_page)`，但实际结果是 `(nullptr) vs NULL`。
#    - `IsDirty` 失败原因是期望值为1，但实际结果是 false。
# 3. 测试集 `BufferPoolManagerConcurrencyTest` 包含5个测试，其中2个失败。失败的测试为 `HardTest_3` 和 `HardTest_4`。
#    - 两个测试的失败原因都是期望值为1，但实际结果是 false。
# 4. 测试集 `StorageTest` 和 `BigStorageTest` 各包含1个测试，都通过。
# 5. 测试集 `RecordManagerTest` 包含2个测试，其中1个失败。失败的测试为 `SimpleTest`，因为在测试体中抛出了描述为 "Error: DiskManager::read_page Error" 的C++异常。

# 总的来说，需要注意的是 `BufferPoolManagerTest`，`BufferPoolManagerConcurrencyTest`，以及 `RecordManagerTest` 中的失败测试，需要进行问题的定位和修复。# 1. 测试集 `LRUReplacerTest` 包含6个测试，全部通过。
# 2. 测试集 `BufferPoolManagerTest` 包含8个测试，其中2个失败。失败的测试为 `fetch_page` 和 `IsDirty`。
#    - `fetch_page` 失败原因有4个:
#      - 第一个失败，期望值为NULL，但实际结果是地址 `0x55de2128c010`。
#      - 第二个失败，期望值为NULL，但实际结果是地址 `0x55de21289ff0`。
#      - 第三个失败，期望值为1，但实际结果是 false。
#      - 第四个失败，期望 `(nullptr) != (new_page)`，但实际结果是 `(nullptr) vs NULL`。
#    - `IsDirty` 失败原因是期望值为1，但实际结果是 false。
# 3. 测试集 `BufferPoolManagerConcurrencyTest` 包含5个测试，其中2个失败。失败的测试为 `HardTest_3` 和 `HardTest_4`。
#    - 两个测试的失败原因都是期望值为1，但实际结果是 false。
# 4. 测试集 `StorageTest` 和 `BigStorageTest` 各包含1个测试，都通过。
# 5. 测试集 `RecordManagerTest` 包含2个测试，其中1个失败。失败的测试为 `SimpleTest`，因为在测试体中抛出了描述为 "Error: DiskManager::read_page Error" 的C++异常。

# 总的来说，需要注意的是 `BufferPoolManagerTest`，`BufferPoolManagerConcurrencyTest`，以及 `RecordManagerTest` 中的失败测试，需要进行问题的定位和修复。

cmake_minimum_required(VERSION 3.20)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../)
link_libraries(gtest_main)

enable_testing()

# test_buffer_pool_manager
add_executable(test_buffer_pool_manager ${CMAKE_CURRENT_SOURCE_DIR}/test_buffer_pool_manager.cpp)
target_link_libraries(test_buffer_pool_manager storage)
add_test(NAME test_buffer_pool_manager COMMAND test_buffer_pool_manager)

# test_lru_replacer
add_executable(test_lru_replacer ${CMAKE_CURRENT_SOURCE_DIR}/test_lru_replacer.cpp)
target_link_libraries(test_lru_replacer lru_replacer)
add_test(NAME test_lru_replacer COMMAND test_lru_replacer)

# test_buffer_pool_manager_concurrency
add_executable(test_buffer_pool_manager_concurrency ${CMAKE_CURRENT_SOURCE_DIR}/test_buffer_pool_manager_concurrency.cpp)
target_link_libraries(test_buffer_pool_manager_concurrency lru_replacer storage)
add_test(NAME test_buffer_pool_manager_concurrency COMMAND test_buffer_pool_manager_concurrency) 

# test_storage
add_executable(test_storage ${CMAKE_CURRENT_SOURCE_DIR}/test_storage.cpp)
target_link_libraries(test_storage lru_replacer storage)
add_test(NAME test_storage COMMAND test_storage) 

# test_record_manager
add_executable(test_record_manager ${CMAKE_CURRENT_SOURCE_DIR}/test_record_manager.cpp)
target_link_libraries(test_record_manager storage lru_replacer record gtest_main)
add_test(NAME test_record_manager COMMAND test_record_manager)
