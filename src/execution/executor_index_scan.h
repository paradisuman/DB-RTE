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
#include "execution/executor_utils.hpp"

#include <cassert>
#include <set>

class IndexScanExecutor : public AbstractExecutor {
   private:
    std::string tab_name_;                      // 表名称
    TabMeta tab_;                               // 表的元数据
    std::vector<Condition> conds_;              // 扫描条件
    RmFileHandle *fh_;                          // 表的数据文件句柄
    IxIndexHandle  *ih_;                        // 表的索引句柄
    std::vector<ColMeta> cols_;                 // 需要读取的字段
    size_t len_;                                // 选取出来的一条记录的长度
    std::vector<Condition> fed_conds_;          // 扫描条件，和conds_字段相同

    std::vector<std::string> index_col_names_;  // index scan涉及到的索引包含的字段
    IndexMeta index_meta_;                      // index scan涉及到的索引元数据

    Rid rid_;
    std::unique_ptr<RecScan> scan_;

    SmManager *sm_manager_;
    

    const std::vector<ColMeta> &cols() const {
        return cols_;
    };

    bool _checkConds() {
        auto a = scan_->rid();
        auto b = fh_->get_record(scan_->rid(), context_);
        return executor_utils::checkConds(
            fh_->get_record(scan_->rid(), context_),
            conds_,
            cols_
        );
    }

    // 确定初始范围
    void init_key_range (RmRecord *min_rm, RmRecord *max_rm) {
        size_t offset = 0;
        // 检查更新范围
        for (auto &col : index_meta_.cols) {
            Value tem_min{col.type}, tem_max{col.type};
            // 设置默认最大，最小值
            switch (col.type)
            {
                case TYPE_INT: {
                    tem_max.set_int(INT32_MAX); tem_max.init_raw(col.len);
                    tem_min.set_int(INT32_MIN); tem_min.init_raw(col.len);
                    break;
                }
            
                case TYPE_FLOAT: {
                    tem_max.set_float(__FLT_MAX__);  tem_max.init_raw(col.len);
                    tem_min.set_float(-__FLT_MAX__); tem_min.init_raw(col.len);
                    break;
                }
                
                case TYPE_STRING : {
                    tem_max.set_str(std::string(col.len, 255));  tem_max.init_raw(col.len);
                    tem_min.set_str(std::string(col.len, 0)); tem_min.init_raw(col.len);
                    break;
                }

                case TYPE_DATETIME : {
                    /* fix */
                    throw RMDBError("TYPE_DATETIME todo");
                }

                case TYPE_BIGINT : {
                    throw RMDBError("TYPE_BIGINT todo");
                }

                default:{
                    throw InvalidTypeError();
                }
            }

            // 更新,这里只需要某些操作符，不等这种不需要考虑，到时候跑的时候再测试一遍
            for (auto &cond : fed_conds_) {
                // 直接遍历找到每个col的判断类型
                if(cond.lhs_col.col_name == col.name && cond.is_rhs_val) {
                    switch (cond.op)
                    {
                    case OP_EQ: {
                        if(binop(OP_GT, cond.rhs_val, tem_min)) {
                            tem_min = cond.rhs_val;
                        }
                        if(binop(OP_LT, cond.rhs_val, tem_max)) {
                            tem_max = cond.rhs_val;
                        }
                        break;
                    }
                    // >和>= ，更新最小值
                    case OP_GT: { }
                    case OP_GE: {
                        if(binop(OP_GT, cond.rhs_val, tem_min)) {
                            tem_min = cond.rhs_val;
                        }
                        break;
                    }
                    // <和<= ，更新最大值
                    case OP_LT: { }
                    case OP_LE: {
                        if(binop(OP_LT, cond.rhs_val, tem_max)) {
                            tem_max = cond.rhs_val;
                        }
                        break;
                    }
                    // != 这里后续再检测
                    case OP_NE: {
                        break;
                    }
                    default:
                        break;
                    }
                }
            }
            // 加一个assert判定
            // assert binop(OP_LE, tem_min, tem_max);
            std::copy_n(tem_max.raw->data, col.len, max_rm->data + offset);
            std::copy_n(tem_min.raw->data, col.len, min_rm->data + offset);
            offset += col.len;
        }   
    }


   public:
    IndexScanExecutor(SmManager *sm_manager, std::string tab_name, std::vector<Condition> conds, std::vector<std::string> index_col_names,
                    Context *context) {
        sm_manager_ = sm_manager;
        context_ = context;
        tab_name_ = std::move(tab_name);
        tab_ = sm_manager_->db_.get_table(tab_name_);
        conds_ = std::move(conds);
        // index_no_ = index_no;
        index_col_names_ = index_col_names; 
        index_meta_ = *(tab_.get_index_meta(index_col_names_));
        fh_ = sm_manager_->fhs_.at(tab_name_).get();

        // 获取index handler
        ih_ = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index_meta_.cols)).get();
        cols_ = tab_.cols;
        len_ = cols_.back().offset + cols_.back().len;
        std::map<CompOp, CompOp> swap_op = {
            {OP_EQ, OP_EQ}, {OP_NE, OP_NE}, {OP_LT, OP_GT}, {OP_GT, OP_LT}, {OP_LE, OP_GE}, {OP_GE, OP_LE},
        };

        // scan右值为值或者右值为同一张表
        for (auto &cond : conds_) {
            // 这种情况如何出现？
            if (cond.lhs_col.tab_name != tab_name_) {
                // lhs is on other table, now rhs must be on this table
                assert(!cond.is_rhs_val && cond.rhs_col.tab_name == tab_name_);
                // swap lhs and rhs
                std::swap(cond.lhs_col, cond.rhs_col);
                cond.op = swap_op.at(cond.op);
            }
        }
        fed_conds_ = conds_;
    }

    void beginTuple() override {
        static int cnt = 0;
        cnt++;
        // 初步确定最大值和最小值
        RmRecord  max_key(index_meta_.col_tot_len), min_key(index_meta_.col_tot_len);

        // 构建初步范围
        init_key_range(&min_key, &max_key);

        // 建立一个迭代器，每次选择迭代一个
        auto min_ = ih_->lower_bound(min_key.data);
        auto max_ = ih_->upper_bound(min_key.data);
        scan_ = std::make_unique<IxScan>(
            ih_,
            min_,
            max_,
            sm_manager_->get_bpm()
        );
        auto a = scan_->rid();


        // 在范围中和seq同理
        while (!scan_->is_end()) {
            if (_checkConds()) {
                rid_ = scan_->rid();
                return;
            }
            scan_->next();
        }
        
    }

    void nextTuple() override {
        while (scan_->next(), !scan_->is_end()) {
            if (!_checkConds())
                continue;
            rid_ = scan_->rid();
            return;
        }
    }

    std::unique_ptr<RmRecord> Next() override {
        assert(!is_end());
        return fh_->get_record(rid_,context_);
    }

    Rid &rid() override { return rid_; }

    std::string getType() { return "IndexScanExecutor"; };

    bool is_end() const { return scan_->is_end(); };

};