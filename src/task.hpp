/*
purple-youtube
Copyright (C) 2026 Cole Blakley

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#pragma once

#include <cassert>
#include <concepts>
#include <exception>
#include <type_traits>
#include <coroutine>
#include <expected>
#include <peel/GLib/Error.h>
#include <peel/UniquePtr.h>
#include "error_wrapper.hpp"

namespace peel {
    namespace GObject {
        class Object;
    }
    namespace Gio {
        class AsyncResult;
    }
}

namespace gio = peel::Gio;
namespace gobject = peel::GObject;

struct awaiter_base {
    constexpr bool await_ready() const noexcept { return false; }
    constexpr void await_resume() const noexcept {}
};

struct promise_type_base {
    constexpr std::suspend_always initial_suspend() const noexcept { return {}; }
    void unhandled_exception() { std::terminate(); }

    // If no caller is set, fall through to prior stack frame
    std::coroutine_handle<> caller = std::noop_coroutine();
};

class AsyncResult {
public:
    constexpr bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept { handle = h; }
    gio::AsyncResult* await_resume() { return result; }

    auto callback()
    {
        return [this](gobject::Object*, gio::AsyncResult* res) {
            result = res;
            assert(handle);
            handle.resume();
        };
    }
private:
    std::coroutine_handle<> handle;
    gio::AsyncResult* result = nullptr;
};

template<typename T>
class [[nodiscard]] Task : public awaiter_base {
public:
    using ResultT = std::conditional_t<std::same_as<T, void>,
        ErrorPtr,
        std::expected<T, ErrorPtr>>;

    struct promise_type : public promise_type_base {
        struct FinalAwaitable : public awaiter_base {
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> handle) const noexcept
            {
                auto caller = handle.promise().caller;
                handle.destroy();
                return caller;
            }
        };

        FinalAwaitable final_suspend() noexcept { return {}; }

        Task get_return_object()
        {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        void return_value(ResultT&& value)
        {
            // task might be null if this is an outer task that has ".start()" called on it
            // and the Task object is then discarded
            if(task) {
                task->result = std::move(value);
            }
        }

        Task* task = nullptr;
    };

    Task(const Task&) = delete;
    Task(Task&& other) = delete;
    Task& operator=(const Task&) = delete;
    Task& operator=(Task&& other) = delete;

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept
    {
        handle.promise().caller = caller;
        handle.promise().task = this;
        return handle;
    }

    auto await_resume() noexcept { return std::move(result); }

    void start() { handle.resume(); }
private:
    explicit
    Task(std::coroutine_handle<promise_type> handle)
        : handle(handle) {}

    std::coroutine_handle<promise_type> handle;
    ResultT result;
};
