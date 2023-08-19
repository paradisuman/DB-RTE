/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "rm_file_handle.h"

#include <algorithm>

/**
 * @description: 获取当前表中记录号为rid的记录
 * @param {Rid&} rid 记录号，指定记录的位置
 * @param {Context*} context
 * @return {unique_ptr<RmRecord>} rid对应的记录对象指针
 */
std::unique_ptr<RmRecord> RmFileHandle::get_record(const Rid& rid, Context* context) const {
    // Todo:
    // 1. 获取指定记录所在的page handle
    // 2. 初始化一个指向RmRecord的指针（赋值其内部的data和size）

    // 获取指定记录所在的 page handle
    const auto &target_page_handle = fetch_page_handle(rid.page_no);

    // 初始化一个指向RmRecord的指针(赋值其内部的data和size)
    auto ret = std::make_unique<RmRecord>(
        file_hdr_.record_size,
        target_page_handle.get_slot(rid.slot_no)
    );

    // unpin 分配的页面
    buffer_pool_manager_->unpin_page(PageId {fd_, rid.page_no}, true);

    return ret;
}

/**
 * @description: 在当前表中插入一条记录，不指定插入位置
 * @param {char*} buf 要插入的记录的数据
 * @param {Context*} context
 * @return {Rid} 插入的记录的记录号（位置）
 */
Rid RmFileHandle::insert_record(char* buf, Context* context) {
    // Todo:
    // 1. 获取当前未满的page handle
    // 2. 在page handle中找到空闲slot位置
    // 3. 将buf复制到空闲slot位置
    // 4. 更新page_handle.page_hdr中的数据结构
    // 注意考虑插入一条记录后页面已满的情况，需要更新file_hdr_.first_free_page_no

    // Caution: 页面插满后自动顺序（或链表序）扩充下一个页面 未检查是否符合file_hdr

    // 获取当前未满的 page handle
    auto available_page_handle = create_page_handle();
    // 获得未满的 page handle 的 page header
    auto &available_page_hdr = *available_page_handle.page_hdr;

    // 获取空闲 slot 的位置
    const auto available_slot_no = Bitmap::next_bit(
        false,
        available_page_handle.bitmap,
        file_hdr_.num_records_per_page,
        -1
    );

    // 将buf复制到空闲slot位置
    std::copy_n(buf, file_hdr_.record_size, available_page_handle.get_slot(available_slot_no));

    // 更新 bitmap
    Bitmap::set(available_page_handle.bitmap, available_slot_no);
    // 更新 page hdr
    available_page_hdr.num_records += 1;

    // 注意考虑插入一条记录后页面已满的情况，需要更新file_hdr_.first_free_page_no
    if (available_page_hdr.num_records == file_hdr_.num_records_per_page) {
        file_hdr_.first_free_page_no = available_page_hdr.next_free_page_no;
        disk_manager_->write_page(fd_, RM_FILE_HDR_PAGE, (char *)&file_hdr_, sizeof(RmFileHdr));
    }

    // 返回新的rid
    auto new_rid = Rid {
        .page_no = available_page_handle.page->get_page_id().page_no,
        .slot_no = available_slot_no
    };
    // 完成后更新lsn
    if (context != nullptr) {
        available_page_handle.page->set_page_lsn(context->txn_->get_prev_lsn());
    }

    // unpin 分配的页面
    buffer_pool_manager_->unpin_page(PageId {fd_, new_rid.page_no}, true);

    return new_rid;
}

/**
 * @description: 在当前表中的指定位置插入一条记录
 * @param {Rid&} rid 要插入记录的位置
 * @param {char*} buf 要插入记录的数据
 */
void RmFileHandle::insert_record(const Rid& rid, char* buf) {
    // 获得待插入的页面
    auto target_page_handle = fetch_page_handle(rid.page_no);
    // 该位置是否已经有记录，如果无，更新page hdr与bitmap
    if (!is_record(rid)) {
        Bitmap::set(target_page_handle.bitmap, rid.slot_no);
        target_page_handle.page_hdr->num_records += 1;
    }
    // 插入记录
    std::copy_n(buf, file_hdr_.record_size, target_page_handle.get_slot(rid.slot_no));

    // unpin 分配的页面
    buffer_pool_manager_->unpin_page(PageId {fd_, rid.page_no}, true);
}

/**
 * @description: 删除记录文件中记录号为rid的记录
 * @param {Rid&} rid 要删除的记录的记录号（位置）
 * @param {Context*} context
 */
void RmFileHandle::delete_record(const Rid& rid, Context* context) {
    // Todo:
    // 1. 获取指定记录所在的page handle
    // 2. 更新page_handle.page_hdr中的数据结构
    // 注意考虑删除一条记录后页面未满的情况，需要调用release_page_handle()

    // 获取指定记录所在的page handle
    auto target_page_handle = fetch_page_handle(rid.page_no);
    // 删除一条记录后页面由满变为未满 需要调用release_page_handle()
    if (target_page_handle.page_hdr->num_records == file_hdr_.num_records_per_page) {
        release_page_handle(target_page_handle);
    }
    // 更新 page_handle.page_hdr中的数据结构
    target_page_handle.page_hdr->num_records -= 1;
    Bitmap::reset(target_page_handle.bitmap, rid.slot_no);
    // 完成后记录lsn
    if (context != nullptr) {
        target_page_handle.page->set_page_lsn(context->txn_->get_prev_lsn());
    }
    // unpin 分配的页面
    buffer_pool_manager_->unpin_page(target_page_handle.page->get_page_id(), true);
}

/**
 * @description: 更新记录文件中记录号为rid的记录
 * @param {Rid&} rid 要更新的记录的记录号（位置）
 * @param {char*} buf 新记录的数据
 * @param {Context*} context
 */
void RmFileHandle::update_record(const Rid& rid, char* buf, Context* context) {
    // Todo:
    // 1. 获取指定记录所在的page handle
    // 2. 更新记录

    // 获取指定记录所在的 page handle
    auto target_page_handle = fetch_page_handle(rid.page_no);
    // 更新记录
    std::copy_n(buf, file_hdr_.record_size, target_page_handle.get_slot(rid.slot_no));
    // unpin 分配的页面
    buffer_pool_manager_->unpin_page(PageId {fd_, rid.page_no}, true);
    // 完成后记录lsn
    if (context != nullptr) {
        target_page_handle.page->set_page_lsn(context->txn_->get_prev_lsn());
    }
}

/**
 * 以下函数为辅助函数，仅提供参考，可以选择完成如下函数，也可以删除如下函数，在单元测试中不涉及如下函数接口的直接调用
*/
/**
 * @description: 获取指定页面的页面句柄
 * @param {int} page_no 页面号
 * @return {RmPageHandle} 指定页面的句柄
 */
RmPageHandle RmFileHandle::fetch_page_handle(int page_no) const {
    // Todo:
    // 使用缓冲池获取指定页面，并生成page_handle返回给上层
    // if page_no is invalid, throw PageNotExistError exception

    // 构建 page id
    if (page_no == INVALID_PAGE_ID) {
        throw PageNotExistError("", page_no);
    }
    // 从缓冲池获得指定页面
    auto target_page =
        buffer_pool_manager_->fetch_page(PageId {fd_, page_no});

    return RmPageHandle(&file_hdr_, target_page);
}

/**
 * @description: 创建一个新的page handle
 * @return {RmPageHandle} 新的PageHandle
 */
RmPageHandle RmFileHandle::create_new_page_handle() {
    // Todo:
    // 1.使用缓冲池来创建一个新page
    // 2.更新page handle中的相关信息
    // 3.更新file_hdr_

    // 使用缓冲池来创建一个新page
    PageId new_page_id{.fd = fd_, .page_no = INVALID_PAGE_ID};
    auto new_page = buffer_pool_manager_->new_page(&new_page_id);
    if (new_page == nullptr) {
        throw InternalError("Create new page handle failed.");
    }
    // 更新新页面中相关信息
    auto new_page_handle = RmPageHandle(&file_hdr_, new_page);
    *new_page_handle.page_hdr = RmPageHdr {
        .next_free_page_no = RM_NO_PAGE,
        .num_records = 0,
    };
    // 更新新页面的bit map
    Bitmap::init(
        new_page_handle.bitmap,
        new_page_handle.file_hdr->bitmap_size
    );

    // 将新页面写入磁盘 防呆
    buffer_pool_manager_->flush_page(new_page_id);
    // 更新file_hdr_
    file_hdr_.num_pages += 1;
    file_hdr_.first_free_page_no = new_page_id.page_no;
    disk_manager_->write_page(fd_, RM_FILE_HDR_PAGE, (char *)&file_hdr_, sizeof(RmFileHdr));

    return new_page_handle;
}

/**
 * @brief 创建或获取一个空闲的page handle
 *
 * @return RmPageHandle 返回生成的空闲page handle
 * @note pin the page, remember to unpin it outside!
 */
RmPageHandle RmFileHandle::create_page_handle() {
    // Todo:
    // 1. 判断file_hdr_中是否还有空闲页
    //     1.1 没有空闲页：使用缓冲池来创建一个新page；可直接调用create_new_page_handle()
    //     1.2 有空闲页：直接获取第一个空闲页
    // 2. 生成page handle并返回给上层

    // Question: 使用以下条件判定是否还有空闲页，即：空闲页即free page
    if (file_hdr_.first_free_page_no == RM_NO_PAGE) {
        return create_new_page_handle();
    }
    return fetch_page_handle(file_hdr_.first_free_page_no);
}

/**
 * @description: 当一个页面从没有空闲空间的状态变为有空闲空间状态时，更新文件头和页头中空闲页面相关的元数据
 */
void RmFileHandle::release_page_handle(RmPageHandle &page_handle) {
    // Todo:
    // 当page从已满变成未满，考虑如何更新：
    // 1. page_handle.page_hdr->next_free_page_no
    // 2. file_hdr_.first_free_page_no

    // 单链表插入结点
    page_handle.page_hdr->next_free_page_no = file_hdr_.first_free_page_no;
    file_hdr_.first_free_page_no = page_handle.page->get_page_id().page_no;
    disk_manager_->write_page(fd_, RM_FILE_HDR_PAGE, (char *)&file_hdr_, sizeof(RmFileHdr));
}

lsn_t RmFileHandle::get_page_lsn(page_id_t page_no) {
    auto target_page = fetch_page_handle(page_no);

    auto page_lsn = target_page.page->get_page_lsn();

    buffer_pool_manager_->unpin_page(PageId {fd_, page_no}, false);

    return page_lsn;
}