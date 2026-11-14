#include "pch.hpp"

#include <algorithm>

#include "tools/create_dialog.hpp"
#include "tools/sentinel.hpp"
#include "tools/staff.hpp"

#include "sentinel_ui.hpp"

namespace
{
    create_dialog themed() // @note dark teal window
    {
        create_dialog dialog;
        dialog.set_bg_color(10, 46, 50, 232).set_border_color(90, 230, 200, 255).set_default_color("`o");
        return dialog;
    }

    std::string rule_text(const std::string &rule)
    {
        if (rule == "bounds")    return "koordinat di luar dunia";
        if (rule == "ownership") return "memakai item yang tidak dimiliki";
        if (rule == "range")     return "aksi dari jarak mustahil";
        if (rule == "teleport")  return "berpindah terlalu jauh";
        if (rule == "flood")     return "banjir paket";
        if (rule == "malformed") return "paket rusak";
        if (rule == "exception") return "membuat server error";
        if (rule == "rate")      return "terlalu cepat";
        if (rule == "regular")   return "irama mesin";
        if (rule == "nonstop")   return "farming tanpa henti";
        if (rule == "sweep")     return "pola sapuan bot";
        if (rule == "spam")      return "spam chat";
        if (rule == "crash")     return "server mati mendadak";
        return rule;
    }
}

void sentinel_open(ENetEvent& event, const std::string &notice)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->role < GUARDIAN) return;

    const long long brake = sentinel::brake_left();
    const bool shadow = sentinel::shadow_active();
    const sentinel::today today = sentinel::counts_today();

    create_dialog dialog = themed();
    dialog.add_label_with_icon("big", "`wSentinel``", 32).add_spacer("small");
    if (!notice.empty()) dialog.add_textbox(notice).add_spacer("small");

    if (brake > 0)
        dialog.add_textbox(std::format("`4MERAH: rem darurat aktif`` ({} lagi). Terlalu banyak hukuman dalam waktu singkat, jadi hanya kick sampai dilepas. Periksa tabel di bawah: mungkin ada aturan yang salah.", fmt_duration(brake)));
    else if (shadow)
        dialog.add_textbox(std::format("`6KUNING: mode pengamatan`` ({} lagi). Sinyal lemah (irama, sapuan, jeda) hanya dicatat, belum menghukum. Bukti pasti tetap langsung dihukum.", fmt_duration(sentinel::shadow_left())));
    else
        dialog.add_textbox("`2HIJAU: semua normal.`` Sistem aktif penuh.");

    dialog.add_spacer("small");
    dialog.add_textbox(std::format("Tingkat: `w{}``. Hari ini: `w{}`` dihukum karena bukti pasti, `w{}`` ban, `w{}`` sinyal lemah tercatat.",
        sentinel::level_name(sentinel::g_level), today.proofs, today.bans, today.softs));
    dialog.add_spacer("small");

    const std::vector<sentinel::entry> list = sentinel::recent(8);
    if (list.empty()) dialog.add_textbox("`wBelum ada catatan.`` Itu berarti belum ada yang mencurigakan.");
    else
    {
        dialog.add_textbox("`wKejadian terakhir:``");
        const long long now = static_cast<long long>(std::time(nullptr));
        for (const sentinel::entry &e : list)
            dialog.add_smalltext(std::format("{}{} `w{}``: {} -> {}{}``", e.proof ? "`4" : (e.shadow ? "`8" : "`6"),
                fmt_duration(now - e.ts) + " lalu", e.growid, rule_text(e.rule), e.action, e.detail.empty() ? "" : " (" + e.detail + ")"));
    }

    dialog.add_spacer("small").add_button("refresh", "`wSegarkan``");
    if (pPeer->role >= ADMINISTRATOR)
    {
        dialog.add_spacer("small").add_textbox("Ubah tingkat kegalakan:");
        for (int i = 0; i < 3; ++i)
        {
            const sentinel::lvl l = static_cast<sentinel::lvl>(i);
            dialog.add_button(std::format("lvl_{}", i), std::format("{}{}", sentinel::level_name(l), sentinel::g_level == l ? " (aktif)" : ""));
        }
        if (brake > 0) dialog.add_button("release", "`2Lepas rem darurat``");
    }

    send_varlist(event.peer, { "OnDialogRequest", dialog.end_dialog("sentinel", "Tutup", "") });
}

void sentinel_click(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->role < GUARDIAN) return; // @note never trust the dialog, check again

    const std::string button = hPipe["buttonClicked"];
    if (button == "refresh") { sentinel_open(event); return; }

    if (pPeer->role < ADMINISTRATOR) return;
    if (button.starts_with("lvl_"))
    {
        const int i = std::clamp(std::atoi(button.c_str() + 4), 0, 2);
        sentinel::set_level(static_cast<sentinel::lvl>(i));
        staff_log("sentinel", "level", pPeer->growid, sentinel::level_name(static_cast<sentinel::lvl>(i)));
        sentinel_open(event, std::format("`2Tingkat diubah ke `w{}``.``", sentinel::level_name(static_cast<sentinel::lvl>(i))));
    }
    else if (button == "release")
    {
        sentinel::release_brake();
        staff_log("sentinel", "brake", pPeer->growid, "released");
        sentinel_open(event, "`2Rem darurat dilepas.``");
    }
}
