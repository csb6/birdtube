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
#include <peel/Purple/AccountSettings.h>
#include <peel/Purple/Core.h>
#include <peel/Purple/Ui.h>
#include <peel/GLib/DateTime.h>
#include "youtube_chat_client.hpp"
#include "task.hpp"
#include <memory>

namespace youtube {

PEEL_CLASS_IMPL(Connection, "YoutubeConnection", purple::Connection)

struct Connection::Impl {
    peel::RefPtr<purple::Account> account;
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
    auto connection = Object::create<Connection>();
    connection->m_impl->account = std::move(account);

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

    auto* settings = connection->m_impl->account->get_settings();
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
    client->connect_new_messages([connection](youtube::ChatClient*, void*) {
        // TODO
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
                m_impl->account->disconnect_with_error(nullptr, error);
                co_return {};
            }
            // Open URL in user's browser so that they can begin the OAuth flow
            auto* ui = purple::Core::get_default()->get_ui();
            AsyncResult result;
            ui->open_uri(url->c_str(), nullptr, result.callback());
            ui->open_uri_finish(co_await result, &error);
            if(error) {
                m_impl->account->disconnect_with_error(nullptr, error);
                co_return {};
            }
        }

        // Connect to the YouTube stream
        auto* settings = m_impl->account->get_settings();
        const char* stream_url = settings->get_string("stream_url", "");
        auto error = co_await m_impl->client->connect_to_chat_async(stream_url, nullptr);
        if(error) {
            m_impl->account->disconnect_with_error(nullptr, error.get());
        }
        co_return {};
    };
    task().start();
    return true;
}

} // namespace youtube
