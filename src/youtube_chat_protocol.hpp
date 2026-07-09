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

#include <peel/class.h>
#include <peel/GLib/Error.h>
#include <peel/Purple/AccountSettings.h>
#include <peel/Purple/ChannelJoinDetails.h>
#include <peel/Purple/Conversation.h>
#include <peel/Purple/Connection.h>
#include <peel/Purple/Protocol.h>
#include <peel/Purple/ProtocolConversation.h>
#include <peel/UniquePtr.h>
#include "youtube_types.hpp"
#include "task.hpp"

namespace peel::Purple {
class Account;
class Message;
}

namespace peel::Gio {
class Cancellable;
}

namespace youtube {

class Protocol final : public purple::Protocol {
    PEEL_SIMPLE_DYNAMIC_CLASS(Protocol, purple::Protocol)
public:
    static void init_type(gobject::TypeModule*, peel::Type);
    static void init_interface(purple::ProtocolConversation::Iface*);
    void init(Class*) {}

    static peel::RefPtr<Protocol> create();

    peel::RefPtr<purple::AccountSettings> vfunc_get_default_account_settings();
    bool vfunc_can_connect(purple::Account*);
    peel::RefPtr<purple::Connection> vfunc_create_connection(purple::Account*, peel::UniquePtr<glib::Error>*);
    peel::RefPtr<purple::ChannelJoinDetails> vfunc_get_channel_join_details(purple::Account*);
    Task<peel::RefPtr<purple::Conversation>> vfunc_join_channel_async(
        purple::Account*, purple::ChannelJoinDetails*, gio::Cancellable*);
    Task<void> vfunc_leave_conversation_async(purple::Conversation*, gio::Cancellable*);
    Task<void> vfunc_send_message_async(purple::Conversation*, purple::Message*, gio::Cancellable*);
    Task<void> vfunc_refresh_async(purple::Conversation*, gio::Cancellable*);
};

} // namespace youtube
