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
#include "one_shot_server.hpp"
#include <peel/Soup/ServerListenOptions.h>
#include "youtube_error.h"

/* Awaiter for server listen() async operation */
class ServerListenResult {
public:
    constexpr bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept { handle = h; }
    void await_resume() {}

    auto callback()
    {
        return [this](soup::Server*, soup::ServerMessage* msg, const char*, glib::HashTable* query) {
            this->msg = msg;
            this->query = query;
            assert(handle);
            handle.resume();
        };
    }
    soup::ServerMessage* msg = nullptr;
    glib::HashTable* query = nullptr;
private:
    std::coroutine_handle<> handle;
};

/* Awaiter for server response (i.e. back to user's browser) async operation */
class ServerSendResponseResult {
public:
    constexpr bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept { handle = h; }
    void await_resume() {}

    auto callback()
    {
        return [](soup::ServerMessage*) {
            // If browser doesn't accept our response then not really
            // any reason to emit an error. The user already approved the
            // OAuth authorization and the client will continue from there.
        };
    }
private:
    std::coroutine_handle<> handle;
};

PEEL_CLASS_IMPL(OneShotServer, "OneShotServer", gobject::Object)

void OneShotServer::Class::init()
{}

void OneShotServer::init(Class*)
{
    server = peel::RefPtr<soup::Server>::adopt_ref(reinterpret_cast<soup::Server*>(
        soup_server_new("server-header", "PurpleYoutube", nullptr)));
}

peel::RefPtr<OneShotServer> OneShotServer::create()
{
    return Object::create<OneShotServer>();
}

Task<peel::RefPtr<glib::HashTable>> OneShotServer::listen(unsigned port)
{
    ServerListenResult result;
    peel::UniquePtr<glib::Error> error;
    this->server->add_handler("/", result.callback());
    // TODO: timeout for server?
    this->server->listen_local(port, soup::ServerListenOptions::IPV4_ONLY, &error);
    if(error) {
        co_return std::unexpected(std::move(error));
    }

    co_await result;
    this->msg = result.msg;
    this->msg->pause();
    peel::RefPtr query = result.query;
    this->server->remove_handler("/");
    co_return query;
}

Task<void> OneShotServer::respond(soup::Status status, soup::MemoryUse mem_use,
                                  peel::ArrayRef<const uint8_t> content)
{
    this->msg->set_status((unsigned)status, nullptr);
    this->msg->set_response("text/html", mem_use, content);
    ServerSendResponseResult result;
    this->msg->connect_finished(result.callback());
    this->msg->unpause();
    co_await result;
    co_return {};
}

Task<void> OneShotServer::respond(soup::Status status, char* content)
{
    return respond(status, soup::MemoryUse::TAKE, {(uint8_t*)content, strlen(content)});
}
