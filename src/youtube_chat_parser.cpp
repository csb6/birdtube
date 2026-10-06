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
#include "youtube_chat_parser.hpp"
#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>
#include <peel/Json/Json.h>
#include <peel/Json/Builder.h>
#include <peel/Json/Parser.h>
#include <peel/Json/Node.h>
#include <peel/Json/Path.h>
#include <peel/GLib/functions.h>
#include <peel/GLib/DateTime.h>
#include <peel/GLib/HashTable.h>
#include <peel/GLib/Uri.h>
#include <peel/GLib/UriFlags.h>
#include <peel/GLib/UriParamsFlags.h>

using namespace std::string_view_literals;

namespace youtube {

static constexpr struct SupportedMsg {
    std::string_view name;
    ChatMessage::Type type;
} supported_messages[] = {
    {"textMessageEvent"sv, ChatMessage::Type::Text},
    {"superChatEvent"sv,   ChatMessage::Type::Super},
    {"userBannedEvent"sv,  ChatMessage::Type::Ban},
};

static
std::optional<ChatMessage> parse_chat_message(json::Node* item);

static std::expected<peel::RefPtr<json::Node>, ErrorPtr> parse_json(peel::ArrayRef<const uint8_t> response);

static peel::RefPtr<json::Array> match_json_path(json::Node* root, const char* path);
static peel::String match_json_string(json::Node* root, const char* path);
static std::optional<guint> match_json_uint(json::Node* root, const char* path);
static std::optional<bool> match_json_bool(json::Node* root, const char* path);
static peel::RefPtr<glib::DateTime> match_json_date(json::Node* root, const char* path);

std::expected<peel::String, ErrorPtr> extract_video_id(const char* stream_url)
{
    peel::UniquePtr<glib::Error> error;
    auto stream_uri = glib::Uri::parse(stream_url, glib::UriFlags::NONE, &error);
    if(error) {
        return std::unexpected(std::move(error));
    }
    const char* query = stream_uri->get_query();
    auto params = glib::Uri::parse_params(query, strlen(query), "&", glib::UriParamsFlags::NONE, &error);
    if(error) {
        return std::unexpected(std::move(error));
    }
    auto* video_id = (const char*)glib::HashTable::lookup(params, "v");
    if(!video_id) {
        return std::unexpected(glib::Error::create(YOUTUBE_CHAT_ERROR, 1, "Missing parameter in video URL"));
    }
    return video_id;
}

std::expected<StreamInfo, ErrorPtr> parse_stream_info(peel::ArrayRef<const uint8_t> response)
{
    auto root = parse_json(response);
    if(!root.has_value()) {
        return std::unexpected(std::move(root.error()));
    }

    // Get stream title
    auto title = match_json_string(*root, "$.items[*].snippet.title");
    if(!title) {
        return std::unexpected(ErrorPtr(YOUTUBE_CHAT_ERROR, 1, "Missing live stream title"));
    }
    // Get stream live chat ID
    auto live_chat_id = match_json_string(*root, "$.items[*].liveStreamingDetails.activeLiveChatId");
    if(!live_chat_id) {
        return std::unexpected(ErrorPtr(YOUTUBE_CHAT_ERROR, 1, "Missing live chat ID"));
    }
    return StreamInfo{std::move(title), std::move(live_chat_id)};
}

std::expected<peel::String, ErrorPtr> parse_display_name(peel::ArrayRef<const uint8_t> response)
{
    auto root = parse_json(response);
    if(!root.has_value()) {
        return std::unexpected(std::move(root.error()));
    }

    // Note: Some channels may have legacy custom URLs that are not the same as their handles. The API
    //  doesn't appear to expose this so not sure how we can handle it
    auto display_name = match_json_string(*root, "$.items[*].snippet.customUrl");
    if(!display_name) {
        return std::unexpected(ErrorPtr(YOUTUBE_CHAT_ERROR, 1, "Missing channel handle"));
    }
    return display_name;
}

std::expected<ResponseInfo, ErrorPtr> parse_chat_messages(peel::ArrayRef<const uint8_t> response)
{
    auto root = parse_json(response);
    if(!root.has_value()) {
        return std::unexpected(std::move(root.error()));
    }
    // Get interval to wait before sending next request
    auto poll_interval = match_json_uint(*root, "$.pollingIntervalMillis");
    if(!poll_interval.has_value()) {
        return std::unexpected(ErrorPtr(YOUTUBE_CHAT_ERROR, 1, "Invalid polling interval"));
    }
    // Get the page token to sent in the next request
    auto next_page_token = match_json_string(*root, "$.nextPageToken");
    if(!next_page_token) {
        return std::unexpected(ErrorPtr(YOUTUBE_CHAT_ERROR, 1, "Missing nextPageToken"));
    }
    // Process the batch of chat messages we have received
    auto items = match_json_path(*root, "$.items[*]");
    if(!items) {
        return std::unexpected(ErrorPtr(YOUTUBE_CHAT_ERROR, 1, "Missing chat messages"));
    }
    ResponseInfo result;
    result.poll_interval = poll_interval.value();
    result.next_page_token = next_page_token;
    auto item_count = items->get_length();
    result.messages.reserve(item_count);
    for(guint i = 0; i < item_count; ++i) {
        auto message = parse_chat_message(items->get_element(i));
        if(message) {
            result.messages.push_back(std::move(message.value()));
        }
    }
    return result;
}

peel::RefPtr<glib::Bytes> create_text_message(const char* live_chat_id, const char* message)
{
    auto builder = json::Builder::create_immutable();
    builder->begin_object();
        builder->set_member_name("snippet");
        builder->begin_object();
            builder->set_member_name("liveChatId");
            builder->add_string_value(live_chat_id);
            builder->set_member_name("type");
            builder->add_string_value("textMessageEvent");
            builder->set_member_name("textMessageDetails");
            builder->begin_object();
                builder->set_member_name("messageText");
                builder->add_string_value(message);
            builder->end_object();
        builder->end_object();
    builder->end_object();
    auto root = builder->get_root();
    auto msg_str = json::to_string(root, /*pretty=*/true);
    return glib::Bytes::create({(const uint8_t*)msg_str.c_str(), strlen(msg_str)});
}

static
std::optional<ChatMessage> parse_chat_message(json::Node* item)
{
    ChatMessage message;

    auto message_type_name = match_json_string(item, "$.snippet.type");
    if(!message_type_name) {
        g_warning("Message is missing a message type - ignoring");
        return {};
    }
    auto message_type = std::ranges::find(supported_messages, message_type_name.c_str(), &SupportedMsg::name);
    if(message_type == std::end(supported_messages)) {
        g_warning("Ignored unsupported message type: %s", message_type_name.c_str());
        return {};
    }

    // These fields should be present for all supported message types
    //  Get timestamp
    message.timestamp = match_json_date(item, "$.snippet.publishedAt");
    if(!message.timestamp) {
        g_warning("Message of type '%s' was missing timestamp", message_type_name.c_str());
        return {};
    }
    //  Get commenter's display name
    message.display_name = match_json_string(item, "$.authorDetails.displayName");
    if(!message.display_name) {
        g_warning("Message of type '%s' was missing a display name", message_type_name.c_str());
        return {};
    }
    //  Get commenter's channel ID
    message.channel_id = match_json_string(item, "$.authorDetails.channelId");
    if(!message.channel_id) {
        g_warning("Message of type '%s' was missing channel ID", message_type_name.c_str());
        return {};
    }
    //  Get if the commenter is a moderator
    message.is_moderator = match_json_bool(item, "$.authorDetails.isChatModerator").value_or(false);
    // Get (or construct) the message's content
    if(message_type->type == ChatMessage::Type::Ban) {
        auto ban_type = match_json_string(item, "$.snippet.userBannedDetails.banType");
        if(!ban_type) {
            ban_type = "Not Given";
        }
        auto banned_display_name = match_json_string(
            item, "$.snippet.userBannedDetails.bannedUserDetails.displayName");
        if(!banned_display_name) {
            g_warning("Ban message missing information about the user being banned - ignored");
            return {};
        }
        message.content = glib::strdup_printf(
            "%s was banned (Ban Type: %s)", banned_display_name.c_str(), ban_type.c_str());
    } else {
        message.content = match_json_string(item, "$.snippet.displayMessage");
        if(!message.content) {
            g_warning("Message of type '%s' was missing display message", message_type_name.c_str());
            return {};
        }
    }

    return message;
}

static
std::expected<peel::RefPtr<json::Node>, ErrorPtr> parse_json(peel::ArrayRef<const uint8_t> response)
{
    peel::UniquePtr<glib::Error> error;
    auto parser = json::Parser::create_immutable();
    parser->load_from_data((const char*)response.begin(), response.size(), &error);
    if(error) {
        return std::unexpected(std::move(error));
    }

    auto response_root = parser->get_root();
    if(!response_root) {
        return std::unexpected(ErrorPtr(YOUTUBE_CHAT_ERROR, 1, "Unexpected empty JSON"));
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

static std::optional<bool> match_json_bool(json::Node* root, const char* path)
{
    auto matches = match_json_path(root, path);
    if(matches->get_length() != 1) {
        return {};
    }
    return matches->get_boolean_element(0);
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
