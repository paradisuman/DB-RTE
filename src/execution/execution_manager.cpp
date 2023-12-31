/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "execution_manager.h"
#include "errors.h"

#include "executor_delete.h"
#include "executor_index_scan.h"
#include "executor_insert.h"
#include "executor_nestedloop_join.h"
#include "executor_projection.h"
#include "executor_seq_scan.h"
#include "executor_update.h"
#include "index/ix.h"
#include "record_printer.h"
#include "common/datetime_utils.hpp"

#include <fstream>
#include <boost/tokenizer.hpp>

const char *help_info = "Supported SQL syntax:\n"
                   "  command ;\n"
                   "command:\n"
                   "  CREATE TABLE table_name (column_name type [, column_name type ...])\n"
                   "  DROP TABLE table_name\n"
                   "  CREATE INDEX table_name (column_name)\n"
                   "  DROP INDEX table_name (column_name)\n"
                   "  INSERT INTO table_name VALUES (value [, value ...])\n"
                   "  DELETE FROM table_name [WHERE where_clause]\n"
                   "  UPDATE table_name SET column_name = value [, column_name = value ...] [WHERE where_clause]\n"
                   "  SELECT selector FROM table_name [WHERE where_clause]\n"
                   "type:\n"
                   "  {INT | FLOAT | CHAR(n)}\n"
                   "where_clause:\n"
                   "  condition [AND condition ...]\n"
                   "condition:\n"
                   "  column op {column | value}\n"
                   "column:\n"
                   "  [table_name.]column_name\n"
                   "op:\n"
                   "  {= | <> | < | > | <= | >=}\n"
                   "selector:\n"
                   "  {* | column [, column ...]}\n";

// 主要负责执行DDL语句
void QlManager::run_mutli_query(std::shared_ptr<Plan> plan, Context *context){
    if (auto x = std::dynamic_pointer_cast<DDLPlan>(plan)) {
        switch(x->tag) {
            case T_CreateTable:
            {
                sm_manager_->create_table(x->tab_name_, x->cols_, context);
                break;
            }
            case T_DropTable:
            {
                sm_manager_->drop_table(x->tab_name_, context);
                break;
            }
            case T_CreateIndex:
            {
                sm_manager_->create_index(x->tab_name_, x->tab_col_names_, context);
                break;
            }
            case T_DropIndex:
            {
                sm_manager_->drop_index(x->tab_name_, x->tab_col_names_, context);
                break;
            }
            default:
                throw InternalError("Unexpected field type");
                break;  
        }
    }
}

// 执行help; show tables; desc table; begin; commit; abort;语句
void QlManager::run_cmd_utility(std::shared_ptr<Plan> plan, txn_id_t *txn_id, Context *context) {
    if (auto x = std::dynamic_pointer_cast<OtherPlan>(plan)) {
        switch(x->tag) {
            case T_Help:
            {
                memcpy(context->data_send_ + *(context->offset_), help_info, strlen(help_info));
                *(context->offset_) = strlen(help_info);
                break;
            }
            case T_ShowTable:
            {
                sm_manager_->show_tables(context);
                break;
            }
            case T_ShowIndex:
            {
                sm_manager_->show_index(x->tab_name_, context);
                break;
            }
            case T_DescTable:
            {
                sm_manager_->desc_table(x->tab_name_, context);
                break;
            }
            case T_Transaction_begin:
            {
                // 显示开启一个事务
                context->txn_->set_txn_mode(true);
                break;
            }  
            case T_Transaction_commit:
            {
                context->txn_ = txn_mgr_->get_transaction(*txn_id);
                txn_mgr_->commit(context->txn_, context->log_mgr_);
                break;
            }    
            case T_Transaction_rollback:
            {
                context->txn_ = txn_mgr_->get_transaction(*txn_id);
                txn_mgr_->abort(context->txn_, context->log_mgr_);
                break;
            }    
            case T_Transaction_abort:
            {
                context->txn_ = txn_mgr_->get_transaction(*txn_id);
                txn_mgr_->abort(context->txn_, context->log_mgr_);
                break;
            }     
            default:
                throw InternalError("Unexpected field type");
                break;                        
        }

    }
}

// 执行select语句，select语句的输出除了需要返回客户端外，还需要写入output.txt文件中
void QlManager::select_from(std::unique_ptr<AbstractExecutor> executorTreeRoot, std::vector<TabCol> sel_cols, SelectTag tag,
                        Context *context) {
    std::vector<std::string> captions;
    captions.reserve(sel_cols.size());
    for (auto &sel_col : sel_cols) {
        captions.push_back(sel_col.col_name);
    }

    // print header into file
    std::fstream outfile;
    if (output2file) {
        outfile.open("output.txt", std::ios::out | std::ios::app);
    }
    // Print header into buffer
    RecordPrinter rec_printer(sel_cols.size());
    switch (tag) {
    case (ONE_SELECT) : {
        rec_printer.print_separator(context);
        rec_printer.print_record(captions, context);
        rec_printer.print_separator(context);

        if (output2file) {
            outfile << "|";
            for(size_t i = 0; i < captions.size(); ++i) {
                outfile << " " << captions[i] << " |";
            }
            outfile << "\n";
        }

        // Print records
        size_t num_rec = 0;
        // 执行query_plan
        for (executorTreeRoot->beginTuple(); !executorTreeRoot->is_end(); executorTreeRoot->nextTuple()) {
            auto Tuple = executorTreeRoot->Next();
            std::vector<std::string> columns;
            for (auto &col : executorTreeRoot->cols()) {
                std::string col_str;
                char *rec_buf = Tuple->data + col.offset;
                if (col.type == TYPE_INT) {
                    col_str = std::to_string(*(int *)rec_buf);
                } else if (col.type == TYPE_BIGINT) {
                    col_str = std::to_string(*(int64_t *)rec_buf);
                } else if (col.type == TYPE_FLOAT) {
                    col_str = std::to_string(*(float *)rec_buf);
                } else if (col.type == TYPE_DATETIME) {
                    col_str = datetime::to_string((char *)rec_buf);
                } else if (col.type == TYPE_STRING) {
                    col_str = std::string((char *)rec_buf, col.len);
                    col_str.resize(strlen(col_str.c_str()));
                }
                columns.push_back(col_str);
            }
            // print record into buffer
            rec_printer.print_record(columns, context);
            // print record into file
            if (output2file) {
                outfile << "|";
                for(size_t i = 0; i < columns.size(); ++i) {
                    outfile << " " << columns[i] << " |";
                }
                outfile << "\n";
            }
            num_rec++;
        }
        // Print footer into buffer
        rec_printer.print_separator(context);
        // Print record count into buffer
        RecordPrinter::print_record_count(num_rec, context);
        return;
    }
    case (SELECT_WITH_UNIQUE_COUNT) :
    case (SELECT_WITH_COUNT) : {
        rec_printer.print_separator(context);
        rec_printer.print_record(captions, context);
        rec_printer.print_separator(context);
        // print header into file
        if (output2file) {
            outfile << "|";
            for(size_t i = 0; i < captions.size(); ++i) {
                outfile << " " << captions[i] << " |";
            }
            outfile << "\n";
        }

        // Print records
        size_t num_rec = 0;
        // 执行query_plan
        for (executorTreeRoot->beginTuple(); !executorTreeRoot->is_end(); executorTreeRoot->nextTuple()) {
            num_rec++;
        }
        // print record into buffer
        rec_printer.print_record({std::to_string(num_rec)}, context);
        // print record into file
        if (output2file) {
            outfile << "|" << " " << std::to_string(num_rec) << " |" << '\n';
        }
        // Print footer into buffer
        rec_printer.print_separator(context);
        // Print record count into buffer
        RecordPrinter::print_record_count(1, context);
        return;
    }
    // MAX()
    case (SELECT_WITH_MAX) : {
        rec_printer.print_separator(context);
        rec_printer.print_record(captions, context);
        rec_printer.print_separator(context);
        // print header into file
        if (output2file) {
            outfile << "|";
            for(size_t i = 0; i < captions.size(); ++i) {
                outfile << " " << captions[i] << " |";
            }
            outfile << "\n";
        }

        const auto &col = executorTreeRoot->cols().front();
        // Print records
        size_t num_rec = 0;
        // 执行query_plan
        std::unique_ptr<RmRecord> Tuple;
        Value val;
        val.type = col.type;
        val.type = col.type;
        if (col.type == TYPE_INT)
            val.int_val = 0;
        else if (col.type == TYPE_BIGINT)
            val.bigint_val = 0;
        else if (col.type == TYPE_FLOAT)
            val.float_val = 0;
        else if (col.type == TYPE_STRING)
            val.str_val = std::string();
        for (executorTreeRoot->beginTuple(); !executorTreeRoot->is_end(); executorTreeRoot->nextTuple()) {
            auto tempTuple = executorTreeRoot->Next();
            Value tempVal;
            tempVal.type = col.type;
            tempVal.load_raw(col.len, tempTuple->data + col.offset);
            if (!Tuple.get()) {
                Tuple = std::move(tempTuple);
                val = tempVal;
                continue;
            }
            if (binop(OP_LT, val, tempVal)) {
                Tuple = std::move(tempTuple);
                val = tempVal;
            }
            num_rec++;
        }
        if (Tuple.get()) {
            std::vector<std::string> columns;
            switch (col.type) {
                case TYPE_INT : columns.push_back(std::to_string(*(int *)Tuple->data)); break;
                case TYPE_BIGINT : columns.push_back(std::to_string(*(int64_t *)Tuple->data)); break;
                case TYPE_FLOAT : columns.push_back(std::to_string(*(float *)Tuple->data)); break;
                case TYPE_STRING : columns.push_back(std::string((char *)Tuple->data, col.len)); break;
                default : throw InternalError("Unsupported type.");
            }
            // print record into buffer
            rec_printer.print_record(columns, context);
            // print record into file
            if (output2file) {
                outfile << "|";
                for(size_t i = 0; i < columns.size(); ++i) {
                    outfile << " " << columns[i] << " |";
                }
                outfile << "\n";
            }
        }
        // Print footer into buffer
        rec_printer.print_separator(context);
        // Print record count into buffer
        RecordPrinter::print_record_count(1, context);
        return;
    }
    // MIN()
    case (SELECT_WITH_MIN) : {
        rec_printer.print_separator(context);
        rec_printer.print_record(captions, context);
        rec_printer.print_separator(context);
        // print header into file
        if (output2file) {
            outfile << "|";
            for(size_t i = 0; i < captions.size(); ++i) {
                outfile << " " << captions[i] << " |";
            }
            outfile << "\n";
        }

        const auto &col = executorTreeRoot->cols().front();
        // Print records
        size_t num_rec = 0;
        // 执行query_plan
        std::unique_ptr<RmRecord> Tuple;
        Value val;
        val.type = col.type;
        if (col.type == TYPE_INT)
            val.int_val = 0;
        else if (col.type == TYPE_BIGINT)
            val.bigint_val = 0;
        else if (col.type == TYPE_FLOAT)
            val.float_val = 0;
        else if (col.type == TYPE_STRING)
            val.str_val = std::string();
        for (executorTreeRoot->beginTuple(); !executorTreeRoot->is_end(); executorTreeRoot->nextTuple()) {
            auto tempTuple = executorTreeRoot->Next();
            Value tempVal;
            tempVal.type = col.type;
            tempVal.load_raw(col.len, tempTuple->data + col.offset);
            if (!Tuple.get()) {
                Tuple = std::move(tempTuple);
                val = tempVal;
                continue;
            }
            if (binop(OP_GT, val, tempVal)) {
                Tuple = std::move(tempTuple);
                val = tempVal;
            }
            num_rec++;
        }
        if (Tuple.get()) {
            std::vector<std::string> columns;
            switch (col.type) {
                case TYPE_INT : columns.push_back(std::to_string(*(int *)Tuple->data)); break;
                case TYPE_BIGINT : columns.push_back(std::to_string(*(int64_t *)Tuple->data)); break;
                case TYPE_FLOAT : columns.push_back(std::to_string(*(float *)Tuple->data)); break;
                case TYPE_STRING : columns.push_back(std::string((char *)Tuple->data, col.len)); break;
                default : throw InternalError("Unsupported type.");
            }
            // print record into buffer
            rec_printer.print_record(columns, context);
            // print record into file
            if (output2file) {
                outfile << "|";
                for(size_t i = 0; i < columns.size(); ++i) {
                    outfile << " " << columns[i] << " |";
                }
                outfile << "\n";
            }
        }
        // Print footer into buffer
        rec_printer.print_separator(context);
        // Print record count into buffer
        RecordPrinter::print_record_count(1, context);
        return;
    }
    case (SELECT_WITH_SUM) : {
        rec_printer.print_separator(context);
        rec_printer.print_record(captions, context);
        rec_printer.print_separator(context);
        // print header into file
        if (output2file) {
            outfile << "|";
            for(size_t i = 0; i < captions.size(); ++i) {
                outfile << " " << captions[i] << " |";
            }
            outfile << "\n";
        }

        const auto &col = executorTreeRoot->cols().front();
        Value sum;
        sum.type = col.type;
        if (col.type == TYPE_INT)
            sum.int_val = 0;
        else if (col.type == TYPE_BIGINT)
            sum.bigint_val = 0;
        else if (col.type == TYPE_FLOAT)
            sum.float_val = 0;
        // 执行query_plan
        for (executorTreeRoot->beginTuple(); !executorTreeRoot->is_end(); executorTreeRoot->nextTuple()) {
            auto Tuple = executorTreeRoot->Next();
            Value val;
            val.type = col.type;
            val.load_raw(col.len, Tuple->data);
            if (col.type == TYPE_INT) {
                sum.int_val += val.int_val;
            } else if (col.type == TYPE_BIGINT) {
                sum.bigint_val += val.bigint_val;
            } else if (col.type == TYPE_FLOAT) {
                sum.float_val += val.float_val;
            }
        }
        // print record into buffer
        if (col.type == TYPE_INT) {
            rec_printer.print_record({std::to_string(sum.int_val)}, context);
        } else if (col.type == TYPE_BIGINT) {
            rec_printer.print_record({std::to_string(sum.bigint_val)}, context);
        } else if (col.type == TYPE_FLOAT) {
            rec_printer.print_record({std::to_string(sum.float_val)}, context);
        }
        if (output2file) {
            if (col.type == TYPE_INT) {
                outfile << "|" << " " << std::to_string(sum.int_val) << " |" << '\n';
            } else if (col.type == TYPE_BIGINT) {
                outfile << "|" << " " << std::to_string(sum.bigint_val) << " |" << '\n';
            } else if (col.type == TYPE_FLOAT) {
                outfile << "|" << " " << std::to_string(sum.float_val) << " |" << '\n';
            }
        }
        // Print footer into buffer
        rec_printer.print_separator(context);
        // Print record count into buffer
        RecordPrinter::print_record_count(1, context);
        return;
    }
    default : throw InternalError("Unkown portal tag.");
    }
    if (output2file) {
        outfile.close();
    }
}

// 执行DML语句
void QlManager::run_dml(std::unique_ptr<AbstractExecutor> exec) {
    exec->Next();
}

void QlManager::load_csv(std::shared_ptr<LoadPlan> plan, Context *context) {
    const auto &tab_name = plan->tab_name_;
    const auto &path = plan->path_;

    const auto &tab = sm_manager_->db_.get_table(tab_name);
    const auto &cols = tab.cols;

    auto csv_file = std::ifstream(path);

    if (!csv_file.is_open()) {
        throw InternalError("Cannot open the CSV file.");
    }

    using Token = boost::tokenizer<boost::escaped_list_separator<char>>;
    // Check header
    auto is_header_valid = [&] () {
        std::string header_raw;
        std::getline(csv_file, header_raw);
        auto headers = Token(header_raw);

        auto hdr_itr = headers.begin();
        auto col_itr = cols.begin();
        for (; hdr_itr != headers.end() && col_itr != cols.end(); hdr_itr++, col_itr++) {
            if (*hdr_itr == col_itr->name) {
                continue;
            }
            return false;
        }
        if (hdr_itr != headers.end() || col_itr != cols.end()) {
            return false;
        }
        return true;
    } ();
    if (!is_header_valid) {
        throw InternalError("The CSV header mismatches table header.");
    }

    std::string line;
    while (std::getline(csv_file, line)) {
        auto line_tok = Token(line);

        auto csv_itr = line_tok.begin();
        auto col_itr = cols.begin();

        std::vector<Value> values;
        for (; !csv_itr.at_end(); csv_itr++, col_itr++) {
            Value val;
            val.type = col_itr->type;
            switch (val.type) {
                case TYPE_INT : val.set_int(std::stoi(*csv_itr)); break;
                case TYPE_BIGINT : val.set_bigint(std::stoll(*csv_itr)); break;
                case TYPE_FLOAT : val.set_float(std::stof(*csv_itr)); break;
                case TYPE_STRING : val.set_str(*csv_itr); break;
                case TYPE_DATETIME : val.set_datetime(datetime::to_bcd(*csv_itr)); break;
            }
            values.push_back(std::move(val));
        }

        InsertExecutor(sm_manager_, tab_name, std::move(values), context).Next();
    }
}