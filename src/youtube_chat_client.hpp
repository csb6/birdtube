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

#include <peel/GLib/DateTime.h>
#include <peel/GLib/Error.h>
#include <peel/GObject/GObject.h>
#include <peel/Gio/Cancellable.h>
#include <peel/class.h>
#include <peel/RefPtr.h>
#include <peel/String.h>
#include <peel/signal.h>
#include <peel/property.h>
#include <memory>
#include <expected>
#include "youtube_types.hpp"
#include "error_wrapper.hpp"
#include "task.hpp"

namespace youtube {

/* Manages a YouTube Live Chat connection. Low-level/does not depend on libpurple */
class ChatClient final : public gobject::Object {
    PEEL_SIMPLE_CLASS(ChatClient, Object)
public:
    ~ChatClient() noexcept;

    void init(Class*);
    static peel::RefPtr<ChatClient> create(const char* client_id, const char* client_secret);
    static peel::RefPtr<ChatClient> create_authorized(const char* client_id, const char* client_secret,
                                                      const char* access_token, const char* refresh_token,
                                                      peel::RefPtr<glib::DateTime> access_token_expiration);

    std::expected<peel::String, ErrorPtr> generate_auth_url();
    Task<void> authorize();
    Task<peel::String> get_user_display_name(gio::Cancellable*);
    Task<void> connect_to_chat_async(const char* stream_url, gio::Cancellable*);
    void disconnect();
    void disconnect_chat();
    Task<void> send_message_async(const char* message, gio::Cancellable*);
    bool is_authorized() const;
    bool is_connected_to_chat() const;
    const char* get_title() const;
    peel::String get_access_token() const;
    peel::String get_refresh_token() const;
    peel::RefPtr<glib::DateTime> get_access_token_expiration() const;

    PEEL_SIGNAL_CONNECT_METHOD(new_messages, sig_new_messages)
    PEEL_SIGNAL_CONNECT_METHOD(error, sig_error);
    PEEL_SIGNAL_CONNECT_METHOD(tokens_changed, sig_tokens_changed)
    PEEL_SIGNAL_CONNECT_METHOD(access_token_expiration_changed, sig_access_token_expiration_changed)
private:
    void on_tokens_changed(gobject::Object*, gobject::ParamSpec*);
    void on_access_token_expiration_changed(gobject::Object*, gobject::ParamSpec*);

    // TODO: find way to pass std::ArrayRef<const ChatMessage> as a parameter
    inline static peel::Signal<ChatClient, void(void*)> sig_new_messages;
    inline static peel::Signal<ChatClient, void(const glib::Error*)> sig_error;
    inline static peel::Signal<ChatClient, void(const char* access_token, const char* refresh_token)> sig_tokens_changed;
    inline static peel::Signal<ChatClient, void(glib::DateTime*)> sig_access_token_expiration_changed;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace youtube
