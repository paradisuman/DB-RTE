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
#include "executor_seq_scan.h"

class ProjectionExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> prev_;        // 投影节点的儿子节点
    std::vector<ColMeta> cols_;                     // 需要投影的字段
    size_t len_;                                    // 字段总长度
    std::vector<size_t> sel_idxs_;                  

   public:
    ProjectionExecutor(std::unique_ptr<AbstractExecutor> prev, const std::vector<TabCol> &sel_cols) {
        prev_ = std::move(prev);

        size_t curr_offset = 0;
        const auto &prev_cols = prev_->cols();
        for (auto &sel_col : sel_cols) {
            auto pos = get_col(prev_cols, sel_col);
            sel_idxs_.push_back(pos - prev_cols.begin());
            auto col = *pos;
            col.offset = curr_offset;
            curr_offset += col.len;
            cols_.push_back(col);
        }
        len_ = curr_offset;
    }

    virtual const std::vector<ColMeta> &cols() const override { return cols_; }

    void beginTuple() override {
        prev_->beginTuple();
    }

    void nextTuple() override {
        prev_->nextTuple();
    }

    virtual size_t tupleLen() const override { return len_; }

    std::unique_ptr<RmRecord> Next() override {
        // Make record buffer
        auto result = std::make_unique<RmRecord>(len_);
        auto prev_next = prev_->Next();
        for (const auto &col : cols_) {
            Value col_val;
            col_val.type = col.type;
            const auto &prev_cols = prev_->cols();
            const auto &raw_col = *std::find_if(
                prev_cols.begin(),
                prev_cols.end(),
                [&] (const auto &prev_col) { return col.name == prev_col.name && col.tab_name == prev_col.tab_name; }
            );
            std::copy_n(prev_next->data + raw_col.offset, col.len, result->data + col.offset);
        }
        return result;
    }

    bool is_end() const override { return prev_->is_end(); }

    Rid &rid() override { return _abstract_rid; }
};