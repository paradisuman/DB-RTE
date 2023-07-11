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

#include <cassert>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <set>
#include "defs.h"
#include "record/rm_defs.h"


struct TabCol {
    std::string tab_name;
    std::string col_name;

    friend bool operator<(const TabCol &x, const TabCol &y) {
        return std::make_pair(x.tab_name, x.col_name) < std::make_pair(y.tab_name, y.col_name);
    }
};

struct Value {
    ColType type;  // type of value
    union {
        int int_val;      // int value
        float float_val;  // float value
    };
    std::string str_val;  // string value

    std::shared_ptr<RmRecord> raw;  // raw record buffer

    void set_int(int int_val_) {
        type = TYPE_INT;
        int_val = int_val_;
    }

    void set_float(float float_val_) {
        type = TYPE_FLOAT;
        float_val = float_val_;
    }

    void set_str(std::string str_val_) {
        type = TYPE_STRING;
        str_val = std::move(str_val_);
    }

    void init_raw(size_t len) {
        assert(raw == nullptr);
        raw = std::make_shared<RmRecord>(len);
        if (type == TYPE_INT) {
            assert(len == sizeof(int));
            *(int *)(raw->data) = int_val;
        } else if (type == TYPE_FLOAT) {
            assert(len == sizeof(float));
            *(float *)(raw->data) = float_val;
        } else if (type == TYPE_STRING) {
            if (len < str_val.size()) {
                throw StringOverflowError();
            }
            memset(raw->data, 0, len);
            memcpy(raw->data, str_val.c_str(), str_val.size());
        }
    }

    void load_raw(size_t len, char *data) {
        assert(raw == nullptr);
        if (type == TYPE_INT) {
            assert(len == sizeof(int));
            int_val = *(int *)(data);
        } else if (type == TYPE_FLOAT) {
            assert(len == sizeof(float));
            float_val = *(float *)(data);
        } else if (type == TYPE_STRING) {
            if (len < str_val.size()) {
                throw StringOverflowError();
            }
            str_val = std::string(data, len);
        }
    }
};

enum CompOp { OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE };

struct Condition {
    TabCol lhs_col;   // left-hand side column
    CompOp op;        // comparison operator
    bool is_rhs_val;  // true if right-hand side is a value (not a column)
    TabCol rhs_col;   // right-hand side column
    Value rhs_val;    // right-hand side value
};

struct SetClause {
    TabCol lhs;
    Value rhs;
};

const std::vector<std::set<ColType>> legal_binop = {
    /* [TYPE_INT]     = */ {TYPE_INT, TYPE_BIGINT, TYPE_FLOAT},
    /* [TYPE_FLOAT]   = */ {TYPE_INT, TYPE_BIGINT, TYPE_FLOAT},
    /* [TYPE_BIGINT]  = */ {TYPE_INT, TYPE_BIGINT, TYPE_FLOAT},
    /* [TYPE_STRING]  = */ {TYPE_STRING},
};

inline bool is_compatible_type(const ColType a, const ColType b) {
    return legal_binop.at(a).count(b) != 0;
}

inline bool binop(const CompOp op, const Value &lval, const Value &rval) {
    auto _binop = [&] (auto t1, auto t2) {
        switch (op) {
            case OP_EQ : return t1 == t2;
            case OP_NE : return t1 != t2;
            case OP_LT : return t1 <  t2;
            case OP_GT : return t1 >  t2;
            case OP_LE : return t1 <= t2;
            case OP_GE : return t1 >= t2;
            default    : throw InternalError("Invalid operand.");
        }
    };

    auto _str_binop = [&] (std::string s1, std::string s2) {
        int _strcmp = std::strcmp(s1.c_str(), s2.c_str());
        switch (op) {
            case OP_EQ : return _strcmp == 0;
            case OP_NE : return _strcmp != 0;
            case OP_LT : return _strcmp <  0;
            case OP_GT : return _strcmp >  0;
            case OP_LE : return _strcmp <= 0;
            case OP_GE : return _strcmp >= 0;
            default    : throw InternalError("Invalid operand.");
        }
    };

    switch (lval.type) {
        case TYPE_INT : switch (rval.type) {
                            case TYPE_INT    : return _binop(lval.int_val, rval.int_val);
                            case TYPE_FLOAT  : return _binop(lval.int_val, rval.float_val);
                            case TYPE_BIGINT : return _binop(lval.int_val, rval.bigint_val);
                            default : throw IncompatibleTypeError(coltype2str(lval.type), coltype2str(rval.type));
                        }
        case TYPE_BIGINT : switch (rval.type) {
                            case TYPE_INT    : return _binop(lval.bigint_val, rval.int_val);
                            case TYPE_FLOAT  : return _binop(lval.bigint_val, rval.float_val);
                            case TYPE_BIGINT : return _binop(lval.bigint_val, rval.bigint_val);
                            default : throw IncompatibleTypeError(coltype2str(lval.type), coltype2str(rval.type));
                        }
        case TYPE_FLOAT : switch (rval.type) {
                            case TYPE_INT    : return _binop(lval.float_val, rval.int_val);
                            case TYPE_FLOAT  : return _binop(lval.float_val, rval.float_val);
                            case TYPE_BIGINT : return _binop(lval.float_val, rval.bigint_val);
                            default : throw IncompatibleTypeError(coltype2str(lval.type), coltype2str(rval.type));
                        }
        case TYPE_STRING : return _str_binop(lval.str_val, rval.str_val);
        default : throw IncompatibleTypeError(coltype2str(lval.type), coltype2str(rval.type));
    }
}