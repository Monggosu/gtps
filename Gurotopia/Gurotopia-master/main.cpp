/*
    @copyright gurotopia (c) 2024-05-25
    @version parent SHA: 313310dc606c852ed72b64e7e325a15eee3be42b 2026-11-3
*/
#include "include/pch.hpp"
#include "include/eventType/_eventType.hpp"

#include "include/database/shouhin.hpp" // @include parse_store()
#include "include/https/https.hpp" // @include https::listener()
#include "include/https/server_data.hpp" // @include gServer_data
#include "include/database/database.hpp"
#include "include/automate/holiday.hpp"
#include "include/tools/sentinel.hpp"
#include "include/tools/terminal.hpp"

#include <csignal>
#include <thread>

namespace
{
    volatile std::sig_atomic_t gSignal = 0;
}
static void signal_handler(int signal) { gSignal = signal; }

int main()
{
    /* @note installed FIRST: this must protect every step below, not just the main loop */
    sentinel::install_crash_handler();
    std::fprintf(stderr, "STAGE: crash handler installed\n");

    std::signal(SIGINT, signal_handler);
#ifdef SIGHUP // @note unix
    std::signal(SIGHUP, signal_handler); // @note PuTTY, SSH problems
#endif

    mysql_library_init(0, NULL, NULL);
    enet_initialize();
    std::fprintf(stderr, "STAGE: enet initialized, creating host...\n");
    {
        gServer_data.init(); // @note ./server_data.php
        std::thread(&https::listener).detach();

        ENetAddress address{
            .type = ENET_ADDRESS_TYPE_IPV4, 
            .port = gServer_data.port
        };
        host = enet_host_create (ENET_ADDRESS_TYPE_IPV4, &address, 50ull/* max peer count */, 2ull, 0u, 0u);
    } // @note delete address
    if (host == nullptr) { std::fprintf(stderr, "FATAL: enet_host_create failed (is port %u already in use?)\n", gServer_data.port); return EXIT_FAILURE; }
    host->usingNewPacketForServer = true;
    host->checksum = enet_crc32;
    enet_host_compress_with_range_coder(host);
    std::fprintf(stderr, "STAGE: enet host ready, connecting to database...\n");

    db.load_login(); // @note ./mysql_login.txt
    db.mysql_connect();
    std::fprintf(stderr, "STAGE: database connected, parsing items.dat...\n");

    decode_items_dat();
    std::fprintf(stderr, "STAGE: items.dat parsed, parsing store...\n");
    parse_store();
    std::fprintf(stderr, "STAGE: store parsed, checking holidays...\n");
    check_for_holiday();
    std::fprintf(stderr, "STAGE: holiday check done, sentinel startup...\n");

    sentinel::startup();
    std::fprintf(stderr, "STAGE: sentinel ready, entering main loop.\n");

    ENetEvent event{};
    while (!gSignal)
    {
        while (enet_host_service(host, &event, 1000/*ms*/) > 0)
        {
            try { eventType_pool[event.type](event); }
            catch (const std::exception &e) { print(std::string("handler exception: ") + e.what()); }
            sentinel::tick();
        }
        sentinel::tick(); // @note also runs when nobody is sending anything
    }

    safe_disconnect_peers(gSignal);
    enet_host_destroy(host);
    if (host != nullptr) host = nullptr;
    enet_deinitialize();
    mysql_library_end();

    print("killed gurotopia! [EXIT_SUCCESS]");
    return EXIT_SUCCESS;
}