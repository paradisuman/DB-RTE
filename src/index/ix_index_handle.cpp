/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "ix_index_handle.h"

#include "ix_scan.h"

#include <algorithm>

#include<functional>

/**
 * @brief 在当前node中查找第一个>=target的key_idx
 *
 * @return key_idx，范围为[0,num_key)，如果返回的key_idx=num_key，则表示target大于最后一个key
 * @note 返回key index（同时也是rid index），作为slot no
 */
int IxNodeHandle::lower_bound(const char *target) const {
    // Todo:
    // 查找当前节点中第一个大于等于target的key，并返回key的位置给上层
    // 提示: 可以采用多种查找方式，如顺序遍历、二分查找等；使用ix_compare()函数进行比较
    // 二分
    // 一个节点一千个数据，顺序和二分最好斟酌使用
    if(binary_search) {
        int left = 0;
        int right = page_hdr->num_key;
        int result = page_hdr->num_key;

        while (left <= right) {
            int mid = left + (right - left) / 2;
            char *key_addr = get_key(mid);
            if (ix_compare(key_addr , target, file_hdr->col_types_, file_hdr->col_lens_) < 0) {
                left = mid + 1;
            } else {
                right = mid - 1;
            }
        }

        // 若未找到目标，返回end
        return left;
    }
    else {
        int key_index = 0;
        for(; key_index < page_hdr->num_key; key_index ++) {
            char *key_addr = get_key(key_index);
            if(ix_compare(target, key_addr, file_hdr->col_types_, file_hdr->col_lens_) <= 0) {
                break;
            }
        }
        return key_index;

    }
}

/**
 * @brief 在当前node中查找第一个>target的key_idx
 *
 * @return key_idx，范围为[1,num_key)，如果返回的key_idx=num_key，则表示target大于等于最后一个key
 * @note 注意此处的范围从1开始
 */
int IxNodeHandle::upper_bound(const char *target) const {
    // Todo:
    // 查找当前节点中第一个大于target的key，并返回key的位置给上层
    // 提示: 可以采用多种查找方式：顺序遍历、二分查找等；使用ix_compare()函数进行比较
    if(binary_search) {
        int left = 0;
        int right = page_hdr->num_key;
        int result = page_hdr->num_key;

        while (left <= right) {
            int mid = left + (right - left) / 2;
            char *key_addr = get_key(mid);
            if (ix_compare(key_addr, target, file_hdr->col_types_, file_hdr->col_lens_) <= 0) {
                left = mid + 1;
            } else {
                right = mid - 1;
            }
        }

        // 若未找到目标，返回end指针
        return left;
    }
    else {
        int key_index = 1;
        for(; key_index < page_hdr->num_key; key_index ++) {
            char *key_addr = get_key(key_index);
            if(ix_compare(target, key_addr, file_hdr->col_types_, file_hdr->col_lens_) < 0) {
                break;
            }
        }
        return key_index;
    }
}

/**
 * @brief 用于叶子结点根据key来查找该结点中的键值对
 * 值value作为传出参数，函数返回是否查找成功
 *
 * @param key 目标key
 * @param[out] value 传出参数，目标key对应的Rid
 * @return 目标key是否存在
 */
bool IxNodeHandle::leaf_lookup(const char *key, Rid **value) {
    // Todo:
    // 1. 在叶子节点中获取目标key所在位置
    // 2. 判断目标key是否存在
    // 3. 如果存在，获取key对应的Rid，并赋值给传出参数value
    // 提示：可以调用lower_bound()和get_rid()函数。
    int return_index = lower_bound(key);
    // 小于所有数，因此返回错误
    if (return_index == get_size())
        return false;
    // 所有涉及key使用的用函数而不是=
    if (ix_compare(get_key(return_index), key, file_hdr->col_types_, file_hdr->col_lens_) == 0) {
        *value = get_rid(return_index);
        return true;
    }
    else {
        return false;
    }
}

/**
 * 用于内部结点（非叶子节点）查找目标key所在的孩子结点（子树）
 * @param key 目标key
 * @return page_id_t 目标key所在的孩子节点（子树）的存储页面编号
 */
page_id_t IxNodeHandle::internal_lookup(const char *key) {
    // Todo:
    // 1. 查找当前非叶子节点中目标key所在孩子节点（子树）的位置
    // 2. 获取该孩子节点（子树）所在页面的编号
    // 3. 返回页面编号
    // 获取大于等于该值的index，该值减少一即为第一个小于等于key的
    int get_index = upper_bound(key);
    get_index-- ;
    // 找到第一个大于该值的索引
    // 此index即为子树index
    return value_at(get_index);
}

/**
 * @brief 在指定位置插入n个连续的键值对
 * 将key的前n位插入到原来keys中的pos位置；将rid的前n位插入到原来rids中的pos位置
 *
 * @param pos 要插入键值对的位置
 * @param (key, rid) 连续键值对的起始地址，也就是第一个键值对，可以通过(key, rid)来获取n个键值对
 * @param n 键值对数量
 * @note [0,pos)           [pos,num_key)
 *                            key_slot
 *                            /      \
 *                           /        \
 *       [0,pos)     [pos,pos+n)   [pos+n,num_key+n)
 *                      key           key_slot
 */
void IxNodeHandle::insert_pairs(int pos, const char *key, const Rid *rid, int n) {
    // Todo:
    // 1. 判断pos的合法性
    // 2. 通过key获取n个连续键值对的key值，并把n个key值插入到pos位置
    int old_size = get_size();
    if (pos < 0 || pos > old_size) {
        // 临时使用的throw，不一定契合
        throw RMDBError("insert_pairs pos位置错误！");
    }
    int key_len = file_hdr->col_tot_len_;
    int rid_len = sizeof(Rid);

    // 插入key
    int num = page_hdr->num_key - pos;
    char *begin_key = get_key(pos);
    memmove(begin_key + n * key_len, begin_key, num * key_len);
    memcpy(begin_key, key, n * key_len);

    // 3. 通过rid获取n个连续键值对的rid值，并把n个rid值插入到pos位置
    Rid *begin_rid = get_rid(pos);
    memmove(begin_rid + n, begin_rid, num * rid_len);
    memcpy(begin_rid, rid, n * rid_len);
    // 4. 更新当前节点的键数量
    set_size(get_size() + n);
}

/**
 * @brief 用于在结点中插入单个键值对。
 * 函数返回插入后的键值对数量
 *
 * @param (key, value) 要插入的键值对
 * @return int 键值对数量
 */
int IxNodeHandle::insert(const char *key, const Rid &value) {
    // Todo:
    // 1. 查找要插入的键值对应该插入到当前节点的哪个位置
    // 2. 如果key重复则不插入
    // 3. 如果key不重复则插入键值对
    // 4. 返回完成插入操作之后的键值对数量
    int get_index = lower_bound(key);
    int old_size = get_size();
    // 如果已存在，不处理
    if (get_size() != 0 && ix_compare(get_key(get_index), key, file_hdr->col_types_, file_hdr->col_lens_) == 0) {
        throw RMDBError("索引出错！");
        return old_size;
    }
    else {
        // 处理key
        insert_pair(get_index, key, value);
    }
    
    return old_size + 1;
}

/**
 * @brief 用于在结点中的指定位置删除单个键值对
 *
 * @param pos 要删除键值对的位置
 */
void IxNodeHandle::erase_pair(int pos) {
    // Todo:
    // 1. 删除该位置的key
    // 2. 删除该位置的rid
    // 3. 更新结点的键值对数量
    int old_size = get_size();
    int num =  old_size - 1  - pos;
    if (pos < 0 || pos >= old_size) {
        // fix 临时使用的throw，不一定契合
        throw RMDBError("erase_pair pos位置错误！");
    }

    // 删除key
    int key_len = file_hdr->col_tot_len_;
    char* key = get_key(pos);
    memmove(key, key+key_len, num*key_len);

    // 删除rid
    int rid_len = sizeof(Rid);
    Rid *rid = get_rid(pos);
    char *rid_char_ptr = reinterpret_cast<char*>(rid);
    memmove(rid_char_ptr, rid_char_ptr+rid_len, num*rid_len);

    // 设置size
    set_size(old_size - 1);
}

/**
 * @brief 用于在结点中删除指定key的键值对。函数返回删除后的键值对数量
 *
 * @param key 要删除的键值对key值
 * @return 完成删除操作后的键值对数量
 */
int IxNodeHandle::remove(const char *key) {
    // Todo:
    // 1. 查找要删除键值对的位置
    // 2. 如果要删除的键值对存在，删除键值对
    // 3. 返回完成删除操作后的键值对数量

    int pos = lower_bound(key);
    int old_size = get_size();
    // 检查
    if (pos < 0 || pos >= old_size) {
        // fix 临时使用的throw，不一定契合
        throw RMDBError("remove 中输入错误pos！\n");
    }

    // 没找到
    if (ix_compare(get_key(pos), key, file_hdr->col_types_, file_hdr->col_lens_) != 0) {
        // fix 临时使用
        return -1;
    }

    // 处理keys
    erase_pair(pos);
    return old_size - 1;
}

IxIndexHandle::IxIndexHandle(DiskManager *disk_manager, BufferPoolManager *buffer_pool_manager, int fd)
    : disk_manager_(disk_manager), buffer_pool_manager_(buffer_pool_manager), fd_(fd) {
    // init file_hdr_
    disk_manager_->read_page(fd, IX_FILE_HDR_PAGE, (char *)&file_hdr_, sizeof(file_hdr_));
    char* buf = new char[PAGE_SIZE];
    memset(buf, 0, PAGE_SIZE);
    disk_manager_->read_page(fd, IX_FILE_HDR_PAGE, buf, PAGE_SIZE);
    file_hdr_ = new IxFileHdr();
    file_hdr_->deserialize(buf);
    
    // disk_manager管理的fd对应的文件中，设置从file_hdr_->num_pages开始分配page_no
    // int now_page_no = disk_manager_->get_fd2pageno(fd);
    int now_page_no = file_hdr_->num_pages_ - 1;
    disk_manager_->set_fd2pageno(fd, now_page_no + 1);
}

/**
 * @brief 用于查找指定键所在的叶子结点
 * @param key 要查找的目标key值
 * @param operation 查找到目标键值对后要进行的操作类型
 * @param transaction 事务参数，如果不需要则默认传入nullptr
 * @return [leaf node] and [root_is_latched] 返回目标叶子结点以及根结点是否加锁
 * @note 需要去 Unlatch and unpin the leaf node outside!
 * 注意：用了FindLeafPage之后一定要unlatch叶结点，否则下次latch该结点会堵塞！
 */
std::pair<IxNodeHandle *, bool> IxIndexHandle::find_leaf_page(const char *key, Operation operation,
                                                            Transaction *transaction, bool find_first) {
    // Todo:
    // 1. 获取根节点
    // 2. 从根节点开始不断向下查找目标key
    // 3. 找到包含该key值的叶子结点停止查找，并返回叶子节点

    // fix :先没有考虑锁;没有operation
    // debug :用到了fetch_page
    // 先判断是否是叶子节点，否则一直找到叶子节点，再之后返回所需值
    IxNodeHandle *root_node = fetch_node(file_hdr_->root_page_);
    IxNodeHandle *tem = root_node;

    while (!tem->is_leaf_page()) {
        // 更新pageid
        page_id_t child_page_no = tem->internal_lookup(key);
        // unpin page，由于没有修改因此不是脏页,并且释放指针
        buffer_pool_manager_->unpin_page(tem->get_page_id(), false);
        delete(tem);

        tem = fetch_node(child_page_no);
    }

    return std::make_pair(tem, false);
}

/**
 * @brief 用于查找指定键在叶子结点中的对应的值result
 *
 * @param key 查找的目标key值
 * @param result 用于存放结果的容器
 * @param transaction 事务指针
 * @return bool 返回目标键值对是否存在
 */
bool IxIndexHandle::get_value(const char *key, std::vector<Rid> *result, Transaction *transaction) {
    // Todo:
    // 1. 获取目标key值所在的叶子结点
    // 2. 在叶子节点中查找目标key值的位置，并读取key对应的rid
    // 3. 把rid存入result参数中
    // 提示：使用完buffer_pool提供的page之后，记得unpin page；fix 记得处理并发的上锁
    PageId last = PageId {fd_, file_hdr_->root_page_};
    IxNodeHandle *tem = new IxNodeHandle(
        file_hdr_,
        buffer_pool_manager_->fetch_page(last)
    );
    while(!tem->is_leaf_page()){
        // 更新pageid
        last = PageId {fd_, tem->internal_lookup(key)};
        // unpin page，由于没有修改因此不是脏页,并且释放指针
        buffer_pool_manager_->unpin_page(last, false);
        delete tem;
        tem = new IxNodeHandle(
            file_hdr_,
            buffer_pool_manager_->fetch_page(last)
        );
    }
    bool res = tem->leaf_lookup(key, (Rid**)result);
    buffer_pool_manager_->unpin_page(last, false);
    delete tem;

    // 找到并放入
    return res;
}

/**
 * @brief  将传入的一个node拆分(Split)成两个结点，在node的右边生成一个新结点new node
 * @param node 需要拆分的结点
 * @return 拆分得到的new_node
 * @note need to unpin the new node outside
 * 注意：本函数执行完毕后，原node和new node都需要在函数外面进行unpin
 */
IxNodeHandle *IxIndexHandle::split(IxNodeHandle *node) {
    // Todo:
    // 1. 将原结点的键值对平均分配，右半部分分裂为新的右兄弟结点
    //    需要初始化新节点的page_hdr内容
    // 2. 如果新的右兄弟结点是叶子结点，更新新旧节点的prev_leaf和next_leaf指针
    //    为新节点分配键值对，更新旧节点的键值对数记录
    //   
    // 3. 如果新的右兄弟结点不是叶子结点，更新该结点的所有孩子结点的父节点信息(使用IxIndexHandle::maintain_child())
    int total_nodes = node->get_size();
    int left_end_index = total_nodes/2;
    // 创建新page
    IxNodeHandle *new_node = create_node();
    // 初始化新节点的page_hdr内容
    IxPageHdr* new_hdr = new_node->page_hdr;
    new_hdr->num_key = 0;
    new_hdr->parent = node->get_parent_page_no();
    new_hdr->is_leaf = node->page_hdr->is_leaf;
    // 更新键值对
    new_node->insert_pairs(
        0,
        node->get_key(left_end_index),
        node->get_rid(left_end_index),
        total_nodes - left_end_index
    );
    // 删除键值对
    node->set_size(left_end_index);
    

    if (new_hdr->is_leaf) {
        // 更新兄弟指针
        new_hdr->prev_leaf = node->get_page_no();
        /* fix 首页节点和1的关系 */
        new_hdr->next_leaf = node->page_hdr->next_leaf;
        node->page_hdr->next_leaf = new_node->get_page_no();

        // 如果是最后一个节点，就更新最后节点
        if (file_hdr_->last_leaf_ == node->get_page_no()) {
            file_hdr_->last_leaf_ = new_node->get_page_no();
        }
    }
    else {
        // 更新新节点下子节点的父节点
        page_id_t new_id = new_node->page->get_page_id().page_no;
        for (size_t i = 0; i < total_nodes - left_end_index; ++i) {
            maintain_child(new_node, i);
        }
    }

    return new_node;
}

/**
 * @brief Insert key & value pair into internal page after split
 * 拆分(Split)后，向上找到old_node的父结点
 * 将new_node的第一个key插入到父结点，其位置在 父结点指向old_node的孩子指针 之后
 * 如果插入后>=maxsize，则必须继续拆分父结点，然后在其父结点的父结点再插入，即需要递归
 * 直到找到的old_node为根结点时，结束递归（此时将会新建一个根R，关键字为key，old_node和new_node为其孩子）
 *
 * @param (old_node, new_node) 原结点为old_node，old_node被分裂之后产生了新的右兄弟结点new_node
 * @param key 要插入parent的key
 * @note 一个结点插入了键值对之后需要分裂，分裂后左半部分的键值对保留在原结点，在参数中称为old_node，
 * 右半部分的键值对分裂为新的右兄弟节点，在参数中称为new_node（参考Split函数来理解old_node和new_node）
 * @note 本函数执行完毕后，new node和old node都需要在函数外面进行unpin
 */
void IxIndexHandle::insert_into_parent(IxNodeHandle *old_node, const char *key, IxNodeHandle *new_node,
                                     Transaction *transaction) {
    // Todo:
    // 1. 分裂前的结点（原结点, old_node）是否为根结点，如果为根结点需要分配新的root
    // 2. 获取原结点（old_node）的父亲结点
    // 3. 获取key对应的rid，并将(key, rid)插入到父亲结点
    // 4. 如果父亲结点仍需要继续分裂，则进行递归插入
    // 提示：记得unpin page
    /* fix 可以之后换顺序，先插入后分裂 */

    // 如果是根节点就需要创建父节点
    if (old_node->is_root_page()) {
        //创建新节点
        IxNodeHandle *root_node = create_node();
        root_node->page_hdr->num_key = 0;
        root_node->set_parent_page_no(INVALID_PAGE_ID);
        root_node->page_hdr->is_leaf = false;
        // 这里key就默认是子节点的首部了
        root_node->insert_pair(0, old_node->keys, {old_node->get_page_no(), -1});
        root_node->insert_pair(1, new_node->keys, {new_node->get_page_no(), -1});
        // 设置他们的父节点
        old_node->set_parent_page_no(root_node->get_page_no());
        new_node->set_parent_page_no(root_node->get_page_no());
        // 修改文件的root节点
        file_hdr_->root_page_ = root_node->get_page_no();
        // 此时修改了，需要true
        buffer_pool_manager_->unpin_page(root_node->get_page_id(), true);
        delete root_node;
    }
    // 如果是中间节点
    else {
        IxNodeHandle *parent_node = fetch_node(new_node->get_parent_page_no());
        IxNodeHandle *new_pnode;
        // 如果满员，将父节点分裂，然后向上插入，检测是否满员，重复流程，直到头节点
        if (parent_node ->get_size() == parent_node->get_max_size()) {
            // 父亲节点的右边新节点
            new_pnode = split(parent_node);
            insert_into_parent(parent_node, new_pnode->keys, new_pnode, transaction);
            buffer_pool_manager_->unpin_page(new_pnode->get_page_id(), true);
            delete new_pnode;
            buffer_pool_manager_->unpin_page(parent_node->get_page_id(), true);
            delete parent_node;

            // 由于多次分裂，情况复杂，因此重新获取
            parent_node = fetch_node(new_node->get_parent_page_no());
            parent_node->insert(new_node->keys, {new_node->get_page_no(), -1});
            buffer_pool_manager_->unpin_page(parent_node->get_page_id(), true);
            delete parent_node;
        }
        else {
            // 插入父节点应该不会插入到第一个
            parent_node->insert(new_node->keys, {new_node->get_page_id().page_no, -1});
            buffer_pool_manager_->unpin_page(parent_node->get_page_id(), true);
            delete parent_node;
        }
    }
}

/**
 * @brief 将指定键值对插入到B+树中
 * @param (key, value) 要插入的键值对
 * @param transaction 事务指针
 * @return page_id_t 插入到的叶结点的page_no
 */
page_id_t IxIndexHandle::insert_entry(const char *key, const Rid &value, Transaction *transaction) {
    // Todo:
    // 1. 查找key值应该插入到哪个叶子节点
    // 2. 在该叶子节点中插入键值对,如果是最小值，那么需要 fix 更新节点了
    // 3. 如果结点已满，分裂结点，并把新结点的相关信息插入父节点
    // 提示：记得unpin page；若当前叶子节点是最右叶子节点，则需要更新file_hdr_.last_leaf；记得处理并发的上锁
    auto result = find_leaf_page(key, Operation::FIND, transaction, true);
    IxNodeHandle *leaf_node = result.first;
    if(leaf_node->get_size() == leaf_node->get_max_size()){
        // 如果满了先分裂，后插入
        IxNodeHandle *new_node = split(leaf_node);
        insert_into_parent(leaf_node, new_node->keys, new_node, transaction);

        // 处理垃圾
        buffer_pool_manager_->unpin_page(leaf_node->get_page_id(), true);
        delete leaf_node;
        buffer_pool_manager_->unpin_page(new_node->get_page_id(), true);
        delete new_node;
        
        // 分裂情况可能会比较复杂，因此重新找点
        result = find_leaf_page(key, Operation::FIND, transaction, true);
        leaf_node = result.first;
        // int pos = leaf_node->lower_bound(key);
        
        // // // 先更新父节点
        // if (pos == 0 && !leaf_node->is_root_page()) {
        //     IxNodeHandle *parent = fetch_node(leaf_node->get_parent_page_no());
        //     update_node(
        //         parent,
        //         leaf_node,
        //         key,
        //         transaction
        //     );

        //     buffer_pool_manager_->unpin_page(parent->get_page_id(), true);
        //     delete parent;
        // }
        // 再更新子节点
        leaf_node->insert(key, value);
        maintain_parent(leaf_node);
    }
    else {
        // int pos = leaf_node->lower_bound(key);
        
        // if (pos == 0 && !leaf_node->is_root_page()) {
        //     IxNodeHandle *parent = fetch_node(leaf_node->get_parent_page_no());
        //     int pos = parent->lower_bound(leaf_node->keys);
        //     update_node(
        //         parent,
        //         leaf_node,
        //         key,
        //         transaction
        //     );

        //     buffer_pool_manager_->unpin_page(parent->get_page_id(), true);
        //     delete parent;
        // }
        leaf_node->insert(key, value);
        maintain_parent(leaf_node);
    }
    
    page_id_t id = leaf_node->get_page_no();
    
    // 删除和unpin
    buffer_pool_manager_->unpin_page(leaf_node->get_page_id(), true);
    delete leaf_node;

    return id;
}

/**
 * @brief 用于删除B+树中含有指定key的键值对
 * @param key 要删除的key值
 * @param transaction 事务指针
 */
bool IxIndexHandle::delete_entry(const char *key, Transaction *transaction) {
    // Todo:
    // 1. 获取该键值对所在的叶子结点
    // 2. 在该叶子结点中删除键值对
    // 3. 如果删除成功需要调用CoalesceOrRedistribute来进行合并或重分配操作，并根据函数返回结果判断是否有结点需要删除
    // 4. fix 如果需要并发，并且需要删除叶子结点，则需要在事务的delete_page_set中添加删除结点的对应页面；记得处理并发的上锁
    auto result = find_leaf_page(key, Operation::FIND, transaction, true);
    int pos = result.first->lower_bound(key);

    // 删除键值对
    result.first->erase_pair(pos);
    maintain_parent(result.first);

    IxNodeHandle *leaf_node = result.first;
    bool need_delete = coalesce_or_redistribute(leaf_node, transaction, nullptr);
    // 处理
    buffer_pool_manager_->unpin_page(leaf_node->get_page_id(),true);
    if (need_delete) {
        //erase_leaf(leaf_node);
        // 还有些删除工作在合并中完成
        /* fix delete还没有做 */
        // transaction->append_index_deleted_page(leaf_node->page);
    }
    return false;
}

/**
 * @brief 用于处理合并和重分配的逻辑，用于删除键值对后调用
 *
 * @param node 执行完删除操作的结点
 * @param transaction 事务指针
 * @param root_is_latched 传出参数：根节点是否上锁，用于并发操作
 * @return 是否需要删除结点
 * @note User needs to first find the sibling of input page.
 * If sibling's size + input page's size >= 2 * page's minsize, then redistribute.
 * Otherwise, merge(Coalesce).
 */
bool IxIndexHandle::coalesce_or_redistribute(IxNodeHandle *node, Transaction *transaction, bool *root_is_latched) {
    // Todo:
    // 1. 判断node结点是否为根节点
    //    1.1 如果是根节点，需要调用AdjustRoot() 函数来进行处理，返回根节点是否需要被删除
    //    1.2 如果不是根节点，并且不需要执行合并或重分配操作，则直接返回false，否则执行2
    // 2. 获取node结点的父亲结点
    // 3. 寻找node结点的兄弟结点（优先选取前驱结点）
    // 4. 如果node结点和兄弟结点的键值对数量之和，能够支撑两个B+树结点（即node.size+neighbor.size >=
    // NodeMinSize*2)，则只需要重新分配键值对（调用Redistribute函数）
    // 5. 如果不满足上述条件，则需要合并两个结点，将右边的结点合并到左边的结点（调用Coalesce函数）
    bool need_delete;
    if(node->is_root_page()){
        return adjust_root(node);
    }
    else if (node->get_size() >= node->get_min_size()) {
        return false;
    }

    // 获取父节点和兄弟节点
    IxNodeHandle *father = fetch_node(node->get_parent_page_no());
    IxNodeHandle *brother;
    int index = father->find_child(node);
    if (index == 0) {
        brother = fetch_node(father->value_at(1));
    }
    else {
        brother = fetch_node(father->value_at(0));
    }

    // 选择重分配还是合并
    if (node->get_size() + brother->get_size() >= 2 * node->get_min_size()) {
        redistribute(
            brother,
            node,
            father,
            father->lower_bound(node->keys)
        );
        // 资源处理
        buffer_pool_manager_->unpin_page(brother->get_page_id(), true);
        delete brother;
        buffer_pool_manager_->unpin_page(father->get_page_id(), true);
        delete father;
        return false;
    }
    // 合并
    else {
        bool delete_pa = coalesce (
            &brother,
            &node,
            &father,
            father->lower_bound(node->keys),
            transaction,
            nullptr
        );
         // 资源处理
        buffer_pool_manager_->unpin_page(brother->get_page_id(), true);
        delete brother;
        buffer_pool_manager_->unpin_page(father->get_page_id(), true);
        delete father;
        if (delete_pa) {
            // transaction->append_index_deleted_page(father->page);
        }
        return true;
    }
    
}

/**
 * @brief 用于当根结点被删除了一个键值对之后的处理
 * @param old_root_node 原根节点
 * @return bool 根结点是否需要被删除
 * @note size of root page can be less than min size and this method is only called within coalesce_or_redistribute()
 */
bool IxIndexHandle::adjust_root(IxNodeHandle *old_root_node) {
    // Todo:
    // 1. 如果old_root_node是内部结点，并且大小为1，则直接把它的孩子更新成新的根结点
    // 2. 如果old_root_node是叶结点，且大小为0，则直接更新root page
    // 3. 除了上述两种情况，不需要进行操作
    if (!old_root_node->is_leaf_page() && old_root_node->get_size() == 1) {
        IxNodeHandle *child = fetch_node(old_root_node->value_at(0));
        page_id_t new_id = old_root_node->value_at(0);
        child->page_hdr->parent = INVALID_PAGE_ID;

        // 处理资源
        buffer_pool_manager_->unpin_page(child->get_page_id(), true);

        //更新
        release_node_handle(*old_root_node);
        update_root_page_no(new_id);
    }
    else if (old_root_node->is_leaf_page() && old_root_node->get_size() == 0) {
        // fix 删除的话，下次插入插哪呢？
        // release_node_handle(*old_root_node);
        // file_hdr_->root_page_ = INVALID_PAGE_ID;
        // return true;
    }
    return false;
}

/**
 * @brief 重新分配node和兄弟结点neighbor_node的键值对
 * Redistribute key & value pairs from one page to its sibling page. If index == 0, move sibling page's first key
 * & value pair into end of input "node", otherwise move sibling page's last key & value pair into head of input "node".
 *
 * @param neighbor_node sibling page of input "node"
 * @param node input from method coalesceOrRedistribute()
 * @param parent the parent of "node" and "neighbor_node"
 * @param index node在parent中的rid_idx
 * @note node是之前刚被删除过一个key的结点
 * index=0，则neighbor是node后继结点，表示：node(left)      neighbor(right)
 * index>0，则neighbor是node前驱结点，表示：neighbor(left)  node(right)
 * 注意更新parent结点的相关kv对
 */
void IxIndexHandle::redistribute(IxNodeHandle *neighbor_node, IxNodeHandle *node, IxNodeHandle *parent, int index) {
    // Todo:
    // 1. 通过index判断neighbor_node是否为node的前驱结点
    // 2. 从neighbor_node中移动一个键值对到node结点中
    // 3. 更新父节点中的相关信息，并且修改移动键值对对应孩字结点的父结点信息（maintain_child函数）
    // 注意：neighbor_node的位置不同，需要移动的键值对不同，需要分类讨论
    // neighbor是node后继结点
    if (index == 0) {
        // neighbor是后继节点
        node->insert_pair(node->get_size(), neighbor_node->keys, neighbor_node->rids[0]);
        /* 上方key的维护 */
        neighbor_node->erase_pair(0);
        maintain_child(node, node->get_size() - 1);
        maintain_parent(neighbor_node);
    }
    // neighbor是node前驱结点
    else {
        node->insert_pair(
            0,
            neighbor_node->get_key(neighbor_node->get_size() - 1),
            *neighbor_node->get_rid(neighbor_node->get_size() - 1)
        );
        neighbor_node->erase_pair(neighbor_node->get_size() - 1);
        maintain_parent(node);
        maintain_child(node, 0);
    }

}

/**
 * @brief 合并(Coalesce)函数是将node和其直接前驱进行合并，也就是和它左边的neighbor_node进行合并；
 * 假设node一定在右边。如果上层传入的index=0，说明node在左边，那么交换node和neighbor_node，保证node在右边；合并到左结点，实际上就是删除了右结点；
 * Move all the key & value pairs from one page to its sibling page, and notify buffer pool manager to delete this page.
 * Parent page must be adjusted to take info of deletion into account. Remember to deal with coalesce or redistribute
 * recursively if necessary.
 *
 * @param neighbor_node sibling page of input "node" (neighbor_node是node的前结点)
 * @param node input from method coalesceOrRedistribute() (node结点是需要被删除的)
 * @param parent parent page of input "node"
 * @param index node在parent中的rid_idx
 * @return true means parent node should be deleted, false means no deletion happend
 * @note Assume that *neighbor_node is the left sibling of *node (neighbor -> node)
 */
bool IxIndexHandle::coalesce(IxNodeHandle **neighbor_node, IxNodeHandle **node, IxNodeHandle **parent, int index,
                             Transaction *transaction, bool *root_is_latched) {
    // Todo:
    // 1. 用index判断neighbor_node是否为node的前驱结点，若不是则交换两个结点，让neighbor_node作为左结点，node作为右结点
    // 2. 把node结点的键值对移动到neighbor_node中，并更新node结点孩子结点的父节点信息（调用maintain_child函数）
    // 3. 释放和删除node结点，并删除parent中node结点的信息，返回parent是否需要被删除

    // 提示：如果是叶子结点且为最右叶子结点，需要更新file_hdr_.last_leaf
    if (index == 0) {
        IxNodeHandle **tem = node;
        node = neighbor_node;
        neighbor_node = tem;
    }
    // 将所有node键值对移动到neighbor_node中
    char *key = (*node)->keys;
    Rid *rid = (*node)->rids;
    int pre_size = (*neighbor_node)->get_size();
    (*neighbor_node)->insert_pairs(
        (*neighbor_node)->get_size(),
        key,
        rid,
        (*node)->get_size()
    );
    for (size_t i = pre_size; i < (*node)->get_size(); ++i) {
        maintain_child(*neighbor_node, i + pre_size);
    }

    // 更新叶节点信息
    if ((*node)->is_leaf_page()) {
        erase_leaf(*node);
        if (file_hdr_->last_leaf_ == (*node)->get_page_no()) {
            file_hdr_->last_leaf_ = (*neighbor_node)->get_page_no();
        }
    }

    // 删除释放
    release_node_handle(**node);
    int pos = (*parent)->lower_bound((*node)->keys);
    // 由于是右边的，不会影响父节点首值更新
    (*parent)->erase_pair(pos);
    
    return coalesce_or_redistribute(*parent);
}

/**
 * @brief 这里把iid转换成了rid，即iid的slot_no作为node的rid_idx(key_idx)
 * node其实就是把slot_no作为键值对数组的下标
 * 换而言之，每个iid对应的索引槽存了一对(key,rid)，指向了(要建立索引的属性首地址,插入/删除记录的位置)
 *
 * @param iid
 * @return Rid
 * @note iid和rid存的不是一个东西，rid是上层传过来的记录位置，iid是索引内部生成的索引槽位置
 */
Rid IxIndexHandle::get_rid(const Iid &iid) const {
    IxNodeHandle *node = fetch_node(iid.page_no);
    if (iid.slot_no >= node->get_size()) {
        throw IndexEntryNotFoundError();
    }
    buffer_pool_manager_->unpin_page(node->get_page_id(), false);  // unpin it!
    return *node->get_rid(iid.slot_no);
}

/**
 * @brief FindLeafPage + lower_bound
 *
 * @param key
 * @return Iid
 * @note 上层传入的key本来是int类型，通过(const char *)&key进行了转换
 * 可用*(int *)key转换回去
 */
Iid IxIndexHandle::lower_bound(const char *key) {
    std::pair<IxNodeHandle *, bool> entry = find_leaf_page(key, Operation::FIND, nullptr);
     if (!entry.first) {
        return Iid{-1, -1};
    }

    IxNodeHandle *leaf_node = entry.first;
    int key_idx = leaf_node->lower_bound(key);
    Iid iid = {.page_no = leaf_node->get_page_no(), .slot_no = key_idx};

    // 处理
    buffer_pool_manager_->unpin_page(leaf_node->get_page_id(), false);
    delete leaf_node;
    return iid;
}

/**
 * @brief FindLeafPage + upper_bound
 *
 * @param key
 * @return Iid
 */
Iid IxIndexHandle::upper_bound(const char *key) {
    std::pair<IxNodeHandle *, bool> entry = find_leaf_page(key, Operation::FIND, nullptr);
     if (!entry.first) {
        return Iid{-1, -1};
    }

    IxNodeHandle *leaf_node = entry.first;
    Iid iid;
    int key_idx = leaf_node->upper_bound(key);
    // iid最后一个不能空了记得s
    if (key_idx == leaf_node->get_size()) {
        iid = leaf_end();
    } else {
        iid = {.page_no = leaf_node->get_page_no(), .slot_no = key_idx};
    }


    // 处理
    buffer_pool_manager_->unpin_page(leaf_node->get_page_id(), false);
    delete leaf_node;
    return iid;
}

/**
 * @brief 指向最后一个叶子的最后一个结点的后一个
 * 用处在于可以作为IxScan的最后一个
 *
 * @return Iid
 */
Iid IxIndexHandle::leaf_end() const {
    IxNodeHandle *node = fetch_node(file_hdr_->last_leaf_);
    Iid iid = {.page_no = file_hdr_->last_leaf_, .slot_no = node->get_size()};
    buffer_pool_manager_->unpin_page(node->get_page_id(), false);  // unpin it!
    return iid;
}

/**
 * @brief 指向第一个叶子的第一个结点
 * 用处在于可以作为IxScan的第一个
 *
 * @return Iid
 */
Iid IxIndexHandle::leaf_begin() const {
    Iid iid = {.page_no = file_hdr_->first_leaf_, .slot_no = 0};
    return iid;
}

/**
 * @brief 获取一个指定结点
 *
 * @param page_no
 * @return IxNodeHandle*
 * @note pin the page, remember to unpin it outside!
 */
IxNodeHandle *IxIndexHandle::fetch_node(int page_no) const {
    Page *page = buffer_pool_manager_->fetch_page(PageId{fd_, page_no});
    IxNodeHandle *node = new IxNodeHandle(file_hdr_, page);
    
    return node;
}

/**
 * @brief 创建一个新结点
 *
 * @return IxNodeHandle*
 * @note pin the page, remember to unpin it outside!指针是new的
 * 注意：对于Index的处理是，删除某个页面后，认为该被删除的页面是free_page
 * 而first_free_page实际上就是最新被删除的页面，初始为IX_NO_PAGE
 * 在最开始插入时，一直是create node，那么first_page_no一直没变，一直是IX_NO_PAGE
 * 与Record的处理不同，Record将未插入满的记录页认为是free_page
 */
IxNodeHandle *IxIndexHandle::create_node() {
    IxNodeHandle *node;
    file_hdr_->num_pages_++;

    PageId new_page_id = {.fd = fd_, .page_no = INVALID_PAGE_ID};
    // 从3开始分配page_no，第一次分配之后，new_page_id.page_no=3，file_hdr_.num_pages=4
    Page *page = buffer_pool_manager_->new_page(&new_page_id);
    node = new IxNodeHandle(file_hdr_, page);
    node->page_hdr->num_key = 0;
    return node;
}

/**
 * @brief 从node开始更新其父节点的第一个key，一直向上更新直到根节点
 *
 * @param node
 */
void IxIndexHandle::maintain_parent(IxNodeHandle *node) {
    IxNodeHandle *curr = node;
    while (curr->get_parent_page_no() != IX_NO_PAGE) {
        // Load its parent
        IxNodeHandle *parent = fetch_node(curr->get_parent_page_no());
        int rank = parent->find_child(curr);
        char *parent_key = parent->get_key(rank);
        char *child_first_key = curr->get_key(0);
        // 更新父节点，判断节点第一个值和父节点是否相同
        if (memcmp(parent_key, child_first_key, file_hdr_->col_tot_len_) == 0) {
            assert(buffer_pool_manager_->unpin_page(parent->get_page_id(), true));
            break;
        }
        memcpy(parent_key, child_first_key, file_hdr_->col_tot_len_);  // 修改了parent node
        curr = parent;

        assert(buffer_pool_manager_->unpin_page(parent->get_page_id(), true));
    }
}

/**
 * @brief 要删除leaf之前调用此函数，更新leaf前驱结点的next指针和后继结点的prev指针
 *
 * @param leaf 要删除的leaf
 */
void IxIndexHandle::erase_leaf(IxNodeHandle *leaf) {
    assert(leaf->is_leaf_page());

    IxNodeHandle *prev = fetch_node(leaf->get_prev_leaf());
    prev->set_next_leaf(leaf->get_next_leaf());
    buffer_pool_manager_->unpin_page(prev->get_page_id(), true);

    IxNodeHandle *next = fetch_node(leaf->get_next_leaf());
    next->set_prev_leaf(leaf->get_prev_leaf());  // 注意此处是SetPrevLeaf()
    buffer_pool_manager_->unpin_page(next->get_page_id(), true);
}

/**
 * @brief 删除node时，更新file_hdr_.num_pages
 *
 * @param node
 */
void IxIndexHandle::release_node_handle(IxNodeHandle &node) {
    file_hdr_->num_pages_--;
}

/**
 * @brief 将node的第child_idx个孩子结点的父节点置为node
 */
void IxIndexHandle::maintain_child(IxNodeHandle *node, int child_idx) {
    if (!node->is_leaf_page()) {
        //  Current node is inner node, load its child and set its parent to current node
        int child_page_no = node->value_at(child_idx);
        IxNodeHandle *child = fetch_node(child_page_no);
        child->set_parent_page_no(node->get_page_no());
        buffer_pool_manager_->unpin_page(child->get_page_id(), true);
    }
}

/**
 * @brief 将node在index处进行更新
 * @note pos是在父节点中的位置而不是子节点！
 */
void IxIndexHandle::update_node(IxNodeHandle *parent_node, IxNodeHandle *node, const char* key, Transaction *transaction) {
    // 找到该节点index

    // 判断是否是非root且其父节点是否要更新，如果要更新，更新父节点
    int pos = parent_node->lower_bound(node->keys);
    if (pos == 0 && !node->is_root_page()) {
        IxNodeHandle *parent = fetch_node(parent_node->get_parent_page_no());
        char *old_key = parent_node->keys;
        // 先更新父节点，再更新子节点
        update_node(
            parent,
            parent_node,
            key,
            transaction
        );
        // 更新parent_node节点
        parent_node->set_key(pos, node->keys);
        // 无需修改rid
        //parent_node->set_rid(pos, {node->get_page_no(), -1});

        buffer_pool_manager_->unpin_page(parent->get_page_id(), true);
        delete parent;
    }
    else {
        // 更新parent_node节点
        parent_node->set_key(pos, node->keys);
        //parent_node->set_rid(pos, {node->get_page_no(), -1});
    }
    
}