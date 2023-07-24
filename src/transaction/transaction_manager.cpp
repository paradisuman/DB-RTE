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
    // auto write_set_ = txn->get_write_set();
    // while (!write_set_->empty()) {
    //     auto x = write_set_->back();
    //     // 确保栈内存被释放
    //     delete x;
    //     write_set_->pop_back();
    // }
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
    auto lock_set = txn->get_lock_set();
    for (auto &lock: *lock_set) {
		lock_manager_->unlock(txn, lock);
	}
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
void TransactionManager::abort(Transaction * txn, LogManager *log_manager) {
    // Todo:
    // 1. 回滚所有写操作
    auto write_set_ = txn->get_write_set();
    while (!write_set_->empty()) {
        // 1.Todo 目前commit不修改内存，abort直接进行内存操作 将内存写回磁盘
        // 2.Todo 目前commit不修改索引，abort直接进行索引操作 将索引写回磁盘
        auto wr = write_set_->back();
        Rid pre_rid_ = wr->GetRid();
        std::string tab_name_ = wr->GetTableName();
        // Todo 这里context,以及其成员等都直接使用了nullptr
        if (wr->GetWriteType() == WType::INSERT_TUPLE) {
            auto tab_ = sm_manager_->db_.get_table(tab_name_);
            auto fh_ = sm_manager_->fhs_.at(tab_name_).get();
            auto rec = fh_->get_record(pre_rid_, nullptr);

            for(size_t i = 0; i < tab_.indexes.size(); ++i) {
                IndexMeta& index = tab_.indexes[i];
                IxIndexHandle *ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
                char key[index.col_tot_len];
                int offset = 0;
                for(size_t i = 0; i < index.col_num; ++i) {
                    memcpy(key + offset, rec->data + index.cols[i].offset, index.cols[i].len);
                    offset += index.cols[i].len;
                }
                ih->delete_entry(key, nullptr);
            }

             // 删除record
            fh_->delete_record(pre_rid_, nullptr);
        }
        else if (wr->GetWriteType() == WType::DELETE_TUPLE) {
            auto tab_ = sm_manager_->db_.get_table(tab_name_);
            auto fh_ = sm_manager_->fhs_.at(tab_name_).get();
            RmRecord pre_record_ = wr->GetRecord();
            // Insert into record file
            fh_->insert_record(pre_record_.data, nullptr);

            std::vector<std::unique_ptr<char[]>> keys;
            // 检查索引唯一性
            for(size_t i = 0; i < tab_.indexes.size(); ++i) {
                auto& index = tab_.indexes[i];
                auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
                keys.emplace_back(new char[index.col_tot_len]);
                auto key = keys.back().get();
                int offset = 0;
                for(size_t i = 0; i < index.col_num; ++i) {
                    memcpy(key + offset, pre_record_.data + index.cols[i].offset, index.cols[i].len);
                    offset += index.cols[i].len;
                }
                if (ih->is_key_exist(key, nullptr)) {
                    fh_->delete_record(pre_rid_, nullptr);
                    throw RMDBError("index unique error!");
                }
            }
            // 插入索引
            for(size_t i = 0; i < tab_.indexes.size(); ++i) {
                auto &index = tab_.indexes[i];
                auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
                auto key = keys[i].get();
                ih->insert_entry(key, pre_rid_, nullptr);
            }
        }
        else if (wr->GetWriteType() == WType::UPDATE_TUPLE) {
            RmRecord pre_record_ = wr->GetRecord();
            auto tab_ = sm_manager_->db_.get_table(tab_name_);
            auto fh_ = sm_manager_->fhs_.at(tab_name_).get();
            auto target_record = fh_->get_record(pre_rid_, nullptr);
            RmRecord new_rcd = pre_record_;

            std::vector<std::unique_ptr<char[]>> new_keys;
            std::vector<std::unique_ptr<char[]>> old_keys;
            // 检查索引唯一性
            for(size_t i = 0; i < tab_.indexes.size(); ++i) {
                auto& index = tab_.indexes[i];
                auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
                new_keys.emplace_back(new char[index.col_tot_len]);
                old_keys.emplace_back(new char[index.col_tot_len]);
                auto key1 = new_keys.back().get();
                auto key2 = old_keys.back().get();
                int offset = 0;
                for(size_t i = 0; i < index.col_num; ++i) {
                    memcpy(key1 + offset, new_rcd.data + index.cols[i].offset, index.cols[i].len);
                    offset += index.cols[i].len;
                }
                offset = 0;
                for(size_t i = 0; i < index.col_num; ++i) {
                    memcpy(key2 + offset, target_record->data + index.cols[i].offset, index.cols[i].len);
                    offset += index.cols[i].len;
                }
                if (strncmp(key1, key2, index.cols[i].len) != 0 && ih->is_key_exist(key1, nullptr)) {
                    throw RMDBError("update index unique error!");
                }
            }

            // 删除和插入索引
            for(size_t i = 0; i < tab_.indexes.size(); ++i) {
                auto& index = tab_.indexes[i];
                auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
                auto old_key = old_keys[i].get();
                auto new_key = new_keys[i].get();
                // 相同的key就不用管了
                if (strncmp(old_key, new_key, index.cols[i].len) == 0) {
                    continue;
                }
                ih->delete_entry(old_key, nullptr);
                // 更新记录
                ih->insert_entry(new_key, pre_rid_, nullptr);
            }

            // 更新record
            fh_->update_record(pre_rid_, new_rcd.data, nullptr);
        }
        else throw RMDBError("No transaction type exists");
        // 释放资源
        write_set_->pop_back();
        delete wr;
    }
    write_set_->clear();
    // 2. 释放所有锁
    auto lock_set = txn->get_lock_set();
	for (auto &lock: *lock_set) {
		lock_manager_->unlock(txn, lock);
	}
	// clean related resource
	lock_set->clear();

    // 3. 清空事务相关资源，eg.锁集
    // 4. 把事务日志刷入磁盘中
    // 5. 更新事务状态
    txn->set_state(TransactionState::ABORTED);
}