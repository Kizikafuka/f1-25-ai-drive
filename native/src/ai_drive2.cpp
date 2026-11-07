// SPDX-License-Identifier: MIT
// AI Drive (id kizikafuka.ai-drive, version 1.0.0 and up): the AI drives your car. Only that: no spectator HUD, no web panel, no telemetry port.
// Built on the Swofty 1.6 standard library: sessions::set_control hands the car to the AI and back, the event
// car.controllers.rebuilt (after a restart, flashback or return to the garage) makes it take the car again, and
// Every step is logged (lines "[ai-drive]").
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <string>
#include <string_view>

#include "kizi_scan.hpp"

#include <swofty/events.hpp>
#include <swofty/systems/cars.hpp>
#include <swofty/hud.hpp>
#include <swofty/mixin.hpp>
#include <swofty/systems/sessions.hpp>

#define ADDON_STR2(x) #x
#define ADDON_STR(x) ADDON_STR2(x)
#ifdef ADDON_VERSION_RAW
#define ADDON_VERSION ADDON_STR(ADDON_VERSION_RAW)
#else
#define ADDON_VERSION "?"
#endif
#define TAG "ai-drive"

SWOFTY_SETTING(bool, no_qte, true);
SWOFTY_SETTING(bool, ai_setup, true);
SWOFTY_SETTING(std::string_view, hotkey, "F5");
SWOFTY_SETTING(std::string_view, handback, "3");
SWOFTY_SETTING(bool, from_start, false);
SWOFTY_SETTING(bool, beeps, true);

namespace {

#define LOG(...) swofty::log("[" TAG "] " __VA_ARGS__)

double now()
{
    return double(GetTickCount64()) / 1000.0;
}

bool game_in_front()
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

int key_vk(std::string_view k)
{
    if (k.size() >= 2 && k[0] == 'F') {
        int n = std::atoi(std::string(k.substr(1)).c_str());
        if (n >= 1 && n <= 12)
            return VK_F1 + n - 1;
    }
    return 0;   // "none"
}

const char *rname(swofty::sessions::Result r)
{
    using R = swofty::sessions::Result;
    return r == R::ok ? "ok" : r == R::late ? "late" : r == R::unavailable ? "unavailable" : r == R::blocked ? "blocked" : "invalid";
}

std::atomic<int> g_player{-1};   // your vehicle index, learnt from the session events

void note_player(const swofty::events::Context &ctx, const char *event)
{
    const auto &p = ctx.player();
    if (!p)
        return;
    int car = int(p->car);
    if (g_player.exchange(car) != car)
        LOG("your car is vehicle %d (learnt at %s)", car, event);
}

void clear_player(const char *event)
{
    if (g_player.exchange(-1) != -1)
        LOG("%s: session over, your car forgotten", event);
}

void forget_session(const char *event);   // below, with the AI state it clears

void watch_player_events()
{
    for (const char *pt : {"session.start", "race.start", "session.state.enter", "session.grid", "session.formation_lap"}) {
        std::string name = pt;
        auto r = swofty::events::on(pt).after([name](std::span<const std::byte>, const swofty::events::Context &ctx) {
            note_player(ctx, name.c_str());
        });
        if (r != swofty::events::Result::ok)
            LOG("event %s: %s", pt, swofty::events::name(r));
    }
    for (const char *pt : {"session.end", "session.unload"}) {
        std::string name = pt;
        auto r = swofty::events::on(pt).after([name](std::span<const std::byte>, const swofty::events::Context &) {
            clear_player(name.c_str());
            forget_session(name.c_str());
        });
        if (r != swofty::events::Result::ok)
            LOG("event %s: %s", pt, swofty::events::name(r));
    }
}

// ---- pit stop quick-time event ------------------------------------------------------------------------------------------
// With the game's "Immersive pit stops" setting the game runs a quick-time event (a countdown and a button press) in the pit
// lane for the player's car. When the AI drives, nobody presses, so the event is missed and the stop comes out late. The countdown
// starts in one block of the game's pit input code. A stub is put in front of it (ported from Manual Pit Lane, where it was
// confirmed in game): for your car, and only while the AI drives it (flag = 1), the stub jumps past the start of the countdown, so
// there is no countdown and the crew does a normal stop. In every other case the stub runs the game's own instructions.
uint8_t *g_noqte_flag = nullptr, *g_noqte_vid = nullptr;

bool install_no_qte()
{
    auto code = kizi::image_ranges(true);
    uintptr_t post2 = 0;
    int hits = 0;
    kizi::find_all(code,
                   kizi::Pattern("C5 7A 11 7C 24 54 C7 44 24 58 00 00 80 3F 48 8B 48 08 4C 89 F2 E8 ?? ?? ?? ?? C6 47 19 01"),
                   [&](uintptr_t a) {
                       post2 = a, hits++;
                       return true;
                   });
    if (hits != 1)
        return false;
    uintptr_t site = post2 - 0x5f, after = post2 + 30;
    const uint8_t *b = reinterpret_cast<const uint8_t *>(site);
    if (!kizi::Pattern("48 8B 05 ?? ?? ?? ?? 48 C7 44 24 48 00 00 00 00 48 8D 0D ?? ?? ?? ?? 48 89 4C 24 40 40 88 6C 24 50").at(b))
        return false;
    uint64_t bus = site + 7 + kizi::rel32(site + 3);
    uint8_t *m = kizi::alloc_near(site, 4096);
    if (!m)
        return false;
    uint8_t *flag = m + 0xf00, *vid = m + 0xf01, *p = m;
    auto put32 = [&](int32_t v) { memcpy(p, &v, 4), p += 4; };
    auto put64 = [&](uint64_t v) { memcpy(p, &v, 8), p += 8; };
    *p++ = 0x80, *p++ = 0x3D, put32(int32_t(flag - (p + 5))), *p++ = 0x00;   // cmp byte [rip+flag],0
    *p++ = 0x74, *p++ = 0x17;                                                // je orig
    *p++ = 0x40, *p++ = 0x3A, *p++ = 0x2D, put32(int32_t(vid - (p + 4)));    // cmp bpl,[rip+vid]
    *p++ = 0x75, *p++ = 0x0E;                                                // jne orig
    *p++ = 0xFF, *p++ = 0x25, put32(0), put64(after);                        // jmp past the countdown start
    *p++ = 0x48, *p++ = 0xB8, put64(bus);                                    // orig: mov rax,BUS
    *p++ = 0x48, *p++ = 0x8B, *p++ = 0x00;                                   //       mov rax,[rax]
    *p++ = 0xFF, *p++ = 0x25, put32(0), put64(site + 7);                     //       jmp back
    *flag = 0, *vid = 0xff;
    FlushInstructionCache(GetCurrentProcess(), m, 4096);
    g_noqte_flag = flag, g_noqte_vid = vid;
    uint8_t jump[7] = {0xE9, 0, 0, 0, 0, 0x90, 0x90};
    int32_t r = int32_t(reinterpret_cast<intptr_t>(m) - intptr_t(site + 5));
    memcpy(jump + 1, &r, 4);
    return kizi::patch_code(site, jump, 7);
}

DWORD WINAPI qte_thread(void *)
{
    bool ok = install_no_qte();
    LOG("pit stop quick-time event: %s", ok ? "removal installed (only active while the AI drives your car)" : "NOT found on this game build (the stop may come out late)");
    return 0;
}

// ---- AI setup ----------------------------------------------------------------------------------------------------------
// While the AI drives, your car gets the setup of the AI car that is the same car as yours (your teammate): the fields that Swofty's
// live car operations (1.7, experimental) can write. Your own values are saved first and written back when you take the car back.
// The AI car is found by its car data (steering lock, clutch torque, final drive, gear ratios, engine limiter): the lowest AI car whose
// data is identical to yours. Wings, tyre pressures and suspension stiffness can't be written live, so they stay as you set them.
namespace live = swofty::cars::live;

struct SetupField {
    live::Field f;
    const char *name;
};
const SetupField kSetup[] = {
    {live::Field::diff_on_throttle, "differential on throttle"},
    {live::Field::diff_off_throttle, "differential off throttle"},
    {live::Field::brakes_bias, "brake bias"},
    {live::Field::brakes_pressure, "brake pressure"},
    {live::Field::susp_front_camber, "front camber"},
    {live::Field::susp_front_toe, "front toe"},
    {live::Field::susp_front_anti_roll, "front anti-roll bar"},
    {live::Field::susp_front_ride_height_offset, "front ride height"},
    {live::Field::susp_rear_camber, "rear camber"},
    {live::Field::susp_rear_toe, "rear toe"},
    {live::Field::susp_rear_anti_roll, "rear anti-roll bar"},
    {live::Field::susp_rear_ride_height_offset, "rear ride height"},
    {live::Field::chassis_ballast_kg, "ballast"},
};
constexpr int kSetupN = int(sizeof kSetup / sizeof kSetup[0]);
const live::Field kFingerprint[] = {live::Field::steering_lock, live::Field::clutch_torque, live::Field::final_drive,
                                    live::Field::gear_ratio_1,  live::Field::gear_ratio_2,  live::Field::gear_ratio_3,
                                    live::Field::gear_ratio_4,  live::Field::gear_ratio_5,  live::Field::gear_ratio_6,
                                    live::Field::gear_ratio_7,  live::Field::gear_ratio_8,  live::Field::engine_limiter};

bool g_setup_applied = false;       // your car carries the AI car's setup now
bool g_setup_saved_ok[kSetupN];
float g_setup_saved[kSetupN];
double g_setup_next = 0;            // when to try again (the car or the fields may not be ready)
int g_setup_tries = 0;
bool g_setup_unavailable_logged = false;

bool same_car(const live::Car &a, const live::Car &b)
{
    int known = 0;
    for (auto f : kFingerprint) {
        auto x = a.get(f), y = b.get(f);
        if (!x || !y)
            continue;
        known++;
        if (std::fabs(*x - *y) > 1e-4f * std::max(1.0f, std::fabs(*x)))
            return false;
    }
    return known >= 6;   // enough of the car's data is readable to say it is the same car
}

void apply_setup(int me)
{
    if (!live::available()) {
        if (!g_setup_unavailable_logged) {
            g_setup_unavailable_logged = true;
            LOG("SETUP: the live car operations are not available (needs Swofty Launcher 1.7 and this game build): your setup is kept");
        }
        return;
    }
    auto cars = live::cars();
    const live::Car *mine = nullptr;
    for (const auto &c : cars)
        if (int(c.index()) == me)
            mine = &c;
    if (!mine) {
        LOG("SETUP: your car (%d) is not in the live car list yet", me);
        return;
    }
    // the AI cars whose car data is identical to yours. Only ONE such car is the same car as yours (your teammate); when many cars
    // share the data we can't tell the teammate from the others, and copying from the wrong team's car could be wrong: keep your setup.
    std::vector<const live::Car *> twins;
    for (const auto &c : cars)
        if (int(c.index()) != me && c.is_ai() && same_car(*mine, c))
            twins.push_back(&c);
    std::string list;
    for (auto *c : twins)
        list += " " + std::to_string(c->index());
    if (twins.size() != 1) {
        LOG("SETUP: %zu AI car(s) have the same car data as yours (%s ): %s. Your setup is kept.", twins.size(), list.c_str(),
            twins.empty() ? "none found" : "your teammate can't be told apart from them");
        return;
    }
    const live::Car *mate = twins[0];
    int copied = 0, same = 0, failed = 0;
    for (int i = 0; i < kSetupN; i++) {
        auto own = mine->get(kSetup[i].f), theirs = mate->get(kSetup[i].f);
        g_setup_saved_ok[i] = false;
        if (!own || !theirs) {
            failed++;
            LOG("SETUP:   %s: can't be read (%s)", kSetup[i].name, !own ? "your car" : "the AI car");
            continue;
        }
        g_setup_saved[i] = *own;
        g_setup_saved_ok[i] = true;
        if (std::fabs(*own - *theirs) < 1e-6f) {
            same++;
            continue;
        }
        auto r = mine->set(kSetup[i].f, *theirs);
        if (r == swofty::Result::ok) {
            copied++;
            LOG("SETUP:   %s: %.4f -> %.4f (the AI car's)", kSetup[i].name, *own, *theirs);
        } else {
            failed++;
            LOG("SETUP:   %s: %.4f -> %.4f refused (%s)", kSetup[i].name, *own, *theirs, swofty::result_name(r));
        }
    }
    g_setup_applied = true;
    LOG("SETUP: matched AI car %u: %d value(s) copied, %d already the same, %d not possible. Yours are saved and come back when you take the car.",
        mate->index(), copied, same, failed);
}

void restore_setup(int me)
{
    if (!g_setup_applied)
        return;
    g_setup_applied = false;
    g_setup_tries = 0;
    if (!live::available())
        return;
    for (const auto &c : live::cars())
        if (int(c.index()) == me) {
            int back = 0;
            for (int i = 0; i < kSetupN; i++)
                if (g_setup_saved_ok[i] && c.set(kSetup[i].f, g_setup_saved[i]) == swofty::Result::ok)
                    back++;
            LOG("SETUP: your own setup is back on the car (%d value(s))", back);
            return;
        }
}

bool race_started = false;      // race.start was seen in this session
double retake_failing_since = 0;   // since when taking the car back has not worked (the session is probably over)
bool want_ai = false;           // the AI should drive your car
bool was_ai = false;            // the AI had the car (so a later change to human was the game's, not ours)
double lost_since = 0;          // since when the game has had the car back without us asking
double armed_at = 0;            // from start: when the AI may take the car (after race.start, once the game has settled)
double session_started = 0;
bool handing_back = false;      // the car is being handed back (waiting for the game to allow it)
double handback_started = 0, next_try = 0, countdown_end = 0, last_state_log = 0;
int countdown_beep = 0;
bool key_was_down = false;
int last_result = -1, last_mode = -1;
HANDLE active_event = nullptr;

void forget_session(const char *event)
{
    if (want_ai || handing_back || countdown_end)
        LOG("%s: the AI lets go of the car", event);
    want_ai = false;
    handing_back = false;
    countdown_end = 0;
    was_ai = false;
    lost_since = 0;
    armed_at = 0;
    retake_failing_since = 0;
}

void signal_active(bool on)
{
    if (!active_event)
        active_event = CreateEventA(nullptr, TRUE, FALSE, "kizi.ai-drive.active");   // Manual Pit Lane watches this event
    if (active_event)
        on ? SetEvent(active_event) : ResetEvent(active_event);
}

void beep_once(int hz)
{
    if (beeps)
        if (HANDLE t = CreateThread(nullptr, 0, [](void *f) -> DWORD {
                Beep(DWORD(reinterpret_cast<uintptr_t>(f)), 90);
                return 0;
            }, reinterpret_cast<void *>(uintptr_t(hz)), 0, nullptr))
            CloseHandle(t);
}

void toast(const char *text, const char *detail)
{
    auto r = swofty::hud::toast(text, detail, 3.0f);
    if (r != swofty::hud::Result::ok)
        LOG("toast not shown (%s): %s", swofty::hud::name(r), text);
}

int hand_wanted_seconds()
{
    std::string_view v = handback;
    return v == "3" ? 3 : v == "5" ? 5 : 0;
}

const char *mode_name(int m)
{
    return m == 1 ? "AI" : m == 0 ? "human" : "unknown";
}

void state_log(const char *why, int mode, int result, bool pit)
{
    if (mode != last_mode || result != last_result || now() - last_state_log > 10.0) {
        last_state_log = now();
        last_mode = mode;
        last_result = result;
        LOG("STATE (%s): want AI %d, handing back %d, car %d, controller %s%s, last set_control result %d", why, want_ai ? 1 : 0,
            handing_back ? 1 : 0, g_player.load(), mode_name(mode), pit ? " (pit lane flag)" : "", result);
    }
}

// Your car: the one car in human control. The session events don't tell us (the player is empty in their context), so we look.
int find_human_car()
{
    for (int c = 0; c < 32; c++)
        if (auto st = swofty::cars::live::Car(uint32_t(c)).control())
            if (st->mode == swofty::sessions::Control::human)
                return c;
    return -1;
}

double next_scan = 0;

void learn_player()
{
    if (g_player.load() >= 0 || now() < next_scan)
        return;
    // not while the game builds the session (no controller calls in the first seconds)
    if (session_started && now() - session_started < 4.0)
        return;
    next_scan = now() + 1.0;
    int c = find_human_car();
    if (c >= 0) {
        g_player = c;
        LOG("your car is vehicle %d (the car in human control)", c);
    }
}

void tick()
{
    learn_player();
    int me = g_player.load();
    if (me < 0)
        return;
    if (armed_at && now() >= armed_at) {
        armed_at = 0;
        if (!want_ai) {
            want_ai = true;
            handing_back = false;
            next_try = 0;
            LOG("from start: the race is running, the AI takes the car now");
        }
    } else if (from_start && !armed_at && !want_ai && !race_started && session_started && now() - session_started > 150.0) {
        session_started = 0;
        LOG("from start: no race.start in this session after 150 s, press the hotkey to start the AI");
    }
    swofty::cars::live::Car car{static_cast<uint32_t>(me)};
    auto st = car.control();
    int mode = st ? (st->mode == swofty::sessions::Control::ai ? 1 : 0) : -1;
    bool pit = st && st->pit_lane();
    if (countdown_end && now() >= countdown_end) {
        countdown_end = 0;
        want_ai = false;
        handing_back = true;
        handback_started = now();
        next_try = 0;
        beep_once(1320);
        toast("YOU DRIVE", "THE AI HANDS BACK ONCE YOU ARE OUT OF THE PIT LANE");
        LOG("hand back: countdown over, asking the game for the car");
    } else if (countdown_end) {
        double left = countdown_end - now();
        if (countdown_beep > 0 && left <= countdown_beep - 1) {
            beep_once(880);
            countdown_beep--;
        }
    }
    if (g_noqte_flag) {
        *g_noqte_vid = me >= 0 && me < 0xff ? uint8_t(me) : 0xff;
        *g_noqte_flag = (no_qte && want_ai && mode == 1) ? 1 : 0;
    }
    if (ai_setup && want_ai && mode == 1 && !g_setup_applied && g_setup_tries < 4 && now() >= g_setup_next) {
        g_setup_next = now() + 3.0;   // after the take-over settled; tried again a few times if the car list is not ready
        g_setup_tries++;
        apply_setup(me);
    }
    if (g_setup_applied && handing_back && mode == 0)
        restore_setup(me);
    if (want_ai) {
        signal_active(mode == 1);
        if (mode == 1) {
            was_ai = true;
            lost_since = 0;
            retake_failing_since = 0;
        } else if (was_ai) {
            // the game took the car back (restart, flashback, back to the garage): it rebuilds its controllers for a moment, and
            // calling set_control in the middle of that crashed the game. Wait until it has settled.
            if (lost_since == 0) {
                lost_since = now();
                g_setup_applied = false;   // the game rebuilt the car with your own values: the AI setup is applied again after the retake
                g_setup_tries = 0;
                LOG("the game took the car back: taking it again in 2.5 s");
            }
            if (now() - lost_since < 2.5) {
                state_log("wait", mode, last_result, pit);
                return;
            }
        }
        if (mode != 1 && now() >= next_try) {
            auto r = swofty::sessions::set_control(car, swofty::sessions::Control::ai);
            next_try = now() + (r == swofty::sessions::Result::ok ? 1.0 : 0.5);
            if (r == swofty::sessions::Result::ok) {
                retake_failing_since = 0;
            } else if (retake_failing_since == 0) {
                retake_failing_since = now();
            } else if (now() - retake_failing_since > 20.0) {
                // The game keeps refusing: the session is over (this core does not fire session.end yet). A new session starts clean.
                LOG("take the car: still %s after 20 s, the session is probably over: the AI lets go of the car", rname(r));
                want_ai = false;
                was_ai = false;
                lost_since = 0;
                retake_failing_since = 0;
                return;
            }
            if (int(r) != last_result || mode != last_mode)
                LOG("take the car: set_control(AI) -> %s (controller now %s%s)", rname(r), mode_name(mode), pit ? ", pit lane flag" : "");
            state_log("take", mode, int(r), pit);
            return;
        }
        state_log("AI drives", mode, last_result, pit);
    } else if (handing_back) {
        was_ai = false;
        lost_since = 0;
        signal_active(false);
        if (mode == 0) {
            handing_back = false;
            LOG("hand back: done after %.1f s, you are driving again", now() - handback_started);
        } else if (now() - handback_started > 90.0) {
            handing_back = false;
            LOG("hand back: gave up after 90 s (controller %s%s)", mode_name(mode), pit ? ", pit lane flag" : "");
        } else if (now() >= next_try) {
            auto r = swofty::sessions::set_control(car, swofty::sessions::Control::human);
            next_try = now() + 0.25;
            if (int(r) != last_result)
                LOG("hand back: set_control(human) -> %s (%s%s)", rname(r), mode_name(mode), pit ? ", pit lane flag" : "");
            state_log("hand back", mode, int(r), pit);
        }
    } else {
        was_ai = false;
        lost_since = 0;
        signal_active(false);
        state_log("you drive", mode, last_result, pit);
    }
}

void handle_key()
{
    int vk = key_vk(hotkey);
    bool down = vk && game_in_front() && (GetAsyncKeyState(vk) & 0x8000);
    if (down && !key_was_down) {
        if (g_player.load() < 0) {
            int c = find_human_car();
            if (c >= 0)
                g_player = c;
        }
        if (g_player.load() < 0) {
            LOG("hotkey pressed but your car is not known (no car in human control found)");
        } else if (countdown_end) {
            countdown_end = 0;
            LOG("hand back: cancelled, the AI keeps driving");
            toast("AI KEEPS DRIVING", "PRESS THE HOTKEY TO TAKE OVER");
        } else if (want_ai) {
            int s = hand_wanted_seconds();
            if (s > 0) {
                countdown_end = now() + s;
                countdown_beep = s;
                LOG("hand back: %d s countdown started", s);
                toast("YOU DRIVE SOON", "GET READY");
            } else {
                want_ai = false;
                handing_back = true;
                handback_started = now();
                next_try = 0;
                LOG("hand back: asking the game for the car now");
                toast("YOU DRIVE", "THE AI HANDS BACK ONCE YOU ARE OUT OF THE PIT LANE");
            }
        } else {
            want_ai = true;
            handing_back = false;
            next_try = 0;
            beep_once(880);
            LOG("take the car: the AI should drive now");
            toast("AI DRIVES YOUR CAR", "PRESS THE HOTKEY TO TAKE OVER");
        }
    }
    key_was_down = down;
}

void frame(double, void *)
{
    handle_key();
    static double last_tick = 0;
    if (now() - last_tick < 0.05)
        return;
    last_tick = now();
    tick();
}

}  // namespace

SWOFTY_ON_LOAD
{
    LOG("v" ADDON_VERSION " loading: hotkey %.*s, hand back %.*s s, from start %d", int(std::string_view(hotkey).size()),
        std::string_view(hotkey).data(), int(std::string_view(handback).size()), std::string_view(handback).data(), from_start.get() ? 1 : 0);
    watch_player_events();
    if (HANDLE t = CreateThread(nullptr, 0, qte_thread, nullptr, 0, nullptr))
        CloseHandle(t);
    // new session: forget the old state. from start: the AI takes the car a few seconds after race.start, never while the game
    // is still building the session (taking it at session.start crashed the game in the controller setup).
    {
        auto r = swofty::events::on("session.start").after([](std::span<const std::byte>, const swofty::events::Context &ctx) {
            g_player = -1;   // a new session: your car may be another one, look for it again
            note_player(ctx, "session.start");
            want_ai = false;
            handing_back = false;
            countdown_end = 0;
            was_ai = false;
            lost_since = 0;
            armed_at = 0;
            g_setup_applied = false;
            g_setup_tries = 0;
            session_started = now();
            race_started = false;
            retake_failing_since = 0;
            LOG("new session: the AI waits (from start %s)", from_start ? "on: after race.start" : "off");
        });
        if (r != swofty::events::Result::ok)
            LOG("event session.start: %s", swofty::events::name(r));
        auto r2 = swofty::events::on("race.start").after([](std::span<const std::byte>, const swofty::events::Context &) {
            race_started = true;
            if (from_start && !want_ai) {
                armed_at = now() + 3.0;
                LOG("from start: race.start seen, the AI takes the car in 3 s");
            }
        });
        if (r2 != swofty::events::Result::ok)
            LOG("event race.start: %s", swofty::events::name(r2));
    }
    auto r = swofty::events::on_frame(frame);
    if (r != swofty::events::Result::ok)
        LOG("frame callback %s: the addon can't run", swofty::events::name(r));
}

SWOFTY_ADDON()
