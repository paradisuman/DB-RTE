/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "buffer_pool_manager.h"

/**
 * @description: 从free_list或replacer中得到可淘汰帧页的 *frame_id。
 * @return {bool} true: 可替换帧查找成功 , false: 可替换帧查找失败
 * @param {frame_id_t*} frame_id 帧页id指针,返回成功找到的可替换帧id
 */
bool BufferPoolManager::find_victim_page(frame_id_t* frame_id) {
    std::scoped_lock lock{latch_};
    // 判断是否有free frame，有则直接分配 free frame，并且无需淘汰页面
    if (!free_list_.empty()) {
        *frame_id = free_list_.front();
        free_list_.pop_front();
        return true; // 可替換幀查找成功
    // 已满则使用lru_replacer中的方法选择淘汰页面
    } else {
        return replacer_->victim(frame_id);
    }
}

/**
 * @description: 更新页面数据, 如果为脏页则需写入磁盘，再更新为新页面，更新page元数据(data, is_dirty, page_id)和page table。
 * @param {Page*} page 写回页指针
 * @param {PageId} new_page_id 新的page_id
 * @param {frame_id_t} new_frame_id 新的帧frame_id
 */
void BufferPoolManager::update_page(Page *page, PageId new_page_id, frame_id_t new_frame_id) {
    std::scoped_lock lock{latch_};
    // 仅对脏页执行更新数据的操作
    if (page->is_dirty()) {
        // 写入脏页旧数据
        const auto& page_id = page->get_page_id();
        disk_manager_->write_page(page_id.fd, page_id.page_no, page->get_data(), PAGE_SIZE);
        // 更新脏页元数据
        page->is_dirty_ = false;
    }
    // 在page table删去旧的页面记录
    page_table_.erase(page->get_page_id());
    // 重置 page 的 data
    disk_manager_->read_page(new_page_id.fd, new_page_id.page_no, page->data_, PAGE_SIZE);
    // 更新 page 元数据
    page->is_dirty_ = false;
    // 更新 page id
    page->id_ = new_page_id;

    // 更新 page table
    page_table_.emplace(new_page_id, new_frame_id);
}

/**
 * @description: 从buffer pool获取需要的页。
 *              如果页表中存在page_id（说明该page在缓冲池中），并且pin_count++。
 *              如果页表不存在page_id（说明该page在磁盘中），则找缓冲池victim page，将其替换为磁盘中读取的page，pin_count置1。
 * @return {Page*} 若获得了需要的页则将其返回，否则返回nullptr
 * @param {PageId} page_id 需要获取的页的PageId
 */
Page* BufferPoolManager::fetch_page(PageId page_id) {
    std::scoped_lock lock{latch_};

    // 尝试在 page table 中查找指定 PageId
    const auto& page_table_record = page_table_.find(page_id);
    if (page_table_record != page_table_.end()) {
        // 获得目标帧
        auto target_frame_id = page_table_record->second;
        // 页表中存在page_id, 该page在缓冲池, pin_count + 1
        pages_[target_frame_id].pin_count_ += 1;
        return &pages_[target_frame_id];
    }

    // 若未找到指定 PageId 从find_victime_page中获得一个可用的frame
    frame_id_t *victim_frame_id = nullptr;
    if (!find_victim_page(victim_frame_id)) {
        // find_victim_page 失败
        return nullptr;
    }

    // 从find_victim_page中获得的frame对应的page
    auto& target_page = pages_[*victim_frame_id];

    // update_page, 更新 page 内容
    update_page(&target_page, target_page.get_page_id(), *victim_frame_id); // update_page 方法中判断是否为脏页

    // 调用disk_manager_的read_page读取目标页到frame
    disk_manager_->read_page(page_id.fd, page_id.page_no, target_page.data_, PAGE_SIZE);

    // 固定目标页，更新pin_count_
    replacer_->pin(*victim_frame_id);
    target_page.pin_count_ += 1;

    // 返回目标页
    return &target_page;
}

/**
 * @description: 取消固定pin_count>0的在缓冲池中的page
 * @return {bool} 如果目标页的pin_count<=0则返回false，否则返回true
 * @param {PageId} page_id 目标page的page_id
 * @param {bool} is_dirty 若目标page应该被标记为dirty则为true，否则为false
 */
bool BufferPoolManager::unpin_page(PageId page_id, bool is_dirty) {
    std::scoped_lock lock{latch_};

    // 尝试在page_table_中搜寻page_id对应的页P
    const auto& target_page_record = page_table_.find(page_id);
    if (target_page_record == page_table_.end()) {
        // P在页表中不存在 return false
        return false;
    }
    // 获取对应帧和对应页缓冲
    auto  target_frame = target_page_record->second;
    auto& target_page  = pages_[target_frame];

    // P在页表中存在，获取其pin_count_
    auto& target_page_pin_count = target_page.pin_count_;

    // 若pin_count_已经等于0，则返回false
    if (target_page_pin_count == 0) {
        return false;
    }

    // 若pin_count_大于0，则pin_count_自减一
    target_page_pin_count -= 1;

    // 若自减后等于0，则调用replacer_的Unpin
    if (target_page_pin_count == 0) {
        replacer_->unpin(target_frame);
    }

    // 根据参数is_dirty，更改P的is_dirty_
    if (!target_page.is_dirty()) {
        target_page.is_dirty_ = is_dirty;
    }

    return true;
}

/**
 * @description: 将目标页写回磁盘，不考虑当前页面是否正在被使用
 * @return {bool} 成功则返回true，否则返回false(只有page_table_中没有目标页时)
 * @param {PageId} page_id 目标页的page_id，不能为INVALID_PAGE_ID
 */
bool BufferPoolManager::flush_page(PageId page_id) {
    std::scoped_lock lock{latch_};

    if (page_id.page_no == INVALID_PAGE_ID) {
        return false;
    }

    // 查找页表,尝试获取目标页P缓冲
    const auto& target_page_record = page_table_.find(page_id);
    if (target_page_record == page_table_.end()) {
        // 目标页P没有被page_table_记录 ，返回false
        return false;
    }

    // 获取对应帧和对应页
    auto  target_frame = target_page_record->second;
    auto& target_page  = pages_[target_frame];
    // 无论P是否为脏都将其写回磁盘。
    disk_manager_->write_page(page_id.fd, page_id.page_no, target_page.data_, PAGE_SIZE);
    // 更新P的is_dirty_
    target_page.is_dirty_ = false;

    return true;
}

/**
 * @description: 创建一个新的page，即从磁盘中移动一个新建的空page到缓冲池某个位置。
 * @return {Page*} 返回新创建的page，若创建失败则返回nullptr
 * @param {PageId*} page_id 当成功创建一个新的page时存储其page_id
 */
Page* BufferPoolManager::new_page(PageId* page_id) {
    std::scoped_lock lock{latch_};

    // 获得一个可用的frame，若无法获得则返回nullptr
    frame_id_t *target_frame_id = nullptr;
    if (!find_victim_page(target_frame_id)) {
        return nullptr;
    }

    // 获取对应的页缓冲, 并写回磁盘
    auto& target_page = pages_[*target_frame_id];
    flush_page(target_page.get_page_id());
    // 在fd对应的文件分配一个新的page_id
    target_page.id_ = *page_id;
    // 固定frame，更新pin_count_
    replacer_->pin(*target_frame_id);
    target_page.pin_count_ = 1;

    // 返回获得的page
    return &target_page;
}

/**
 * @description: 从buffer_pool删除目标页
 * @return {bool} 如果目标页不存在于buffer_pool或者成功被删除则返回true，若其存在于buffer_pool但无法删除则返回false
 * @param {PageId} page_id 目标页
 */
bool BufferPoolManager::delete_page(PageId page_id) {
    // Caution: 清除元数据仅重置了 is_dirty 与 pin count, 其余元数据理应在加入数据时更新

    std::scoped_lock lock{latch_};

    // 在page_table_中查找目标页，若不存在返回true
    const auto target_record = page_table_.find(page_id);
    if (target_record == page_table_.end()) {
        return true;
    }
    // 获取目标页缓存
    auto  target_frame = target_record->second;
    auto& target_page =  pages_[target_frame];
    // 若目标页的pin_count不为0，则返回false
    if (target_page.pin_count_ != 0) {
        return false;
    }

    // 将目标页数据写回磁盘
    flush_page(page_id);
    // 从页表中删除目录页
    page_table_.erase(page_id);
    // 重置元数据,将其加入free_list_
    target_page.is_dirty_ = false;
    target_page.pin_count_ = 0;

    free_list_.push_back(target_frame);

    return true;
}

/**
 * @description: 将buffer_pool中的所有页写回到磁盘
 * @param {int} fd 文件句柄
 */
void BufferPoolManager::flush_all_pages(int fd) {
    // Caution: 疑惑. fd理应存放于页表的page_id中, 为什么作为参数被提供

    std::scoped_lock lock{latch_};
    // 对buffer_pool中所有被缓冲的页面
    for (auto& [page_id, frame_id] : page_table_) {
        // 将页面写入磁盘
        disk_manager_->write_page(fd, page_id.page_no, pages_[frame_id].data_, PAGE_SIZE);
        // 更新is_dirty_
        pages_[frame_id].is_dirty_ = false;
    }
}