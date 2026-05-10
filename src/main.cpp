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
#include <peel/GLib/functions.h>
#include <peel/GLib/MainLoop.h>
#include <peel/GLib/MainContext.h>
#include <peel/GLib/DateTime.h>
#include <peel/ArrayRef.h>
#include <peel/UniquePtr.h>
#include "youtube_types.hpp"
#include "youtube_chat_client.hpp"
#include <memory>

namespace glib = peel::GLib;

// TODO: have callback for saving auth info to persistent storage once obtained
int main(int argc, char** argv)
{
    if(argc != 2) {
        g_printerr("Usage: %s stream_url\n", argv[0]);
        return 1;
    }

    std::unique_ptr<char*, decltype(&g_strfreev)> env{g_get_environ(), &g_strfreev};
    // Required environment variables
    const char* client_id = g_environ_getenv(env.get(), "YT_CLIENT_ID");
    if(!client_id) {
        g_printerr("Missing environment variable YT_CLIENT_ID");
        return 1;
    }
    const char* client_secret = g_environ_getenv(env.get(), "YT_CLIENT_SECRET");
    if(!client_secret) {
        g_printerr("Missing environment variable YT_CLIENT_SECRET");
        return 1;
    }
    // Optional environment variables
    const char* access_token = g_environ_getenv(env.get(), "YT_ACCESS_TOKEN");
    const char* refresh_token = g_environ_getenv(env.get(), "YT_REFRESH_TOKEN");
    const char* expiration = g_environ_getenv(env.get(), "YT_EXPIRATION");

    const char* stream_url = argv[1];
    peel::UniquePtr<glib::Error> error;
    peel::RefPtr<youtube::ChatClient> client;
    auto main_loop = glib::MainLoop::create(glib::MainContext::default_(), /*is_running=*/false);

    if(!access_token && !refresh_token && !expiration) {
        // Stream URL provided, but no existing access/refresh tokens (need to request authorization)
        client = youtube::ChatClient::create(client_id, client_secret);
        auto auth_url = client->generate_auth_url(&error);
        if(error) {
            g_printerr("Failed to get OAuth authorization URL: %s\n", error->message);
            return 1;
        }
        // Launch OAuth authorization flow in web browser
        char* cmd = g_strdup_printf("xdg-open \"%s\"", auth_url.c_str());
        system(cmd);
        g_free(cmd);
    } else if(access_token && refresh_token && expiration) {
        // Access token and refresh token provided (no need to request authorization again)
        auto expiration_time = glib::DateTime::create_from_iso8601(expiration, nullptr);
        if(!expiration_time) {
            g_printerr("Invalid YT_EXPIRATION (not in ISO8601 format)");
            return 1;
        }
        client = youtube::ChatClient::create_authorized(client_id, client_secret, access_token,
                                                        refresh_token, std::move(expiration_time));
    } else {
        g_printerr("Missing YT_ACCESS_TOKEN, YT_REFRESH_TOKEN, and/or YT_EXPIRATION");
        return 1;
    }

    client->set_error_callback([main_loop](glib::Error* error) {
        g_printerr("Error: %s\n", error->message);
        main_loop->quit();
    });
    client->connect_new_messages([](youtube::ChatClient*, void* data) {
        auto& messages = *static_cast<peel::ArrayRef<const youtube::ChatMessage>*>(data);
        for(const auto& msg : messages) {
            auto local_timestamp = msg.timestamp->to_local();
            auto timestamp_str = local_timestamp->format("%I:%M:%S %p");
            g_print("%s (%s): %s\n\n", msg.display_name.c_str(), timestamp_str.c_str(), msg.content.c_str());
        }
    });

    auto main_task = [main_loop, client, stream_url]() -> Task<void> {
        auto error = co_await client->connect_to_chat_async(stream_url, nullptr);
        if(error) {
            g_printerr("Error: %s\n", error->message);
            main_loop->quit();
        }
        co_return {};
    };
    main_task().start();
    main_loop->run();

    return 0;
}
