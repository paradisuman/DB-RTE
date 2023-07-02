/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "lru_replacer.h"

LRUReplacer::LRUReplacer(size_t num_pages) { max_size_ = num_pages; }

LRUReplacer::~LRUReplacer() = default;  

/**
 * @description: 使用LRU策略删除一个victim frame，并返回该frame的id
 * @param {frame_id_t*} frame_id 被移除的frame的id，如果没有frame被移除返回nullptr
 * @return {bool} 如果成功淘汰了一个页面则返回true，否则返回false
 */
bool LRUReplacer::victim(frame_id_t* frame_id) {
    std::scoped_lock lock{latch_};

    if (LRUlist_.empty()) {  // 如果LRU列表为空，那么就没有页面可以淘汰，返回false
        frame_id = nullptr;
        return false;
    }

    // 最少被访问的frame位于链表头部
    *frame_id = LRUlist_.front();

    // 移除链表中的尾部元素，并在哈希表中移除对应的项
    LRUhash_.erase(*frame_id);
    LRUlist_.pop_front();

    return true;
}


/**
 * @description: 固定指定的frame，即该页面无法被淘汰
 * @param {frame_id_t} 需要固定的frame的id
 */
void LRUReplacer::pin(frame_id_t frame_id) {
    std::scoped_lock lock{latch_};
    // 在数据结构中找到指定的frame
    auto it = LRUhash_.find(frame_id);

    // 若存在则在数据结构中和在LRU链表中删除
    if(it != LRUhash_.end()) {
        LRUlist_.erase(it->second);  // 在 list 中移除这个 frame
        LRUhash_.erase(it);  // 在 map 中移除这个 frame
    }
}


/**
 * @description: 取消固定一个frame，代表该页面可以被淘汰
 * @param {frame_id_t} frame_id 取消固定的frame的id
 */
void LRUReplacer::unpin(frame_id_t frame_id) {
    std::scoped_lock lock{latch_};

    // 在数据结构中查找指定的frame
    auto it = LRUhash_.find(frame_id);

    // 若不存在则在数据结构中和在LRU链表中添加
    if(it == LRUhash_.end()) {
        // 将frame添加到链表的尾部
        LRUlist_.push_back(frame_id);
        // 在hash表中存储该frame和其在链表中的位置
        LRUhash_[frame_id] = std::prev(LRUlist_.end());
    }
}

/**
 * @description: 获取当前replacer中可以被淘汰的页面数量
 */
size_t LRUReplacer::Size() { return LRUlist_.size(); }
/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "lru_replacer.h"

LRUReplacer::LRUReplacer(size_t num_pages) { max_size_ = num_pages; }

LRUReplacer::~LRUReplacer() = default;  

/**
 * @description: 使用LRU策略删除一个victim frame，并返回该frame的id
 * @param {frame_id_t*} frame_id 被移除的frame的id，如果没有frame被移除返回nullptr
 * @return {bool} 如果成功淘汰了一个页面则返回true，否则返回false
 */
bool LRUReplacer::victim(frame_id_t* frame_id) {
    std::scoped_lock lock{latch_};

    if (LRUlist_.empty()) {  // 如果LRU列表为空，那么就没有页面可以淘汰，返回false
        frame_id = nullptr;
        return false;
    }

    // 最少被访问的frame位于链表头部
    *frame_id = LRUlist_.front();

    // 移除链表中的尾部元素，并在哈希表中移除对应的项
    LRUhash_.erase(*frame_id);
    LRUlist_.pop_front();

    return true;
}


/**
 * @description: 固定指定的frame，即该页面无法被淘汰
 * @param {frame_id_t} 需要固定的frame的id
 */
void LRUReplacer::pin(frame_id_t frame_id) {
    std::scoped_lock lock{latch_};
    // 在数据结构中找到指定的frame
    auto it = LRUhash_.find(frame_id);

    // 若存在则在数据结构中和在LRU链表中删除
    if(it != LRUhash_.end()) {
        LRUlist_.erase(it->second);  // 在 list 中移除这个 frame
        LRUhash_.erase(it);  // 在 map 中移除这个 frame
    }
}


/**
 * @description: 取消固定一个frame，代表该页面可以被淘汰
 * @param {frame_id_t} frame_id 取消固定的frame的id
 */
void LRUReplacer::unpin(frame_id_t frame_id) {
    std::scoped_lock lock{latch_};

    // 在数据结构中查找指定的frame
    auto it = LRUhash_.find(frame_id);

    // 若不存在则在数据结构中和在LRU链表中添加
    if(it == LRUhash_.end()) {
        // 将frame添加到链表的尾部
        LRUlist_.push_back(frame_id);
        // 在hash表中存储该frame和其在链表中的位置
        LRUhash_[frame_id] = std::prev(LRUlist_.end());
    }
}

/**
 * @description: 获取当前replacer中可以被淘汰的页面数量
 */
size_t LRUReplacer::Size() { return LRUlist_.size(); }
