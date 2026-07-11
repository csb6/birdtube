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
#include <glib.h>
#include <glib-object.h>
#include <gplugin.h>
#include <gplugin-native.h>
#include <peel/GLib/Error.h>
#include <peel/GObject/TypeModule.h>
#include <peel/Purple/Core.h>
#include <peel/Purple/ProtocolManager.h>
#include "youtube_chat_connection.hpp"
#include "youtube_chat_protocol.hpp"
#include "youtube_error.h"

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
        "id", "csb6/birdtube",
        "name", "BirdTube",
        "authors", authors,
        "version", "0.1",
        "category", "Protocol",
        "summary", "YouTube Live Chat Protocol Plugin",
        "description", "Support for YouTube Live chats",
        "website", "https://github.com/csb6/birdtube",
        "abi-version", PURPLE_ABI_VERSION,
        "flags", flags,
        nullptr);
}

static
gboolean youtube_chat_load(GPluginPlugin* plugin, GError** error)
{
    if(youtube_chat_protocol) {
        g_set_error_literal(error, YOUTUBE_CHAT_ERROR, 1, "Plugin was not cleaned up properly");
        return false;
    }

    youtube::Connection::register_type_dynamic(reinterpret_cast<gobject::TypeModule*>(plugin));
    youtube::Protocol::register_type_dynamic(reinterpret_cast<gobject::TypeModule*>(plugin));

    auto* manager = purple::Core::get_default()->get_protocol_manager();
    youtube_chat_protocol = youtube::Protocol::create();
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
    youtube_chat_protocol = nullptr;
    return true;
}

// Ensure the exported functions are not mangled
extern "C" {
GPLUGIN_NATIVE_PLUGIN_DECLARE(youtube_chat)
}
