/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "transaction_manager.h"
#include "record/rm_file_handle.h"
#include "system/sm_manager.h"

std::unordered_map<txn_id_t, Transaction *> TransactionManager::txn_map = {};

/**
 * @description: 事务的开始方法
 * @return {Transaction*} 开始事务的指针
 * @param {Transaction*} txn 事务指针，空指针代表需要创建新事务，否则开始已有事务
 * @param {LogManager*} log_manager 日志管理器指针
 */
Transaction * TransactionManager::begin(Transaction* txn, LogManager* log_manager) {
    // Todo:
    // 1. 判断传入事务参数是否为空指针
    // 2. 如果为空指针，创建新事务
    if (txn == nullptr) {
        txn = new Transaction(next_txn_id_);
        next_txn_id_++;
    } 

    // 3. 把开始事务加入到全局事务表中
    // timestamp_t new_ts = next_timestamp_.fetch_add(1);
    // txn->set_start_ts(new_ts);
    txn_id_t id = txn->get_transaction_id();

    txn_map.emplace(id, txn);
    // 4. 返回当前事务指针
    return txn;
}

/**
 * @description: 事务的提交方法
 * @param {Transaction*} txn 需要提交的事务
 * @param {LogManager*} log_manager 日志管理器指针
 */
void TransactionManager::commit(Transaction* txn, LogManager* log_manager) {
    // Todo:
    // 1. 如果存在未提交的写操作，提交所有的写操作
    auto write_set_ = txn->get_write_set();
    while (!write_set_->empty()) {
        auto x = write_set_->back();
        // 确保栈内存被释放
        delete x;
        write_set_->pop_back();
    }
    // auto write_set_ = txn->get_write_set();
    // for (auto &wr : *write_set_) {
    //     // 1.Todo 目前commit不修改内存，abort直接进行内存操作 将内存写回磁盘
    //     // 2.Todo 目前commit不修改索引，abort直接进行索引操作 将索引写回磁盘
    //     if (wr->GetWriteType() == WType::INSERT_TUPLE) {

    //     }
    //     else if (wr->GetWriteType() == WType::DELETE_TUPLE) {

    //     }
    //     else if (wr->GetWriteType() == WType::UPDATE_TUPLE) {

    //     }
    // }
    // 2. 释放所有锁
    // Todo:
    // auto lock_set = txn->get_lock_set();
    // for (auto &lock: *lock_set) {
	// 	lock_manager_->unlock(txn, lock);
	// }
    // 3. 释放事务相关资源，eg.锁集
    // Todo:
    // 4. 把事务日志刷入磁盘中
    // Todo:
    // 5. 更新事务状态
    txn->set_state(TransactionState::COMMITTED);
}

/**
 * @description: 事务的终止（回滚）方法
 * @param {Transaction *} txn 需要回滚的事务
 * @param {LogManager} *log_manager 日志管理器指针
 */
void TransactionManager::abort(Transaction *txn, LogManager *log_manager) {
    // Todo:
    // 1. 回滚所有写操作
    // 2. 释放所有锁
    // 3. 清空事务相关资源，eg.锁集
    // 4. 把事务日志刷入磁盘中
    // 5. 更新事务状态

    Context context(lock_manager_, log_manager, txn);

    // 1. 回滚所有写操作
    auto write_set = txn->get_write_set();
    while (!write_set->empty()) {
        const auto &wr = *write_set->back();
        const auto &prev_rid = wr.GetRid();
        const auto &tab_name = wr.GetTableName();
        const auto &tab = sm_manager_->db_.get_table(tab_name);
        const auto &fh = sm_manager_->fhs_.at(tab_name);
        const auto &prev_rec = fh->get_record(prev_rid, &context);

        // 回滚具体操作
        switch (wr.GetWriteType()) {
            case WType::INSERT_TUPLE : {
                // 回滚删除索引
                for (const auto &index : tab.indexes) {
                    const auto &index_name = sm_manager_->get_ix_manager()->get_index_name(tab_name, index.cols);
                    const auto &ih = sm_manager_->ihs_.at(index_name);

                    auto key = std::make_unique<char[]>(index.col_tot_len);
                    size_t offset = 0;
                    for (const auto &col : index.cols) {
                        std::copy_n(prev_rec->data + col.offset, col.len, key.get() + offset);
                        offset += col.offset;
                    }
                    ih->delete_entry(key.get(), txn);
                }
                // 回滚删除记录
                fh->delete_record(prev_rid, &context);
                break;
            }
            case WType::DELETE_TUPLE : {
                // 回滚插入记录 记录回滚时插入的位置
                const auto new_rid = fh->insert_record(prev_rec->data, &context);
                // 回滚插入索引
                for (const auto &index : tab.indexes) {
                    const auto &index_name = sm_manager_->get_ix_manager()->get_index_name(tab_name, index.cols);
                    const auto &ih = sm_manager_->ihs_.at(index_name);

                    auto key = std::make_unique<char[]>(index.col_tot_len);
                    size_t offset = 0;
                    for (const auto &col : index.cols) {
                        std::copy_n(prev_rec->data + col.offset, col.len, key.get() + offset);
                        offset += col.offset;
                    }
                    // 记录回滚时插入的位置
                    ih->insert_entry(key.get(), new_rid, txn);
                }
                break;
            }
            case WType::UPDATE_TUPLE : {
                const auto &new_rec = fh->get_record(prev_rid, &context);
                // 回滚修改记录
                fh->update_record(prev_rid, prev_rec->data, &context);
                // 回滚修改索引
                for (const auto &index : tab.indexes) {
                    const auto &index_name = sm_manager_->get_ix_manager()->get_index_name(tab_name, index.cols);
                    const auto &ih = sm_manager_->ihs_.at(index_name);

                    auto new_key = std::make_unique<char[]>(index.col_tot_len);
                    auto old_key = std::make_unique<char[]>(index.col_tot_len);
                    size_t offset = 0;
                    for (const auto &col : index.cols) {
                        std::copy_n(new_rec->data  + col.offset, col.len, new_key.get() + offset);
                        std::copy_n(prev_rec->data + col.offset, col.len, old_key.get() + offset);
                        offset += col.offset;
                    }

                    if (std::memcmp(new_key.get(), old_key.get(), index.col_tot_len) == 0)
                        continue;
                    ih->delete_entry(old_key.get(), txn);
                    ih->insert_entry(new_key.get(), prev_rid, txn);
                }
                break;
            }
        } // end of switch (wr.GetWriteType())
        delete write_set->back();
        write_set->pop_back();
    } // end of while (!write_set->empty())
    
    // 2. 释放所有锁
    // auto lock_set = txn->get_lock_set();
	// for (auto &lock: *lock_set) {
	// 	lock_manager_->unlock(txn, lock);
	// }
	// clean related resource
	// lock_set->clear();

    // 3. 清空事务相关资源，eg.锁集
    // 4. 把事务日志刷入磁盘中
    // 5. 更新事务状态
    txn->set_state(TransactionState::ABORTED);
}