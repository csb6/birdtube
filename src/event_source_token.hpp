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

#include <glib.h>

class EventSourceToken {
public:
    EventSourceToken()
        : source_id(0) {}
    explicit
    EventSourceToken(guint source_id)
        : source_id(source_id) {}
    EventSourceToken(const EventSourceToken&) = delete;
    EventSourceToken(EventSourceToken&& other) noexcept
        : source_id(other.source_id)
    {
        other.source_id = 0;
    }
    ~EventSourceToken() noexcept
    {
        disconnect();
    }
    EventSourceToken& operator=(const EventSourceToken&) = delete;
    EventSourceToken& operator=(EventSourceToken&& other) noexcept
    {
        disconnect();
        source_id = other.source_id;
        other.source_id = 0;
        return *this;
    }
    EventSourceToken& operator=(guint new_source_id) noexcept
    {
        disconnect();
        source_id = new_source_id;
        return *this;
    }
    void disconnect() noexcept
    {
        if(source_id) {
            if(!g_source_remove(source_id)) {
                g_warning("Failed to remove event source: %d", source_id);
            }
            source_id = 0;
        }
    }
private:
    guint source_id;
};
