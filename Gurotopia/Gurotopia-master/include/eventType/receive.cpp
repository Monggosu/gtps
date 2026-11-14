#include "pch.hpp"

#include <cmath>

#include "action/__action.hpp"
#include "gamePacket/_gamePacket.hpp"
#include "tools/sentinel.hpp"

#include "receive.hpp"

namespace
{
    /* @brief passwords must never reach the console or a log file */
    std::string redact(std::string text)
    {
        for (const char *key : { "tankIDPass|", "ltoken|", "ltoken=" })
            for (std::size_t at = text.find(key); at != std::string::npos; at = text.find(key, at + 1))
            {
                const std::size_t from = at + std::char_traits<char>::length(key);
                const std::size_t to = text.find('\n', from);
                text.replace(from, (to == std::string::npos ? text.size() : to) - from, "***");
            }
        return text;
    }
}

void receive(ENetEvent& event) 
{
    struct release { ENetPacket *packet; ~release() { enet_packet_destroy(packet); } } cleanup{ event.packet }; // @note the packet is freed on every path

    const std::size_t size = event.packet->dataLength;
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (size == 0 || size > 16384)
    {
        sentinel::malformed(event, std::format("packet of {} bytes", size));
        return;
    }

    try
    {
        switch (event.packet->data[0ull])
        {
            case 2: case 3: 
            {
                if (size < 6 || size > 4096) // @note a text packet always has a 4 byte type and a closing 0
                {
                    sentinel::malformed(event, std::format("text packet of {} bytes", size));
                    return;
                }
                if (!sentinel::admit(event, 2)) return;

                std::string header = { 
                    reinterpret_cast<const char*>(event.packet->data + 4), 
                    size - 4 - sizeof('\0')
                };
                sentinel::remember_packet(pPeer, "text", static_cast<int>(size), 0, 0);
                print(redact(header));
                
                for (char &c : header) if (c == '\n') c = '|';
                const std::vector<std::string> pipes = readch(header, '|');
                if (pipes.size() < 2) break;
                
                std::string action{};
                if (pipes[0ull] == "protocol" || pipes[0ull] == "tankIDName" || pipes[0ull] == "requestedName")
                {
                    action = (pipes[0ull] == "requestedName") ? "tankIDName" : pipes[0ull];
                }
                else action = std::format("{}|{}", pipes[0ull], pipes[1ull]);

                if (const auto i = action_pool.find(action); i != action_pool.end())
                    i->second(event, header);
                break;
            }
            case 4: 
            {
                if (size < sizeof(::gamePacket)) break; // @note not a real game packet, ignore

                ::gamePacket gamePacket = make_gamePacket(event.packet->data);
                gamePacket.size = size - sizeof(::gamePacket); // @todo did i do this right? or check for flag ::EXTENDED

                const int klass = (gamePacket.type == 0x00) ? 0 :
                    (gamePacket.type == 0x03 || gamePacket.type == 0x07 || gamePacket.type == 0x0a || gamePacket.type == 0x0b) ? 1 : 3;
                if (!sentinel::admit(event, klass)) break;
                sentinel::remember_packet(pPeer, "game", gamePacket.type, static_cast<int>(gamePacket.id),
                    std::isfinite(gamePacket.punch.x) ? static_cast<int>(gamePacket.punch.x) : -999999);

                if (const auto i = gamePacket_pool.find(gamePacket.type); i != gamePacket_pool.end())
                    i->second(event, std::move(gamePacket));
                break;
            }
        }
    }
    catch (const std::exception &e) { sentinel::exception(event, e.what()); }
    catch (...) { sentinel::exception(event, "unknown exception"); }
}
