#include "lock_manager.h"

#include <algorithm>

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
    const auto txn_itr = std::find_if(
        request_queue.request_queue_.begin(),
        request_queue.request_queue_.end(),
        [&] (const LockRequest &req) { return txn->get_transaction_id() == req.txn_id_; }
    );
    // 当前事务对表已经持有锁则直接返回
    if (txn_itr != request_queue.request_queue_.end()) {
        // 当前事务已经持有 S 锁 则直接返回 true
        return true;
    }

    if (request_queue.group_lock_mode_ != GroupLockMode::NON_LOCK &&
        request_queue.group_lock_mode_ != GroupLockMode::S)
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

    // 在锁表中查找 lockdata_id 锁表中没有对应的记录则创建新的空记录
    if (lock_table_.count(lockdata_id) == 0) {
        lock_table_.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(lockdata_id),
            std::forward_as_tuple()
        );
    }

    auto request = LockRequest(txn->get_transaction_id(), LockMode::EXCLUSIVE);
    auto &request_queue = lock_table_[lockdata_id];

    // 查找当前事务是否已经对当前表持有锁
    const auto txn_itr = std::find_if(
        request_queue.request_queue_.begin(),
        request_queue.request_queue_.end(),
        [&] (const LockRequest &req) { return txn->get_transaction_id() == req.txn_id_; }
    );
    // 如果当前事务对表持有锁
    if (txn_itr != request_queue.request_queue_.end()) {
        // 如果已经持有 X 锁则直接返回 true
        if (txn_itr->lock_mode_ == LockMode::EXCLUSIVE)
            return true;
        // 该事务已经持有锁 等待队列仅有该事务则可以升锁
        if (request_queue.request_queue_.size() != 1)
            throw TransactionAbortException(txn->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);
        txn_itr->lock_mode_ = LockMode::EXCLUSIVE;
        request_queue.group_lock_mode_ = GroupLockMode::X;
        return true;
    }

    // 若组策略为 NON_LOCK 则为该事务创建锁
    if (request_queue.group_lock_mode_ != GroupLockMode::NON_LOCK)
        throw TransactionAbortException(txn->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);

    txn->get_lock_set()->emplace(lockdata_id);
    request.granted_ = true;
    request_queue.group_lock_mode_ = GroupLockMode::X;
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

    // 在锁表中查找 lockdata_id 若不在锁表中则直接返回解锁成功
    if (lock_table_.count(lock_data_id) == 0)
        return true;

    auto &request_queue = lock_table_[lock_data_id];
    // 查找当前事务对当前 lockdata_id 持有的锁并删除
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

    // 更新当前持有 lockdata_id 的锁的事务队列的最高等级
    if (lockmode_cnt[LockMode::EXCLUSIVE])
        request_queue.group_lock_mode_ = GroupLockMode::X;
    else if (lockmode_cnt[LockMode::SHARED])
        request_queue.group_lock_mode_ = GroupLockMode::S;
    else
        request_queue.group_lock_mode_ = GroupLockMode::NON_LOCK;
    return true;
}
