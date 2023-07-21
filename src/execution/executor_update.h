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

        // 预处理 Value
        for (auto &x : set_clauses_) {
            x.rhs.init_raw(tab_.get_col(x.lhs.col_name)->len);
        }
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
                auto &val = x.rhs;
                if (!is_compatible_type(col.type, val.type)) {
                    throw IncompatibleTypeError(coltype2str(col.type), coltype2str(val.type));
                }
                // 新纪录在这里
                memcpy(new_rcd.data + col.offset, val.raw->data, col.len);
            }
            

            std::vector<std::unique_ptr<char[]>> keys;
            // 检查索引唯一性
            for(size_t i = 0; i < tab_.indexes.size(); ++i) {
                auto& index = tab_.indexes[i];
                auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
                keys.emplace_back(new char[index.col_tot_len]);
                auto key = keys.back().get();
                int offset = 0;
                for(size_t i = 0; i < index.col_num; ++i) {
                    memcpy(key + offset, new_rcd.data + index.cols[i].offset, index.cols[i].len);
                    offset += index.cols[i].len;
                }
                if (ih->is_key_exist(key, context_->txn_)) {
                    throw RMDBError("index unique error!");
                }
            }

            // 删除索引
            for(size_t i = 0; i < tab_.indexes.size(); ++i) {
                auto& index = tab_.indexes[i];
                auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
                char key[index.col_tot_len];
                int offset = 0;
                for(size_t i = 0; i < index.col_num; ++i) {
                    memcpy(key + offset, target_record->data + index.cols[i].offset, index.cols[i].len);
                    offset += index.cols[i].len;
                }
                ih->delete_entry(key, context_->txn_);
            }

            // 插入索引
            for(size_t i = 0; i < tab_.indexes.size(); ++i) {
                auto& index = tab_.indexes[i];
                auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
                auto key = keys[i].get();
                ih->insert_entry(key, rid, context_->txn_);
            }

            // 更新记录
            fh_->update_record(rid, new_rcd.data, context_);
        }
        return nullptr;
    }

    Rid &rid() override { return _abstract_rid; }
};