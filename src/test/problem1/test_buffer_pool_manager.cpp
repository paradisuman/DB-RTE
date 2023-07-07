#define private public

#include "storage/buffer_pool_manager.h"
#include "storage/disk_manager.h"

#undef private

#include "gtest/gtest.h"
#include "replacer/lru_replacer.h"

#include <bits/stdc++.h>

const std::string TEST_DB_NAME = "BufferPoolManagerTest_db";  // 以数据库名作为根目录
const std::string TEST_FILE_NAME = "basic";                   // 测试文件的名字
const std::string TEST_FILE_NAME_CCUR = "concurrency";        // 测试文件的名字
const std::string TEST_FILE_NAME_BIG = "bigdata";             // 测试文件的名字
constexpr int MAX_FILES = 32;
constexpr int MAX_PAGES = 128;
constexpr size_t TEST_BUFFER_POOL_SIZE = MAX_FILES * MAX_PAGES;

/** 注意：每个测试点只测试了单个文件！
 * 对于每个测试点，先创建和进入目录TEST_DB_NAME
 * 然后在此目录下创建和打开文件TEST_FILE_NAME，记录其文件描述符fd */
class BufferPoolManagerTest : public ::testing::Test {
   public:
    std::unique_ptr<DiskManager> disk_manager_;
    int fd_ = -1;  // 此文件描述符为disk_manager_->open_file的返回值

   public:
    // This function is called before every test.
    void SetUp() override {
        ::testing::Test::SetUp();
        std::cout << "Entering ::testing::Test::Setup()" << std::endl;
        // For each test, we create a new DiskManager
        disk_manager_ = std::make_unique<DiskManager>();
        // 如果测试目录不存在，则先创建测试目录
        if (!disk_manager_->is_dir(TEST_DB_NAME)) {
            disk_manager_->create_dir(TEST_DB_NAME);
        }
        assert(disk_manager_->is_dir(TEST_DB_NAME));
        // 进入测试目录
        if (chdir(TEST_DB_NAME.c_str()) < 0) {
            throw UnixError();
        }
        // 如果测试文件存在，则先删除原文件（最后留下来的文件存的是最后一个测试点的数据）
        if (disk_manager_->is_file(TEST_FILE_NAME)) {
            disk_manager_->destroy_file(TEST_FILE_NAME);
        }
        // 创建测试文件
        disk_manager_->create_file(TEST_FILE_NAME);
        assert(disk_manager_->is_file(TEST_FILE_NAME));
        // 打开测试文件
        fd_ = disk_manager_->open_file(TEST_FILE_NAME);
        assert(fd_ != -1);
        std::cout << "Leaving ::testing::Test::Setup()" << std::endl;
    }

    // This function is called after every test.
    void TearDown() override {
        disk_manager_->close_file(fd_);
        // disk_manager_->destroy_file(TEST_FILE_NAME);  // you can choose to delete the file

        // 返回上一层目录
        if (chdir("..") < 0) {
            throw UnixError();
        }
        assert(disk_manager_->is_dir(TEST_DB_NAME));
    };
};

void print_page_table(BufferPoolManager &bpm) {
    std::cout << "BufferPoolManager.page_table_:" << std::endl;
    for (auto &[page_id, frame_id] : bpm.page_table_) {
        std::cout << page_id.toString() << '\t'
                  << frame_id << '\t'
                  << bpm.pages_[frame_id].pin_count_ << '\t'
                  << bpm.pages_[frame_id].is_dirty_ << std::endl;
    }
}

TEST_F(BufferPoolManagerTest, SampleTest) {
    std::cout << "Entering TEST_F(BufferPoolManagerTest, SampleTest)" << std::endl;
    // create BufferPoolManager
    const size_t buffer_pool_size = 10;
    auto disk_manager = BufferPoolManagerTest::disk_manager_.get();
    auto bpm = std::make_unique<BufferPoolManager>(buffer_pool_size, disk_manager);
    std::cout << "Created BufferPoolManager instance" << std::endl;
    
    // create tmp PageId
    int fd = BufferPoolManagerTest::fd_;
    PageId page_id_temp = {.fd = fd, .page_no = INVALID_PAGE_ID};
    auto *page0 = bpm->new_page(&page_id_temp);
    std::cout << "Created new page" << std::endl;

    // Scenario: The buffer pool is empty. We should be able to create a new page.
    ASSERT_NE(nullptr, page0);
    std::cout << "Checked page is not null" << std::endl;
    EXPECT_EQ(0, page_id_temp.page_no);
    std::cout << "Checked page number" << std::endl;

    // Scenario: Once we have a page, we should be able to read and write content.
    snprintf(page0->get_data(), sizeof(page0->get_data()), "Hello");
    std::cout << "Wrote data to page" << std::endl;
    EXPECT_EQ(0, strcmp(page0->get_data(), "Hello"));
    std::cout << "Read data from page" << std::endl;

    print_page_table(*bpm);
    // Scenario: We should be able to create new pages until we fill up the buffer pool.
    for (size_t i = 1; i < buffer_pool_size; ++i) {
        EXPECT_NE(nullptr, bpm->new_page(&page_id_temp));
        std::cout << "Created page " << i << std::endl;
    }

    print_page_table(*bpm);
    // Scenario: Once the buffer pool is full, we should not be able to create any new pages.
    for (size_t i = buffer_pool_size; i < buffer_pool_size * 2; ++i) {
        EXPECT_EQ(nullptr, bpm->new_page(&page_id_temp));
        std::cout << "Checked buffer pool is full at " << i << std::endl;
    }

    // print_page_table(*bpm);

    // Scenario: After unpinning pages {0, 1, 2, 3, 4} and pinning another 4 new pages,
    // there would still be one cache frame left for reading page 0.
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(true, bpm->unpin_page(PageId{fd, i}, true));
        std::cout << "Unpinned page " << i << std::endl;
    }
    print_page_table(*bpm);
    for (int i = 0; i < 4; ++i) {
        EXPECT_NE(nullptr, bpm->new_page(&page_id_temp));
        std::cout << "Created new page after unpinning" << i << std::endl;
    }

    print_page_table(*bpm);
    // Scenario: We should be able to fetch the data we wrote a while ago.
    page0 = bpm->fetch_page(PageId{fd, 0});
    std::cout << "Fetched data from page 0" << std::endl;

    print_page_table(*bpm);
    std::cout << page0->get_data() << '\t' << "Hello" << std::endl;
    EXPECT_EQ(0, strcmp(page0->get_data(), "Hello"));
    std::cout << "Checked data on fetched page" << std::endl;
    EXPECT_EQ(true, bpm->unpin_page(PageId{fd, 0}, true));
    std::cout << "Unpinned page 0 after fetch" << std::endl;

    // new_page again, and now all buffers are pinned. Page 0 would be failed to fetch.
    EXPECT_NE(nullptr, bpm->new_page(&page_id_temp));
    std::cout << "Created new page after unpinning all" << std::endl;
    EXPECT_EQ(nullptr, bpm->fetch_page(PageId{fd, 0}));
    std::cout << "Checked page 0 is not fetchable" << std::endl;

    bpm->flush_all_pages(fd);
    std::cout << "Flushed all pages" << std::endl;
}

