#include <bits/stdc++.h>

#define private public

#include "storage/disk_manager.h"

#undef private

#include "gtest/gtest.h"

const std::string TEST_FILE_NAME = "basic";                   // 测试文件的名字

namespace {

TEST(TestDiskManager, create_file) {
    DiskManager disk_manager;

    disk_manager.create_file(TEST_FILE_NAME);
    ASSERT_TRUE(disk_manager.is_file(TEST_FILE_NAME));
}

TEST(TestDiskManager, open_close_file) {
    DiskManager disk_manager;

    int fd = disk_manager.open_file(TEST_FILE_NAME);
    ASSERT_EQ(disk_manager.path2fd_.count(TEST_FILE_NAME), 1);

    ASSERT_EQ(disk_manager.open_file(TEST_FILE_NAME), fd);

    disk_manager.close_file(fd);
    ASSERT_EQ(disk_manager.path2fd_.count(TEST_FILE_NAME), 0);

    EXPECT_PRED1(disk_manager.close_file, fd);
}

TEST(TestDiskManager, write_read_file) {
    DiskManager disk_manager;

    const static char msg[] = "Hello, world!";
    static char buffer[128];

    int fd = disk_manager.open_file(TEST_FILE_NAME);

    disk_manager.write_page(fd, 0, msg, sizeof(msg));
    disk_manager.read_page(fd, 0, buffer, sizeof(msg));

    ASSERT_STREQ(msg, buffer);
}

TEST(TestDiskManager, destroy_file) {
    DiskManager disk_manager;

    disk_manager.destroy_file(TEST_FILE_NAME);
    ASSERT_FALSE(disk_manager.is_file(TEST_FILE_NAME));
}

}