#include "pch.hpp"

#include "tools/sentinel.hpp"

#include "onVariant/EmoticonDataChanged.hpp"
#include "onVariant/Spawn.hpp"
#include "onVariant/BillboardChange.hpp"
#include "onVariant/SetClothing.hpp"
#include "onVariant/CountryState.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/TradeStatus.hpp"
#include "tools/time.hpp"
#include "tools/staff.hpp"
#include "tools/nametag.hpp"

#include "join_request.hpp"

#include <algorithm> // linux

namespace
{
    void world_lock_message(ENetEvent &event, const ::world &world)
    {
        if (world.owner <= 0) return;

        const ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        ::peer owner{.user_id = world.owner};
        
        const bool granted = (pPeer->user_id == world.owner || std::ranges::find(world.access, pPeer->user_id) != world.access.end());
        on::ConsoleMessage(event.peer, std::format("`5[```w{}`` `$World Locked`` by {}{}`5]``", world.name,
            owner.mysql_select<std::string>("growid", "", true), granted ? " (`2ACCESS GRANTED``)" : ""));
    }
}

void action::join_request(ENetEvent& event, const std::string &header, const std::string &world_name = "") 
{
    try 
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        ::hPipe hPipe{ header };

        send_varlist(event.peer, { "OnForceTradeEnd" }); // @note sent before OnSendToServer

        std::string name = (world_name.empty()) ? hPipe["name"] : world_name;

        if (name.length() > 24) throw std::runtime_error(""); // @note impossible unless using a proxy since client caps at 24
        if (!isalnum(name)) throw std::runtime_error("Sorry, spaces and special characters are not allowed in world or door names.  Try again.");

        for (char &c : name) c = std::toupper(c); // @note start -> START

        if (is_nuked(name) && pPeer->role < GUARDIAN) // @note guardian+ may still enter, to investigate
            throw std::runtime_error("`4That world is inaccessible.`` It was nuked by the staff.");
        
        auto it = std::ranges::find(worlds, name, &::world::name);
        if (it == worlds.end()) 
            it = worlds.emplace(worlds.end(), name);
            
        ::world &world = *it;
        {
            ::blob blob = compress_state(::gamePacket{ .type = 0x04, /*PACKET_SEND_MAP_DATA*/ .state = (int)gamePacketState::EXTENDED });
            blob.push_back(world.serialize());

            ENetPacket *packet = enet_packet_create(blob.data().data(), blob.size(), ENET_PACKET_FLAG_RELIABLE);
            if (enet_peer_send(event.peer, 0, packet)) enet_packet_destroy(packet);
        } // @note delete blob
        {
            std::string *this_world = std::ranges::find(pPeer->recent_worlds, world.name);
            std::string *end = pPeer->recent_worlds.end();
            std::string *first = this_world != end ? this_world : pPeer->recent_worlds.begin();

            std::rotate(first, first + 1, end);
            pPeer->recent_worlds.back() = world.name;
        } // @note delete name, first
        on::EmoticonDataChanged(event);

        pPeer->display_growid = build_display_name(*pPeer,
            (pPeer->user_id == world.owner) ? name_state::OWNER :
            (std::ranges::find(world.access, pPeer->user_id) != world.access.end()) ? name_state::ACCESS : name_state::NORMAL);

        pPeer->rest_pos = world.spawn;

        pPeer->netid = ++world.netid_counter;
        peers(pPeer->recent_worlds.back(), PeerSearch::WORLD, [&event, &pPeer, &world](ENetPeer *peer/*send to everyone in world*/) 
        {
            ::peer *pOthers = static_cast<::peer*>(peer->data); // @note everyone in world's peer.data
            
            if (pOthers->user_id != pPeer->user_id)
            {
                on::Spawn(*event.peer, pOthers->netid, pOthers->user_id, pOthers->pos, pOthers->display_growid, pOthers->country, pOthers->role >= GUARDIAN, pOthers->role >= ADMINISTRATOR, false);
                on::Spawn(*peer, pPeer->netid, pPeer->user_id, pPeer->rest_pos, pPeer->display_growid, pPeer->country, pPeer->role >= GUARDIAN, pPeer->role >= ADMINISTRATOR, false);
                on::SetClothing(peer);
                on::ConsoleMessage(peer, std::format("`5<{} entered, `w{}`` others here>``", pPeer->display_growid, world.visitors));
                send_varlist(peer, {
                    "OnTalkBubble",
                    pPeer->netid,
                    std::format("`5<{} entered, `w{}`` others here>``", pPeer->display_growid, world.visitors),
                    1u
                });
            }
        });
        on::Spawn(*event.peer, pPeer->netid, pPeer->user_id, pPeer->rest_pos, pPeer->display_growid, pPeer->country, pPeer->role >= GUARDIAN, pPeer->role >= ADMINISTRATOR, true);

        if (pPeer->billboard.id != 0) on::BillboardChange(event); // @note don't waste memory if billboard is empty.

        sentinel::grace(*pPeer); // @note the server moves the player now, do not treat it as a teleport
        send_varlist(event.peer, {
            "OnSetPos", 
            CL_Vec2f{pPeer->rest_pos.x, pPeer->rest_pos.y}
        }, pPeer->netid);

        on::ConsoleMessage(event.peer, 
            std::format(
                "World `w{}`` entered.  There are `w{}`` other people here, `w{}`` online.", 
                world.name, world.visitors, host->connectedPeers
            )
        );
        world_lock_message(event, world);
        ++world.visitors;
        on::SetClothing(event.peer);
        on::CountryState(event);
        if (pPeer->role >= GUARDIAN && g_staff_zoom > 0.0f) // @note staff see more of the world, find the number with /zoom
            send_varlist(event.peer, { "OnZoomCamera", g_staff_zoom, 1000u });

        send_varlist(event.peer, {
            "OnSetFeatureEnableFlags",
            "EA4PAgcGAQoDEAQRBQ0JCAw="
        });
        peers(world.name, PeerSearch::WORLD, [&](ENetPeer *peer)
        {
            const ::peer *pOthers = static_cast<::peer*>(peer->data);
            if (pOthers->user_id != pPeer->user_id)
                send_varlist(event.peer, { "OnTalkBubble", static_cast<u_int>(pOthers->netid), pOthers->growid, 1u });
        });
    }
    catch (const std::exception& exc)
    {
        on::ConsoleMessage(event.peer, exc.what()); // @note tell the player why
        send_varlist(event.peer, { "OnFailedToEnterWorld" });
        return;
    }
}
