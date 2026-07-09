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
#include "youtube_chat_protocol.hpp"
#include <peel/Gio/Cancellable.h>
#include <peel/Gio/NetworkMonitor.h>
#include <peel/Gio/Task.h>
#include <peel/Purple/Account.h>
#include <peel/Purple/AccountSettingString.h>
#include <peel/Purple/Badge.h>
#include <peel/Purple/Badges.h>
#include <peel/Purple/BadgeManager.h>
#include <peel/Purple/ConversationManager.h>
#include <peel/Purple/ConversationType.h>
#include <peel/Purple/Core.h>
#include <peel/Purple/Message.h>
#include "youtube_chat_connection.hpp"

namespace youtube {

PEEL_CLASS_IMPL_DYNAMIC(Protocol, "YoutubeProtocol", purple::Protocol)

peel::RefPtr<Protocol> Protocol::create()
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

peel::RefPtr<purple::AccountSettings> Protocol::vfunc_get_default_account_settings()
{
    auto account_settings = purple::AccountSettings::create();

    auto access_token_expiration = purple::AccountSettingString::create(
        "access_token_expiration", "OAuth Access Token Expiration", "");
    access_token_expiration->set_advanced(true);
    account_settings->add_setting(std::move(access_token_expiration));

    return account_settings;
}

bool Protocol::vfunc_can_connect(purple::Account*)
{
    return gio::NetworkMonitor::get_default()->get_network_available();
}

peel::RefPtr<purple::Connection> Protocol::vfunc_create_connection(
    purple::Account* account, peel::UniquePtr<glib::Error>*)
{
    return youtube::Connection::create(account);
}

peel::RefPtr<purple::ChannelJoinDetails> Protocol::vfunc_get_channel_join_details(purple::Account*)
{
    return purple::ChannelJoinDetails::create(
        /*name_max_length=*/0,
        /*nickname_supported=*/false, /*nickname_max_length=*/0,
        /*password_supported=*/false, /*password_max_length=*/0
    );
}

Task<peel::RefPtr<purple::Conversation>> Protocol::vfunc_join_channel_async(
    purple::Account* account, purple::ChannelJoinDetails* channel_details, gio::Cancellable* cancellable)
{
    using ConvType = purple::ConversationType;

    // Add moderator badge if there isn't one already
    auto* core = purple::Core::get_default();
    auto* badge_manager = core->get_badge_manager();
    if(auto* mod_badge = badge_manager->find("moderator"); !mod_badge) {
        auto badge_ptr = purple::Badge::create("moderator", 1, "pidgin-status-operator", "M");
        badge_manager->add(badge_ptr);
    }

    auto* connection = static_cast<youtube::Connection*>(account->get_connection());
    auto* stream_url = channel_details->get_name();
    auto error = co_await connection->connect_to_chat_async(stream_url, cancellable);
    if(error) {
        co_return std::unexpected(std::move(error));
    }
    auto* conversation_manager = core->get_conversation_manager();
    auto channel_id = connection->get_channel_id();
    if(auto* conversation = conversation_manager->find(account, ConvType::CHANNEL, channel_id)) {
        // These fields might have changed since we last joined this channel, so refresh them
        conversation->set_online(true);
        conversation->set_title(connection->get_title());
        conversation->set_topic(stream_url);
        co_return conversation;
    }

    auto conversation = purple::Conversation::create(account, ConvType::CHANNEL, channel_id);
    conversation->set_online(true);
    conversation->set_title(connection->get_title());
    conversation->set_topic(stream_url);
    // TODO: set contact info of this account's user in the conversation
    conversation_manager->add(conversation);
    co_return conversation;
}

Task<void> Protocol::vfunc_leave_conversation_async(purple::Conversation* conversation, gio::Cancellable*)
{
    auto* connection = static_cast<youtube::Connection*>(conversation->get_connection());
    connection->disconnect_chat();
    co_return {};
}

Task<void> Protocol::vfunc_send_message_async(
    purple::Conversation* conversation, purple::Message* message, gio::Cancellable* cancellable)
{
    auto* connection = static_cast<youtube::Connection*>(conversation->get_connection());
    // TODO: eventually need to stringify the message contents better
    auto error = co_await connection->send_message_async(message->get_contents(), cancellable);
    co_return error;
}

Task<void> Protocol::vfunc_refresh_async(purple::Conversation* conversation, gio::Cancellable* cancellable)
{
    auto* connection = static_cast<youtube::Connection*>(conversation->get_connection());
    if(connection->is_connected_to_chat()) {
        // Nothing to do if we are already connected
        co_return {};
    }

    // Rejoin the channel
    auto* account = connection->get_account();
    auto channel_join_details = this->vfunc_get_channel_join_details(account);
    channel_join_details->set_name(conversation->get_topic());
    auto result = co_await this->vfunc_join_channel_async(account, channel_join_details, cancellable);
    if(result.has_value()) {
        co_return {};
    } else {
        co_return std::move(result.error());
    }
}

void Protocol::Class::init()
{
    override_vfunc_create_connection<Protocol>();
    override_vfunc_get_default_account_settings<Protocol>();
    auto* klass = reinterpret_cast<PurpleProtocolClass*>(this);
    klass->can_connect_async = [](PurpleProtocol* protocol, PurpleAccount* account, GCancellable* cancellable,
                                  GAsyncReadyCallback callback, gpointer data) {
        auto* self = reinterpret_cast<youtube::Protocol*>(protocol);
        auto* task = reinterpret_cast<gio::Task*>(g_task_new(protocol, cancellable, callback, data));
        auto* _peel_account = reinterpret_cast<purple::Account*>(account);
        task->return_boolean(self->vfunc_can_connect(_peel_account));
    };
    klass->can_connect_finish = [](PurpleProtocol*, GAsyncResult* result, GError** error) {
        return g_task_propagate_boolean(G_TASK(result), error);
    };
}

void Protocol::init_type(gobject::TypeModule* type_module, peel::Type type)
{
    PEEL_IMPLEMENT_INTERFACE_DYNAMIC(type_module, type, purple::ProtocolConversation);
}

void Protocol::init_interface(purple::ProtocolConversation::Iface* iface)
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
            [](youtube::Protocol* self, purple::Account* account, purple::ChannelJoinDetails* details,
                gio::Task* task) -> VoidTask {
                auto conversation = co_await self->vfunc_join_channel_async(
                    account, details, task->get_cancellable());
                if(conversation.has_value()) {
                    task->return_pointer(std::move(conversation.value()).release_ref(), g_object_unref);
                } else {
                    task->return_error(conversation.error()->copy());
                }
            }(self, _peel_account, _peel_details, task).start();
        };
    iface_class->join_channel_finish =
        [](PurpleProtocolConversation*, GAsyncResult* result, GError** error) {
            return (PurpleConversation*)g_task_propagate_pointer(G_TASK(result), error);
        };
    iface_class->leave_conversation_async =
        [](PurpleProtocolConversation* protocol, PurpleConversation* conversation, GCancellable* cancellable,
            GAsyncReadyCallback callback, gpointer data) {
            auto* self = reinterpret_cast<youtube::Protocol*>(protocol);
            auto* task = reinterpret_cast<gio::Task*>(g_task_new(protocol, cancellable, callback, data));
            auto* _peel_conversation = reinterpret_cast<purple::Conversation*>(conversation);
            [](youtube::Protocol* self, purple::Conversation* conversation, gio::Task* task) -> VoidTask {
                auto error = co_await self->vfunc_leave_conversation_async(
                    conversation, task->get_cancellable());
                if(error) {
                    task->return_error(error->copy());
                } else {
                    task->return_boolean(true);
                }
            }(self, _peel_conversation, task).start();
        };
    iface_class->leave_conversation_finish = [](PurpleProtocolConversation*, GAsyncResult* result, GError** error) {
        return g_task_propagate_boolean(G_TASK(result), error);
    };
    iface_class->send_message_async =
        [](PurpleProtocolConversation* protocol, PurpleConversation* conversation, PurpleMessage* message,
            GCancellable* cancellable, GAsyncReadyCallback callback, gpointer data) {
            auto* self = reinterpret_cast<youtube::Protocol*>(protocol);
            auto* task = reinterpret_cast<gio::Task*>(g_task_new(protocol, cancellable, callback, data));
            auto* _peel_conversation = reinterpret_cast<purple::Conversation*>(conversation);
            auto* _peel_message = reinterpret_cast<purple::Message*>(message);
            [](youtube::Protocol* self, purple::Conversation* conversation, purple::Message* message,
                gio::Task* task) -> VoidTask {
                auto error = co_await self->vfunc_send_message_async(
                    conversation, message, task->get_cancellable());
                if(error) {
                    task->return_error(error->copy());
                } else {
                    task->return_boolean(true);
                }
            }(self, _peel_conversation, _peel_message, task).start();
        };
    iface_class->send_message_finish = [](PurpleProtocolConversation*, GAsyncResult* result, GError** error) {
        return g_task_propagate_boolean(G_TASK(result), error);
    };
    iface_class->refresh_async = [](PurpleProtocolConversation* protocol, PurpleConversation* conversation,
                                    GCancellable* cancellable, GAsyncReadyCallback callback, gpointer data) {
        auto* self = reinterpret_cast<youtube::Protocol*>(protocol);
        auto* task = reinterpret_cast<gio::Task*>(g_task_new(protocol, cancellable, callback, data));
        auto* _peel_conversation = reinterpret_cast<purple::Conversation*>(conversation);
        [](youtube::Protocol* self, purple::Conversation* conversation, gio::Task* task) -> VoidTask {
            auto error = co_await self->vfunc_refresh_async(conversation, task->get_cancellable());
            if(error) {
                task->return_error(error->copy());
            } else {
                task->return_boolean(true);
            }
        }(self, _peel_conversation, task).start();
    };
    iface_class->refresh_finish = [](PurpleProtocolConversation*, GAsyncResult* result, GError** error) {
        return g_task_propagate_boolean(G_TASK(result), error);
    };
}

} // namespace youtube
