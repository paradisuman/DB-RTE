/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */
#undef NDEBUG

#include <cassert>
#include <fstream>

#include "parser.h"

int main(int argc, char *argv[]) {
    if (argc == 1) {
        std::cerr << "Error: Please provide the test script file.";
        return -1;
    }
    std::string sql;
    std::ifstream sqlFile(argv[1]);

    if (!sqlFile) {
        std::cerr << "Error: Unable to open file " << argv[1];
        return -1;
    }

    while (std::getline(sqlFile, sql)) {
        std::cout << sql << std::endl;
        YY_BUFFER_STATE buf = yy_scan_string(sql.c_str());
        assert(yyparse() == 0);
        if (ast::parse_tree != nullptr) {
            ast::TreePrinter::print(ast::parse_tree);
            yy_delete_buffer(buf);
            std::cout << std::endl;
        } else {
            std::cout << "exit/EOF" << std::endl;
        }
    }

    sqlFile.close();
    ast::parse_tree.reset();

    return 0;
}
