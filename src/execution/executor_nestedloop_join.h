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
#include "execution/executor_utils.hpp"

class NestedLoopJoinExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> left_;    // 左儿子节点（需要join的表）
    std::unique_ptr<AbstractExecutor> right_;   // 右儿子节点（需要join的表）
    size_t left_len_, right_len_;
    size_t len_;                                // join后获得的每条记录的长度
    std::vector<ColMeta> cols_;                 // join后获得的记录的字段

    std::vector<Condition> fed_conds_;          // join条件
    bool isend;

    bool _checkCond() { return executor_utils::checkConds(this->Next(), fed_conds_, cols_); }
    

   public:
    NestedLoopJoinExecutor(std::unique_ptr<AbstractExecutor> left, std::unique_ptr<AbstractExecutor> right, 
                            std::vector<Condition> conds) {
        left_ = std::move(left);
        right_ = std::move(right);
        left_len_ = left_->tupleLen();
        right_len_ = right_->tupleLen();
        len_ = left_len_ + right_len_;
        cols_ = left_->cols();
        auto right_cols = right_->cols();
        for (auto &col : right_cols) {
            col.offset += left_->tupleLen();
        }

        cols_.insert(cols_.end(), right_cols.begin(), right_cols.end());
        isend = false;
        fed_conds_ = std::move(conds);

    }

    virtual bool is_end() const override { return isend; }

    virtual size_t tupleLen() const override { return len_; }

    virtual const std::vector<ColMeta> &cols() const override { return cols_; }

    void beginTuple() override {
        left_->beginTuple();
        right_->beginTuple();

        for (; !right_->is_end(); right_->nextTuple()) {
            for (; !left_->is_end(); left_->nextTuple()) {
                if (!_checkCond())
                    continue;
                return;
            }
            left_->beginTuple();
        }
    }

    void nextTuple() override {
        left_->nextTuple();
        for (; !right_->is_end(); right_->nextTuple()) {
            for (; !left_->is_end(); left_->nextTuple()) {
                if (!_checkCond())
                    continue;
                return;
            }
            left_->beginTuple();
        }
        isend = true;
    }

    std::unique_ptr<RmRecord> Next() override {
        auto current_record = std::make_unique<RmRecord>(len_);
        std::copy_n(left_->Next()->data, left_len_, current_record->data);
        std::copy_n(right_->Next()->data, right_len_, current_record->data + left_len_);

        return current_record;
    }

    Rid &rid() override { return _abstract_rid; }
};