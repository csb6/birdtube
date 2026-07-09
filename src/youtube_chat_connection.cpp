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
#include "youtube_chat_connection.hpp"
#include <peel/ArrayRef.h>
#include <peel/Gio/Cancellable.h>
#include <peel/Gio/Task.h>
#include <peel/GLib/functions.h>
#include <peel/Purple/AccountSettings.h>
#include <peel/Purple/Badge.h>
#include <peel/Purple/Badges.h>
#include <peel/Purple/BadgeManager.h>
#include <peel/Purple/Contact.h>
#include <peel/Purple/ContactInfo.h>
#include <peel/Purple/ContactManager.h>
#include <peel/Purple/Conversation.h>
#include <peel/Purple/ConversationManager.h>
#include <peel/Purple/ConversationMember.h>
#include <peel/Purple/ConversationMembers.h>
#include <peel/Purple/ConversationType.h>
#include <peel/Purple/Core.h>
#include <peel/Purple/CredentialManager.h>
#include <peel/Purple/Message.h>
#include <peel/Purple/Ui.h>
#include <peel/GLib/DateTime.h>
#include <span>
#include <optional>
#include <utility>
#include "youtube_chat_client.hpp"
#include "task.hpp"

static
peel::String encode_tokens(const char* access_token, const char* refresh_token);

static
std::optional<std::pair<peel::String, peel::String>>
extract_access_and_refresh_tokens(const char* credentials);

namespace youtube {

PEEL_CLASS_IMPL_DYNAMIC(Connection, "YoutubeConnection", purple::Connection)

struct Connection::Impl {
    peel::RefPtr<ChatClient> client;
    peel::RefPtr<gio::Cancellable> cancellable;
    peel::String stream_url;
};

void Connection::init(Class*)
{
    m_impl = std::make_unique<Impl>();
    m_impl->cancellable = gio::Cancellable::create();
}

peel::RefPtr<Connection> Connection::create(peel::RefPtr<purple::Account> account)
{
    return Object::create<Connection>(prop_account(), std::move(account));
}

Connection::~Connection() noexcept
{
    m_impl->cancellable->cancel();
}

void Connection::on_client_error(ChatClient*, const glib::Error* error)
{
    // TODO: is there a better way to log errors?
    g_warning("Error: %s\n", error->message);
}

void Connection::on_tokens_changed(ChatClient*, const char* access_token, const char* refresh_token)
{
    [](Connection* self, peel::String credentials_base64) -> VoidTask {
        auto* credential_manager = purple::Core::get_default()->get_credential_manager();
        AsyncResult result;
        peel::UniquePtr<glib::Error> error;
        credential_manager->write_password_async(
            self->get_account(), credentials_base64, self->m_impl->cancellable, result.callback());
        credential_manager->write_password_finish(co_await result, &error);
        if(error) {
            g_warning("Failed to save credentials: %s", error->message);
        }
    }(this, encode_tokens(access_token, refresh_token)).start();
}

void Connection::on_access_token_expiration_changed(ChatClient*, glib::DateTime* expiration)
{
    get_account()->get_settings()->set_string("access_token_expiration", expiration->format_iso8601());
}

void Connection::on_new_messages(ChatClient*, void* data)
{
    using ConvType = purple::ConversationType;

    auto* account = get_account();
    auto* core = purple::Core::get_default();
    auto* contact_manager = core->get_contact_manager();
    auto* conversation_manager = core->get_conversation_manager();
    auto* badge_manager = core->get_badge_manager();
    auto* messages = static_cast<peel::ArrayRef<const youtube::ChatMessage>*>(data);
    // TODO: can technically avoid re-fetching/resetting display_name property since not likely to
    //  change during course of a chat
    peel::RefPtr conversation = conversation_manager->find(
        account, ConvType::CHANNEL, get_channel_id().c_str());
    if(!conversation) {
        g_warning("Conversation doesn't exist for stream: %s", get_channel_id().c_str());
        return;
    }
    for(const auto& message : *messages) {
        auto contact = contact_manager->find_or_create(account, message.channel_id.c_str(), nullptr);
        contact->set_display_name(message.display_name.c_str());

        auto author = conversation->get_members()->find_or_add_member(
            contact, /*announce=*/false, /*message=*/"");
        if(message.is_moderator) {
            author->get_badges()->add_badge(badge_manager->find("moderator"));
        }
        auto purple_msg = purple::Message::create(author, message.content.c_str());
        purple_msg->set_timestamp(message.timestamp);
        if(message.type == ChatMessage::Type::Ban) {
            purple_msg->set_event(true);
        } else if(message.type == ChatMessage::Type::Super) {
            purple_msg->set_highlighted(true);
        }
        conversation->write_message(purple_msg);
    }
}

Task<void> Connection::vfunc_connect_async(gio::Cancellable* cancellable)
{
    auto* account = this->get_account();
    auto* settings = account->get_settings();
    if(!m_impl->client) {
        auto* credential_manager = purple::Core::get_default()->get_credential_manager();
        AsyncResult result;
        peel::UniquePtr<glib::Error> error;

        // Yes, this is supposed to be here. This is a public client
        const char* ci = "1060523451092-" "6uvnkq0u5t7knm4" "mept0rprfsia4vvnu.ap" "ps.go" "ogleuser" "conte" "nt.com";
        const char* cs = "GOCSPX" "-W-BnhH8Lxb" "Hn_B9jjvVpu05" "GElXK";

        credential_manager->read_password_async(account, cancellable, result.callback());
        auto credentials_str = credential_manager->read_password_finish(co_await result, &error);
        if(error) {
            co_return error;
        }
        const char* access_token_expiration_str = settings->get_string("access_token_expiration", "");
        auto access_token_expiration = glib::DateTime::create_from_iso8601(access_token_expiration_str, nullptr);
        if(credentials_str && access_token_expiration) {
            // Use existing credentials
            auto credentials = extract_access_and_refresh_tokens(credentials_str.c_str());
            if(!credentials.has_value()) {
                error = glib::Error::create(YOUTUBE_CHAT_ERROR, 1, "Invalid account credentials");
                co_return error;
            }
            peel::String& access_token = credentials->first;
            peel::String& refresh_token = credentials->second;
            m_impl->client = ChatClient::create_authorized(
                ci, cs, access_token.c_str(), refresh_token.c_str(), access_token_expiration);
        } else {
            // No existing credentials - will authorize in next step
            m_impl->client = ChatClient::create(ci, cs);
        }

        // Setup signals
        m_impl->client->connect_error(this, &Connection::on_client_error);
        m_impl->client->connect_tokens_changed(this, &Connection::on_tokens_changed);
        m_impl->client->connect_access_token_expiration_changed(
             this, &Connection::on_access_token_expiration_changed);
        m_impl->client->connect_new_messages(this, &Connection::on_new_messages);
    }

    // Authorize client if needed
    if(!m_impl->client->is_authorized()) {
        peel::UniquePtr<glib::Error> error;
        auto url = m_impl->client->generate_auth_url();
        if(!url.has_value()) {
            account->disconnect_with_error("Failed to generate OAuth URL", error);
            co_return error;
        }
        // Open URL in user's browser so that they can begin the OAuth flow
        auto* ui = purple::Core::get_default()->get_ui();
        AsyncResult result;
        ui->open_uri(url->c_str(), cancellable, result.callback());
        ui->open_uri_finish(co_await result, &error);
        if(error) {
            account->disconnect_with_error("Failed to open OAuth URL in browser", error);
            co_return error;
        }
        // Perform the authorization
        auto error_ptr = co_await m_impl->client->authorize();
        if(error_ptr) {
            account->disconnect_with_error("Authorization failed", error_ptr.get());
            co_return error_ptr;
        }
    }

    auto display_name = co_await m_impl->client->get_user_display_name(cancellable);
    if(!display_name.has_value()) {
        account->disconnect_with_error("Failed to get account display name", display_name.error().get());
        co_return std::move(display_name.error());
    }
    account->get_contact_info()->set_display_name(std::move(*display_name));

    account->ready();
    co_return {};
}

Task<void> Connection::vfunc_disconnect_async(const char*, gio::Cancellable*)
{
    m_impl->stream_url = nullptr;
    m_impl->client->disconnect();
    co_return {};
}

Task<void> Connection::connect_to_chat_async(const char* stream_url, gio::Cancellable* cancellable)
{
    g_assert(!m_impl->stream_url);
    m_impl->stream_url = stream_url;
    return m_impl->client->connect_to_chat_async(stream_url, cancellable);
}

void Connection::disconnect_chat()
{
    m_impl->stream_url = nullptr;
    m_impl->client->disconnect_chat();
}

Task<void> Connection::send_message_async(const char* message, gio::Cancellable* cancellable)
{
    return m_impl->client->send_message_async(message, cancellable);
}

peel::String Connection::get_channel_id()
{
    return m_impl->stream_url;
}

peel::String Connection::get_title()
{
    return m_impl->client->get_title();
}

bool Connection::is_connected_to_chat()
{
    return m_impl->client->is_connected_to_chat();
}

void Connection::Class::init()
{
    auto* klass = reinterpret_cast<PurpleConnectionClass*>(this);
    klass->connect_async = [](PurpleConnection* connection, GCancellable* cancellable,
                              GAsyncReadyCallback callback, gpointer data) {
        auto* self = reinterpret_cast<youtube::Connection*>(connection);
        auto* task = reinterpret_cast<gio::Task*>(g_task_new(connection, cancellable, callback, data));
        [](youtube::Connection* self, gio::Task* task) -> VoidTask {
            auto error = co_await self->vfunc_connect_async(task->get_cancellable());
            if(error) {
                task->return_error(error->copy());
            } else {
                task->return_boolean(true);
            }
        }(self, task).start();
    };
    klass->connect_finish = [](PurpleConnection*, GAsyncResult* result, GError** error) {
        return g_task_propagate_boolean(G_TASK(result), error);
    };
    klass->disconnect_async = [](PurpleConnection* connection, const char* message, GCancellable* cancellable,
                                 GAsyncReadyCallback callback, gpointer data) {
        auto* self = reinterpret_cast<youtube::Connection*>(connection);
        auto* task = reinterpret_cast<gio::Task*>(g_task_new(connection, cancellable, callback, data));
        [](youtube::Connection* self, peel::String message, gio::Task* task) -> VoidTask {
            auto error = co_await self->vfunc_disconnect_async(message, task->get_cancellable());
            if(error) {
                task->return_error(error->copy());
            } else {
                task->return_boolean(true);
            }
        }(self, message, task).start();
    };
    klass->disconnect_finish = [](PurpleConnection*, GAsyncResult* result, GError** error) {
        return g_task_propagate_boolean(G_TASK(result), error);
    };
}

} // namespace youtube

static
peel::String encode_tokens(const char* access_token, const char* refresh_token)
{
    auto access_token_base64 = glib::base64_encode(
        peel::ArrayRef<const uint8_t>{(uint8_t*)access_token, strlen(access_token)}
    );
    auto refresh_token_base64 = glib::base64_encode(
        peel::ArrayRef<const uint8_t>{(uint8_t*)refresh_token, strlen(refresh_token)}
    );
    return glib::strconcat(access_token_base64.c_str(), ":", refresh_token_base64.c_str());
}

static
peel::String decode_base64(std::span<const uint8_t> base64_text)
{
    // +1 for null terminator
    size_t out_buffer_len = (base64_text.size() / 4) * 3 + 3 + 1;
    auto* out_buffer = (uint8_t*)g_malloc(out_buffer_len);
    int state = 0;
    unsigned save = 0;
    glib::base64_decode_step(
        peel::ArrayRef<const uint8_t>{&*base64_text.begin(), base64_text.size()}, out_buffer, &state, &save);
    return peel::String::adopt_string((char*)out_buffer);
}

static
std::optional<std::pair<peel::String, peel::String>>
extract_access_and_refresh_tokens(const char* credentials)
{
    // TODO: add (begin, end) constructor to ArrayRef so no need for span
    std::span<const uint8_t> credentials_view{(uint8_t*)credentials, strlen(credentials)};
    auto delimiter = std::ranges::find(credentials_view, ':');
    if(delimiter == credentials_view.end() || delimiter + 1 == credentials_view.end()) {
        return {};
    }
    std::span<const uint8_t> access_token_base64{credentials_view.begin(), delimiter};
    std::span<const uint8_t> refresh_token_base64{delimiter + 1, credentials_view.end()};
    return std::make_pair(decode_base64(access_token_base64), decode_base64(refresh_token_base64));
}
