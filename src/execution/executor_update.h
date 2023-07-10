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
    }
    std::unique_ptr<RmRecord> Next() override {
        // 符合条件字段在rids中
        // 对每行数据进行更新
        for (size_t i = 0; i < rids_.size(); i++) {
            // 获取原信息
            RmRecord update = *fh_->get_record(rids_[i], context_);
            // 更新数据
            for(auto &x : set_clauses_){
                auto col = *tab_.get_col(x.lhs.col_name);
                auto &val = x.rhs;
                if (col.type != val.type) {
                    throw IncompatibleTypeError(coltype2str(col.type), coltype2str(val.type));
                }
                val.init_raw(col.len);
                memcpy(update.data + col.offset, val.raw->data, col.len);
            }
            // 更新记录
            fh_->update_record(rids_[i], update.data, context_);

            // 更新索引
            for(size_t i = 0; i < tab_.indexes.size(); ++i) {
                auto& index = tab_.indexes[i];
                auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
                char* key = new char[index.col_tot_len];
                int offset = 0;
                for(size_t i = 0; i < index.col_num; ++i) {
                    memcpy(key + offset, update.data + index.cols[i].offset, index.cols[i].len);
                    offset += index.cols[i].len;
                }
                ih->insert_entry(key, rids_[i], context_->txn_);
            }
        }
        return nullptr;
    }

    Rid &rid() override { return _abstract_rid; }
};