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

#include <cstdint>
#include <peel/GLib/HashTable.h>
#include <peel/GObject/GObject.h>
#include <peel/ArrayRef.h>
#include <peel/class.h>
#include <peel/Soup/Server.h>
#include <peel/Soup/ServerMessage.h>
#include <peel/Soup/Status.h>
#include <peel/Soup/MemoryUse.h>
#include "task.hpp"

namespace gobject = peel::GObject;
namespace soup = peel::Soup;

class OneShotServer final : public gobject::Object {
    PEEL_SIMPLE_CLASS(OneShotServer, Object)
public:
    void init(Class*);
    static peel::RefPtr<OneShotServer> create();

    Task<peel::RefPtr<glib::HashTable>> listen(unsigned port);
    Task<void> respond(soup::Status, soup::MemoryUse mem_use, peel::ArrayRef<const uint8_t> content);
    Task<void> respond(soup::Status, char* content);
private:
    peel::RefPtr<soup::Server> server;
    peel::RefPtr<soup::ServerMessage> msg;
};
