#pragma once
#include <absl/container/flat_hash_set.h>
#include <absl/synchronization/mutex.h>
#include <optional>
#include <string>
#include <vector>

namespace hope {
    namespace utils {
        template<class Key>
        class MsquicHashSet {
        public:
            MsquicHashSet() {
                flatHashSet.reserve(10240);
            }

            ~MsquicHashSet() = default;

            // 拷贝构造函数
            MsquicHashSet(const MsquicHashSet& other) {
                absl::ReaderMutexLock lock_other(&other.mutex);  // 读锁读取源
                absl::WriterMutexLock lock(&mutex);  // 写锁写入当前
                flatHashSet = other.flatHashSet;
            }

            // 移动构造函数
            MsquicHashSet(MsquicHashSet&& other) noexcept {
                absl::WriterMutexLock lock_other(&other.mutex);  // 写锁（移动需要独占）
                absl::WriterMutexLock lock(&mutex);  // 写锁
                flatHashSet = std::move(other.flatHashSet);
            }

            // 拷贝赋值运算符
            MsquicHashSet& operator=(const MsquicHashSet& other) {
                if (this != &other) {
                    absl::ReaderMutexLock lock_other(&other.mutex);  // 读锁读取源
                    absl::WriterMutexLock lock(&mutex);  // 写锁写入当前
                    flatHashSet = other.flatHashSet;
                }
                return *this;
            }

            // 移动赋值运算符
            MsquicHashSet& operator=(MsquicHashSet&& other) noexcept {
                if (this != &other) {
                    absl::WriterMutexLock lock_other(&other.mutex);  // 写锁（移动需要独占）
                    absl::WriterMutexLock lock(&mutex);  // 写锁
                    flatHashSet = std::move(other.flatHashSet);
                }
                return *this;
            }

            // ==== 写操作（使用写锁）====

            // 插入元素
            void insert(const Key& key) {
                absl::WriterMutexLock lock(&mutex);  // 写锁
                flatHashSet.insert(key);
            }

            // 删除元素
            void erase(const Key& key) {
                absl::WriterMutexLock lock(&mutex);  // 写锁
                flatHashSet.erase(key);
            }

            // 通过迭代器删除元素
            void erase(typename absl::flat_hash_set<Key>::iterator it) {
                absl::WriterMutexLock lock(&mutex);  // 写锁
                flatHashSet.erase(it);
            }

            // 清空所有元素
            void clear() {
                absl::WriterMutexLock lock(&mutex);  // 写锁
                flatHashSet.clear();
            }

            // 批量插入元素
            template<typename InputIterator>
            void insertRange(InputIterator first, InputIterator last) {
                absl::WriterMutexLock lock(&mutex);  // 写锁
                flatHashSet.insert(first, last);
            }

            // 新增：获取并删除一个元素（从集合中任意位置）
            std::optional<Key> pop() {
                absl::WriterMutexLock lock(&mutex);  // 写锁
                if (flatHashSet.empty()) {
                    return std::nullopt;
                }
                auto it = flatHashSet.begin();
                Key value = *it;
                flatHashSet.erase(it);
                return value;
            }

            // 新增：获取并删除指定元素
            std::optional<Key> take(const Key& key) {
                absl::WriterMutexLock lock(&mutex);  // 写锁
                auto it = flatHashSet.find(key);
                if (it != flatHashSet.end()) {
                    Key value = *it;
                    flatHashSet.erase(it);
                    return value;
                }
                return std::nullopt;
            }

            // ==== 读操作（使用读锁）====

            // 查找元素（返回迭代器）- 注意：返回迭代器后锁就释放了！
            typename absl::flat_hash_set<Key>::iterator find(const Key& key) {
                absl::ReaderMutexLock lock(&mutex);  // 读锁
                return flatHashSet.find(key);
            }

            // 查找元素（const版本）
            typename absl::flat_hash_set<Key>::const_iterator find(const Key& key) const {
                absl::ReaderMutexLock lock(&mutex);  // 读锁
                return flatHashSet.find(key);
            }

            // 检查元素是否存在
            bool contains(const Key& key) const {
                absl::ReaderMutexLock lock(&mutex);  // 读锁
                return flatHashSet.contains(key);
            }

            // 获取元素数量
            size_t size() const {
                absl::ReaderMutexLock lock(&mutex);  // 读锁
                return flatHashSet.size();
            }

            // 检查是否为空
            bool empty() const {
                absl::ReaderMutexLock lock(&mutex);  // 读锁
                return flatHashSet.empty();
            }

            // 获取所有元素的副本
            std::vector<Key> toVector() const {
                absl::ReaderMutexLock lock(&mutex);  // 读锁
                return std::vector<Key>(flatHashSet.begin(), flatHashSet.end());
            }

            // ==== 迭代器方法（危险：返回迭代器后锁就释放了！）====
            // 警告：这些方法在使用时需要格外小心！

            // 获取快照（推荐使用这个而不是直接迭代器）
            absl::flat_hash_set<Key> snapshot() const {
                absl::ReaderMutexLock lock(&mutex);  // 读锁
                return flatHashSet;  // 返回副本
            }

            // 迭代器相关方法（不推荐使用，仅保留兼容性）
            typename absl::flat_hash_set<Key>::iterator begin() {
                absl::ReaderMutexLock lock(&mutex);  // 读锁
                return flatHashSet.begin();
            }

            typename absl::flat_hash_set<Key>::iterator end() {
                absl::ReaderMutexLock lock(&mutex);  // 读锁
                return flatHashSet.end();
            }

            typename absl::flat_hash_set<Key>::const_iterator begin() const {
                absl::ReaderMutexLock lock(&mutex);  // 读锁
                return flatHashSet.begin();
            }

            typename absl::flat_hash_set<Key>::const_iterator end() const {
                absl::ReaderMutexLock lock(&mutex);  // 读锁
                return flatHashSet.end();
            }

            typename absl::flat_hash_set<Key>::const_iterator cbegin() const {
                absl::ReaderMutexLock lock(&mutex);  // 读锁
                return flatHashSet.cbegin();
            }

            typename absl::flat_hash_set<Key>::const_iterator cend() const {
                absl::ReaderMutexLock lock(&mutex);  // 读锁
                return flatHashSet.cend();
            }

        private:
            absl::flat_hash_set<Key> flatHashSet;
            mutable absl::Mutex mutex;  // absl::Mutex 支持读写锁语义
        };

    }  // namespace utils
}  // namespace hope