/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "planner.h"

#include <memory>

#include "execution/executor_delete.h"
#include "execution/executor_index_scan.h"
#include "execution/executor_insert.h"
#include "execution/executor_nestedloop_join.h"
#include "execution/executor_projection.h"
#include "execution/executor_seq_scan.h"
#include "execution/executor_update.h"
#include "index/ix.h"
#include "record_printer.h"

// 目前的索引匹配规则为：完全匹配索引字段，且全部为单点查询，不会自动调整where条件的顺序
// 最左匹配要求前面全是equal，最后一个可以是大于小于或范围
// bool Planner::get_index_cols(std::string tab_name, std::vector<Condition> curr_conds, std::vector<std::string>& index_col_names) {
//     index_col_names.clear();
//     std::vector<CompOp> ops;
// 	for(auto& cond: curr_conds) {
//         if(cond.is_rhs_val && cond.lhs_col.tab_name.compare(tab_name) == 0){
//             index_col_names.push_back(cond.lhs_col.col_name);
//         }
//     }
//     TabMeta& tab = sm_manager_->db_.get_table(tab_name);
//     return tab.is_index(index_col_names);
// }
bool Planner::get_index_cols(std::string tab_name, std::vector<Condition> curr_conds, std::vector<std::string> &index_col_names) {
	index_col_names.clear();
    std::vector<std::string> col_names;
    std::vector<std::string> indexed_colnames;
    std::vector<CompOp> col_ops;
    std::map<std::string, bool> isequ;
    std::map<std::string, bool> islg;
    std::map<std::string, bool> is_exist;
    size_t maxlen = 0;
	
    for (auto &cond : curr_conds) {
        if(cond.is_rhs_val && cond.lhs_col.tab_name.compare(tab_name) == 0)
        {
            col_names.push_back(cond.lhs_col.col_name);
            col_ops.push_back(cond.op);
        }
    }

    auto &indexes = sm_manager_->db_.get_table(tab_name).indexes;
	
    // 把有效的条件col_names加入
    for (size_t i = 0; i < col_names.size(); i++) {
        is_exist[col_names[i]] = true;

        switch (col_ops[i]) {
            case OP_EQ:
                isequ[col_names[i]] = true;
                break;
            case OP_NE:
            case OP_LT:
            case OP_LE:
            case OP_GT:
            case OP_GE:
                islg[col_names[i]] = true;
                break;
        }
    }

    // 遍历index寻找合适的
	for (auto &index: indexes) {
        std::map<std::string, bool> index_exist;
        for (auto col: index.cols) {
			indexed_colnames.push_back(col.name);
            index_exist[col.name] = true;
        }
        size_t i = 0, j = 0;
        // 检测是否有index中不存在的键
        for (auto &cond : curr_conds) {
            if (cond.is_rhs_val && cond.lhs_col.tab_name.compare(tab_name) == 0 && !index_exist[cond.lhs_col.col_name]) {
                i = indexed_colnames.size() + 1;
            }
        }
        if (i == indexed_colnames.size() + 1) continue;

        // 检测在index中是否按照最左匹配,前面的都是等于，最后一个可以是范围
        while (i < indexed_colnames.size() && is_exist[indexed_colnames[i]] && isequ[indexed_colnames[i]])
            i++;

        if (i < indexed_colnames.size() && is_exist[indexed_colnames[i]] && islg[indexed_colnames[i]])
            i++;

        if (i == 0) break;

        j = i;
        
        while (i < indexed_colnames.size() && !is_exist[indexed_colnames[i]]) {
            i++;
        }

        // 如果匹配且更优
        if (i == indexed_colnames.size() && j > maxlen) {
            maxlen = j;
            index_col_names = std::move(indexed_colnames);
        }
        indexed_colnames.clear();
    }
    return index_col_names.size() > 0;
}

/**
 * @brief 表算子条件谓词生成
 *
 * @param conds 条件
 * @param tab_names 表名
 * @return std::vector<Condition>
 */
std::vector<Condition> pop_conds(std::vector<Condition> &conds, std::string tab_names) {
    // auto has_tab = [&](const std::string &tab_name) {
    //     return std::find(tab_names.begin(), tab_names.end(), tab_name) != tab_names.end();
    // };
    std::vector<Condition> solved_conds;
    auto it = conds.begin();
    while (it != conds.end()) {
        if ((tab_names.compare(it->lhs_col.tab_name) == 0 && it->is_rhs_val) || (it->lhs_col.tab_name.compare(it->rhs_col.tab_name) == 0)) {
            solved_conds.emplace_back(std::move(*it));
            it = conds.erase(it);
        } else {
            it++;
        }
    }
    return solved_conds;
}

int push_conds(Condition *cond, std::shared_ptr<Plan> plan)
{
    if(auto x = std::dynamic_pointer_cast<ScanPlan>(plan))
    {
        if(x->tab_name_.compare(cond->lhs_col.tab_name) == 0) {
            return 1;
        } else if(x->tab_name_.compare(cond->rhs_col.tab_name) == 0){
            return 2;
        } else {
            return 0;
        }
    }
    else if(auto x = std::dynamic_pointer_cast<JoinPlan>(plan))
    {
        int left_res = push_conds(cond, x->left_);
        // 条件已经下推到左子节点
        if(left_res == 3){
            return 3;
        }
        int right_res = push_conds(cond, x->right_);
        // 条件已经下推到右子节点
        if(right_res == 3){
            return 3;
        }
        // 左子节点或右子节点有一个没有匹配到条件的列
        if(left_res == 0 || right_res == 0) {
            return left_res + right_res;
        }
        // 左子节点匹配到条件的右边
        if(left_res == 2) {
            // 需要将左右两边的条件变换位置
            std::map<CompOp, CompOp> swap_op = {
                {OP_EQ, OP_EQ}, {OP_NE, OP_NE}, {OP_LT, OP_GT}, {OP_GT, OP_LT}, {OP_LE, OP_GE}, {OP_GE, OP_LE},
            };
            std::swap(cond->lhs_col, cond->rhs_col);
            cond->op = swap_op.at(cond->op);
        }
        x->conds_.emplace_back(std::move(*cond));
        return 3;
    }
    return false;
}

std::shared_ptr<Plan> pop_scan(int *scantbl, std::string table, std::vector<std::string> &joined_tables, 
                std::vector<std::shared_ptr<Plan>> plans)
{
    for (size_t i = 0; i < plans.size(); i++) {
        auto x = std::dynamic_pointer_cast<ScanPlan>(plans[i]);
        if(x->tab_name_.compare(table) == 0)
        {
            scantbl[i] = 1;
            joined_tables.emplace_back(x->tab_name_);
            return plans[i];
        }
    }
    return nullptr;
}


std::shared_ptr<Query> Planner::logical_optimization(std::shared_ptr<Query> query, Context *context)
{
    
    //TODO 实现逻辑优化规则

    return query;
}

std::shared_ptr<Plan> Planner::physical_optimization(std::shared_ptr<Query> query, Context *context)
{
    std::shared_ptr<Plan> plan = make_one_rel(query);
    
    // 其他物理优化

    // 处理orderby
    plan = generate_sort_plan(query, std::move(plan)); 

    return plan;
}



std::shared_ptr<Plan> Planner::make_one_rel(std::shared_ptr<Query> query)
{
    auto x = std::dynamic_pointer_cast<ast::SelectStmt>(query->parse);
    std::vector<std::string> tables = query->tables;
    // // Scan table , 生成表算子列表tab_nodes
    std::vector<std::shared_ptr<Plan>> table_scan_executors(tables.size());
    for (size_t i = 0; i < tables.size(); i++) {
        auto curr_conds = pop_conds(query->conds, tables[i]);
        // int index_no = get_indexNo(tables[i], curr_conds);
        std::vector<std::string> index_col_names;
        bool index_exist = get_index_cols(tables[i], curr_conds, index_col_names);
        if (index_exist == false) {  // 该表没有索引
            index_col_names.clear();
            table_scan_executors[i] = 
                std::make_shared<ScanPlan>(T_SeqScan, sm_manager_, tables[i], curr_conds, index_col_names);
        } else {  // 存在索引
            // 将索引赋值
            table_scan_executors[i] = 
                std::make_shared<ScanPlan>(T_IndexScan, sm_manager_, tables[i], curr_conds, index_col_names);
        }
    }
    // 只有一个表，不需要join。
    if(tables.size() == 1)
    {
        return table_scan_executors[0];
    }
    // 获取where条件
    auto conds = std::move(query->conds);
    std::shared_ptr<Plan> table_join_executors;
    
    int scantbl[tables.size()];
    for(size_t i = 0; i < tables.size(); i++)
    {
        scantbl[i] = -1;
    }
    // 假设在ast中已经添加了jointree，这里需要修改的逻辑是，先处理jointree，然后再考虑剩下的部分
    if(conds.size() >= 1)
    {
        // 有连接条件

        // 根据连接条件，生成第一层join
        std::vector<std::string> joined_tables(tables.size());
        auto it = conds.begin();
        while (it != conds.end()) {
            std::shared_ptr<Plan> left , right;
            left = pop_scan(scantbl, it->lhs_col.tab_name, joined_tables, table_scan_executors);
            right = pop_scan(scantbl, it->rhs_col.tab_name, joined_tables, table_scan_executors);
            std::vector<Condition> join_conds{*it};
            //建立join
            table_join_executors = std::make_shared<JoinPlan>(T_NestLoop, std::move(left), std::move(right), join_conds);
            it = conds.erase(it);
            break;
        }
        // 根据连接条件，生成第2-n层join
        it = conds.begin();
        while (it != conds.end()) {
            std::shared_ptr<Plan> left_need_to_join_executors = nullptr;
            std::shared_ptr<Plan> right_need_to_join_executors = nullptr;
            bool isneedreverse = false;
            if (std::find(joined_tables.begin(), joined_tables.end(), it->lhs_col.tab_name) == joined_tables.end()) {
                left_need_to_join_executors = pop_scan(scantbl, it->lhs_col.tab_name, joined_tables, table_scan_executors);
            }
            if (std::find(joined_tables.begin(), joined_tables.end(), it->rhs_col.tab_name) == joined_tables.end()) {
                right_need_to_join_executors = pop_scan(scantbl, it->rhs_col.tab_name, joined_tables, table_scan_executors);
                isneedreverse = true;
            } 

            if(left_need_to_join_executors != nullptr && right_need_to_join_executors != nullptr) {
                std::vector<Condition> join_conds{*it};
                std::shared_ptr<Plan> temp_join_executors = std::make_shared<JoinPlan>(T_NestLoop, 
                                                                    std::move(left_need_to_join_executors), 
                                                                    std::move(right_need_to_join_executors), 
                                                                    join_conds);
                table_join_executors = std::make_shared<JoinPlan>(T_NestLoop, std::move(temp_join_executors), 
                                                                    std::move(table_join_executors), 
                                                                    std::vector<Condition>());
            } else if(left_need_to_join_executors != nullptr || right_need_to_join_executors != nullptr) {
                if(isneedreverse) {
                    std::map<CompOp, CompOp> swap_op = {
                        {OP_EQ, OP_EQ}, {OP_NE, OP_NE}, {OP_LT, OP_GT}, {OP_GT, OP_LT}, {OP_LE, OP_GE}, {OP_GE, OP_LE},
                    };
                    std::swap(it->lhs_col, it->rhs_col);
                    it->op = swap_op.at(it->op);
                    left_need_to_join_executors = std::move(right_need_to_join_executors);
                }
                std::vector<Condition> join_conds{*it};
                table_join_executors = std::make_shared<JoinPlan>(T_NestLoop, std::move(left_need_to_join_executors), 
                                                                    std::move(table_join_executors), join_conds);
            } else {
                push_conds(std::move(&(*it)), table_join_executors);
            }
            it = conds.erase(it);
        }
    } else {
        table_join_executors = table_scan_executors[0];
        scantbl[0] = 1;
    }

    //连接剩余表
    for (size_t i = 0; i < tables.size(); i++) {
        if(scantbl[i] == -1) {
            table_join_executors = std::make_shared<JoinPlan>(T_NestLoop, std::move(table_scan_executors[i]), 
                                                    std::move(table_join_executors), std::vector<Condition>());
        }
    }

    return table_join_executors;

}


std::shared_ptr<Plan> Planner::generate_sort_plan(std::shared_ptr<Query> query, std::shared_ptr<Plan> plan)
{
    auto x = std::dynamic_pointer_cast<ast::SelectStmt>(query->parse);
    if(!x->has_sort) {
        return plan;
    }
    std::vector<std::string> tables = query->tables;
    std::vector<ColMeta> all_cols;
    for (const auto &sel_tab_name : tables) {
        // 这里db_不能写成get_db(), 注意要传指针
        const auto &sel_tab_cols = sm_manager_->db_.get_table(sel_tab_name).cols;
        all_cols.insert(all_cols.end(), sel_tab_cols.begin(), sel_tab_cols.end());
    }
    std::vector<std::pair<TabCol, bool>> sel_cols;
    // 检查列是否存在是否又歧义 若无加入sel cols
    for (const auto &order : x->orders) {
        const auto &target_col = order->col;
        // 未指定表 遍历所有潜在列查看列名是否唯一
        if (target_col->tab_name.empty()) {
            std::string tab_name;
            for (const auto &col : all_cols) {
                if (col.name != target_col->col_name)
                    continue;
                if (!tab_name.empty())
                    // 非首次匹配到表名 抛出异常
                    throw AmbiguousColumnError(target_col->col_name);
                // 首次匹配到表名
                tab_name = col.tab_name;
            }
            // 未匹配到表名 列不存在
            if (tab_name.empty())
                throw ColumnNotFoundError(target_col->col_name);
            sel_cols.emplace_back(TabCol {tab_name, target_col->col_name}, (order->orderby_dir == ast::OrderBy_DESC));
        }
        // 指定表 则检查指定表中是否存在指定列
        else {
            if (all_cols.end() ==  std::find_if(
                all_cols.begin(),
                all_cols.end(),
                [&] (const ColMeta &col) { return col.tab_name == target_col->tab_name && col.name == target_col->col_name; }
            ))
                throw ColumnNotFoundError(target_col->col_name);
            // 匹配到则加入 sel_cols
            sel_cols.emplace_back(
                TabCol {target_col->tab_name, target_col->col_name},
                (order->orderby_dir == ast::OrderBy_DESC)
            );
        }
    }
    for (const auto &col : all_cols) {
        for (const auto &order : x->orders) {
            if ((order->col->tab_name == "" ? true : col.tab_name == order->col->tab_name) && col.name == order->col->col_name)
                sel_cols.emplace_back(TabCol {col.tab_name, col.name}, (order->orderby_dir == ast::OrderBy_DESC));
        }
    }
    return std::make_shared<SortPlan>(T_Sort, std::move(plan), std::move(sel_cols), x->limit);
}


/**
 * @brief select plan 生成
 *
 * @param sel_cols select plan 选取的列
 * @param tab_names select plan 目标的表
 * @param conds select plan 选取条件
 */
std::shared_ptr<Plan> Planner::generate_select_plan(std::shared_ptr<Query> query, Context *context) {
    //逻辑优化
    query = logical_optimization(std::move(query), context);

    //物理优化
    auto sel_cols = query->cols;
    auto subplan = physical_optimization(query, context);

    auto plannerRoot = std::make_shared<ProjectionPlan>(
        T_Projection,
        std::move(subplan), 
        std::move(sel_cols)
    );

    return plannerRoot;
}

// 生成DDL语句和DML语句的查询执行计划
std::shared_ptr<Plan> Planner::do_planner(std::shared_ptr<Query> query, Context *context)
{
    std::shared_ptr<Plan> plannerRoot;
    if (auto x = std::dynamic_pointer_cast<ast::CreateTable>(query->parse)) {
        // create table;
        std::vector<ColDef> col_defs;
        for (auto &field : x->fields) {
            if (auto sv_col_def = std::dynamic_pointer_cast<ast::ColDef>(field)) {
                ColDef col_def = {.name = sv_col_def->col_name,
                                  .type = interp_sv_type(sv_col_def->type_len->type),
                                  .len = sv_col_def->type_len->len};
                col_defs.push_back(col_def);
            } else {
                throw InternalError("Unexpected field type");
            }
        }
        plannerRoot = std::make_shared<DDLPlan>(T_CreateTable, x->tab_name, std::vector<std::string>(), col_defs);
    } else if (auto x = std::dynamic_pointer_cast<ast::DropTable>(query->parse)) {
        // drop table;
        plannerRoot = std::make_shared<DDLPlan>(T_DropTable, x->tab_name, std::vector<std::string>(), std::vector<ColDef>());
    } else if (auto x = std::dynamic_pointer_cast<ast::CreateIndex>(query->parse)) {
        // create index;
        plannerRoot = std::make_shared<DDLPlan>(T_CreateIndex, x->tab_name, x->col_names, std::vector<ColDef>());
    } else if (auto x = std::dynamic_pointer_cast<ast::DropIndex>(query->parse)) {
        // drop index
        plannerRoot = std::make_shared<DDLPlan>(T_DropIndex, x->tab_name, x->col_names, std::vector<ColDef>());
    } else if (auto x = std::dynamic_pointer_cast<ast::InsertStmt>(query->parse)) {
        // insert;
        plannerRoot = std::make_shared<DMLPlan>(T_Insert, std::shared_ptr<Plan>(),  x->tab_name,  
                                                    query->values, std::vector<Condition>(), std::vector<SetClause>());
    } else if (auto x = std::dynamic_pointer_cast<ast::LoadStmt>(query->parse)) {
        plannerRoot = std::make_shared<LoadPlan>(x->tab_name, x->path);
    } else if (auto x = std::dynamic_pointer_cast<ast::DeleteStmt>(query->parse)) {
        // delete;
        // 生成表扫描方式
        std::shared_ptr<Plan> table_scan_executors;
        // 只有一张表，不需要进行物理优化了
        // int index_no = get_indexNo(x->tab_name, query->conds);
        std::vector<std::string> index_col_names;
        bool index_exist = get_index_cols(x->tab_name, query->conds, index_col_names);
        
        if (index_exist == false) {  // 该表没有索引
            index_col_names.clear();
            table_scan_executors = 
                std::make_shared<ScanPlan>(T_SeqScan, sm_manager_, x->tab_name, query->conds, index_col_names);
        } else {  // 存在索引
            table_scan_executors =
				std::make_shared<ScanPlan>(T_IndexScan, sm_manager_, x->tab_name, query->conds, index_col_names);
        }

        plannerRoot = std::make_shared<DMLPlan>(T_Delete, table_scan_executors, x->tab_name,  
                                                std::vector<Value>(), query->conds, std::vector<SetClause>());
    } else if (auto x = std::dynamic_pointer_cast<ast::UpdateStmt>(query->parse)) {
        // update;
        // 生成表扫描方式
        std::shared_ptr<Plan> table_scan_executors;
        // 只有一张表，不需要进行物理优化了
        // int index_no = get_indexNo(x->tab_name, query->conds);
        std::vector<std::string> index_col_names;
        bool index_exist = get_index_cols(x->tab_name, query->conds, index_col_names);

        if (index_exist == false) {  // 该表没有索引
        index_col_names.clear();
            table_scan_executors = 
                std::make_shared<ScanPlan>(T_SeqScan, sm_manager_, x->tab_name, query->conds, index_col_names);
        } else {  // 存在索引
            table_scan_executors =
				std::make_shared<ScanPlan>(T_IndexScan, sm_manager_, x->tab_name, query->conds, index_col_names);
        }
        plannerRoot = std::make_shared<DMLPlan>(T_Update, table_scan_executors, x->tab_name,
                                                     std::vector<Value>(), query->conds, 
                                                     query->set_clauses);
    } else if (auto x = std::dynamic_pointer_cast<ast::SelectStmt>(query->parse)) {
        // select;
        std::shared_ptr<plannerInfo> root = std::make_shared<plannerInfo>(x);
        bool is_all = query->is_all;
        // 生成select语句的查询执行计划
        std::shared_ptr<Plan> projection = generate_select_plan(std::move(query), context);
        PlanTag tag;
        switch (x->aggregate_type) {
            case (ast::SV_NONE):  tag = T_select;       break;
            case (ast::SV_COUNT): tag = T_select_count; break;
            case (ast::SV_MAX):   tag = T_select_max;   break;
            case (ast::SV_MIN):   tag = T_select_min;   break;
            case (ast::SV_SUM):   tag = T_select_sum;   break;
            default: throw InternalError("Unkown aggregate type.");
        }
        plannerRoot = std::make_shared<DMLPlan>(
            tag,
            projection,
            std::string(),
            std::vector<Value>(),
            std::vector<Condition>(),
            std::vector<SetClause>(),
            x->alias,
            is_all
        );
    } else {
        throw InternalError("Unexpected AST root");
    }
    return plannerRoot;
}