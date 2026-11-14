#include "pch.hpp"

#include "action/respawn.hpp"
#include "tools/sentinel.hpp"

#include "movement.hpp"

void movement(ENetEvent& event, ::gamePacket gamePacket) 
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->netid == 0) return; // @note not inside a world: nothing to move

    /* @note the server decides what is possible: coordinates inside the world, no teleporting */
    if (!sentinel::valid_position(event, gamePacket.pos) || !sentinel::movement_ok(event, gamePacket.pos)) return;
    sentinel::record(event, sentinel::MOVE, static_cast<int>(gamePacket.pos.x / 32.0f), static_cast<int>(gamePacket.pos.y / 32.0f), static_cast<int>(gamePacket.state));

    pPeer->pos = gamePacket.pos;
    pPeer->facing_left = gamePacket.state & (int)gamePacketState::MOVE_LEFT;

    /* add fireproof only take away 1 hp instead of 2 */
    if (gamePacket.state & (int)gamePacketState::HIT_BY_LAVA) pPeer->pain_hp -= 2;
    if (pPeer->pain_hp <= 0) action::respawn(event, ""), pPeer->pain_hp = 10;
    
    gamePacket.netid = pPeer->netid;
    send_gamePacket(*event.peer, gamePacket); // finished.
}
