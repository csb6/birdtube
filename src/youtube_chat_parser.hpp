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

#include <vector>
#include <expected>
#include <peel/GLib/Error.h>
#include <peel/Gio/MemoryInputStream.h>
#include <peel/String.h>
#include <peel/UniquePtr.h>
#include <peel/ArrayRef.h>
#include "youtube_types.hpp"
#include "error_wrapper.hpp"
#include "task.hpp"

namespace youtube {

struct ResponseInfo {
    std::vector<ChatMessage> messages;
};

std::expected<peel::String, ErrorPtr> extract_video_id(const char* stream_url);

std::expected<StreamInfo, ErrorPtr> parse_stream_info(peel::ArrayRef<const char> response);

Task<ResponseInfo> parse_chat_messages_async(peel::RefPtr<gio::MemoryInputStream>, gio::Cancellable*);

peel::String create_text_message(const char* live_chat_id, const char* message);

} // namespace youtube
