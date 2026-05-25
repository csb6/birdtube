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
#include <peel/Purple/AccountSettings.h>
#include <peel/Purple/Contact.h>
#include <peel/Purple/ContactManager.h>
#include <peel/Purple/Conversation.h>
#include <peel/Purple/ConversationManager.h>
#include <peel/Purple/ConversationMembers.h>
#include <peel/Purple/ConversationType.h>
#include <peel/Purple/Core.h>
#include <peel/Purple/Message.h>
#include <peel/Purple/Ui.h>
#include <peel/GLib/DateTime.h>
#include "youtube_chat_client.hpp"
#include "task.hpp"
#include <memory>

namespace youtube {

PEEL_CLASS_IMPL(Connection, "YoutubeConnection", purple::Connection)

struct Connection::Impl {
    peel::RefPtr<ChatClient> client;
};

void Connection::Class::init()
{
    override_vfunc_connect<Connection>();
}

void Connection::init(Class*)
{
    m_impl = std::make_unique<Impl>();
}

peel::RefPtr<Connection> Connection::create(peel::RefPtr<purple::Account> account,
                                            peel::UniquePtr<glib::Error>* error)
{
    auto connection = Object::create<Connection>(prop_account(), std::move(account));

    // TODO: not sure if env variable is right way to include client ID/secrets
    std::unique_ptr<char*, decltype(&g_strfreev)> env{g_get_environ(), &g_strfreev};
    const char* client_id = g_environ_getenv(env.get(), "YT_CLIENT_ID");
    if(!client_id) {
        *error = glib::Error::create(YOUTUBE_CHAT_ERROR, 1, "%s", "Missing OAuth client ID");
        return {};
    }
    const char* client_secret = g_environ_getenv(env.get(), "YT_CLIENT_SECRET");
    if(!client_secret) {
        *error = glib::Error::create(YOUTUBE_CHAT_ERROR, 1, "%s", "Missing OAuth client secret");
        return {};
    }

    auto* settings = connection->get_account()->get_settings();
    const char* access_token = settings->get_string("access_token", "");
    const char* refresh_token = settings->get_string("refresh_token", "");
    const char* access_token_expiration_str = settings->get_string("access_token_expiration", "");
    auto access_token_expiration = glib::DateTime::create_from_iso8601(access_token_expiration_str, nullptr);
    if(strcmp(access_token, "") == 0 || strcmp(refresh_token, "") == 0 || !access_token_expiration) {
        // Need to request an access token
        connection->m_impl->client = ChatClient::create(client_id, client_secret);
    } else {
        // Already have access and refresh tokens
        connection->m_impl->client = ChatClient::create_authorized(
            client_id, client_secret,
            access_token, refresh_token, access_token_expiration
        );
    }

    // Setup callbacks/signals
    ChatClient* client = connection->m_impl->client;
    client->set_error_callback([](glib::Error* error) {
        // TODO: is there a better way to log errors?
        g_printerr("Error: %s\n", error->message);
    });
    client->connect_new_messages([connection](youtube::ChatClient*, void* data) {
        auto* account = connection->get_account();
        auto* core = purple::Core::get_default();
        auto* contact_manager = core->get_contact_manager();
        auto* conversation_manager = core->get_conversation_manager();
        auto* messages = static_cast<peel::ArrayRef<const youtube::ChatMessage>*>(data);
        // TODO: can technically avoid re-fetching/resetting display_name property since not likely to
        //  change during course of a chat
        for(const auto& message : *messages) {
            using ConvType = purple::ConversationType;

            auto contact = contact_manager->find_or_create(account, message.channel_id.c_str(), nullptr);
            contact->set_display_name(message.display_name.c_str());

            // Note: also using channel ID as conversation ID
            peel::RefPtr conversation = conversation_manager->find(
                account, ConvType::CHANNEL, message.channel_id.c_str());
            if(!conversation) {
                conversation = purple::Conversation::create(account, ConvType::CHANNEL, message.channel_id.c_str());
                conversation_manager->add(conversation);
            }

            auto author = conversation->get_members()->find_or_add_member(
                contact, /*announce=*/false, /*message=*/"");
            auto purple_msg = purple::Message::create(author, message.content.c_str());
            purple_msg->set_timestamp(message.timestamp);
            conversation->write_message(purple_msg);
        }
    });

    return connection;
}

bool Connection::vfunc_connect(peel::UniquePtr<glib::Error>*)
{
    auto task = [this] -> Task<void> {
        // Authorize client if needed
        if(!m_impl->client->get_is_authorized()) {
            peel::UniquePtr<glib::Error> error;
            auto url = m_impl->client->generate_auth_url();
            if(!url.has_value()) {
                this->get_account()->disconnect_with_error(nullptr, error);
                co_return {};
            }
            // Open URL in user's browser so that they can begin the OAuth flow
            auto* ui = purple::Core::get_default()->get_ui();
            AsyncResult result;
            ui->open_uri(url->c_str(), nullptr, result.callback());
            ui->open_uri_finish(co_await result, &error);
            if(error) {
                this->get_account()->disconnect_with_error(nullptr, error);
                co_return {};
            }
        }

        // Connect to the YouTube stream
        auto* settings = this->get_account()->get_settings();
        const char* stream_url = settings->get_string("stream_url", "");
        auto error = co_await m_impl->client->connect_to_chat_async(stream_url, nullptr);
        if(error) {
            this->get_account()->disconnect_with_error(nullptr, error.get());
        }
        co_return {};
    };
    task().start();
    return true;
}

} // namespace youtube
