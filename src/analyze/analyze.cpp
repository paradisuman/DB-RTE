/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "analyze.h"

/**
 * @description: 分析器，进行语义分析和查询重写，需要检查不符合语义规定的部分
 * @param {shared_ptr<ast::TreeNode>} parse parser生成的结果集
 * @return {shared_ptr<Query>} Query 
 */
std::shared_ptr<Query> Analyze::do_analyze(std::shared_ptr<ast::TreeNode> parse)
{
    std::shared_ptr<Query> query = std::make_shared<Query>();

    if (auto x = std::dynamic_pointer_cast<ast::SelectStmt>(parse)) {
        // 处理表名
        query->tables = std::move(x->tabs);
        /** TODO: 检查表是否存在 */
        // 遍历检查所有tables
        for (const auto &query_table : query->tables) {
            if (!(sm_manager_->db_).is_table(query_table))
                throw TableNotFoundError(query_table);
        }

        query->cols.reserve(x->cols.size());
        // 处理target list, 再target list中添加上表名，例如 a.id
        for (const auto &sv_sel_col : x->cols) {
            query->cols.push_back(TabCol {
                .tab_name = sv_sel_col->tab_name,
                .col_name = sv_sel_col->col_name,
            });
        }

        // 获得参与的表中所有的列
        std::vector<ColMeta> all_cols;
        get_all_cols(query->tables, all_cols);

        // 若语句中未指定列 将所有列加入
        if (query->cols.empty()) {
            // select all columns
            for (const auto &col : all_cols) {
                query->cols.push_back(TabCol {
                    .tab_name = col.tab_name,
                    .col_name = col.name,
                });
            }
        }
        // 若语句中指定了具体列 检查指定列是否存在
        else {
            // infer table name from column name
            for (auto &sel_col : query->cols)
                sel_col = check_column(all_cols, sel_col);  // 列元数据校验
        }
        //处理where条件
        get_clause(x->conds, query->conds);
        check_clause(query->tables, query->conds);
    } else if (auto x = std::dynamic_pointer_cast<ast::UpdateStmt>(parse)) {
        /** TODO: */
        const std::string tab_name = x->tab_name;
        // 检查表, 由于update只有一个table值, 所以只插入一个
        if ((sm_manager_->db_).is_table(tab_name) == false)
            throw TableNotFoundError(tab_name);
        query->tables.push_back(tab_name);

        // 处理set值 std::vector<SetClause> set_clauses;
        // x中sel_set：std::string col_name;  std::shared_ptr<Value> val;
        // 要转换的sel_set： TabCol lhs;  Value rhs;
        TabMeta table = (sm_manager_->db_).get_table(tab_name);
        for (auto &set_clause : x->set_clauses) {
            TabCol sel_col = {
                .tab_name = tab_name,
                .col_name = set_clause->col_name,
            };
            // 检查列是否在表中 不在则抛出异常
            if (!table.is_col(set_clause->col_name))
                throw ColumnNotFoundError(set_clause->col_name);
            // 类型检查以及类型设置
            Value val = convert_sv_value(set_clause->val);
            auto col = table.get_col(set_clause->col_name);
            ColType lhs_type = col->type;
            ColType &rhs_type = val.type;
            if (!is_compatible_type(lhs_type, rhs_type)) {
                throw IncompatibleTypeError(coltype2str(lhs_type), coltype2str(rhs_type));
            }
            val.init_raw(col->len);
            query->set_clauses.push_back(SetClause {sel_col, val});
        }

        //处理where条件
        get_clause(x->conds, query->conds);
        check_clause(query->tables, query->conds);
    } else if (auto x = std::dynamic_pointer_cast<ast::DeleteStmt>(parse)) {
        //处理where条件
        get_clause(x->conds, query->conds);
        check_clause({x->tab_name}, query->conds);        
    } else if (auto x = std::dynamic_pointer_cast<ast::InsertStmt>(parse)) {
        // 处理insert 的values值
        for (auto &sv_val : x->vals) {
            query->values.push_back(convert_sv_value(sv_val));
        }
    } else {
        // do nothing
    }
    query->parse = std::move(parse);
    return query;
}


TabCol Analyze::check_column(const std::vector<ColMeta> &all_cols, TabCol target) {
    // 未指定表 遍历所有表的所有列中 确定指定列存在且唯一
    if (target.tab_name.empty()) {
        // Table name not specified, infer table name from column name
        std::string tab_name;
        for (const auto &col : all_cols) {
            if (col.name == target.col_name) {
                // 非首次匹配表明 抛出异常
                if (!tab_name.empty())
                    throw AmbiguousColumnError(target.col_name);
                // 首次匹配到列名 记录表名
                tab_name = col.tab_name;
            }
        }
        // 未匹配到表名 列不存在
        if (tab_name.empty())
            throw ColumnNotFoundError(target.col_name);
        // 匹配到表名则设置 target.tab_name 为匹配到的表名
        target.tab_name = tab_name;
        return target;
    }
    // 指定表 检查对应表中的列是否存在
    else {
        /** TODO: Make sure target column exists */
        for (const auto &col : all_cols) {
            // 匹配到表名列名相同的字段则返回
            if (col.tab_name == target.tab_name && col.name == target.col_name)
                return target;
        }
        // 否则抛出异常
        throw ColumnNotFoundError(target.col_name);
    }
}

void Analyze::get_all_cols(const std::vector<std::string> &tab_names, std::vector<ColMeta> &all_cols) {
    for (const auto &sel_tab_name : tab_names) {
        // 这里db_不能写成get_db(), 注意要传指针
        const auto &sel_tab_cols = sm_manager_->db_.get_table(sel_tab_name).cols;
        all_cols.insert(all_cols.end(), sel_tab_cols.begin(), sel_tab_cols.end());
    }
}

void Analyze::get_clause(const std::vector<std::shared_ptr<ast::BinaryExpr>> &sv_conds, std::vector<Condition> &conds) {
    conds.clear();
    for (auto &expr : sv_conds) {
        Condition cond;
        cond.lhs_col = {.tab_name = expr->lhs->tab_name, .col_name = expr->lhs->col_name};
        cond.op = convert_sv_comp_op(expr->op);
        if (auto rhs_val = std::dynamic_pointer_cast<ast::Value>(expr->rhs)) {
            cond.is_rhs_val = true;
            cond.rhs_val = convert_sv_value(rhs_val);
        } else if (auto rhs_col = std::dynamic_pointer_cast<ast::Col>(expr->rhs)) {
            cond.is_rhs_val = false;
            cond.rhs_col = {.tab_name = rhs_col->tab_name, .col_name = rhs_col->col_name};
        }
        conds.push_back(cond);
    }
}

void Analyze::check_clause(const std::vector<std::string> &tab_names, std::vector<Condition> &conds) {
    // auto all_cols = get_all_cols(tab_names);
    std::vector<ColMeta> all_cols;
    get_all_cols(tab_names, all_cols);
    // Get raw values in where clause
    for (auto &cond : conds) {
        // Infer table name from column name
        cond.lhs_col = check_column(all_cols, cond.lhs_col);
        if (!cond.is_rhs_val) {
            cond.rhs_col = check_column(all_cols, cond.rhs_col);
        }
        TabMeta &lhs_tab = sm_manager_->db_.get_table(cond.lhs_col.tab_name);
        auto lhs_col = lhs_tab.get_col(cond.lhs_col.col_name);
        ColType lhs_type = lhs_col->type;
        ColType rhs_type;
        if (cond.is_rhs_val) {
            cond.rhs_val.init_raw(lhs_col->len);
            rhs_type = cond.rhs_val.type;
        } else {
            TabMeta &rhs_tab = sm_manager_->db_.get_table(cond.rhs_col.tab_name);
            auto rhs_col = rhs_tab.get_col(cond.rhs_col.col_name);
            rhs_type = rhs_col->type;
        }
        if (!is_compatible_type(lhs_type, rhs_type)) {
            throw IncompatibleTypeError(coltype2str(lhs_type), coltype2str(rhs_type));
        }
    }
}


Value Analyze::convert_sv_value(const std::shared_ptr<ast::Value> &sv_val) {
    Value val;
    if (auto int_lit = std::dynamic_pointer_cast<ast::IntLit>(sv_val)) {
        val.set_int(int_lit->val);
    } else if (auto bigint_lit = std::dynamic_pointer_cast<ast::BigintLit>(sv_val)) {
        val.set_bigint(bigint_lit->val);
    } else if (auto float_lit = std::dynamic_pointer_cast<ast::FloatLit>(sv_val)) {
        val.set_float(float_lit->val);
    } else if (auto str_lit = std::dynamic_pointer_cast<ast::StringLit>(sv_val)) {
        val.set_str(str_lit->val);
    } else if (auto datetime_lit = std::dynamic_pointer_cast<ast::DatetimeLit>(sv_val)) {
        val.set_datetime(datetime_lit->val);
    } else {
        throw InternalError("Unexpected sv value type");
    }
    return val;
}

CompOp Analyze::convert_sv_comp_op(ast::SvCompOp op) {
    std::map<ast::SvCompOp, CompOp> m = {
        {ast::SV_OP_EQ, OP_EQ}, {ast::SV_OP_NE, OP_NE}, {ast::SV_OP_LT, OP_LT},
        {ast::SV_OP_GT, OP_GT}, {ast::SV_OP_LE, OP_LE}, {ast::SV_OP_GE, OP_GE},
    };
    return m.at(op);
}
