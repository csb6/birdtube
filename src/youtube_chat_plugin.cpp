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
#include <peel/Gio/Task.h>
#include <peel/Purple/Account.h>
#include <peel/Purple/Conversation.h>
#include <peel/Purple/ConversationManager.h>
#include "peel/Purple/ConversationType.h"
#include <peel/Purple/Core.h>
#include <peel/Purple/ChannelJoinDetails.h>
#include <peel/Purple/Message.h>
#include <peel/Purple/Protocol.h>
#include <peel/Purple/ProtocolManager.h>
#include <peel/Purple/ProtocolConversation.h>
#include "task.hpp"
#include "youtube_chat_connection.hpp"
#include "youtube_error.h"

namespace youtube {

class Protocol final : public purple::Protocol {
    PEEL_DYNAMIC_CLASS(Protocol, purple::Protocol)
public:
    static void init_type(peel::Type type)
    {
        PEEL_IMPLEMENT_INTERFACE(type, purple::ProtocolConversation);
    }

    static void init_interface(purple::ProtocolConversation::Iface* iface)
    {
        iface->override_vfunc_get_channel_join_details<Protocol>();
        // TODO: unsupported currently in peel. Overriding manually for now
        auto* iface_class = reinterpret_cast<PurpleProtocolConversationInterface*>(iface);
        iface_class->join_channel_async =
            [](PurpleProtocolConversation* protocol, PurpleAccount* account, PurpleChannelJoinDetails* details,
               GCancellable* cancellable, GAsyncReadyCallback callback, gpointer data) {
                auto* self = reinterpret_cast<youtube::Protocol*>(protocol);
                auto* task = reinterpret_cast<gio::Task*>(g_task_new(protocol, cancellable, callback, data));
                auto* _peel_account = reinterpret_cast<purple::Account*>(account);
                auto* _peel_details = reinterpret_cast<purple::ChannelJoinDetails*>(details);
                auto* _peel_cancellable = reinterpret_cast<gio::Cancellable*>(cancellable);
                [](youtube::Protocol* self,
                   purple::Account* account, purple::ChannelJoinDetails* details,
                   gio::Cancellable* cancellable, gio::Task* task) -> VoidTask {
                        auto conversation = co_await self->vfunc_join_channel_async(account, details, cancellable);
                        if(conversation.has_value()) {
                            task->return_pointer(std::move(conversation.value()).release_ref(), g_object_unref);
                        } else {
                            task->return_error(conversation.error()->copy());
                        }
                }(self, _peel_account, _peel_details, _peel_cancellable, task).start();
            };
        iface_class->join_channel_finish =
            [](PurpleProtocolConversation*, GAsyncResult* result, GError** error) {
                return (PurpleConversation*)g_task_propagate_pointer(G_TASK(result), error);
            };
        //iface->override_vfunc_leave_conversation_async<Protocol>();
        //iface->override_vfunc_leave_conversation_finish<Protocol>();
        iface_class->send_message_async =
            [](PurpleProtocolConversation* protocol, PurpleConversation* conversation, PurpleMessage* message,
               GCancellable* cancellable, GAsyncReadyCallback callback, gpointer data) {
                auto* self = reinterpret_cast<youtube::Protocol*>(protocol);
                auto* task = reinterpret_cast<gio::Task*>(g_task_new(protocol, cancellable, callback, data));
                auto* _peel_conversation = reinterpret_cast<purple::Conversation*>(conversation);
                auto* _peel_message = reinterpret_cast<purple::Message*>(message);
                auto* _peel_cancellable = reinterpret_cast<gio::Cancellable*>(cancellable);
                [](youtube::Protocol* self, purple::Conversation* conversation, purple::Message* message,
                   gio::Cancellable* cancellable, gio::Task* task) -> VoidTask {
                    auto error = co_await self->vfunc_send_message_async(conversation, message, cancellable);
                    if(error) {
                        task->return_error(error->copy());
                    } else {
                        task->return_boolean(true);
                    }
                }(self, _peel_conversation, _peel_message, _peel_cancellable, task).start();
            };
        iface_class->send_message_finish =
            [](PurpleProtocolConversation*, GAsyncResult* result, GError** error) {
                return g_task_propagate_boolean(G_TASK(result), error);
            };
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

    peel::RefPtr<purple::Connection> vfunc_create_connection(purple::Account* account, peel::UniquePtr<glib::Error>*)
    {
        return youtube::Connection::create(account);
    }

    peel::RefPtr<purple::ChannelJoinDetails> vfunc_get_channel_join_details(purple::Account*)
    {
        return purple::ChannelJoinDetails::create(
            /*name_max_length=*/0,
            /*nickname_supported=*/false, /*nickname_max_length=*/0,
            /*password_supported=*/false, /*password_max_length=*/0
        );
    }

    Task<peel::RefPtr<purple::Conversation>> vfunc_join_channel_async(
        purple::Account* account, purple::ChannelJoinDetails*, gio::Cancellable* cancellable)
    {
        using ConvType = purple::ConversationType;

        auto* connection = static_cast<youtube::Connection*>(account->get_connection());
        auto error = co_await connection->vfunc_connect_async(cancellable);
        if(error) {
            co_return std::unexpected(std::move(error));
        }
        auto* conversation_manager = purple::Core::get_default()->get_conversation_manager();
        auto channel_id = connection->get_channel_id();
        if(auto* conversation = conversation_manager->find(account, ConvType::CHANNEL, channel_id)) {
            conversation->set_title(connection->get_title());
            co_return conversation;
        }

        auto conversation = purple::Conversation::create(account, ConvType::CHANNEL, channel_id);
        conversation->set_online(false);
        conversation->set_title(connection->get_title());
        conversation_manager->add(conversation);
        co_return conversation;
    }

    Task<void> vfunc_send_message_async(purple::Conversation* conversation, purple::Message* message,
                                        gio::Cancellable*)
    {
        auto* connection = static_cast<youtube::Connection*>(conversation->get_connection());
        // TODO: eventually need to stringify the message contents better
        auto error = co_await connection->send_message_async(message->get_contents());
        co_return error;
    }
};

PEEL_CLASS_IMPL_DYNAMIC(Protocol, "YoutubeProtocol", purple::Protocol)

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
gboolean youtube_chat_load(GPluginPlugin* plugin, GError** error)
{
    if(youtube_chat_protocol) {
        g_set_error_literal(error, YOUTUBE_CHAT_ERROR, 1, "Plugin was not cleaned up properly");
        return false;
    }

    youtube::Connection::register_dynamic_type(G_TYPE_MODULE(plugin));
    youtube::Protocol::register_dynamic_type(G_TYPE_MODULE(plugin));

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
    youtube_chat_protocol = {};
    return true;
}

// Ensure the exported functions are not mangled
extern "C" {
GPLUGIN_NATIVE_PLUGIN_DECLARE(youtube_chat)
}
