#pragma once

#include <boost/asio.hpp>
#include <execution>
#include <optional>
#include <type_traits>
namespace Util::Async {
template <typename Executor, typename Func>
boost::asio::awaitable<std::invoke_result_t<Func>> AsyncExecute(Executor&& executor, Func&& func) {
    using ReturnType = std::invoke_result_t<Func>;

    auto mainContext = co_await boost::asio::this_coro::executor;
    co_await boost::asio::post(executor, boost::asio::use_awaitable);

    std::exception_ptr e_ptr;

    if constexpr (std::is_void_v<ReturnType>) {
        try {
            func();
        } catch (...) {
            e_ptr = std::current_exception();
        }

        co_await boost::asio::post(mainContext, boost::asio::use_awaitable);
        if (e_ptr) std::rethrow_exception(e_ptr);
    } else {
        std::optional<ReturnType> opt;
        try {
            opt = func();
        } catch (...) {
            e_ptr = std::current_exception();
        }

        co_await boost::asio::post(mainContext, boost::asio::use_awaitable);
        if (e_ptr) std::rethrow_exception(e_ptr);
        co_return std::move(*opt);
    }
}
}
