# set(SOURCES concurrency/lock_manager.cpp transaction_manager.cpp)
set(SOURCES concurrency/fake_lock_manager.cpp transaction_manager.cpp)
add_library(transaction STATIC ${SOURCES})
target_link_libraries(transaction system recovery pthread)