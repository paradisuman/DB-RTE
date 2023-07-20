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

class SortExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> prev_;
    std::vector<std::pair<ColMeta, bool>> cols_;                              // 框架中只支持一个键排序，需要自行修改数据结构支持多个键排序
    size_t tuple_num;
    std::vector<size_t> used_tuple;
    std::unique_ptr<RmRecord> current_tuple;

    size_t tuple_ptr = 0;
    std::vector<std::unique_ptr<RmRecord>> all_record;
    int limit_;

   public:
    SortExecutor(std::unique_ptr<AbstractExecutor> prev, std::vector<std::pair<TabCol, bool>> sel_cols, int limit) {
        prev_ = std::move(prev);
        cols_.reserve(sel_cols.size());
        for (const auto &[sel_col, is_desc] : sel_cols)
            cols_.emplace_back(prev_->get_col_offset(sel_col), is_desc);
        limit_ = limit;

        tuple_num = 0;
        used_tuple.clear();
    }

    void beginTuple() override { 
        for (prev_->beginTuple(); !prev_->is_end(); prev_->nextTuple()) {
            all_record.push_back(prev_->Next());
        }
        std::stable_sort(
            all_record.begin(),
            all_record.end(),
            [&] (const std::unique_ptr<RmRecord> &a, const std::unique_ptr<RmRecord> &b) {
                for (const auto &[col, is_desc] : cols_) {
                    Value v1, v2;
                    v1.type = col.type;
                    v1.load_raw(col.len, a->data + col.offset);
                    v2.type = col.type;
                    v2.load_raw(col.len, b->data + col.offset);

                    bool is_gt = binop(OP_GT, v1, v2), is_lt = binop(OP_LT, v1, v2);
                    if (!is_gt && !is_lt)
                        continue;
                    return ((is_desc && is_gt) || (!is_desc && !is_gt)); // 推真值表可证
                }
                return false;
            }
        );
    }

    void nextTuple() override {
        tuple_ptr += 1;
    }

    std::unique_ptr<RmRecord> Next() override {
        return std::move(all_record[tuple_ptr]);
    }

    Rid &rid() override { return _abstract_rid; }

    virtual bool is_end() const { return ((limit_ == -1 || limit_ >= tuple_ptr) ? tuple_ptr == all_record.size() : tuple_ptr == (size_t)limit_); };

    virtual const std::vector<ColMeta> &cols() const override { return prev_->cols(); }
};