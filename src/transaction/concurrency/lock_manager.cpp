/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "lock_manager.h"

/**
 * @description: 申请行级共享锁
 * @return {bool} 加锁是否成功
 * @param {Transaction*} txn 要申请锁的事务对象指针
 * @param {Rid&} rid 加锁的目标记录ID 记录所在的表的fd
 * @param {int} tab_fd
 */
bool LockManager::lock_shared_on_record(Transaction* txn, const Rid& rid, int tab_fd) {
    return true;
}

/**
 * @description: 申请行级排他锁
 * @return {bool} 加锁是否成功
 * @param {Transaction*} txn 要申请锁的事务对象指针
 * @param {Rid&} rid 加锁的目标记录ID
 * @param {int} tab_fd 记录所在的表的fd
 */
bool LockManager::lock_exclusive_on_record(Transaction* txn, const Rid& rid, int tab_fd) {
    return true;
}


/**
 * @description: 申请表级读锁
 * @return {bool} 返回加锁是否成功
 * @param {Transaction*} txn 要申请锁的事务对象指针
 * @param {int} tab_fd 目标表的fd
 */
bool LockManager::lock_shared_on_table(Transaction* txn, int tab_fd) {
    std::scoped_lock lock{latch_};

    const auto lockdata_id = LockDataId(tab_fd, LockDataType::TABLE);

    // 在锁表中查找表 锁表中没有对应的记录则创建新的空记录
    if (lock_table_.count(lockdata_id) == 0) {
        lock_table_.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(lockdata_id),
            std::forward_as_tuple()
        );
    }

    auto request = LockRequest(txn->get_transaction_id(), LockMode::SHARED);
    auto &request_queue = lock_table_[lockdata_id];

    // 查找当前事务是否已经对当前表 持有锁
    if (request_queue.cnt < 0) {
        // 如果自己是写锁
        auto reque = request_queue.request_queue_.begin();
        if (reque->txn_id_ == txn->get_transaction_id()) {
            return true;
        }
        else throw TransactionAbortException(txn->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);
    }
    else {
        // 如果读锁或者无锁直接加入
        request.granted_ = true;
        request_queue.cnt++;
        request_queue.group_lock_mode_ = GroupLockMode::S;
        request_queue.request_queue_.emplace_back(request);
        txn->get_lock_set()->emplace(lockdata_id);
        return true;
    }

}

/**
 * @description: 申请表级写锁
 * @return {bool} 返回加锁是否成功
 * @param {Transaction*} txn 要申请锁的事务对象指针
 * @param {int} tab_fd 目标表的fd
 */
bool LockManager::lock_exclusive_on_table(Transaction* txn, int tab_fd) {
    std::scoped_lock lock{latch_};

    const auto lockdata_id = LockDataId(tab_fd, LockDataType::TABLE);

    // 1 在锁表中查找 lockdata_id 锁表中没有对应的记录则创建新的空记录
    if (lock_table_.count(lockdata_id) == 0) {
        lock_table_.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(lockdata_id),
            std::forward_as_tuple()
        );
    }

    auto request = LockRequest(txn->get_transaction_id(), LockMode::EXCLUSIVE);
    auto &request_queue = lock_table_[lockdata_id];

    // 2 查找当前事务是否已经对当前表持有锁
    if (request_queue.cnt < 0){
        // 如果写锁是自己
        auto reque = request_queue.request_queue_.begin();
        if (request_queue.cnt == 1 && reque->txn_id_ == txn->get_transaction_id()) {
            return true;
        }
        else throw TransactionAbortException(txn->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);
    }
    // 如果无锁
    else if (request_queue.cnt == 0) {
        request_queue.request_queue_.clear();
        request_queue.cnt = -1;
        request.granted_ = true;
        request_queue.group_lock_mode_ = GroupLockMode::X;
        request_queue.request_queue_.emplace_back(request);
        txn->get_lock_set()->insert(lockdata_id);

        return true;
    }
    // 如果有读锁
    else {
        auto reque = request_queue.request_queue_.begin();
        // 如果唯一的读锁是他自己
        if (request_queue.cnt == 1 && reque->txn_id_ == txn->get_transaction_id()) {
            request_queue.request_queue_.clear();
            request_queue.cnt = -1;
            request.granted_ = true;
            request_queue.group_lock_mode_ = GroupLockMode::X;
            request_queue.request_queue_.emplace_back(request);
            txn->get_lock_set()->insert(lockdata_id);

            return true;
        }
        // 如果多个读锁
        else throw TransactionAbortException(txn->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);
    }
}
    

/**
 * @description: 申请表级意向读锁
 * @return {bool} 返回加锁是否成功
 * @param {Transaction*} txn 要申请锁的事务对象指针
 * @param {int} tab_fd 目标表的fd
 */
bool LockManager::lock_IS_on_table(Transaction* txn, int tab_fd) {
    return true;
}

/**
 * @description: 申请表级意向写锁
 * @return {bool} 返回加锁是否成功
 * @param {Transaction*} txn 要申请锁的事务对象指针
 * @param {int} tab_fd 目标表的fd
 */
bool LockManager::lock_IX_on_table(Transaction* txn, int tab_fd) {
    return true;
}

/**
 * @description: 释放锁
 * @return {bool} 返回解锁是否成功
 * @param {Transaction*} txn 要释放锁的事务对象指针
 * @param {LockDataId} lock_data_id 要释放的锁ID
 */
bool LockManager::unlock(Transaction* txn, LockDataId lock_data_id) {
    // 对每个表锁上关于该事务清除
    std::scoped_lock lock{latch_};

    auto &request_queue = lock_table_[lock_data_id];

    // 写锁删除
    if (request_queue.cnt < 0) {
        auto reque = request_queue.request_queue_.begin();
        // 如果此事务没有写锁，直接返回
        if (reque->txn_id_ != txn->get_transaction_id()) {
            return true;
        }
        // 否则改为纯净锁
        request_queue.cnt = 0;
        request_queue.request_queue_.clear();
        request_queue.group_lock_mode_ = GroupLockMode::NON_LOCK;
        return true;
    }
    else if(request_queue.cnt > 0) {
        // 读锁删除
        const auto txn_itr = std::find_if(
            request_queue.request_queue_.begin(),
            request_queue.request_queue_.end(),
            [&] (const LockRequest &req) { return txn->get_transaction_id() == req.txn_id_; }
        );
        // 如果写锁有就删除
        if (txn_itr != request_queue.request_queue_.end()) {
            request_queue.request_queue_.erase(txn_itr);
            request_queue.cnt--;

            if (request_queue.cnt == 0) {
                request_queue.group_lock_mode_ = GroupLockMode::NON_LOCK;
            }
        
            return true;
        }
        return false;
    }
}