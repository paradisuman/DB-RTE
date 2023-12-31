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

class InsertExecutor : public AbstractExecutor {
   private:
    TabMeta tab_;                   // 表的元数据
    std::vector<Value> values_;     // 需要插入的数据
    RmFileHandle *fh_;              // 表的数据文件句柄
    std::string tab_name_;          // 表名称
    Rid rid_;                       // 插入的位置，由于系统默认插入时不指定位置，因此当前rid_在插入后才赋值
    SmManager *sm_manager_;

   public:
    InsertExecutor(SmManager *sm_manager, const std::string &tab_name, std::vector<Value> values, Context *context) {
        sm_manager_ = sm_manager;
        tab_ = sm_manager_->db_.get_table(tab_name);
        values_ = values;
        tab_name_ = tab_name;
        if (values.size() != tab_.cols.size()) {
            throw InvalidValueCountError();
        }
        fh_ = sm_manager_->fhs_.at(tab_name).get();
        context_ = context;
        context_->lock_mgr_->lock_IX_on_table(context_->txn_, fh_->GetFd());
    };

    std::unique_ptr<RmRecord> Next() override {
        
        RmRecord rec(fh_->get_file_hdr().record_size);

        // Make record buffer
        for (size_t i = 0; i < values_.size(); i++) {
            auto &col = tab_.cols[i];
            auto &val = values_[i];
            if (!is_compatible_type(col.type, val.type)) {
                throw IncompatibleTypeError(coltype2str(col.type), coltype2str(val.type));
            }
            val.init_raw(col.len);
            memcpy(rec.data + col.offset, val.raw->data, col.len);
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
                memcpy(key + offset, rec.data + index.cols[i].offset, index.cols[i].len);
                offset += index.cols[i].len;
            }
            if (ih->is_key_exist(key, context_->txn_))
                throw RMDBError("index unique error!");
        }

        // Insert into record file
        rid_ = fh_->insert_record(rec.data, context_);

        // 日志落盘
        auto insert_log_record = InsertLogRecord(context_->txn_->get_transaction_id(), rec, rid_, tab_name_);
        insert_log_record.prev_lsn_ = context_->txn_->get_prev_lsn();
        auto last_lsn = context_->log_mgr_->add_log_to_buffer(&insert_log_record);
        // context_->log_mgr_->flush_log_to_disk();
        context_->txn_->set_prev_lsn(last_lsn);

        // 插入索引
        for(size_t i = 0; i < tab_.indexes.size(); ++i) {
            auto &index = tab_.indexes[i];
            auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
            auto key = keys[i].get();
            ih->insert_entry(key, rid_, context_->txn_);
        }

        WriteRecord *wr = new WriteRecord(WType::INSERT_TUPLE, tab_name_, rid_);
        context_->txn_->append_write_record(wr);

        return nullptr;
    }
    Rid &rid() override { return rid_; }
};