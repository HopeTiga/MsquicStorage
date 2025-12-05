#pragma once
#include <absl/container/flat_hash_map.h>
#include <absl/synchronization/mutex.h>
#include <optional>
#include <string>

namespace hope {
    namespace utils {
        template<class Key, class Value>
        class MsquicHashMap {
        public:

            MsquicHashMap() {
                flatHashMap.reserve(10240);
            }

            ~MsquicHashMap() = default;

            // ==== 写操作（使用写锁）====

            // 插入元素
            void insert(const Key& key, const Value& value) {
                absl::WriterMutexLock lock(&mutex);  // 改为写锁
                flatHashMap.insert({ key, value });
            }

            // 删除元素
            void erase(const Key& key) {
                absl::WriterMutexLock lock(&mutex);  // 改为写锁
                flatHashMap.erase(key);
            }

            // 通过迭代器删除元素
            void erase(typename absl::flat_hash_map<Key, Value>::iterator it) {
                absl::WriterMutexLock lock(&mutex);  // 改为写锁
                flatHashMap.erase(it);
            }

            // 清空所有元素
            void clear() {
                absl::WriterMutexLock lock(&mutex);  // 改为写锁
                flatHashMap.clear();
            }

            // 直接访问操作符（注意：如果key不存在会插入默认值）
            Value& operator[](const Key& key) {
                absl::WriterMutexLock lock(&mutex);  // 改为写锁
                return flatHashMap[key];
            }

            // ==== 读操作（使用读锁）====

            // 查找元素（返回迭代器）
            typename absl::flat_hash_map<Key, Value>::iterator find(const Key& key) {
                absl::ReaderMutexLock lock(&mutex);  // 改为读锁
                return flatHashMap.find(key);
            }

            // 查找元素（const版本）
            typename absl::flat_hash_map<Key, Value>::const_iterator find(const Key& key) const {
                absl::ReaderMutexLock lock(&mutex);  // 改为读锁
                return flatHashMap.find(key);
            }

            // 检查元素是否存在
            bool contains(const Key& key) const {
                absl::ReaderMutexLock lock(&mutex);  // 改为读锁
                return flatHashMap.contains(key);
            }

            // 安全获取值
            std::optional<Value> get(const Key& key) const {
                absl::ReaderMutexLock lock(&mutex);  // 改为读锁
                auto it = flatHashMap.find(key);
                if (it != flatHashMap.end()) {
                    return it->second;
                }
                return std::nullopt;
            }

            // 获取元素数量
            size_t size() const {
                absl::ReaderMutexLock lock(&mutex);  // 改为读锁
                return flatHashMap.size();
            }

            // 检查是否为空
            bool empty() const {
                absl::ReaderMutexLock lock(&mutex);  // 改为读锁
                return flatHashMap.empty();
            }

            // ==== 迭代器方法（注意：返回迭代器后锁就释放了！）====

            // 迭代器相关方法
            typename absl::flat_hash_map<Key, Value>::iterator begin() {
                absl::ReaderMutexLock lock(&mutex);  // 改为读锁
                return flatHashMap.begin();
            }

            typename absl::flat_hash_map<Key, Value>::iterator end() {
                absl::ReaderMutexLock lock(&mutex);  // 改为读锁
                return flatHashMap.end();
            }

            typename absl::flat_hash_map<Key, Value>::const_iterator begin() const {
                absl::ReaderMutexLock lock(&mutex);  // 改为读锁
                return flatHashMap.begin();
            }

            typename absl::flat_hash_map<Key, Value>::const_iterator end() const {
                absl::ReaderMutexLock lock(&mutex);  // 改为读锁
                return flatHashMap.end();
            }

            typename absl::flat_hash_map<Key, Value>::const_iterator cbegin() const {
                absl::ReaderMutexLock lock(&mutex);  // 改为读锁
                return flatHashMap.cbegin();
            }

            typename absl::flat_hash_map<Key, Value>::const_iterator cend() const {
                absl::ReaderMutexLock lock(&mutex);  // 改为读锁
                return flatHashMap.cend();
            }

            // ==== 新增：获取快照（推荐使用）====

            // 获取快照（复制一份数据，避免迭代器失效问题）
            absl::flat_hash_map<Key, Value> snapshot() const {
                absl::ReaderMutexLock lock(&mutex);  // 读锁
                return flatHashMap;  // 返回副本
            }

        private:
            absl::flat_hash_map<Key, Value> flatHashMap;
            mutable absl::Mutex mutex;  // absl::Mutex 支持读写锁语义
        };
    }
}