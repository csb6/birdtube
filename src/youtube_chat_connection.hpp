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

#include <memory>
#include <peel/class.h>
#include <peel/String.h>
#include <peel/RefPtr.h>
#include <peel/UniquePtr.h>
#include <peel/GLib/Error.h>
#include <peel/Purple/Connection.h>
#include <peel/Purple/Account.h>
#include "youtube_types.hpp"
#include "task.hpp"

namespace peel::GLib {
class DateTime;
} // namespace peel::GLib

namespace youtube {

class ChatClient;

/* Represents a YouTube Live Chat connection */
class Connection final : public purple::Connection {
    PEEL_SIMPLE_DYNAMIC_CLASS(Connection, purple::Connection)
public:
    void init(Class*);
    static peel::RefPtr<Connection> create(peel::RefPtr<purple::Account>);
    ~Connection() noexcept;

    Task<void> vfunc_connect_async(gio::Cancellable*);
    Task<void> vfunc_disconnect_async(const char* message, gio::Cancellable*);
    Task<void> connect_to_chat_async(const char* stream_url, gio::Cancellable*);
    void disconnect_chat();
    Task<void> send_message_async(const char* message, gio::Cancellable*);

    peel::String get_channel_id();
    peel::String get_title();
    bool is_connected_to_chat();
private:
    struct Impl;

    void on_client_error(ChatClient*, const glib::Error*);
    void on_tokens_changed(ChatClient*, const char* access_token, const char* refresh_token);
    void on_access_token_expiration_changed(ChatClient*, glib::DateTime*);
    void on_new_messages(ChatClient*, void* data);

    std::unique_ptr<Impl> m_impl;
};

} // namespace youtube
