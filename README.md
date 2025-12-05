# MsquicStorage - 高性能信令服务器框架

## 项目概述

MsquicStorage 是一个基于 C++ 开发的高性能、可扩展的信令服务器框架，支持 WebTransport 和裸 MsQuic 两种实现方式。该框架专为实时通信场景设计，特别适用于云游戏、实时音视频、物联网等需要低延迟、高并发的应用场景。

## 架构特点

### 核心架构
- **双协议支持**: 同时支持 WebTransport 和原生 MsQuic 协议
- **协程异步**: 基于 Boost.Asio 的协程异步处理模型
- **无锁设计**: 广泛使用无锁队列和并发数据结构
- **模块化**: 高度模块化的组件设计，易于扩展和维护

### 性能特性
- 毫秒级延迟响应
- 支持数万并发连接
- 自动负载均衡
- 内存池和连接池优化

## 核心模块详解

### 1. 异步I/O引擎 (AsioProactors)

基于 Boost.Asio 的高性能线程池，提供可靠的异步I/O基础设施：

```cpp
namespace hope::iocp {
    class AsioProactors {
    public:
        // 获取IO上下文对（索引和引用）
        std::pair<int, boost::asio::io_context&> getIoCompletePorts();
        
        // 优雅停止所有线程
        void stop();
    };
}
```

**特性**:
- 自动负载均衡的IO上下文分配
- 工作守护机制防止线程空闲退出
- 线程安全的资源管理

### 2. 配置管理系统

统一的配置管理接口，支持多种格式：

```cpp
class ConfigManager {
public:
    enum class Format { Ini, Json, Xml };
    
    // 单例访问
    static ConfigManager& Instance();
    
    // 配置读写
    template<typename T>
    std::optional<T> Get(const std::string& key, const T& defaultValue = T{}) const;
    
    void Set(const std::string& key, const T& value);
};
```

**支持功能**:
- 热重载配置
- 类型安全的配置访问
- 多格式支持（INI/JSON/XML）

### 3. 数据库管理层

#### 数据库连接池 (MsquicMysqlManagerPools)

```cpp
namespace hope::mysql {
    class MsquicMysqlManagerPools {
    public:
        // 获取普通连接（读操作）
        std::shared_ptr<MsquicMysqlManager> getMysqlManager();
        
        // 获取事务连接（写操作）
        std::shared_ptr<MsquicMysqlManager> getTransactionMysqlManager();
        
        // 归还连接
        void returnTransactionMysqlManager(std::shared_ptr<MsquicMysqlManager> mysqlManager);
    };
}
```

#### 实体模型
- **GameServers**: 游戏服务器实例管理
- **GameProcesses**: 游戏进程实例管理

### 4. 并发数据结构

#### 线程安全哈希映射 (MsquicHashMap)
```cpp
namespace hope::utils {
    template<class Key, class Value>
    class MsquicHashMap {
    public:
        // 读写分离的锁机制
        void insert(const Key& key, const Value& value);  // 写锁
        std::optional<Value> get(const Key& key) const;  // 读锁
        
        // 快照功能（避免迭代器失效）
        absl::flat_hash_map<Key, Value> snapshot() const;
    };
}
```

#### 线程安全哈希集合 (MsquicHashSet)
类似的读写锁机制，支持安全的集合操作。

### 5. WebTransport 服务器实现

#### 服务器主类 (MsquicWebTransportServer)
```cpp
namespace hope::quic {
    class MsquicWebTransportServer {
    public:
        // 初始化服务器
        bool initialize();
        
        // 提交异步任务到指定通道
        void postTaskAsync(size_t channelIndex, 
                          std::function<boost::asio::awaitable<void>(MsquicManager*)> asyncHandle);
    };
}
```

#### 连接管理器 (MsquicManager)
```cpp
class MsquicManager {
private:
    // 连接映射表
    hope::utils::MsquicHashMap<std::string, MsquicWebTransportSocket*> msquicSocketMap;
    hope::utils::MsquicHashMap<std::string, MsquicWebTransportSocket*> msquicCloudProcessSocketMap;
    
    // 路由缓存（LRU优化）
    tbb::concurrent_lru_cache<std::string, int> localRouteCache;
};
```

#### 客户端连接 (MsquicWebTransportSocket)
```cpp
class MsquicWebTransportSocket {
public:
    // 消息发送
    void writeAsync(unsigned char* data, size_t size);
    
    // 连接状态管理
    void setRegistered(bool registered);
    bool getRegistered() const;
    
    // 远程地址获取
    std::string getRemoteAddress();
};
```

### 6. 业务逻辑处理器 (MsquicLogicSystem)

核心的业务逻辑分发和处理系统：

```cpp
namespace hope::handle {
    class MsquicLogicSystem {
    public:
        // 注册处理器
        void initHandlers();
        
        // 异步任务提交
        void postTaskAsync(std::shared_ptr<hope::quic::MsquicData> data);
    };
}
```

**支持的请求类型**:
- `0`: 用户注册 (REGISTER)
- `1`: 普通请求 (REQUEST) 
- `5`: 云服务器注册 (CLOUD_GAME_SERVERS_REGISTER)
- `7`: 云进程登录 (CLOUD_PROCESS_LOGIN)
- `13`: 获取游戏进程ID (USER_GET_GAMES_PROCESS_ID)

## 裸 MsQuic 实现版本

除了 WebTransport 版本，框架还提供原生 MsQuic 实现，架构基本一致，主要区别：

### 协议层差异
- **WebTransport版本**: 基于 WTF (WebTransport Framework)
- **裸MsQuic版本**: 直接使用 MsQuic API，更底层，性能更高

### 共同架构
- 相同的业务逻辑层 (MsquicLogicSystem)
- 统一的数据模型 (GameServers/GameProcesses)
- 一致的配置管理和日志系统

## 快速开始

### 环境要求
- Windows/Linux (MSVC/GCC)
- Boost 1.70+
- MsQuic 库
- MySQL 客户端库

### 编译运行
```bash
# 克隆项目
git clone https://github.com/your-repo/hope-signal-server.git

# 编译
mkdir build && cd build
cmake .. -DUSE_WEBTRANSPORT=ON  # 或 -DUSE_MSQUIC=ON
make -j4

# 运行
./hope_signal_server
```

### 配置示例
```ini
[MquicWebTransportServer]
port = 8088
certificateFile = cert.pem
privateKeyFile = key.pem

[Mysql]
ip = 127.0.0.1
port = 3306
username = root
password = password
database = hope_server
```

## 性能指标

- **连接数**: 支持 10,000+ 并发连接
- **延迟**: 平均响应时间 < 10ms
- **吞吐量**: 10,000+ QPS
- **内存**: 连接内存占用 < 50KB/连接

## 应用场景

### 云游戏平台
```cpp
// 游戏进程动态分配
auto processId = cloudProcessHashMap[gameType].pop();
if (processId) {
    // 分配空闲游戏进程
    forwardGameRequest(userId, processId.value());
}
```

### 实时通信服务
```cpp
// 消息路由转发
auto targetSocket = msquicSocketMap.find(targetId);
if (targetSocket != msquicSocketMap.end()) {
    targetSocket->second->writeAsync(messageData, messageSize);
}
```

### 微服务协调
支持多节点分布式部署，自动服务发现和负载均衡。

## 扩展开发

### 添加新的处理器
```cpp
// 在 MsquicLogicSystem::initHandlers() 中注册
msquicHandlersauto data, auto mysqlManager -> boost::asio::awaitable<void> {
    // 实现新的业务逻辑
    co_await handleCustomRequest(data, mysqlManager);
}};
```

### 自定义数据模型
继承现有的实体类或创建新的数据库表映射。

## 许可证

MIT License

## 贡献指南

欢迎提交 Issue 和 Pull Request 来帮助改进这个项目。

## 支持与联系

- 项目主页: [GitHub Repository]
- 文档: [项目Wiki]
- 邮箱: support@hope-signal.com

---

*MsquicStorage - 构建高性能实时服务的理想选择*
