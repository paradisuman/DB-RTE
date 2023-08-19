/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "log_recovery.h"

#include <cassert>

/**
 * @description: analyze阶段，需要获得脏页表（DPT）和未完成的事务列表（ATT）
 */
void RecoveryManager::analyze() {

    while (disk_manager_->read_log(buffer_.buffer_, LOG_BUFFER_SIZE, buffer_.offset_)) {
        buffer_.offset_ = 0;

        while (true) {
            if (buffer_.offset_ + LOG_HEADER_SIZE >= LOG_BUFFER_SIZE ||
                *reinterpret_cast<const uint32_t *>(buffer_.buffer_ + buffer_.offset_ + OFFSET_LOG_TOT_LEN) == 0)
                break;

            LogType log_type = *reinterpret_cast<const LogType*>(buffer_.buffer_ + buffer_.offset_ + OFFSET_LOG_TYPE);

            switch (log_type) {
                // 事务发生 BEGIN 则说明可能未完成 加入 ATT
                case LogType::BEGIN : {
                    auto begin_log_record = std::make_shared<BeginLogRecord>();
                    // 反序列化得到日志记录
                    begin_log_record->deserialize(buffer_.buffer_ + buffer_.offset_);
                    // begin_log_record->format_print();
                    // buffer 指针移动
                    buffer_.offset_ += begin_log_record->log_tot_len_;
                    // 更新 ATT
                    active_transaction_table.emplace(begin_log_record->log_tid_, begin_log_record->lsn_);
                    // 更新 lsn2log
                    lsn2log[begin_log_record->lsn_] = std::move(begin_log_record);

                    break;
                }
                // 事务发生 ABORT COMMIT 则说明已经完成 从 ATT 中删除
                case LogType::ABORT : {
                    auto abort_log_record = std::make_shared<AbortLogRecord>();
                    // 反序列化得到日志记录
                    abort_log_record->deserialize(buffer_.buffer_ + buffer_.offset_);
                    // abort_log_record->format_print();
                    // buffer 指针移动
                    buffer_.offset_ += abort_log_record->log_tot_len_;
                    // 更新 ATT
                    active_transaction_table.erase(abort_log_record->log_tid_);
                    // 更新 lsn2log
                    lsn2log[abort_log_record->lsn_] = std::move(abort_log_record);

                    break;
                }
                case LogType::COMMIT : {
                    auto commit_log_record = std::make_shared<CommitLogRecord>();
                    // 反序列化得到日志记录
                    commit_log_record->deserialize(buffer_.buffer_ + buffer_.offset_);
                    // commit_log_record->format_print();
                    // buffer 指针移动
                    buffer_.offset_ += commit_log_record->log_tot_len_;
                    // 更新 ATT
                    active_transaction_table.erase(commit_log_record->log_tid_);
                    // 更新 lsn2log
                    lsn2log[commit_log_record->lsn_] = std::move(commit_log_record);

                    break;
                }
                // 记录事务的操作
                case LogType::INSERT : {
                    auto insert_log_record = std::make_shared<InsertLogRecord>();
                    // 反序列化得到日志记录
                    insert_log_record->deserialize(buffer_.buffer_ + buffer_.offset_);
                    // insert_log_record->format_print();
                    // buffer 指针移动
                    buffer_.offset_ += insert_log_record->log_tot_len_;
                    // 更新 ATT
                    active_transaction_table[insert_log_record->log_tid_].push_back(insert_log_record->lsn_);
                    // 查看更改的页面是否是脏页 如果是则更新 DPT
                    const auto tab_name = std::string(insert_log_record->table_name_.get(), insert_log_record->table_name_size_);
                    const auto rid = insert_log_record->rid_;
                    auto table_file = sm_manager_->fhs_.at(tab_name).get();
                    auto table_fd = table_file->GetFd();
                    const auto page_id = PageId {table_fd, rid.page_no};

                    // if (dirty_page_table.count(page_id) == 0) {
                    //     auto itr = dirty_page_table.emplace(page_id, RedoLogsInPage());
                    //     auto &redo_log_in_page = itr.first->second;
                    //     redo_log_in_page.table_file_ = table_file;
                    //     redo_log_in_page.redo_logs_.push_back(table_file->get_page_lsn(rid.page_no));
                    // } 
                    // auto &redo_log_in_page = dirty_page_table[page_id];
                    // if (redo_log_in_page.redo_logs_.empty() || redo_log_in_page.redo_logs_[0] < insert_log_record->lsn_) {
                    //     redo_log_in_page.redo_logs_.push_back(insert_log_record->lsn_);
                    // }
                    auto itr = dirty_page_table.emplace(page_id, RedoLogsInPage());
                    auto &redo_log_in_page = itr.first->second;
                    if (itr.second) {
                        redo_log_in_page.table_file_ = table_file;
                        redo_log_in_page.redo_logs_.push_back(table_file->get_page_lsn(rid.page_no));
                    }
                    if (redo_log_in_page.redo_logs_[0] < insert_log_record->lsn_) {
                        redo_log_in_page.redo_logs_.push_back(insert_log_record->lsn_);
                    }
                    // 更新 lsn2log
                    lsn2log[insert_log_record->lsn_] = std::move(insert_log_record);

                    break;
                }
                case LogType::DELETE : {
                    auto delete_log_record = std::make_shared<DeleteLogRecord>();
                    // 反序列化得到日志记录
                    delete_log_record->deserialize(buffer_.buffer_ + buffer_.offset_);
                    // delete_log_record->format_print();
                    // buffer 指针移动
                    buffer_.offset_ += delete_log_record->log_tot_len_;
                    // 更新 ATT
                    active_transaction_table[delete_log_record->log_tid_].push_back(delete_log_record->lsn_);
                    // 更新 DPT
                    // 查看更改的页面是否是脏页 如果是则更新 DPT
                    const auto tab_name = std::string(delete_log_record->table_name_.get(), delete_log_record->table_name_size_);
                    const auto rid = delete_log_record->rid_;
                    auto table_file = sm_manager_->fhs_.at(tab_name).get();
                    auto table_fd = table_file->GetFd();
                    const auto page_id = PageId {table_fd, rid.page_no};

                    // if (dirty_page_table.count(page_id) == 0) {
                    //     auto itr = dirty_page_table.emplace(page_id, RedoLogsInPage());
                    //     auto &redo_log_in_page = itr.first->second;
                    //     redo_log_in_page.table_file_ = table_file;
                    //     redo_log_in_page.redo_logs_.push_back(table_file->get_page_lsn(rid.page_no));
                    // }
                    // auto &redo_log_in_page = dirty_page_table[page_id];
                    // if (redo_log_in_page.redo_logs_[0] < delete_log_record->lsn_) {
                    //     redo_log_in_page.redo_logs_.push_back(delete_log_record->lsn_);
                    // }
                    auto itr = dirty_page_table.emplace(page_id, RedoLogsInPage());
                    auto &redo_log_in_page = itr.first->second;
                    if (itr.second) {
                        redo_log_in_page.table_file_ = table_file;
                        redo_log_in_page.redo_logs_.push_back(table_file->get_page_lsn(rid.page_no));
                    }
                    if (redo_log_in_page.redo_logs_[0] < delete_log_record->lsn_) {
                        redo_log_in_page.redo_logs_.push_back(delete_log_record->lsn_);
                    }
                    // 更新 lsn2log
                    lsn2log[delete_log_record->lsn_] = std::move(delete_log_record);

                    break;
                }
                case LogType::UPDATE : {
                    auto update_log_record = std::make_shared<UpdateLogRecord>();
                    // 反序列化得到日志记录
                    update_log_record->deserialize(buffer_.buffer_ + buffer_.offset_);
                    // update_log_record->format_print();
                    // buffer 指针移动
                    buffer_.offset_ += update_log_record->log_tot_len_;
                    // 更新 ATT
                    active_transaction_table[update_log_record->log_tid_].push_back(update_log_record->lsn_);
                    // 查看更改的页面是否是脏页 如果是则更新 DPT
                    const auto tab_name = std::string(update_log_record->table_name_.get(), update_log_record->table_name_size_);
                    const auto rid = update_log_record->rid_;
                    auto table_file = sm_manager_->fhs_.at(tab_name).get();
                    auto table_fd = table_file->GetFd();
                    const auto page_id = PageId {table_fd, rid.page_no};

                    // if (dirty_page_table.count(page_id) == 0) {
                    //     auto itr = dirty_page_table.emplace(page_id, RedoLogsInPage());
                    //     auto &redo_log_in_page = itr.first->second;
                    //     redo_log_in_page.table_file_ = table_file;
                    //     redo_log_in_page.redo_logs_.push_back(table_file->get_page_lsn(rid.page_no));
                    // }
                    // auto &redo_log_in_page = dirty_page_table[page_id];
                    // if (redo_log_in_page.redo_logs_[0] < update_log_record->lsn_) {
                    //     redo_log_in_page.redo_logs_.push_back(update_log_record->lsn_);
                    // }
                    auto itr = dirty_page_table.emplace(page_id, RedoLogsInPage());
                    auto &redo_log_in_page = itr.first->second;
                    if (itr.second) {
                        redo_log_in_page.table_file_ = table_file;
                        redo_log_in_page.redo_logs_.push_back(table_file->get_page_lsn(rid.page_no));
                    }
                    if (redo_log_in_page.redo_logs_[0] < update_log_record->lsn_) {
                        redo_log_in_page.redo_logs_.push_back(update_log_record->lsn_);
                    }
                    // 更新 lsn2log
                    lsn2log[update_log_record->lsn_] = std::move(update_log_record);

                    break;
                }
            } // end of switch
        } // end of read buffer
    } // end of read log
}

/**
 * @description: 重做所有未落盘的操作
 */
void RecoveryManager::redo() {
    for (auto &[_, redo_log_in_page] : dirty_page_table) {
        auto &fh = *redo_log_in_page.table_file_;
        for (lsn_t lsn : redo_log_in_page.redo_logs_) {
            auto log_record = lsn2log[lsn];
            if (auto x = std::dynamic_pointer_cast<InsertLogRecord>(log_record)) {
                fh.insert_record(x->insert_value_.data, nullptr);
            } else if (auto x = std::dynamic_pointer_cast<DeleteLogRecord>(log_record)) {
                fh.delete_record(x->rid_, nullptr);
            } else if (auto x = std::dynamic_pointer_cast<UpdateLogRecord>(log_record)) {
                fh.update_record(x->rid_, x->after_value_.data, nullptr);
            }
        }
    }
    dirty_page_table.clear();
}

/**
 * @description: 回滚未完成的事务
 */
void RecoveryManager::undo() {
    for (auto &[txn_id, undo_lsn] : active_transaction_table) {
        for (auto &lsn : undo_lsn) {
            auto &log = lsn2log[lsn];
            if (auto x = std::dynamic_pointer_cast<InsertLogRecord>(log)) {
                const auto tab_name = std::string(x->table_name_.get(), x->table_name_size_);
                auto table_file = sm_manager_->fhs_.at(tab_name).get();

                table_file->delete_record(x->rid_, nullptr);
            } else if (auto x = std::dynamic_pointer_cast<DeleteLogRecord>(log)) {
                const auto tab_name = std::string(x->table_name_.get(), x->table_name_size_);
                auto table_file = sm_manager_->fhs_.at(tab_name).get();

                table_file->insert_record(x->delete_value_.data, nullptr);
            } else if (auto x = std::dynamic_pointer_cast<UpdateLogRecord>(log)) {
                const auto tab_name = std::string(x->table_name_.get(), x->table_name_size_);
                auto table_file = sm_manager_->fhs_.at(tab_name).get();

                table_file->update_record(x->rid_, x->before_value_.data, nullptr);
            }
        }
    }
    active_transaction_table.clear();
    lsn2log.clear();
}