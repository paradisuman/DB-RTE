#define private public

#include "storage/buffer_pool_manager.h"
#include "storage/disk_manager.h"
#include "record/rm.h"

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

void rand_buf(int size, char *buf) {
    for (int i = 0; i < size; i++) {
        int rand_ch = rand() & 0xff;
        buf[i] = rand_ch;
    }
}

struct rid_hash_t {
    size_t operator()(const Rid &rid) const { return (rid.page_no << 16) | rid.slot_no; }
};

struct rid_equal_t {
    bool operator()(const Rid &x, const Rid &y) const { return x.page_no == y.page_no && x.slot_no == y.slot_no; }
};

void check_equal(const RmFileHandle *file_handle,
                 const std::unordered_map<Rid, std::string, rid_hash_t, rid_equal_t> &mock) {
    // Test all records
    for (auto &entry : mock) {
        Rid rid = entry.first;
        auto mock_buf = (char *)entry.second.c_str();
        auto rec = file_handle->get_record(rid, nullptr);
        assert(memcmp(mock_buf, rec->data, file_handle->file_hdr_.record_size) == 0);
    }
    // Randomly get record
    for (int i = 0; i < 10; i++) {
        Rid rid = {.page_no = 1 + rand() % (file_handle->file_hdr_.num_pages - 1),
                   .slot_no = rand() % file_handle->file_hdr_.num_records_per_page};
        bool mock_exist = mock.count(rid) > 0;
        bool rm_exist = file_handle->is_record(rid);
        assert(rm_exist == mock_exist);
    }
    // Test RM scan
    size_t num_records = 0;
    for (RmScan scan(file_handle); !scan.is_end(); scan.next()) {
        assert(mock.count(scan.rid()) > 0);
        auto rec = file_handle->get_record(scan.rid(), nullptr);
        assert(memcmp(rec->data, mock.at(scan.rid()).c_str(), file_handle->file_hdr_.record_size) == 0);
        num_records++;
    }
    assert(num_records == mock.size());
}

TEST(RecordManagerTest, SimpleTest) {
    srand((unsigned)time(nullptr));

    std::cout << "Starting SimpleTest..." << std::endl;
    // 创建RmManager类的对象rm_manager
    auto disk_manager = std::make_unique<DiskManager>();
    auto buffer_pool_manager = std::make_unique<BufferPoolManager>(BUFFER_POOL_SIZE, disk_manager.get());
    auto rm_manager = std::make_unique<RmManager>(disk_manager.get(), buffer_pool_manager.get());

    std::unordered_map<Rid, std::string, rid_hash_t, rid_equal_t> mock;

    std::string filename = "abc.txt";

    int record_size = 4 + rand() % 256;  // 元组大小随便设置，只要不超过RM_MAX_RECORD_SIZE
    // test files
    {
        std::cout << "Testing file operations..." << std::endl;
        // 删除残留的同名文件
        if (disk_manager->is_file(filename)) {
            disk_manager->destroy_file(filename);
        }
        // 将file header写入到磁盘中的filename文件
        rm_manager->create_file(filename, record_size);
        // 将磁盘中的filename文件读出到内存中的file handle的file header
        std::unique_ptr<RmFileHandle> file_handle = rm_manager->open_file(filename);
        // 检查filename文件在内存中的file header的参数
        assert(file_handle->file_hdr_.record_size == record_size);
        assert(file_handle->file_hdr_.first_free_page_no == RM_NO_PAGE);
        assert(file_handle->file_hdr_.num_pages == 1);

        int max_bytes = file_handle->file_hdr_.record_size * file_handle->file_hdr_.num_records_per_page +
                        file_handle->file_hdr_.bitmap_size + (int)sizeof(RmPageHdr);
        assert(max_bytes <= PAGE_SIZE);
        int rand_val = rand();
        file_handle->file_hdr_.num_pages = rand_val;
        rm_manager->close_file(file_handle.get());

        // reopen file
        file_handle = rm_manager->open_file(filename);
        assert(file_handle->file_hdr_.num_pages == rand_val);
        rm_manager->close_file(file_handle.get());
        rm_manager->destroy_file(filename);

        std::cout << "Testing pages operations..." << std::endl;
    }
    // test pages
    rm_manager->create_file(filename, record_size);
    std::cout << "File created with filename: " << filename << std::endl;
    auto file_handle = rm_manager->open_file(filename);

    char write_buf[PAGE_SIZE];
    size_t add_cnt = 0;
    size_t upd_cnt = 0;
    size_t del_cnt = 0;
    for (int round = 0; round < 1000; round++) {
        double insert_prob = 1. - mock.size() / 250.;
        double dice = rand() * 1. / RAND_MAX;
        if (mock.empty() || dice < insert_prob) {
            std::cout << "Inserting record round: " << round << std::endl;

            rand_buf(file_handle->file_hdr_.record_size, write_buf);
            Rid rid = file_handle->insert_record(write_buf, nullptr);
            mock[rid] = std::string((char *)write_buf, file_handle->file_hdr_.record_size);
            add_cnt++;
            //            std::cout << "insert " << rid << '\n'; // operator<<(cout,rid)
        } else {
            std::cout << "Updating or erasing record round: " << round << std::endl;
            // update or erase random rid
            int rid_idx = rand() % mock.size();
            auto it = mock.begin();
            for (int i = 0; i < rid_idx; i++) {
                it++;
            }
            auto rid = it->first;
            if (rand() % 2 == 0) {
                // update
                rand_buf(file_handle->file_hdr_.record_size, write_buf);
                file_handle->update_record(rid, write_buf, nullptr);
                mock[rid] = std::string((char *)write_buf, file_handle->file_hdr_.record_size);
                upd_cnt++;
                //                std::cout << "update " << rid << '\n';
            } else {
                // erase
                file_handle->delete_record(rid, nullptr);
                mock.erase(rid);
                del_cnt++;
                //                std::cout << "delete " << rid << '\n';
            }
        }
        // Randomly re-open file
        if (round % 50 == 0) {
            std::cout << "Randomly reopening file at round: " << round << std::endl;
            rm_manager->close_file(file_handle.get());
            file_handle = rm_manager->open_file(filename);
        }
        check_equal(file_handle.get(), mock);
    }
    assert(mock.size() == add_cnt - del_cnt);
    std::cout << "insert " << add_cnt << '\n' << "delete " << del_cnt << '\n' << "update " << upd_cnt << '\n';
    // clean up
    rm_manager->close_file(file_handle.get());
    std::cout << "File closed" << std::endl;
    rm_manager->destroy_file(filename);
    std::cout << "File destroyed" << std::endl;
    std::cout << "SimpleTest completed." << std::endl;
}
