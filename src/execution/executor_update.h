/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once
#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"

#include <algorithm>

class UpdateExecutor : public AbstractExecutor {
   private:
    TabMeta tab_;
    std::vector<Condition> conds_;
    RmFileHandle *fh_;
    std::vector<Rid> rids_;
    std::string tab_name_;
    std::vector<SetClause> set_clauses_;
    SmManager *sm_manager_;

   public:
    UpdateExecutor(SmManager *sm_manager, const std::string &tab_name, std::vector<SetClause> set_clauses,
                   std::vector<Condition> conds, std::vector<Rid> rids, Context *context) {
        sm_manager_ = sm_manager;
        tab_name_ = tab_name;
        set_clauses_ = set_clauses;
        tab_ = sm_manager_->db_.get_table(tab_name);
        fh_ = sm_manager_->fhs_.at(tab_name).get();
        conds_ = conds;
        rids_ = rids;
        context_ = context;
        context_->lock_mgr_->lock_exclusive_on_table(context_->txn_, fh_->GetFd());
    }
    std::unique_ptr<RmRecord> Next() override {
        
        // 符合条件字段在rids中
        // 对每行数据进行更新
        for (const auto &rid : rids_) {
            // 获取原信息
            auto target_record = fh_->get_record(rid, context_);
            RmRecord new_rcd(fh_->get_file_hdr().record_size, target_record->data);


            // 更新数据
            for (const auto &x : set_clauses_) {
                const auto &col = *tab_.get_col(x.lhs.col_name);
                auto val = x.rhs;
                if (!is_compatible_type(col.type, val.type)) {
                    throw IncompatibleTypeError(coltype2str(col.type), coltype2str(val.type));
                }
                // 新纪录在这里
                if (x.is_selfadd) {
                    Value old_val;
                    old_val.type = col.type;
                    old_val.load_raw(col.len, target_record->data + col.offset);
                    switch (col.type) {
                        case TYPE_INT : val.int_val += old_val.int_val; break;
                        case TYPE_BIGINT : val.bigint_val += old_val.bigint_val; break;
                        case TYPE_FLOAT : val.float_val += old_val.float_val; break;
                        default : throw InternalError("Unsupported operation.");
                    }
                    val.init_raw(col.len);
                }
                memcpy(new_rcd.data + col.offset, val.raw->data, col.len);
            }

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
                if (strncmp(key1, key2, index.cols[i].len) != 0 && ih->is_key_exist(key1, context_->txn_)) {
                    throw RMDBError("update index unique error!");
                }
            }

            // 日志落盘
            auto update_log_record = UpdateLogRecord(context_->txn_->get_transaction_id(), target_record, new_rcd, rid, tab_name_);
            context_->log_mgr_->add_log_to_buffer(update_log_record);
            context_->log_mgr_->flush_log_to_disk();

            // TODO 索引日志

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
                ih->delete_entry(old_key, context_->txn_);
                // 更新记录
                ih->insert_entry(new_key, rid, context_->txn_);
            }

            // 更新record
            fh_->update_record(rid, new_rcd.data, context_);

            WriteRecord *wr = new WriteRecord(WType::UPDATE_TUPLE, tab_name_, rid, *target_record);
            context_->txn_->append_write_record(wr);
        }
        return nullptr;
    }

    Rid &rid() override { return _abstract_rid; }
};