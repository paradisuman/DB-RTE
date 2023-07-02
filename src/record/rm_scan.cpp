/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "rm_scan.h"
#include "rm_file_handle.h"

/**
 * @brief 初始化file_handle和rid
 * @param file_handle
 */
RmScan::RmScan(const RmFileHandle *file_handle) : file_handle_(file_handle) {
    // Todo:
    // 初始化file_handle和rid（指向第一个存放了记录的位置）

    rid_.page_no = 0;
    rid_.slot_no = Bitmap::next_bit(true, file_handle_->fetch_page_handle(0).bitmap, file_handle_->file_hdr_.bitmap_size, -1);
}

/**
 * @brief 找到文件中下一个存放了记录的位置
 */
void RmScan::next() {
    // Todo:
    // 找到文件中下一个存放了记录的非空闲位置，用rid_来指向这个位置

    // Caution: 也许报错更合适？
    if (is_end())
        return ;

    if (rid_.slot_no == file_handle_->file_hdr_.bitmap_size) {
        rid_.page_no = file_handle_->fetch_page_handle(rid_.page_no).page_hdr->next_free_page_no;
        rid_.slot_no = 0;
    }
    rid_.slot_no = Bitmap::next_bit(true, file_handle_->fetch_page_handle(0).bitmap, file_handle_->file_hdr_.bitmap_size, rid_.slot_no);
}

/**
 * @brief ​ 判断是否到达文件末尾
 */
bool RmScan::is_end() const {
    // Todo: 修改返回值

    return rid_.page_no == file_handle_->file_hdr_.num_pages && rid_.slot_no == file_handle_->file_hdr_.bitmap_size;
}

/**
 * @brief RmScan内部存放的rid
 */
Rid RmScan::rid() const {
    return rid_;
}