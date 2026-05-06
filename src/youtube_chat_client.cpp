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
#include "youtube_chat_client.hpp"
#include <peel/Rest/OAuth2Proxy.h>
#include <peel/Rest/PkceCodeChallenge.h>
#include <peel/Soup/Logger.h>
#include <peel/Soup/LoggerLogLevel.h>
#include <peel/Soup/MemoryUse.h>
#include <peel/Soup/Status.h>
#include <peel/UniquePtr.h>
#include <peel/Gio/File.h>
#include <peel/ArrayRef.h>
#include <peel/GLib/functions.h>
#include <peel/GLib/HashTable.h>
#include <peel/GLib/Uri.h>
#include <peel/GLib/UriFlags.h>
#include <peel/GLib/UriParamsFlags.h>
#include "task.hpp"
#include "youtube_chat_parser.hpp"
#include "one_shot_server.hpp"

G_DEFINE_QUARK(youtube-chat-error-quark, youtube_chat_error)

namespace youtube {

#define YOUTUBE_API_BASE_URL "https://www.googleapis.com/youtube/v3/"
#define YOUTUBE_API_AUTH_URL "https://accounts.google.com/o/oauth2/v2/auth"
#define YOUTUBE_API_TOKEN_URL "https://oauth2.googleapis.com/token"
#define YOUTUBE_API_SCOPE "https://www.googleapis.com/auth/youtube.force-ssl"
#define LOOPBACK_REDIRECT_URL "http://127.0.0.1:43215"
#define REDIRECT_PORT 43215
#define STATE_STR_LEN 16

using ErrorPtr = peel::UniquePtr<glib::Error>;

static
char* build_server_error_response(const char* error_str);

static
peel::String extract_video_id(const char* stream_url, ErrorPtr*);

static
peel::String get_random_string(ErrorPtr*);

PEEL_CLASS_IMPL(ChatClient, "YoutubeChatClient", gobject::Object)

struct ChatClient::Impl {
    void call_error_callback(glib::Error*);
    // Operations
    Task<void> authorize(peel::RefPtr<OneShotServer> auth_listener);
    Task<StreamInfo> get_live_stream_info_async(peel::String video_id, gio::Cancellable*);
    Task<void> fetch_messages_async(peel::String next_page_token, guint poll_interval);

    ChatClient* client;
    peel::RefPtr<rest::OAuth2Proxy> proxy;
    peel::UniquePtr<rest::PkceCodeChallenge> pkce;
    peel::String state_str;
    ErrorCallback error_callback;
    StreamInfo stream_info;
    bool is_authorized;
};

decltype(ChatClient::sig_new_messages) ChatClient::sig_new_messages;

void ChatClient::Class::init()
{
    sig_new_messages = decltype(sig_new_messages)::create("new-messages");
}

void ChatClient::init(Class*)
{
    m_impl = std::make_unique<Impl>(this);
    m_impl->proxy = rest::OAuth2Proxy::create(
        YOUTUBE_API_AUTH_URL,
        YOUTUBE_API_TOKEN_URL,
        LOOPBACK_REDIRECT_URL,
        "",
        "",
        YOUTUBE_API_BASE_URL);
    #ifdef YOUTUBE_CHAT_CLIENT_LOGGING
    auto logger = soup::Logger::create(soup::Logger::LogLevel::BODY);
    m_impl->proxy->add_soup_feature(logger);
    #endif
    new (&m_impl->error_callback) ErrorCallback{};
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
                                                       glib::DateTime* access_token_expiration)
{
    auto client = Object::create<ChatClient>();
    client->m_impl->proxy->set_client_id(client_id);
    client->m_impl->proxy->set_client_secret(client_secret);
    auto curr_time = glib::DateTime::create_now_utc();
    if(curr_time->compare(access_token_expiration) >= 0) {
        // TODO: schedule refresh
        client->m_impl->is_authorized = false;
    } else {
        client->m_impl->is_authorized = true;
    }
    return client;
}

bool ChatClient::get_is_authorized() const
{
    return m_impl->is_authorized;
}

void ChatClient::set_error_callback(ErrorCallback&& callback)
{
    m_impl->error_callback = std::move(callback);
}

peel::String ChatClient::generate_auth_url(ErrorPtr* error)
{
    if(m_impl->pkce || m_impl->state_str) {
        *error = glib::Error::create(YOUTUBE_CHAT_ERROR, 1, "Already have an in-progress OAuth flow");
        return {};
    }
    // Generate a PKCE challenge (i.e. a hashed random string). Server will use this value to
    // validate that the same client is sending all OAuth requests.
    m_impl->pkce = rest::PkceCodeChallenge::create_random();
    // State string serves as a way to tag this request so that later we can be reasonably sure the server
    // is sending a reply to this request specifically
    m_impl->state_str = get_random_string(error);
    if(*error) {
        return {};
    }
    // User must open this URL in a browser and grant the application permissions.
    // Once they have done so, they will get redirected to LOOPBACK_REDIRECT_URL. We
    // will be listening on REDIRECT_PORT and will continue the authorization flow from
    // there.
    return m_impl->proxy->build_authorization_url(m_impl->pkce->get_challenge(), YOUTUBE_API_SCOPE, &m_impl->state_str);
}

Task<StreamInfo> ChatClient::Impl::get_live_stream_info_async(peel::String video_id, gio::Cancellable* cancellable)
{
    ErrorPtr error;

    if(!this->is_authorized) {
        error = glib::Error::create(YOUTUBE_CHAT_ERROR, 1, "Client is not authorized to make API calls");
        co_return error;
    }
    auto call = this->proxy->new_call();
    call->add_param("part", "snippet,liveStreamingDetails");
    call->add_param("fields", "items(snippet(title),liveStreamingDetails(activeLiveChatId))");
    call->add_param("id", video_id);
    call->set_function("videos");

    AsyncResult result;
    call->invoke_async(cancellable, result.callback());
    call->invoke_finish(co_await result, &error);
    if(error) {
        co_return error;
    }
    const char* response = call->get_payload();
    auto response_len = call->get_payload_length();
    auto stream_info = parse_stream_info(peel::ArrayRef{response, (guint)response_len}, &error);
    if(error) {
        co_return error;
    }
    co_return std::move(stream_info.value());
}

Task<void> ChatClient::Impl::authorize(peel::RefPtr<OneShotServer> auth_listener)
{
    static const uint8_t success_response[] =
        "<!DOCTYPE html>"
        "<html lang=\"en\">"
          "<head>"
            "<title>Purple-Youtube - Authorization Successful</title>"
          "</head>"
          "<body>"
            "<p>Successfully authorized Purple-Youtube! You can close this tab.</p>"
          "</body>"
        "</html>";

    ErrorPtr error;

    if(!this->pkce || !this->state_str) {
        error = glib::Error::create(YOUTUBE_CHAT_ERROR, 1, "No OAuth flow in-progress - call generate_auth_url first");
        co_return error;
    }

    // First, wait for the server's message and determine if we are authorized
    auto auth_response = co_await auth_listener->listen(REDIRECT_PORT);
    if(auto* error = std::get_if<ErrorPtr>(&auth_response)) {
        this->pkce = {};
        this->state_str = {};
        co_return std::move(*error);
    }
    auto query = std::move(std::get<peel::RefPtr<glib::HashTable>>(auth_response));
    auto* error_str = (const char*)glib::HashTable::lookup(query, "error");
    if(error_str) {
        error = glib::Error::create(YOUTUBE_CHAT_ERROR, 1, "OAuth redirect error: %s", error_str);
        auth_listener->respond(soup::Status::FORBIDDEN, build_server_error_response(error->message));
        this->pkce = {};
        this->state_str = {};
        co_return error;
    }
    auto* auth_code = (const char*)glib::HashTable::lookup(query, "code");
    if(!auth_code) {
        error = glib::Error::create(YOUTUBE_CHAT_ERROR, 1, "OAuth redirect error: Missing auth code");
        auth_listener->respond(soup::Status::FORBIDDEN, build_server_error_response(error->message));
        this->pkce = {};
        this->state_str = {};
        co_return error;
    }
    auto* received_state_str = (const char*)glib::HashTable::lookup(query, "state");
    auto expected_state_str = std::move(this->state_str);
    if(!received_state_str || strcmp(received_state_str, expected_state_str) != 0) {
        error = glib::Error::create(YOUTUBE_CHAT_ERROR, 1, "OAuth redirect error: Missing/incorrect state string");
        auth_listener->respond(soup::Status::FORBIDDEN, build_server_error_response(error->message));
        this->pkce = {};
        co_return error;
    }

    // Attempt to get the access token using the authorization code provided by the server
    AsyncResult result;
    this->proxy->fetch_access_token_async(auth_code, this->pkce->get_verifier(), nullptr, result.callback());
    this->proxy->fetch_access_token_finish(co_await result, &error);
    this->pkce = {};
    if(error) {
        // TODO: map GError to HTTP error code
        auth_listener->respond(soup::Status::FORBIDDEN, build_server_error_response(error->message));
        co_return error;
    }

    // From this point forwards, OAuth2Proxy will add the access token as an
    // 'Authorization: Bearer <access_token>' header to each request
    this->is_authorized = true;
    // Send the user's web browser a message letting them know authorization was successful
    auth_listener->respond(soup::Status::OK, soup::MemoryUse::STATIC, success_response);
    // TODO: schedule next refresh token update
    // TODO: how to ensure requests do not use expired access tokens
    // TODO: seems like librest is treating some error responses as success. If we send an empty client
    //  secret, everything appears to succeed but the Bearer token is '(null)', causing API calls to fail
    co_return error;
}

Task<void> ChatClient::connect_to_chat_async(const char* stream_url, gio::Cancellable* cancellable)
{
    ErrorPtr error;

    // Note: needs to stick around for the entirety of this function so there is enough time
    //  for the server to send a response back to the web browser
    peel::RefPtr<OneShotServer> auth_listener;
    if(!m_impl->is_authorized) {
        auth_listener = OneShotServer::create();
        error = co_await m_impl->authorize(auth_listener);
        if(error) {
            co_return error;
        }
    }

    auto video_id = extract_video_id(stream_url, &error);
    if(error) {
        co_return error;
    }
    auto live_stream_info = co_await m_impl->get_live_stream_info_async(std::move(video_id), cancellable);
    if(auto* error2 = std::get_if<ErrorPtr>(&live_stream_info)) {
        co_return std::move(*error2);
    }
    // Cache stream info
    m_impl->stream_info = std::move(std::get<StreamInfo>(live_stream_info));
    assert(m_impl->stream_info.live_chat_id);
    m_impl->fetch_messages_async(nullptr, 5000).start(); // 5000 = Default poll interval

    co_return error;
}

Task<void> ChatClient::Impl::fetch_messages_async(peel::String next_page_token, guint poll_interval)
{
    g_assert(this->is_authorized);
    auto call = this->proxy->new_call();
    call->add_param("liveChatId", this->stream_info.live_chat_id);
    call->add_param("part", "snippet,authorDetails");
    call->add_param("fields", "nextPageToken,pollingIntervalMillis,items(id,authorDetails(displayName),"
                              "snippet(type,publishedAt,displayMessage))");
    if(next_page_token) {
        // Only request messages we haven't seen before
        call->add_param("pageToken", next_page_token);
    }
    call->set_function("liveChat/messages");

    ErrorPtr error;
    AsyncResult result;
    g_print("Poll interval: %u\n", poll_interval);
    call->invoke_async(nullptr, result.callback());
    call->invoke_finish(co_await result, &error);
    if(error) {
        // TODO: implement some kind of retry mechanism then give up
        // Note: will try again using the last known polling interval
        call_error_callback(error);
        co_return error;
    }
    const char* response = call->get_payload();
    auto response_len = call->get_payload_length();
    auto messages_info = parse_chat_messages(peel::ArrayRef{response, (guint)response_len}, &error);
    if(error) {
        call_error_callback(error);
        co_return error;
    }
    if(!messages_info->messages.empty()) {
        // Notify all listeners that a new batch of messages has been received
        peel::ArrayRef<const ChatMessage> messages_span{messages_info->messages.data(), messages_info->messages.size()};
        sig_new_messages.emit(this->client, (void*)&messages_span);
    }
    glib::timeout_add(messages_info->poll_interval,
                      [this, next_page_token = std::move(messages_info->next_page_token),
                       poll_interval = messages_info->poll_interval]() -> bool {
        fetch_messages_async(std::move(next_page_token), poll_interval).start();
        return false;
    });
    co_return {};
}

void ChatClient::Impl::call_error_callback(glib::Error* error)
{
    if(this->error_callback) {
        this->error_callback(error);
    }
}

static
char* build_server_error_response(const char* error_str)
{
    static const char error_response[] =
        "<!DOCTYPE html>"
        "<html lang=\"en\">"
          "<head>"
            "<title>Purple-Youtube - Error</title>"
          "</head>"
          "<body>"
            "<p>Failed to grant permissions to Purple-Youtube:</p>"
            "<p>%s</p>"
          "</body>"
        "</html>";

    return g_strdup_printf(error_response, error_str);
}

static
peel::String extract_video_id(const char* stream_url, ErrorPtr* error)
{
    auto stream_uri = glib::Uri::parse(stream_url, glib::UriFlags::NONE, error);
    if(*error) {
        return nullptr;
    }
    const char* query = stream_uri->get_query();
    auto params = glib::Uri::parse_params(query, strlen(query), "&", glib::UriParamsFlags::NONE, error);
    if(*error) {
        return nullptr;
    }
    auto* video_id = (const char*)glib::HashTable::lookup(params, "v");
    if(!video_id) {
        *error = glib::Error::create(YOUTUBE_CHAT_ERROR, 1, "Missing parameter in video URL");
        return nullptr;
    }
    return video_id;
}

static
peel::String get_random_string(ErrorPtr* error)
{
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~";

    peel::String result;
    #if defined(G_OS_UNIX)
        auto urandom = gio::File::create_for_path("/dev/urandom");
        auto rand_stream = urandom->read(nullptr, error);
        if(*error) {
            return result;
        }
        gsize bytes_read = 0;
        uint8_t* buffer = (uint8_t*)g_malloc(STATE_STR_LEN + 1);
        result = peel::String::adopt_string((char*)buffer);
        rand_stream->read_all(peel::ArrayRef(buffer, STATE_STR_LEN), &bytes_read, nullptr, error);
        if(*error) {
            result = nullptr;
            return result;
        }
    #elif defined(G_OS_WIN32)
        // TODO: test on Windows somehow
        uint8_t* buffer = (uint8_t*)g_malloc(STATE_STR_LEN + 1);
        result = peel::String::adopt_string((char*)buffer);
        NTSTATUS status = BCryptGenRandom(NULL, buffer, STATE_STR_LEN, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if(status != STATUS_SUCCESS) {
            *error = glib::Error::create(YOUTUBE_CHAT_ERROR, 1, "Failed to read random data during OAuth authorization");
            return result;
        }
    #else
        #error "The cryptographic random number generator API for this platform is not supported"
    #endif
    for(guint i = 0; i < STATE_STR_LEN; ++i) {
        buffer[i] = alphabet[buffer[i] % (sizeof(alphabet) - 1)];
    }
    buffer[STATE_STR_LEN] = '\0';
    return result;
}

} // namespace youtube
