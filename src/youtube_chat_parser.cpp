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
#include "youtube_chat_parser.hpp"
#include <peel/Json/Parser.h>
#include <peel/Json/Node.h>
#include <peel/Json/Path.h>
#include <peel/GLib/DateTime.h>

namespace youtube {

static
void parse_text_message(json::Node* item, std::vector<ChatMessage>& messages);

static peel::RefPtr<json::Node> parse_json(peel::ArrayRef<const char> response, peel::UniquePtr<glib::Error>* error);

static peel::RefPtr<json::Array> match_json_path(json::Node* root, const char* path);
static peel::String match_json_string(json::Node* root, const char* path);
static std::optional<guint> match_json_uint(json::Node* root, const char* path);
static peel::RefPtr<glib::DateTime> match_json_date(json::Node* root, const char* path);

std::optional<StreamInfo> parse_stream_info(peel::ArrayRef<const char> response, peel::UniquePtr<glib::Error>* error)
{
    std::optional<StreamInfo> result;
    auto root = parse_json(response, error);
    if(*error) {
        return result;
    }

    // Get stream title
    auto title = match_json_string(root, "$.items[*].snippet.title");
    if(!title) {
        *error = glib::Error::create(YOUTUBE_CHAT_ERROR, 1, "Missing live stream title");
        return result;
    }
    // Get stream live chat ID
    auto live_chat_id = match_json_string(root, "$.items[*].liveStreamingDetails.activeLiveChatId");
    if(live_chat_id) {
        result.emplace(std::move(title), std::move(live_chat_id));
    } else {
        *error = glib::Error::create(YOUTUBE_CHAT_ERROR, 1, "Missing live chat ID");
    }
    return result;
}

std::optional<ResponseInfo> parse_chat_messages(peel::ArrayRef<const char> response, peel::UniquePtr<glib::Error>* error)
{
    std::optional<ResponseInfo> result;
    auto root = parse_json(response, error);
    if(*error) {
        return result;
    }
    // Get interval to wait before sending next request
    auto poll_interval = match_json_uint(root, "$.pollingIntervalMillis");
    if(!poll_interval.has_value()) {
        *error = glib::Error::create(YOUTUBE_CHAT_ERROR, 1, "Invalid polling interval");
        return result;
    }
    // Get the page token to sent in the next request
    auto next_page_token = match_json_string(root, "$.nextPageToken");
    if(!next_page_token) {
        *error = glib::Error::create(YOUTUBE_CHAT_ERROR, 1, "Missing nextPageToken");
        return result;
    }
    // Process the batch of chat messages we have received
    auto items = match_json_path(root, "$.items[*]");
    if(!items) {
        *error = glib::Error::create(YOUTUBE_CHAT_ERROR, 1, "Missing chat messages");
        return result;
    }
    result.emplace();
    result->poll_interval = poll_interval.value();
    result->next_page_token = next_page_token;
    auto item_count = items->get_length();
    result->messages.reserve(item_count);
    for(guint i = 0; i < item_count; ++i) {
        json::Node* item = items->get_element(i);
        auto message_type = match_json_string(item, "$.snippet.type");
        if(!message_type) {
            continue;
        }
        if(message_type == "textMessageEvent") {
            parse_text_message(item, result->messages);
        } else {
            g_message("Unsupported message type: %s", message_type.c_str());
        }
    }
    return result;
}

static
void parse_text_message(json::Node* item, std::vector<ChatMessage>& messages)
{
    // Get commenter's display name
    auto display_name = match_json_string(item, "$.authorDetails.displayName");
    if(!display_name) {
        return;
    }
    // Get timestamp
    auto timestamp = match_json_date(item, "$.snippet.publishedAt");
    if(!timestamp) {
        return;
    }
    auto content = match_json_string(item, "$.snippet.displayMessage");
    if(!content) {
        return;
    }
    messages.emplace_back(std::move(display_name), std::move(timestamp), std::move(content));
}

static
peel::RefPtr<json::Node> parse_json(peel::ArrayRef<const char> response, peel::UniquePtr<glib::Error>* error)
{
    auto parser = json::Parser::create_immutable();

    parser->load_from_data(response.begin(), response.size(), error);
    if(*error) {
        return {};
    }

    auto response_root = parser->get_root();
    if(!response_root) {
        *error = glib::Error::create(YOUTUBE_CHAT_ERROR, 1, "Unexpected empty JSON");
        return {};
    }
    return response_root;
}

static
peel::String match_json_string(json::Node* root, const char* path)
{
    auto matches = match_json_path(root, path);
    if(matches->get_length() != 1) {
        return {};
    }
    const char* str = matches->get_string_element(0);
    if(!str) {
        return {};
    }
    return {str};
}

static
std::optional<guint> match_json_uint(json::Node* root, const char* path)
{
    auto matches = match_json_path(root, path);
    if(matches->get_length() != 1) {
        return {};
    }
    int64_t value = matches->get_int_element(0);
    if(value < 0 || value > G_MAXUINT) {
        return {};
    }
    return (guint)value;
}

static
peel::RefPtr<glib::DateTime> match_json_date(json::Node* root, const char* path)
{
    auto matches = match_json_path(root, path);
    if(matches->get_length() != 1) {
        return {};
    }
    const char* str = matches->get_string_element(0);
    if(!str) {
        return {};
    }
    return glib::DateTime::create_from_iso8601(str, nullptr);
}

static
peel::RefPtr<json::Array> match_json_path(json::Node* root, const char* path)
{
    auto results = json::Path::query(path, root, NULL);
    return results->get_array();
}

} // namespace youtube
