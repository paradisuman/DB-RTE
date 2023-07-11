#pragma once

#include <vector>
#include <memory>
#include <algorithm>
#include "common/common.h"
#include "system/sm_meta.h"

namespace executor_utils {

inline bool checkConds(const std::unique_ptr<RmRecord> record, const std::vector<Condition> &conds, const std::vector<ColMeta> &cols) {
    for (const auto &cond : conds) {
        const auto &lcol = *std::find_if(
            cols.begin(),
            cols.end(),
            [&] (const auto &col) { return cond.lhs_col.col_name == col.name && cond.lhs_col.tab_name == col.tab_name; }
        );
        // 从记录中读取左值
        Value lval;
        lval.type = lcol.type;
        lval.load_raw(lcol.len, record->data + lcol.offset);
        // 准备右值
        Value rval;
        if (cond.is_rhs_val) {
            // 若右值为常量
            rval = cond.rhs_val;
        } else {
            // 若右值为表中值
            const auto &rcol = *std::find_if(
                cols.begin(),
                cols.end(),
                [&] (const auto &col) { return cond.rhs_col.col_name == col.name && cond.rhs_col.tab_name == col.tab_name; }
            );
            rval.type = rcol.type;
            rval.load_raw(rcol.len, record->data + rcol.offset);
        }
        // 二元检定
        if (!binop(cond.op, lval, rval))
            return false;;
    }
    // 通过了全部 conds
    return true;
}

} // end of namespace executor_utils