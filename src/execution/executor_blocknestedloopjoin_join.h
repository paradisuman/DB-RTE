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

#include <memory>
#include <vector>

#include "defs.h"
#include "common/common.h"
#include "record/rm_defs.h"
#include "record/rm_file_handle.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "system/sm_meta.h"

#include "execution/executor_utils.hpp"

class BlockNestedLoopJoinExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> left_;
    std::unique_ptr<AbstractExecutor> right_;

    size_t len_, left_len, right_len;
    std::vector<ColMeta> cols_;

    static constexpr int MAX_BUFFER_SIZE = 2048;
    bool left_isend = false;
    size_t left_ptr = 0, left_size = 0;
    std::array<std::unique_ptr<RmRecord>, MAX_BUFFER_SIZE> left_buffer;

    std::vector<Condition> fed_conds_;


    void load_left_buffer() {
        left_ptr = 0;
        left_isend = left_->is_end();
        for (left_size = 0; !left_->is_end() && left_size < MAX_BUFFER_SIZE; left_->nextTuple()) {
            left_buffer[left_size++] = std::move(left_->Next());
        }
    }

public:
    BlockNestedLoopJoinExecutor(std::unique_ptr<AbstractExecutor> left, std::unique_ptr<AbstractExecutor> right, std::vector<Condition> conds) {
        left_ = std::move(left);
        right_ = std::move(right);

        left_len = left_->tupleLen();
        right_len = right_->tupleLen();
        len_ = left_len + right_len;
    
        cols_ = left_->cols();
        auto right_cols = right_->cols();
        for (auto &col: right_cols) {
            col.offset += left_->tupleLen();
        }
        cols_.insert(cols_.end(), right_cols.begin(), right_cols.end());

        fed_conds_ = std::move(conds);
    }

    void beginTuple() override {
        left_->beginTuple();
        if (left_->is_end())
            return;
        right_->beginTuple();
        if (right_->is_end())
            return;
        load_left_buffer();
        left_ptr = -1;
        nextTuple();
    }

    void nextTuple() override {
        left_ptr += 1;
        while (!left_->is_end() || !right_->is_end()) {
            if (left_ptr == left_size) {
                right_->nextTuple();
                if (right_->is_end() && left_->is_end())
                    return;
                if (right_->is_end()) {
                    right_->beginTuple();
                    load_left_buffer();
                }
                left_ptr = 0;
            }
            for (; left_ptr < left_size; left_ptr += 1) {
                if (!executor_utils::checkConds(this->Next(), fed_conds_, cols_))
                    continue;
                return;
            }
        }
    }

    virtual std::unique_ptr<RmRecord> Next() override {
        auto current_record = std::make_unique<RmRecord>(len_);
        std::copy_n(left_buffer[left_ptr]->data, left_len, current_record->data);
        auto right_record = right_->Next();
        std::copy_n(right_record->data, right_len, current_record->data + left_len);

        return current_record;
    }

    bool is_end() const override { return left_->is_end() && right_->is_end(); }

    const std::vector<ColMeta> &cols() const override { return cols_; }

    size_t tupleLen() const override { return len_; }

    virtual ColMeta get_col_offset(const TabCol &target) override {
        auto ret = std::find_if(
            cols_.begin(),
            cols_.end(),
            [&] (const ColMeta &col) { return col.tab_name == target.tab_name && col.name == target.col_name; }
        );
        if (ret != cols_.end())
            return *ret;
        throw ColumnNotFoundError(target.tab_name + '.' + target.col_name);
    }
};
