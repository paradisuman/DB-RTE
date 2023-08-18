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

#include <algorithm>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "errors.h"
#include "sm_defs.h"
#include "common/common.h"

/* 字段元数据 */
struct ColMeta {
    std::string tab_name;   // 字段所属表名称
    std::string name;       // 字段名称
    ColType type;           // 字段类型
    int len;                // 字段长度
    int offset;             // 字段位于记录中的偏移量
    bool index;             /** unused */

    friend std::ostream &operator<<(std::ostream &os, const ColMeta &col) {
        // ColMeta中有各个基本类型的变量，然后调用重载的这些变量的操作符<<（具体实现逻辑在defs.h）
        return os << col.tab_name << ' ' << col.name << ' ' << col.type << ' ' << col.len << ' ' << col.offset << ' '
                  << col.index;
    }

    friend std::istream &operator>>(std::istream &is, ColMeta &col) {
        return is >> col.tab_name >> col.name >> col.type >> col.len >> col.offset >> col.index;
    }
};

/* 索引元数据 */
struct IndexMeta {
    std::string tab_name;           // 索引所属表名称
    size_t col_tot_len;                // 索引字段长度总和
    size_t col_num;                    // 索引字段数量
    std::vector<ColMeta> cols;      // 索引包含的字段

    friend std::ostream &operator<<(std::ostream &os, const IndexMeta &index) {
        os << index.tab_name << " " << index.col_tot_len << " " << index.col_num;
        for(auto& col: index.cols) {
            os << "\n" << col;
        }
        return os;
    }

    friend std::istream &operator>>(std::istream &is, IndexMeta &index) {
        is >> index.tab_name >> index.col_tot_len >> index.col_num;
        for(size_t _ = 0; _ < index.col_num; ++_) {
            ColMeta col;
            is >> col;
            index.cols.push_back(col);
        }
        return is;
    }
};

/* 表元数据 */
struct TabMeta {
    std::string name;                   // 表名称
    std::vector<ColMeta> cols;          // 表包含的字段
    std::vector<IndexMeta> indexes;     // 表上建立的索引

    TabMeta(std::string name_ = "") { name = name_; }

    TabMeta(const TabMeta &other) {
        name = other.name;
        for(auto col : other.cols) cols.push_back(col);
    }

    /* 判断当前表中是否存在名为col_name的字段 */
    bool is_col(const std::string &col_name) const {
        auto pos = std::find_if(cols.begin(), cols.end(), [&](const ColMeta &col) { return col.name == col_name; });
        return pos != cols.end();
    }

    /* 判断当前表上是否建有指定索引，索引包含的字段为col_names */
    bool is_index(const std::vector<std::string>& col_names) const {
        for(auto& index: indexes) {
            if(index.col_num == col_names.size()) {
                size_t i = 0;
                for(; i < index.col_num; ++i) {
                    if(index.cols[i].name.compare(col_names[i]) != 0)
                        break;
                }
                if(i == index.col_num) return true;
            }
        }
        return false;
    }

    // bool can_index(const std::vector<std::string>& col_names, const std::vector<CompOp>& ops) const {
    //     for(auto& index: indexes) {
    //         bool isequ = true;
    //         if(index.col_num == col_names.size()) {
    //             size_t i = 0;
    //             for(; i < index.col_num; ++i) {
    //                 if (index.cols[i].name.compare(col_names[i]) != 0 && ops[i] == OP_EQ) {
    //                     break;
    //                 }
    //             }
    //             if (i < index.col_num && index.cols[i].name.compare(col_names[i]) == 0) {
    //                 i++;
    //             }
    //             if(i > 0) return true;
    //         }
    //     }
    //     return false;
    // }


    // 最左匹配原则，自动调换col_names的顺序
    // std::pair<bool, IndexMeta> find_index(const std::vector<std::string>& col_names, std::vector<Condition> curr_conds) const {
    //     auto &index_meta = sm_manager_->db_.get_table(tab_name).indexes;
    //     std::vector<std::string> indexed_col;
    //     int maxlen = -1;
    //     for (auto &meta: index_meta) {
    //         for (auto index_detail: meta.cols) {
    //             indexed_col.push_back(index_detail.name);
    //         }
    //         // 最左匹配要求前面全是equal，最后一个可以是大于小于或range(x > 1 && x < 10)
    //         std::vector<std::vector<bool>> status(3, std::vector<bool>(indexed_col.size(), false));
    //         int match_len = 0;
    //         for (size_t i = 0; i < curr_conds.size(); i++) {
    //             auto &cond = curr_conds[i];
    //             auto pos = std::find(indexed_col.begin(), indexed_col.end(), cond.lhs_col.col_name) - indexed_col.begin();
    //             if (cond.is_rhs_val && cond.op != OP_NE) {
    //                 if (pos == 0 || status[1][pos - 1] || status[0][pos] || status[2][pos]) {
    //                     // do nothing
    //                     switch (cond.op) {
    //                         case OP_EQ:
    //                             status[1][pos] = true;
    //                             break;
    //                         case OP_NE:
    //                             // never reach
    //                             break;
    //                         case OP_LT:
    //                         case OP_LE:
    //                             status[2][pos] = true;
    //                             break;
    //                         case OP_GT:
    //                         case OP_GE:
    //                             status[0][pos] = true;
    //                             break;
    //                     }
    //                 } else {
    //                     break;
    //                 }
    //                 match_len = i;
    //             }
    //         }
    //         if (match_len > maxlen) {
    //             maxlen = match_len;
    //             index_col_names = std::move(indexed_col);
    //         }
    //         indexed_col.clear();
    //     }
    //     return index_col_names.size() > 0;
    // }

    /* 根据字段名称集合获取索引元数据 */
    std::vector<IndexMeta>::iterator get_index_meta(const std::vector<std::string>& col_names) {
        for(auto index = indexes.begin(); index != indexes.end(); ++index) {
            if((*index).col_num != col_names.size()) continue;
            auto& index_cols = (*index).cols;
            size_t i = 0;
            for(; i < col_names.size(); ++i) {
                if(index_cols[i].name.compare(col_names[i]) != 0) 
                    break;
            }
            if(i == col_names.size()) return index;
        }
        throw IndexNotFoundError(name, col_names);
    }

    /* 根据字段名称获取字段元数据 */
    std::vector<ColMeta>::iterator get_col(const std::string &col_name) {
        auto pos = std::find_if(cols.begin(), cols.end(), [&](const ColMeta &col) { return col.name == col_name; });
        if (pos == cols.end()) {
            throw ColumnNotFoundError(col_name);
        }
        return pos;
    }

    friend std::ostream &operator<<(std::ostream &os, const TabMeta &tab) {
        os << tab.name << '\n' << tab.cols.size() << '\n';
        for (auto &col : tab.cols) {
            os << col << '\n';  // col是ColMeta类型，然后调用重载的ColMeta的操作符<<
        }
        os << tab.indexes.size() << "\n";
        for (auto &index : tab.indexes) {
            os << index << "\n";
        }
        return os;
    }

    friend std::istream &operator>>(std::istream &is, TabMeta &tab) {
        size_t n;
        is >> tab.name >> n;
        for (size_t i = 0; i < n; i++) {
            ColMeta col;
            is >> col;
            tab.cols.push_back(col);
        }
        is >> n;
        for(size_t i = 0; i < n; ++i) {
            IndexMeta index;
            is >> index;
            tab.indexes.push_back(index);
        }
        return is;
    }
};

// 注意重载了操作符 << 和 >>，这需要更底层同样重载TabMeta、ColMeta的操作符 << 和 >>
/* 数据库元数据 */
class DbMeta {
    friend class SmManager;

   private:
    std::string name_;                      // 数据库名称
    std::map<std::string, TabMeta> tabs_;   // 数据库中包含的表

   public:
    DbMeta(std::string name = "") : name_(name) {}

    /* 判断数据库中是否存在指定名称的表 */
    bool is_table(const std::string &tab_name) const { return tabs_.find(tab_name) != tabs_.end(); }

    void SetTabMeta(const std::string &tab_name, const TabMeta &meta) {
        tabs_[tab_name] = meta;
    }

    /* 获取指定名称表的元数据 */
    TabMeta &get_table(const std::string &tab_name) {
        auto pos = tabs_.find(tab_name);
        if (pos == tabs_.end()) {
            throw TableNotFoundError(tab_name);
        }

        return pos->second;
    }

    // 重载操作符 <<
    friend std::ostream &operator<<(std::ostream &os, const DbMeta &db_meta) {
        os << db_meta.name_ << '\n' << db_meta.tabs_.size() << '\n';
        for (auto &entry : db_meta.tabs_) {
            os << entry.second << '\n';
        }
        return os;
    }

    friend std::istream &operator>>(std::istream &is, DbMeta &db_meta) {
        size_t n;
        is >> db_meta.name_ >> n;
        for (size_t i = 0; i < n; i++) {
            TabMeta tab;
            is >> tab;
            db_meta.tabs_[tab.name] = tab;
        }
        return is;
    }
};
