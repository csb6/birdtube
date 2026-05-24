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
#include <glib.h>
#include <glib-object.h>
#include <gplugin.h>
#include <gplugin-native.h>
#include <peel/Purple/Account.h>
#include <peel/Purple/Core.h>
#include <peel/Purple/ChannelJoinDetails.h>
#include <peel/Purple/Protocol.h>
#include <peel/Purple/ProtocolManager.h>
#include <peel/Purple/ProtocolConversation.h>
#include "youtube_chat_connection.hpp"
#include "youtube_error.h"

namespace youtube {

class Protocol final : public purple::Protocol {
    PEEL_SIMPLE_CLASS(Protocol, purple::Protocol)
public:
    static void init_type(peel::Type type)
    {
        PEEL_IMPLEMENT_INTERFACE(type, purple::ProtocolConversation);
    }

    static void init_interface(purple::ProtocolConversation::Iface* iface)
    {
        iface->override_vfunc_get_channel_join_details<Protocol>();
        // TODO: unsupported currently in peel
        //iface->override_vfunc_join_channel_async<Protocol>();
        //iface->override_vfunc_join_channel_finish<Protocol>();
        //iface->override_vfunc_leave_conversation_async<Protocol>();
        //iface->override_vfunc_leave_conversation_finish<Protocol>();
        //iface->override_vfunc_send_message_async<Protocol>();
        //iface->override_vfunc_send_message_finish<Protocol>();
        iface->override_vfunc_refresh<Protocol>();
    }

    void init(Class*) {}

    static peel::RefPtr<Protocol> create()
    {
        auto protocol = Object::create<Protocol>(
            prop_id(), "prpl-youtube-chat",
            prop_name(), "YouTube Live Chat",
            prop_description(), "YouTube live chat support"
            //"icon-name", "im-youtube",
            //"icon-resource-path", "TODO"
        );
        return protocol;
    }

    peel::RefPtr<purple::Connection> vfunc_create_connection(purple::Account* account,
                                                             peel::UniquePtr<glib::Error>* error)
    {
        return youtube::Connection::create(account, error);
    }

    peel::RefPtr<purple::ChannelJoinDetails> vfunc_get_channel_join_details(purple::Account*)
    {
        return purple::ChannelJoinDetails::create(
            /*name_max_length=*/0,
            /*nickname_supported=*/false, /*nickname_max_length=*/0,
            /*password_supported=*/false, /*password_max_length=*/0
        );
    }

    void vfunc_refresh(purple::Conversation*)
    {

    }
};

PEEL_CLASS_IMPL(Protocol, "YoutubeProtocol", purple::Protocol)

void Protocol::Class::init()
{
    override_vfunc_create_connection<Protocol>();
}

} // namespace youtube

static peel::RefPtr<youtube::Protocol> youtube_chat_protocol;

static
GPluginPluginInfo* youtube_chat_query(GError**)
{
    int flags = PURPLE_PLUGIN_INFO_FLAGS_INTERNAL
              | PURPLE_PLUGIN_INFO_FLAGS_AUTO_LOAD;
    static const char * const authors[] = {
        "Cole Blakley",
        nullptr
    };

    return purple_plugin_info_new(
        "id", "csb6/purple-youtube",
        "name", "YouTube Live Chat",
        "authors", authors,
        "version", "0.1",
        "category", "Protocol",
        "summary", "YouTube Live Chat Protocol Plugin",
        "description", "Support for YouTube Live Chat",
        "website", "https://github.com/csb6/purple-youtube",
        "abi-version", PURPLE_ABI_VERSION,
        "flags", flags,
        nullptr);
}

static
gboolean youtube_chat_load(GPluginPlugin*, GError** error)
{
    if(youtube_chat_protocol) {
        g_set_error_literal(error, YOUTUBE_CHAT_ERROR, 1, "Plugin was not cleaned up properly");
        return false;
    }

    auto* manager = purple::Core::get_default()->get_protocol_manager();
    if(manager) {
        peel::UniquePtr<glib::Error> error2;
        if(!manager->add(youtube_chat_protocol, &error2)) {
            auto* err = reinterpret_cast<GError*>(static_cast<glib::Error*>(error2));
            *error = g_error_copy(err);
            return false;
        }
    }

    return true;
}

static
gboolean youtube_chat_unload(GPluginPlugin*, gboolean, GError** error)
{
    if(!youtube_chat_protocol) {
        g_set_error_literal(error, YOUTUBE_CHAT_ERROR, 1, "Plugin was not setup properly");
        return false;
    }

    auto* manager = purple::Core::get_default()->get_protocol_manager();
    if(manager) {
        peel::UniquePtr<glib::Error> error2;
        if(!manager->remove(youtube_chat_protocol, &error2)) {
            auto* err = reinterpret_cast<GError*>(static_cast<glib::Error*>(error2));
            *error = g_error_copy(err);
            return false;
        }
    }
    return true;
}

// Ensure the exported functions are not mangled
extern "C" {
GPLUGIN_NATIVE_PLUGIN_DECLARE(youtube_chat)
}
