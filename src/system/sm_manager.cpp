/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sm_manager.h"

#include <sys/stat.h>
#include <unistd.h>

#include <fstream>

#include "index/ix.h"
#include "record/rm.h"
#include "record_printer.h"

/**
 * @description: 判断是否为一个文件夹
 * @return {bool} 返回是否为一个文件夹
 * @param {string&} db_name 数据库文件名称，与文件夹同名
 */
bool SmManager::is_dir(const std::string& db_name) {
    struct stat st;
    return stat(db_name.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

/**
 * @description: 创建数据库，所有的数据库相关文件都放在数据库同名文件夹下
 * @param {string&} db_name 数据库名称
 */
void SmManager::create_db(const std::string& db_name) {
    if (is_dir(db_name)) {
        throw DatabaseExistsError(db_name);
    }
    //为数据库创建一个子目录
    std::string cmd = "mkdir " + db_name;
    if (system(cmd.c_str()) < 0) {  // 创建一个名为db_name的目录
        throw UnixError();
    }
    if (chdir(db_name.c_str()) < 0) {  // 进入名为db_name的目录
        throw UnixError();
    }
    //创建系统目录
    DbMeta *new_db = new DbMeta(db_name);

    // 注意，此处ofstream会在当前目录创建(如果没有此文件先创建)和打开一个名为DB_META_NAME的文件
    std::ofstream ofs(DB_META_NAME);

    // 将new_db中的信息，按照定义好的operator<<操作符，写入到ofs打开的DB_META_NAME文件中
    ofs << *new_db;  // 注意：此处重载了操作符<<

    delete new_db;

    // 创建日志文件
    disk_manager_->create_file(LOG_FILE_NAME);

    // 回到根目录
    if (chdir("..") < 0) {
        throw UnixError();
    }
}

/**
 * @description: 删除数据库，同时需要清空相关文件以及数据库同名文件夹
 * @param {string&} db_name 数据库名称，与文件夹同名
 */
void SmManager::drop_db(const std::string& db_name) {
    if (!is_dir(db_name)) {
        throw DatabaseNotFoundError(db_name);
    }
    std::string cmd = "rm -r " + db_name;
    if (system(cmd.c_str()) < 0) {
        throw UnixError();
    }
}

/**
 * @description: 打开数据库，找到数据库对应的文件夹，并加载数据库元数据和相关文件
 * @param {string&} db_name 数据库名称，与文件夹同名
 */
void SmManager::open_db(const std::string& db_name) {
    if (!is_dir(db_name)) {
        throw DatabaseNotFoundError(db_name);
    }
    if (chdir(db_name.c_str()) < 0) {  // 进入名为db_name的目录
        throw UnixError();
    }

    std::ifstream(DB_META_NAME) >> db_; // 加载数据库元数据

    // 加载数据库表文件
    for (const auto &[tab_name, _] : db_.tabs_) {
        fhs_.emplace(
            tab_name,
            rm_manager_->open_file(tab_name)
        );
    }

    // TODO: 加载数据索引文件
}

/**
 * @description: 把数据库相关的元数据刷入磁盘中
 */
void SmManager::flush_meta() {
    // 默认清空文件
    std::ofstream ofs(DB_META_NAME);
    ofs << db_;
}

/**
 * @description: 关闭数据库并把数据落盘
 */
void SmManager::close_db() {
    flush_meta();
    // 刷新全部脏页
    buffer_pool_manager_->flush_all_page();

    // 清空记录
    fhs_.clear();
    ihs_.clear();

    // 回到根目录
    if (chdir("..") < 0) {
        throw UnixError();
    }
}

/**
 * @description: 显示所有的表,通过测试需要将其结果写入到output.txt,详情看题目文档
 * @param {Context*} context 
 */
void SmManager::show_tables(Context* context) {
    if (output2file) {
        std::fstream outfile;
        outfile.open("output.txt", std::ios::out | std::ios::app);
        outfile << "| Tables |\n";
        for (auto &entry : db_.tabs_) {
            auto &tab = entry.second;
            outfile << "| " << tab.name << " |\n";
        }
        outfile.close();
    }

    RecordPrinter printer(1);
    printer.print_separator(context);
    printer.print_record({"Tables"}, context);
    printer.print_separator(context);
    for (auto &entry : db_.tabs_) {
        auto &tab = entry.second;
        printer.print_record({tab.name}, context);
    }
    printer.print_separator(context);
}

/**
 * @description: 显示表的元数据
 * @param {string&} tab_name 表名称
 * @param {Context*} context 
 */
void SmManager::desc_table(const std::string& tab_name, Context* context) {
    TabMeta &tab = db_.get_table(tab_name);

    std::vector<std::string> captions = {"Field", "Type", "Index"};
    RecordPrinter printer(captions.size());
    // Print header
    printer.print_separator(context);
    printer.print_record(captions, context);
    printer.print_separator(context);
    // Print fields
    for (auto &col : tab.cols) {
        std::vector<std::string> field_info = {col.name, coltype2str(col.type), col.index ? "YES" : "NO"};
        printer.print_record(field_info, context);
    }
    // Print footer
    printer.print_separator(context);
}

/**
 * @description: 创建表
 * @param {string&} tab_name 表的名称
 * @param {vector<ColDef>&} col_defs 表的字段
 * @param {Context*} context 
 */
void SmManager::create_table(const std::string& tab_name, const std::vector<ColDef>& col_defs, Context* context) {
    if (db_.is_table(tab_name)) {
        throw TableExistsError(tab_name);
    }
    // Create table meta
    int curr_offset = 0;
    TabMeta tab(tab_name);
    for (const auto &col_def : col_defs) {
        ColMeta col = {.tab_name = tab_name,
                       .name     = col_def.name,
                       .type     = col_def.type,
                       .len      = col_def.len,
                       .offset   = curr_offset,
                       .index    = false};
        curr_offset += col_def.len;
        tab.cols.push_back(col);
    }
    // Create & open record file
    int record_size = curr_offset;  // record_size就是col meta所占的大小（表的元数据也是以记录的形式进行存储的）
    rm_manager_->create_file(tab_name, record_size);
    db_.tabs_.emplace(tab_name, tab);
    fhs_.emplace(tab_name, rm_manager_->open_file(tab_name));

    flush_meta();
}

/**
 * @description: 删除表
 * @param {string&} tab_name 表的名称
 * @param {Context*} context
 */
void SmManager::drop_table(const std::string& tab_name, Context* context) {
    if (!db_.is_table(tab_name)) {
        throw TableNotFoundError(tab_name);
    }

    //先删除db_中的表，再删除文件表，最后删除fhs_中的表
    fhs_[tab_name]->close_all_page();
    rm_manager_->close_file(fhs_[tab_name].get());
    rm_manager_->destroy_file(tab_name);
    fhs_.erase(tab_name);
    db_.tabs_.erase(tab_name);

    // TODO: 删除表索引

    flush_meta();
}

/**
 * @description: 创建索引
 * @param {string&} tab_name 表的名称
 * @param {vector<string>&} col_names 索引包含的字段名称
 * @param {Context*} context
 */
void SmManager::create_index(const std::string& tab_name, const std::vector<std::string>& col_names, Context* context) {
    // 查找索引是否存在
    if(ix_manager_->exists(tab_name,col_names)){
        throw IndexExistsError(tab_name,col_names);
    }

    // 正常的表查询测试
    if (db_.tabs_.find(tab_name) == db_.tabs_.end()) 
        throw RMDBError("tab not find");

    TabMeta &table = db_.get_table(tab_name);
    IndexMeta new_index;
    
    // 加载new_index
    new_index.col_num = col_names.size();
    new_index.tab_name = tab_name;
    // 创建索引meta
    for (auto &y : col_names) {
        for (auto &x : table.cols) {
            if (x.name == y) {
                x.index = true;
                new_index.cols.push_back(x);
                continue;
            }
        }
    }
    // 没找到指定的索引！
    if (new_index.cols.size() != new_index.col_num) {
        throw InvalidColLengthError(new_index.col_num);
    }
    // 加载col len
    new_index.col_tot_len = 0;

    // 加载
    ix_manager_->create_index(tab_name, new_index.cols);
    table.indexes.push_back(new_index);

    // 在ix_manager中进行管理
    std::string index_name = ix_manager_->get_index_name(tab_name, col_names);
    ihs_.emplace(
        index_name, 
        ix_manager_->open_index(tab_name, col_names)
    );

    // 将已经存在的record加入索引
    char *key = new char[new_index.col_tot_len];
    auto ix_hdl = ihs_.at(index_name).get();
    auto file_hdl = fhs_.at(tab_name).get();
    for (RmScan rm_scan(file_hdl); !rm_scan.is_end(); rm_scan.next()) {
        auto rec = file_hdl->get_record(rm_scan.rid(), context);  
        int offset = 0;
        for(size_t i = 0; i < new_index.col_num; ++i) {
                memcpy(key + offset, rec->data + new_index.cols[i].offset, new_index.cols[i].len);
                offset += new_index.cols[i].len;
            }
        ix_hdl->insert_entry(key, rm_scan.rid(), context->txn_);
    }

    delete[] key;
}

/**
 * @description: 删除索引
 * @param {string&} tab_name 表名称
 * @param {vector<string>&} col_names 索引包含的字段名称
 * @param {Context*} context
 */
void SmManager::drop_index(const std::string& tab_name, const std::vector<std::string>& col_names, Context* context) {


    if (db_.tabs_.find(tab_name) == db_.tabs_.end())
        throw IndexEntryNotFoundError();
    TabMeta &table = db_.get_table(tab_name);

    if (! table.is_index(col_names)) {
        throw RMDBError("index not find!");
    }

    table.indexes.erase(table.get_index_meta(col_names));
    std::string index_name = ix_manager_->get_index_name(tab_name, col_names);
    ix_manager_->close_index(ihs_.at(index_name).get());
    ix_manager_->destroy_index(tab_name, col_names);
    ihs_.erase(ix_manager_->get_index_name(tab_name, col_names));
}

/**
 * @description: 删除索引
 * @param {string&} tab_name 表名称
 * @param {vector<ColMeta>&} 索引包含的字段元数据
 * @param {Context*} context
 */
void SmManager::drop_index(const std::string& tab_name, const std::vector<ColMeta>& cols, Context* context) {


    if (db_.tabs_.find(tab_name) == db_.tabs_.end())
        throw IndexEntryNotFoundError();

    TabMeta &table = db_.get_table(tab_name);
    std::vector<std::string> col_names;
    for (auto &x : cols) {
        col_names.push_back(x.name);
    }
    // 查找index是否存在
    if (!table.is_index(col_names)) {
        throw RMDBError("index not find!");
    }

    table.indexes.erase(table.get_index_meta(col_names));
    std::string index_name = ix_manager_->get_index_name(tab_name, col_names);
    ix_manager_->close_index(ihs_.at(index_name).get());
    ix_manager_->destroy_index(tab_name, col_names);
    ihs_.erase(ix_manager_->get_index_name(tab_name, col_names));
}

/**
 * @description: 显示所有的表,通过测试需要将其结果写入到output.txt,详情看题目文档
 * @param {Context*} context 
 */
void SmManager::show_index(const std::string& tab_name, Context* context) {
    if (db_.tabs_.find(tab_name) == db_.tabs_.end())
        throw IndexEntryNotFoundError();

    TabMeta &table = db_.get_table(tab_name);
    // 查找index是否存在
    std::vector<std::string> index_names;

    for (auto &index : table.indexes) {
        std::vector<std::string> col_names;
        std::string name = "(";
        for (auto &x : index.cols) {
            name += x.name;
            name += ",";
        }
        name.back() = ')';
        index_names.push_back(name);
    }

    if (output2file) {
        std::fstream outfile;
        outfile.open("output.txt", std::ios::out | std::ios::app);
        for (auto &x : index_names) {
            outfile << "| "<< tab_name <<" | unique | ";
            outfile << x <<" |\n";
        }
        outfile.close();
    }
    RecordPrinter printer(3);
    printer.print_separator(context);
    for (auto &x : index_names) {
        printer.print_record({tab_name, "unique", x}, context);
    }
    printer.print_separator(context);
}