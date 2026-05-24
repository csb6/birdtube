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

#include <memory>
#include <peel/class.h>
#include <peel/RefPtr.h>
#include <peel/UniquePtr.h>
#include <peel/GLib/Error.h>
#include <peel/Purple/Connection.h>
#include <peel/Purple/Account.h>
#include "youtube_types.hpp"

namespace youtube {

class Connection final : public purple::Connection {
    PEEL_SIMPLE_CLASS(Connection, purple::Connection)
public:
    void init(Class*);
    static peel::RefPtr<Connection> create(peel::RefPtr<purple::Account>, peel::UniquePtr<glib::Error>*);

    bool vfunc_connect(peel::UniquePtr<glib::Error>*);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace youtube
