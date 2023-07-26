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

bool LockManager::check_lock(Transaction *txn) {
    switch (txn->get_state()) {
        // ABORTED COMMITED 状态不能获取锁
        case TransactionState::ABORTED :
        case TransactionState::COMMITTED : return false;
        // GROWING 状态直接返回 true
        case TransactionState::GROWING : return true;
        // SHRINKING 状态违反两阶段规则
        case TransactionState::SHRINKING : throw TransactionAbortException(txn->get_transaction_id(), AbortReason::LOCK_ON_SHIRINKING);
        // DEFAULT 状态则进入 GROWING 状态
        case TransactionState::DEFAULT : {
            txn->set_state(TransactionState::GROWING);
            return true;
        }
        default : return false;
    }
}

/**
 * @description: 申请行级共享锁
 * @return {bool} 加锁是否成功
 * @param {Transaction*} txn 要申请锁的事务对象指针
 * @param {Rid&} rid 加锁的目标记录ID 记录所在的表的fd
 * @param {int} tab_fd
 */
bool LockManager::lock_shared_on_record(Transaction* txn, const Rid& rid, int tab_fd) {
    std::scoped_lock lock{latch_};

    if (!check_lock(txn))
        return false;

    const auto lockdata_id = LockDataId(tab_fd, rid, LockDataType::RECORD);

    if (lock_table_.count(lockdata_id) == 0) {
        lock_table_.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(lockdata_id),
            std::forward_as_tuple()
        );
    }

    auto request = LockRequest(txn->get_transaction_id(), LockMode::SHARED);
    auto &request_queue = lock_table_[lockdata_id];

    const auto txn_itr = std::find_if(
        request_queue.request_queue_.begin(),
        request_queue.request_queue_.end(),
        [&] (const LockRequest &req) { return txn->get_transaction_id() == req.txn_id_; }
    );
    if (txn_itr != request_queue.request_queue_.end())
        return true;

    if (request_queue.group_lock_mode_ != GroupLockMode::S && request_queue.group_lock_mode_ != GroupLockMode::NON_LOCK)
        throw TransactionAbortException(txn->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);

    txn->get_lock_set()->emplace(lockdata_id);
    request.granted_ = true;
    request_queue.group_lock_mode_ = GroupLockMode::S;
    request_queue.request_queue_.emplace_back(request);
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
    std::scoped_lock lock{latch_};

    if (!check_lock(txn))
        return false;

    const auto lockdata_id = LockDataId(tab_fd, rid, LockDataType::RECORD);

    if (lock_table_.count(lockdata_id) == 0) {
        lock_table_.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(lockdata_id),
            std::forward_as_tuple()
        );
    }

    auto request = LockRequest(txn->get_transaction_id(), LockMode::EXCLUSIVE);
    auto &request_queue = lock_table_[lockdata_id];

    const auto txn_itr = std::find_if(
        request_queue.request_queue_.begin(),
        request_queue.request_queue_.end(),
        [&] (const LockRequest &req) { return txn->get_transaction_id() == req.txn_id_; }
    );
    if (txn_itr != request_queue.request_queue_.end()) {
        if (txn_itr->lock_mode_ == LockMode::EXCLUSIVE)
            return true;
        if (request_queue.group_lock_mode_ == GroupLockMode::S && request_queue.request_queue_.size() == 1) {
            txn_itr->lock_mode_ = LockMode::EXCLUSIVE;
            request_queue.group_lock_mode_ = GroupLockMode::X;
            return true;
        }
        throw TransactionAbortException(txn->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);
    }

    if (request_queue.group_lock_mode_ != GroupLockMode::NON_LOCK)
        throw TransactionAbortException(txn->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);

    txn->get_lock_set()->emplace(lockdata_id);
    request.granted_ = true;
    request_queue.group_lock_mode_ = GroupLockMode::X;
    request_queue.request_queue_.emplace_back(request);
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

    if (!check_lock(txn))
        return false;

    const auto lockdata_id = LockDataId(tab_fd, LockDataType::TABLE);

    if (lock_table_.count(lockdata_id) == 0) {
        lock_table_.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(lockdata_id),
            std::forward_as_tuple()
        );
    }

    auto request = LockRequest(txn->get_transaction_id(), LockMode::SHARED);
    auto &request_queue = lock_table_[lockdata_id];

    const auto txn_itr = std::find_if(
        request_queue.request_queue_.begin(),
        request_queue.request_queue_.end(),
        [&] (const LockRequest &req) { return txn->get_transaction_id() == req.txn_id_; }
    );
    if (txn_itr != request_queue.request_queue_.end()) {
        if (txn_itr->lock_mode_ == LockMode::SHARED || txn_itr->lock_mode_ == LockMode::S_IX || txn_itr->lock_mode_ == LockMode::SHARED)
            return true;
        if (txn_itr->lock_mode_ == LockMode::INTENTION_SHARED) {
            if (request_queue.group_lock_mode_ != GroupLockMode::IS && request_queue.group_lock_mode_ != GroupLockMode::S)
                throw TransactionAbortException(txn->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);
            txn_itr->lock_mode_ = LockMode::SHARED;
            request_queue.group_lock_mode_ = GroupLockMode::S;
            return true;
        }
        size_t ix_cnt = std::count_if(
            request_queue.request_queue_.begin(),
            request_queue.request_queue_.end(),
            [&] (const LockRequest &req) { return req.lock_mode_ == LockMode::INTENTION_EXCLUSIVE; }
        );
        if (ix_cnt != 1)
            throw TransactionAbortException(txn->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);
        txn_itr->lock_mode_ = LockMode::S_IX;
        request_queue.group_lock_mode_ = GroupLockMode::SIX;
        return true;
    }

    if (request_queue.group_lock_mode_ != GroupLockMode::NON_LOCK &&
        request_queue.group_lock_mode_ != GroupLockMode::S &&
        request_queue.group_lock_mode_ != GroupLockMode::IS)
        throw TransactionAbortException(txn->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);

    txn->get_lock_set()->emplace(lockdata_id);
    request.granted_ = true;
    request_queue.group_lock_mode_ = GroupLockMode::S;
    request_queue.request_queue_.emplace_back(request);
    return true;
}

/**
 * @description: 申请表级写锁
 * @return {bool} 返回加锁是否成功
 * @param {Transaction*} txn 要申请锁的事务对象指针
 * @param {int} tab_fd 目标表的fd
 */
bool LockManager::lock_exclusive_on_table(Transaction* txn, int tab_fd) {
    std::scoped_lock lock{latch_};

    if (!check_lock(txn))
        return false;

    const auto lockdata_id = LockDataId(tab_fd, LockDataType::TABLE);

    if (lock_table_.count(lockdata_id) == 0) {
        lock_table_.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(lockdata_id),
            std::forward_as_tuple()
        );
    }

    auto request = LockRequest(txn->get_transaction_id(), LockMode::EXCLUSIVE);
    auto &request_queue = lock_table_[lockdata_id];

    const auto txn_itr = std::find_if(
        request_queue.request_queue_.begin(),
        request_queue.request_queue_.end(),
        [&] (const LockRequest &req) { return txn->get_transaction_id() == req.txn_id_; }
    );
    if (txn_itr != request_queue.request_queue_.end()) {
        if (txn_itr->lock_mode_ == LockMode::EXCLUSIVE)
            return true;
        if (request_queue.request_queue_.size() != 1)
            throw TransactionAbortException(txn->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);
        txn_itr->lock_mode_ = LockMode::EXCLUSIVE;
        request_queue.group_lock_mode_ = GroupLockMode::X;
        return true;
    }

    if (request_queue.group_lock_mode_ != GroupLockMode::NON_LOCK)
        throw TransactionAbortException(txn->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);

    txn->get_lock_set()->emplace(lockdata_id);
    request.granted_ = true;
    request_queue.group_lock_mode_ = GroupLockMode::X;
    request_queue.request_queue_.emplace_back(request);
    return true;
}

/**
 * @description: 申请表级意向读锁
 * @return {bool} 返回加锁是否成功
 * @param {Transaction*} txn 要申请锁的事务对象指针
 * @param {int} tab_fd 目标表的fd
 */
bool LockManager::lock_IS_on_table(Transaction* txn, int tab_fd) {
    std::scoped_lock lock{latch_};

    if (!check_lock(txn))
        return false;

    const auto lockdata_id = LockDataId(tab_fd, LockDataType::TABLE);

    if (lock_table_.count(lockdata_id) == 0) {
        lock_table_.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(lockdata_id),
            std::forward_as_tuple()
        );
    }

    auto request = LockRequest(txn->get_transaction_id(), LockMode::INTENTION_SHARED);
    auto &request_queue = lock_table_[lockdata_id];

    const auto txn_itr = std::find_if(
        request_queue.request_queue_.begin(),
        request_queue.request_queue_.end(),
        [&] (const LockRequest &req) { return txn->get_transaction_id() == req.txn_id_; }
    );
    if (txn_itr != request_queue.request_queue_.end())
        return true;

    if (request_queue.group_lock_mode_ == GroupLockMode::X)
        throw TransactionAbortException(txn->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);

    txn->get_lock_set()->emplace(lockdata_id);
    request.granted_ = true;
    if (request_queue.group_lock_mode_ == GroupLockMode::NON_LOCK)
        request_queue.group_lock_mode_ = GroupLockMode::IS;
    request_queue.request_queue_.emplace_back(request);
    return true;
}

/**
 * @description: 申请表级意向写锁
 * @return {bool} 返回加锁是否成功
 * @param {Transaction*} txn 要申请锁的事务对象指针
 * @param {int} tab_fd 目标表的fd
 */
bool LockManager::lock_IX_on_table(Transaction* txn, int tab_fd) {
    std::scoped_lock lock{latch_};

    if (!check_lock(txn))
        return false;

    const auto lockdata_id = LockDataId(tab_fd, LockDataType::TABLE);

    if (lock_table_.count(lockdata_id) == 0) {
        lock_table_.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(lockdata_id),
            std::forward_as_tuple()
        );
    }

    auto request = LockRequest(txn->get_transaction_id(), LockMode::INTENTION_EXCLUSIVE);
    auto &request_queue = lock_table_[lockdata_id];

    const auto txn_itr = std::find_if(
        request_queue.request_queue_.begin(),
        request_queue.request_queue_.end(),
        [&] (const LockRequest &req) { return txn->get_transaction_id() == req.txn_id_; }
    );
    if (txn_itr != request_queue.request_queue_.end()) {
        if (txn_itr->lock_mode_ == LockMode::INTENTION_EXCLUSIVE || txn_itr->lock_mode_ == LockMode::S_IX || txn_itr->lock_mode_ == LockMode::EXCLUSIVE)
            return true;
        if (txn_itr->lock_mode_ == LockMode::SHARED) {
            size_t s_cnt = std::count_if(
                request_queue.request_queue_.begin(),
                request_queue.request_queue_.end(),
                [&] (const LockRequest &req) { return req.lock_mode_ == LockMode::SHARED; }
            );
            if (s_cnt != 1)
                throw TransactionAbortException(txn->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);
            txn_itr->lock_mode_ = LockMode::S_IX;
            request_queue.group_lock_mode_ = GroupLockMode::SIX;
            return true;
        }
        throw TransactionAbortException(txn->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);
    }

    if (request_queue.group_lock_mode_ != GroupLockMode::NON_LOCK &&
        request_queue.group_lock_mode_ != GroupLockMode::IS &&
        request_queue.group_lock_mode_ != GroupLockMode::IX)
        throw TransactionAbortException(txn->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);

    txn->get_lock_set()->emplace(lockdata_id);
    request.granted_ = true;
    request_queue.group_lock_mode_ = GroupLockMode::IX;
    request_queue.request_queue_.emplace_back(request);
    return true;
}

/**
 * @description: 释放锁
 * @return {bool} 返回解锁是否成功
 * @param {Transaction*} txn 要释放锁的事务对象指针
 * @param {LockDataId} lock_data_id 要释放的锁ID
 */
bool LockManager::unlock(Transaction* txn, LockDataId lock_data_id) {
    std::scoped_lock lock{latch_};

    switch (txn->get_state()) {
        // ABORTED COMMITED 状态不能再解锁 直接返回
        case TransactionState::ABORTED :
        case TransactionState::COMMITTED : return false;
        // GROWING 状态则进入 SHRINKING 状态
        case TransactionState::GROWING : {
            txn->set_state(TransactionState::SHRINKING);
            break;
        }
        default : break;
    }

    if (lock_table_.count(lock_data_id) == 0)
        return true;

    auto &request_queue = lock_table_[lock_data_id];
    const auto txn_itr = std::find_if(
        request_queue.request_queue_.begin(),
        request_queue.request_queue_.end(),
        [&] (const LockRequest &req) { return txn->get_transaction_id() == req.txn_id_; }
    );
    if (txn_itr != request_queue.request_queue_.end())
        request_queue.request_queue_.erase(txn_itr);

    std::map<LockMode, size_t> lockmode_cnt;
    for (const auto &req : request_queue.request_queue_)
        lockmode_cnt[req.lock_mode_] += 1;

    if (lockmode_cnt[LockMode::EXCLUSIVE])
        request_queue.group_lock_mode_ = GroupLockMode::X;
    else if (lockmode_cnt[LockMode::S_IX])
        request_queue.group_lock_mode_ = GroupLockMode::SIX;
    else if (lockmode_cnt[LockMode::INTENTION_EXCLUSIVE])
        request_queue.group_lock_mode_ = GroupLockMode::IX;
    else if (lockmode_cnt[LockMode::INTENTION_SHARED])
        request_queue.group_lock_mode_ = GroupLockMode::IS;
    else if (lockmode_cnt[LockMode::SHARED])
        request_queue.group_lock_mode_ = GroupLockMode::S;
    else
        request_queue.group_lock_mode_ = GroupLockMode::NON_LOCK;
    return true;
}