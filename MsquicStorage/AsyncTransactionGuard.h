#pragma once
#include <boost/mysql/any_connection.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <memory>

namespace hope::mysql {

    class AsyncTransactionGuard {
    public:
        AsyncTransactionGuard(const AsyncTransactionGuard&) = delete;
        AsyncTransactionGuard& operator=(const AsyncTransactionGuard&) = delete;
        AsyncTransactionGuard(AsyncTransactionGuard&&) noexcept = default;
        AsyncTransactionGuard& operator=(AsyncTransactionGuard&&) = default;

        static boost::asio::awaitable<AsyncTransactionGuard>
            create(std::shared_ptr<boost::mysql::any_connection> conn) {

            boost::mysql::results r;

            co_await conn->async_execute("START TRANSACTION", r,
                boost::asio::use_awaitable);

            co_return AsyncTransactionGuard(std::move(conn));
        }

        boost::asio::awaitable<void> commit() {

            if (committed) co_return;

            boost::mysql::results r;

            co_await conn->async_execute("COMMIT", r,
                boost::asio::use_awaitable);

            committed = true;
        }

        /**
      * @brief 显式异步回滚（一般不需要手动调用，析构会回滚）
      * @throw boost::mysql::error_with_diagnostics 回滚失败时抛出
      */
        boost::asio::awaitable<void> asyncRollback() {

            if (committed) co_return;

            boost::mysql::results r;

            co_await conn->async_execute("ROLLBACK", r,
                boost::asio::use_awaitable);

            committed = true; // 标记为已处理
        }

        void rollback() {

            if (committed) return;

            boost::mysql::results r;

            conn->execute("ROLLBACK", r);

            committed = true; // 标记为已处理
        }

        ~AsyncTransactionGuard() {

        }

        explicit AsyncTransactionGuard(std::shared_ptr<boost::mysql::any_connection> c)
            : conn(std::move(c)), committed(false) {
        }

    private:
        

        std::shared_ptr<boost::mysql::any_connection> conn;
        bool committed;
    };

} // namespace hope::core