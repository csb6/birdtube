/*
BirdTube - YouTube live chat protocol plugin
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

#include "youtube_error.h"
#include <peel/String.h>
#include <peel/RefPtr.h>
#include <peel/GLib/DateTime.h>
#include <peel/GObject/Value.h>
#include <peel/ArrayRef.h>

namespace peel {
    namespace GObject {}
    namespace Gio {}
    namespace Soup {}
    namespace Json {}
    namespace Purple {}
    namespace GOAuth {};
}

namespace glib = peel::GLib;
namespace gio = peel::Gio;
namespace gobject = peel::GObject;
namespace soup = peel::Soup;
namespace json = peel::Json;
namespace purple = peel::Purple;
namespace goauth = peel::GOAuth;

namespace youtube {

struct StreamInfo {
    peel::String title;
    peel::String live_chat_id;
};

struct ChatMessage {
    enum class Type {
        Text, Super, Ban
    };
    peel::String channel_id;
    peel::String display_name;
    peel::RefPtr<glib::DateTime> timestamp;
    peel::String content;
    Type type;
    bool is_moderator;
};

} // namespace youtube
