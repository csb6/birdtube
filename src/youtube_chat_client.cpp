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
#include "youtube_chat_client.hpp"
#include <string>
#include <map>
#include <peel/GOAuth/Client.h>
#include <peel/GOAuth/GOAuth.h>
#include <peel/Soup/Logger.h>
#include <peel/Soup/LoggerLogLevel.h>
#include <peel/Soup/MemoryUse.h>
#include <peel/Soup/Message.h>
#include <peel/Soup/MessageHeaders.h>
#include <peel/Soup/Session.h>
#include <peel/Soup/Status.h>
#include <peel/UniquePtr.h>
#include <peel/ArrayRef.h>
#include <peel/GLib/functions.h>
#include <peel/GLib/HashTable.h>
#include "task.hpp"
#include "youtube_chat_parser.hpp"
#include "event_source_token.hpp"
#include "error_wrapper.hpp"

G_DEFINE_QUARK(youtube-chat-error-quark, youtube_chat_error)

namespace youtube {

#define YOUTUBE_API_BASE_URL "https://www.googleapis.com/youtube/v3/"
#define YOUTUBE_API_AUTH_URL "https://accounts.google.com/o/oauth2/v2/auth"
#define YOUTUBE_API_TOKEN_URL "https://oauth2.googleapis.com/token"
#define YOUTUBE_API_SCOPE "https://www.googleapis.com/auth/youtube.force-ssl"
#define REDIRECT_PORT 43215

struct Conversation {
    Conversation(StreamInfo stream_info)
        : stream_info(std::move(stream_info)), fetch_cancel(gio::Cancellable::create())
    {}
    Conversation(const Conversation&) = delete;
    Conversation(Conversation&&) noexcept = default;
    ~Conversation() noexcept
    {
        disconnect();
    }
    Conversation& operator=(const Conversation&) = delete;
    Conversation& operator=(Conversation&&) noexcept = default;

    void disconnect()
    {
        this->fetch_messages_source.disconnect();
        this->fetch_cancel->cancel();
    }

    StreamInfo stream_info;
    peel::RefPtr<gio::Cancellable> fetch_cancel;
    EventSourceToken fetch_messages_source;
};

PEEL_CLASS_IMPL(ChatClient, "YoutubeChatClient", gobject::Object)

struct ChatClient::Impl {
    using ConversationIterator = std::map<std::string, Conversation>::iterator;

    // Operations
    void schedule_access_token_refresh();
    Task<void> refresh_access_token_async(gio::Cancellable*);
    Task<StreamInfo> get_live_stream_info_async(peel::String video_id, gio::Cancellable*);
    Task<void> fetch_messages_async(
        ConversationIterator, guint poll_interval, peel::String next_page_token = nullptr);

    bool is_access_expired() const;

    ChatClient* client;
    peel::RefPtr<goauth::Client> proxy;
    peel::RefPtr<soup::Session> session;
    peel::String pkce_code_verifier;
    bool is_authorized;
    EventSourceToken refresh_timer_source;
    peel::RefPtr<gio::Cancellable> refresh_cancel;
    std::map<std::string, Conversation> conversations;
};

void ChatClient::Class::init()
{
    sig_new_messages = decltype(sig_new_messages)::create("new-messages");
    sig_error = decltype(sig_error)::create("error");
    sig_tokens_changed = decltype(sig_tokens_changed)::create("tokens-changed");
    sig_access_token_expiration_changed = decltype(sig_access_token_expiration_changed)::create("access-token-expiration-changed");
}

void ChatClient::init(Class*)
{
    m_impl = std::make_unique<Impl>(this);
    m_impl->session = soup::Session::create();
    m_impl->proxy = goauth::Client::create(
        m_impl->session,
        YOUTUBE_API_AUTH_URL,
        YOUTUBE_API_TOKEN_URL,
        REDIRECT_PORT,
        "",
        "",
        YOUTUBE_API_BASE_URL);
    #ifdef YOUTUBE_CHAT_CLIENT_LOGGING
    auto logger = soup::Logger::create(soup::Logger::LogLevel::BODY);
    m_impl->session->add_feature(logger);
    #endif
    m_impl->proxy->connect_notify(goauth::Client::prop_access_token(),
                                  this, &ChatClient::on_tokens_changed);
    m_impl->proxy->connect_notify(goauth::Client::prop_refresh_token(),
                                  this, &ChatClient::on_tokens_changed);
    m_impl->proxy->connect_notify(goauth::Client::prop_access_token_expiration(),
                                  this, &ChatClient::on_access_token_expiration_changed);
    m_impl->refresh_cancel = gio::Cancellable::create();
}

peel::RefPtr<ChatClient> ChatClient::create(const char* client_id, const char* client_secret)
{
    auto client = Object::create<ChatClient>();
    // TODO: make these constructor properties
    client->m_impl->proxy->set_client_id(client_id);
    client->m_impl->proxy->set_client_secret(client_secret);

    return client;
}

peel::RefPtr<ChatClient> ChatClient::create_authorized(const char* client_id, const char* client_secret,
                                                       const char* access_token, const char* refresh_token,
                                                       peel::RefPtr<glib::DateTime> access_token_expiration)
{
    auto client = create(client_id, client_secret);
    client->m_impl->is_authorized = true;
    client->m_impl->proxy->set_access_token(access_token);
    client->m_impl->proxy->set_refresh_token(refresh_token);
    client->m_impl->proxy->set_access_token_expiration(access_token_expiration);
    return client;
}

ChatClient::~ChatClient() noexcept
{
    disconnect();
}

bool ChatClient::is_authorized() const
{
    return m_impl->is_authorized;
}

bool ChatClient::is_chat_connected(const char* stream_url) const
{
    return m_impl->conversations.find(stream_url) != m_impl->conversations.end();
}

const char* ChatClient::get_title(const char* stream_url) const
{
    auto conversation = m_impl->conversations.find(stream_url);
    if(conversation == m_impl->conversations.end()) {
        g_warning("Unknown conversation: %s", stream_url);
        return "";
    }
    return conversation->second.stream_info.title;
}

peel::String ChatClient::get_access_token() const
{
    return m_impl->proxy->get_access_token();
}

peel::String ChatClient::get_refresh_token() const
{
    return m_impl->proxy->get_refresh_token();
}

peel::RefPtr<glib::DateTime> ChatClient::get_access_token_expiration() const
{
    return m_impl->proxy->get_access_token_expiration();
}

void ChatClient::on_tokens_changed(gobject::Object*, gobject::ParamSpec*)
{
    auto access_token = get_access_token();
    auto refresh_token = get_refresh_token();
    // May receive notifications for a token before the other is set; wait until they are both set
    // to non-null values before doing anything
    if(access_token && refresh_token) {
        sig_tokens_changed.emit(this, access_token, refresh_token);
    }
}

void ChatClient::on_access_token_expiration_changed(gobject::Object*, gobject::ParamSpec*)
{
    auto expiration = get_access_token_expiration();
    sig_access_token_expiration_changed.emit(this, expiration);
}

std::expected<peel::String, ErrorPtr> ChatClient::generate_auth_url()
{
    if(m_impl->pkce_code_verifier) {
        return std::unexpected(ErrorPtr(YOUTUBE_CHAT_ERROR, 1, "Already have an in-progress OAuth flow"));
    }
    auto params = glib::HashTable::new_full(g_str_hash, g_str_equal, nullptr, g_free);
    glib::HashTable::insert(params, (void*)GOAUTH_PARAM_SCOPE, g_strdup(YOUTUBE_API_SCOPE));
    peel::UniquePtr<glib::Error> error;
    m_impl->pkce_code_verifier = goauth::add_auth_url_pkce_params(params, &error);
    if(!m_impl->pkce_code_verifier) {
        return std::unexpected(ErrorPtr(YOUTUBE_CHAT_ERROR, 1, "Failed to generate PKCE code verifier"));
    }
    // User must open this URL in a browser and grant the application permissions.
    // Once they have done so, they will get redirected to http://127.0.0.1:REDIRECT_PORT. We
    // will be listening on REDIRECT_PORT and will continue the authorization flow from
    // there.
    auto auth_url = m_impl->proxy->build_auth_url(params);
    return auth_url->to_string();
}

Task<void> ChatClient::authorize()
{
    if(!m_impl->pkce_code_verifier) {
        co_return ErrorPtr(YOUTUBE_CHAT_ERROR, 1, "No OAuth flow in-progress - call generate_auth_url first");
    }

    auto params = glib::HashTable::new_full(g_str_hash, g_str_equal, nullptr, g_free);
    glib::HashTable::insert(params, (void*)GOAUTH_PARAM_CLIENT_SECRET, g_strdup(m_impl->proxy->get_client_secret()));
    glib::HashTable::insert(params, (void*)GOAUTH_PARAM_CODE_VERIFIER, g_strdup(m_impl->pkce_code_verifier.c_str()));

    peel::UniquePtr<glib::Error> error;
    AsyncResult result;
    m_impl->proxy->await_auth_code_then_access_token_async(params, nullptr, result.callback());
    m_impl->proxy->await_auth_code_then_access_token_finish(co_await result, &error);
    if(error) {
        co_return error;
    }

    // From this point forwards, goauth::Client will add the access token as an
    // 'Authorization: Bearer <access_token>' header to each request
    m_impl->is_authorized = true;
    m_impl->schedule_access_token_refresh();

    co_return error;
}

void ChatClient::Impl::schedule_access_token_refresh()
{
    auto expiration = this->proxy->get_access_token_expiration();
    auto now = glib::DateTime::create_now_utc();
    // Refresh 2 minutes before the expiration date
    int64_t refresh_interval = expiration->difference(now) - 120000;
    if(refresh_interval <= 0) {
        this->refresh_access_token_async(this->refresh_cancel).start();
    } else {
        this->refresh_timer_source = glib::timeout_add_once((unsigned)refresh_interval, [this] {
            this->refresh_access_token_async(this->refresh_cancel).start();
        });
    }
}

Task<void> ChatClient::Impl::refresh_access_token_async(gio::Cancellable* cancellable)
{
    g_assert(this->is_authorized);

    this->refresh_timer_source.disconnect();

    AsyncResult result;
    peel::UniquePtr<glib::Error> error;
    this->proxy->refresh_token_fetch_async(cancellable, result.callback());
    this->proxy->refresh_token_fetch_finish(co_await result, &error);
    if(error) {
        co_return error;
    }
    g_assert(!this->is_access_expired());
    schedule_access_token_refresh();

    g_message("Refreshed access token\n");
    g_message("Access token: %s\n", this->proxy->get_access_token());
    g_message("Refresh token: %s\n", this->proxy->get_refresh_token());
    auto expiration = this->proxy->get_access_token_expiration();
    g_message("Token expiration: %s\n", expiration->format_iso8601().c_str());
    co_return error;
}

Task<peel::String> ChatClient::get_user_display_name(gio::Cancellable* cancellable)
{
    g_assert(m_impl->is_authorized);
    if(m_impl->is_access_expired()) {
        // Note: use passed in cancellable instead of m_impl->cancellable since this is a one-off
        //   operation and not a periodic operation
        auto error = co_await m_impl->refresh_access_token_async(cancellable);
        if(error) {
            co_return std::unexpected(std::move(error));
        }
    }

    auto params = glib::HashTable::new_(g_str_hash, g_str_equal);
    glib::HashTable::insert(params, (void*)"part", (void*)"snippet");
    glib::HashTable::insert(params, (void*)"mine", (void*)"true");
    glib::HashTable::insert(params, (void*)"maxResults", (void*)"1");
    auto request = m_impl->proxy->create_message("GET", "channels", params);

    AsyncResult result;
    peel::UniquePtr<glib::Error> error;
    // Note: use passed in cancellable instead of m_impl->cancellable since this is a one-off
    //   operation and not a periodic operation
    m_impl->session->send_and_read_async(request, G_PRIORITY_DEFAULT, cancellable, result.callback());
    auto response = m_impl->session->send_and_read_finish(co_await result, &error);
    if(error) {
        co_return std::unexpected(std::move(error));
    }
    co_return parse_display_name(response->get_data());
}

// TODO: check where stream_url needs to persist across suspension points - save it into an owning
//  variable where needed (wrapper functions around Task can just forward it so can save some
//  copies)
Task<void> ChatClient::connect_to_chat_async(std::string stream_url, gio::Cancellable* cancellable)
{
    g_assert(m_impl->is_authorized);
    if(m_impl->is_access_expired()) {
        // Note: use passed in cancellable instead of m_impl->cancellable since this is a one-off
        //   operation and not a periodic operation
        auto error = co_await m_impl->refresh_access_token_async(cancellable);
        if(error) {
            co_return error;
        }
    }

    if(auto conversation = m_impl->conversations.find(stream_url);
       conversation != m_impl->conversations.end()) {
        g_warning("Already connected to: %s", stream_url.c_str());
        co_return {};
    }

    auto video_id = extract_video_id(stream_url.c_str());
    if(!video_id.has_value()) {
        co_return std::move(video_id.error());
    }
    // Note: use passed in cancellable instead of m_impl->cancellable since this is a one-off
    //   operation and not a periodic operation
    auto live_stream_info = co_await m_impl->get_live_stream_info_async(std::move(*video_id), cancellable);
    if(!live_stream_info.has_value()) {
        co_return std::move(live_stream_info.error());
    }
    // Add the conversation to the set of active converations
    auto[conversation, _] = m_impl->conversations.emplace(std::move(stream_url), std::move(*live_stream_info));
    m_impl->fetch_messages_async(conversation, 5000).start(); // 5000 = Default poll interval

    co_return {};
}

void ChatClient::disconnect()
{
    m_impl->conversations.clear();
    m_impl->refresh_timer_source.disconnect();
    m_impl->refresh_cancel->cancel();
    m_impl->is_authorized = false;
}

void ChatClient::disconnect_chat(const char* stream_url)
{
    auto conversation = m_impl->conversations.find(stream_url);
    if(conversation == m_impl->conversations.end()) {
        g_warning("Unknown conversation: %s", stream_url);
        return;
    }
    m_impl->conversations.erase(conversation);
}

Task<StreamInfo> ChatClient::Impl::get_live_stream_info_async(peel::String video_id, gio::Cancellable* cancellable)
{
    if(!this->is_authorized) {
        co_return std::unexpected(ErrorPtr(YOUTUBE_CHAT_ERROR, 1, "Client is not authorized to make API calls"));
    }
    if(this->is_access_expired()) {
        // Note: use passed in cancellable instead of m_impl->cancellable since this is a one-off
        //   operation and not a periodic operation
        auto error = co_await this->refresh_access_token_async(cancellable);
        if(error) {
            co_return std::unexpected(error);
        }
    }
    auto params = glib::HashTable::new_(g_str_hash, g_str_equal);
    glib::HashTable::insert(params, (void*)"part", (void*)"snippet,liveStreamingDetails");
    glib::HashTable::insert(params, (void*)"fields",
                            (void*)"items(snippet(title),liveStreamingDetails(activeLiveChatId))");
    glib::HashTable::insert(params, (void*)"id", (void*)video_id.c_str());
    auto request = this->proxy->create_message("GET", "videos", params);

    AsyncResult result;
    peel::UniquePtr<glib::Error> error;
    this->session->send_and_read_async(request, G_PRIORITY_DEFAULT, cancellable, result.callback());
    auto response = this->session->send_and_read_finish(co_await result, &error);
    if(error) {
        co_return std::unexpected(std::move(error));
    }
    co_return parse_stream_info(response->get_data());
}

Task<void> ChatClient::send_message_async(std::string stream_url, const char* message, gio::Cancellable* cancellable)
{
    g_assert(m_impl->is_authorized);
    if(m_impl->is_access_expired()) {
        // Note: use passed in cancellable instead of m_impl->cancellable since this is a one-off
        //   operation and not a periodic operation
        auto error = co_await m_impl->refresh_access_token_async(cancellable);
        if(error) {
            co_return error;
        }
    }

    auto conversation = m_impl->conversations.find(stream_url);
    if(conversation == m_impl->conversations.end()) {
        g_warning("Unknown conversation: %s", stream_url.c_str());
        co_return {};
    }

    auto params = glib::HashTable::new_(g_str_hash, g_str_equal);
    glib::HashTable::insert(params, (void*)"part", (void*)"snippet");
    auto request = m_impl->proxy->create_message("POST", "liveChat/messages", params);
    auto message_bytes = create_text_message(conversation->second.stream_info.live_chat_id, message);
    request->set_request_body_from_bytes("application/json", message_bytes);

    AsyncResult result;
    peel::UniquePtr<glib::Error> error;
    // Note: use passed in cancellable instead of m_impl->cancellable since this is a one-off
    //   operation and not a periodic operation
    m_impl->session->send_and_read_async(request, G_PRIORITY_DEFAULT, cancellable, result.callback());
    m_impl->session->send_and_read_finish(co_await result, &error);
    co_return error;
}

Task<void> ChatClient::Impl::fetch_messages_async(
    ConversationIterator iter, guint poll_interval, peel::String next_page_token)
{
    g_assert(this->is_authorized);

    auto& stream_url = iter->first;
    Conversation& conversation = iter->second;
    conversation.fetch_messages_source.disconnect();

    if(this->is_access_expired()) {
        auto error = co_await this->refresh_access_token_async(conversation.fetch_cancel);
        if(error) {
            co_return error;
        }
    }

    auto params = glib::HashTable::new_(g_str_hash, g_str_equal);
    glib::HashTable::insert(params, (void*)"liveChatId", (void*)conversation.stream_info.live_chat_id.c_str());
    glib::HashTable::insert(params, (void*)"part", (void*)"snippet,authorDetails");
    glib::HashTable::insert(params, (void*)"fields", (void*)"nextPageToken,pollingIntervalMillis,"
                            "items(id,authorDetails(channelId,displayName,isChatModerator),"
                            "snippet(type,publishedAt,displayMessage,"
                            "userBannedDetails(banType,bannedUserDetails(channelId,displayName))))");
    if(next_page_token) {
        // Only request messages we haven't seen before
        glib::HashTable::insert(params, (void*)"pageToken", (void*)next_page_token.c_str());
    }
    auto request = this->proxy->create_message("GET", "liveChat/messages", params);

    AsyncResult result;
    peel::UniquePtr<glib::Error> error;
    g_print("Poll interval: %u\n", poll_interval);
    this->session->send_and_read_async(request, G_PRIORITY_DEFAULT, conversation.fetch_cancel, result.callback());
    auto response = this->session->send_and_read_finish(co_await result, &error);
    if(error) {
        // TODO: implement some kind of retry mechanism then give up
        // Note: will try again using the last known polling interval
        sig_error.emit(this->client, error);
        co_return error;
    }

    auto messages_info = parse_chat_messages(response->get_data());
    if(!messages_info.has_value()) {
        sig_error.emit(this->client, messages_info.error().get());
        co_return std::move(messages_info.error());
    }
    if(!messages_info->messages.empty()) {
        // Notify all listeners that a new batch of messages has been received
        peel::ArrayRef<const ChatMessage> messages_span{messages_info->messages.data(), messages_info->messages.size()};
        sig_new_messages.emit(this->client, stream_url.c_str(), (void*)&messages_span);
    }
    conversation.fetch_messages_source = glib::timeout_add_once(messages_info->poll_interval,
        [this, iter, next_page_token = std::move(messages_info->next_page_token),
         poll_interval = messages_info->poll_interval] {
        fetch_messages_async(iter, poll_interval, std::move(next_page_token)).start();
    });
    co_return {};
}

bool ChatClient::Impl::is_access_expired() const
{
    auto expiration = this->proxy->get_access_token_expiration();
    auto now = glib::DateTime::create_now_utc();
    return expiration->compare(now) <= 0;
}

} // namespace youtube
