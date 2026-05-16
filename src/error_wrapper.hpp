#pragma once

#include <peel/GLib/Error.h>
#include <peel/UniquePtr.h>

namespace glib = peel::GLib;

/* Simple wrapper to make copyable version of peel::UniquePtr<glib::Error> */
class ErrorPtr {
public:
    ErrorPtr() {}
    ErrorPtr(peel::UniquePtr<glib::Error>&& error)
        : error(std::move(error))
    {}
    template<typename ...Args>
    ErrorPtr(glib::Quark domain, int code, const char *format, Args ...args)
        : error(glib::Error::create(domain, code, format, std::forward<Args>(args)...))
    {}
    ErrorPtr(const ErrorPtr& other)
        : error(other.error->copy())
    {}
    ErrorPtr(ErrorPtr&&) noexcept = default;

    ErrorPtr& operator=(const ErrorPtr& other) noexcept
    {
        error = other.error->copy();
        return *this;
    }
    ErrorPtr& operator=(ErrorPtr&&) noexcept = default;

    operator bool() { return error; }
    glib::Error* operator->() { return error; }
    glib::Error* get() { return error; }
private:
    peel::UniquePtr<glib::Error> error;
};
