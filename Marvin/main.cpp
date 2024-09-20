#include "platform/Platform.h"
//
#include <ddraw.h>
#include <fuse/Fuse.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "Bot.h"
#include "Debug.h"
#include "GameProxy.h"
#include "KeyController.h"
#include "Time.h"

using namespace fuse;

std::string kEnabledText = "Continuum (enabled) - ";
std::string kDisabledText = "Continuum (disabled) - ";

using time_clock = std::chrono::high_resolution_clock;
using time_point = time_clock::time_point;
using seconds = std::chrono::duration<float>;

std::unique_ptr<marvin::Bot> bot;

HWND g_hWnd = 0;

class MarvinHook final : public HookInjection {
 public:
  const char* GetHookName() override { return "Marvin"; }

  void OnUpdate() override {
    time_point now = time_clock::now();
    seconds dt = now - last_update_time;
    seconds sec = now - start_time;

    g_hWnd = Fuse::Get().GetGameWindowHandle();

    // Initialize marvin if we don't have a bot created yet, but we have the map downloaded and we are in game.
    if (!bot && Fuse::Get().GetConnectState() == ConnectState::Playing) {
      InitializeMarvin();
      enabled = true;
    }

    // Check for key presses to enable/disable the bot.
    if (GetFocus() == g_hWnd) {
      if (GetAsyncKeyState(VK_F10)) {
        enabled = false;
        SetWindowText(g_hWnd, kDisabledText.c_str());
      } else if (GetAsyncKeyState(VK_F9)) {
        enabled = true;
        SetWindowText(g_hWnd, kEnabledText.c_str());
      }
    }

    if (enabled) {
      if (ProcessChat()) {
        return;
      }

      if (dt.count() > (float)(1.0f / bot->GetUpdateInterval())) {
#if DEBUG_RENDER
        marvin::g_RenderState.renderable_texts.clear();
        marvin::g_RenderState.renderable_lines.clear();
#endif
        if (bot) {
          bot->Update(false, dt.count());
          UpdateCRC();
        }

        last_update_time = now;
      }
    }

#if DEBUG_RENDER
    marvin::g_RenderState.Render();
#endif
  }

  bool OnMenuUpdate(BOOL hasMsg, LPMSG lpMsg, HWND hWnd) override {
    // If we get a menu message pump update then we must be out of the game, so clean it up.
    if (enabled) {
      CleanupMarvin();
      enabled = false;
    }

    return false;
  }

  virtual KeyState OnGetAsyncKeyState(int vKey) {
    // Don't override the enable/disable keys.
    if (vKey >= VK_F9 && vKey <= VK_F10) {
      return {};
    }

#if DEBUG_USER_CONTROL
    if (1) {
#else
    if (!enabled) {
#endif

      if (GetFocus() == g_hWnd) {
        // We want to retain user control here so don't do anything.
        return {};
      }

      // Force nothing being pressed when we don't have focus and bot isn't controlling itself.
      return {true, false};
    } else if (bot && bot->GetKeys().IsPressed(vKey)) {
      // Force press the requested key since the bot says it should be pressed.
      return {true, true};
    }

    // We want this to be forced off since the bot has control but doesn't want to press the key.
    return {true, false};
  }

 private:
  // This function needs to be called whenever anything changes in Continuum's memory.
  // Continuum keeps track of its memory by calculating a checksum over it. Any changes to the memory outside of the
  // normal game update would be caught. So we bypass this by manually calculating the crc at the end of the bot update
  // and replacing the expected crc in memory.
  void UpdateCRC() {
    typedef u32(__fastcall * CRCFunction)(void* This, void* thiscall_garbage);
    CRCFunction func_ptr = (CRCFunction)0x43BB80;

    u32 game_addr = (*(u32*)0x4c1afc) + 0x127ec;
    u32 result = func_ptr((void*)game_addr, 0);

    *(u32*)(game_addr + 0x6d4) = result;
  }

  std::string CreateBot() {
    // create pointer to game and pass the window handle
    auto game = std::make_shared<marvin::ContinuumGameProxy>();

    bot = std::make_unique<marvin::Bot>(std::move(game));

    return bot->GetGame().GetName();
  }

  /* there are limitations on what win32 calls/actions can be made inside of this funcion call (DLLMain) */
  void InitializeMarvin() {
    marvin::PerformanceTimer timer;

    std::string name = CreateBot();
    marvin::log.Write("INITIALIZE MARVIN - NEW TIMER", timer.GetElapsedTime());

    kEnabledText = "Continuum (enabled) - " + name;
    kDisabledText = "Continuum (disabled) - " + name;

    SetWindowText(g_hWnd, kEnabledText.c_str());

    marvin::log.Write("FINISH INITIALIZE MARVIN - TOTAL TIME", timer.TimeSinceConstruction());
  }

  void CleanupMarvin() {
    marvin::log.Write("CLEANUP MARVIN.");

    SetWindowText(g_hWnd, "Continuum");
    bot = nullptr;
    marvin::log.Write("CLEANUP MARVIN FINISHED.");
  }

  bool ProcessChat() {
    for (marvin::ChatMessage chat : bot->GetGame().GetCurrentChat()) {
      std::string name = bot->GetGame().GetPlayer().name;
      std::string eg_msg = "[ " + name + " ]";
      std::string eg_packet_loss_msg = "Packet loss too high for you to enter the game.";
      std::string hs_lag_msg = "You are too lagged to play in this arena.";

      bool eg_lag_locked =
          chat.message.compare(0, 4 + name.size(), eg_msg) == 0 && bot->GetGame().GetZone() == marvin::Zone::ExtremeGames;

      bool eg_locked_in_spec =
          eg_lag_locked || chat.message == eg_packet_loss_msg && bot->GetGame().GetZone() == marvin::Zone::ExtremeGames;

      bool hs_locked_in_spec = chat.message == hs_lag_msg && bot->GetGame().GetZone() == marvin::Zone::Hyperspace;

      bool disconected = chat.message.compare(0, 9, "WARNING: ") == 0;

      if (chat.type == marvin::ChatType::Arena) {
        if (disconected || eg_locked_in_spec || hs_locked_in_spec) {
          PostQuitMessage(0);
          return true;
        }
      }
    }

    return false;
  }

 private:
  bool enabled = false;
  time_point last_update_time;
  time_point start_time = time_clock::now();
};

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD dwReason, LPVOID reserved) {
  switch (dwReason) {
    case DLL_PROCESS_ATTACH: {
      Fuse::Get().RegisterHook(std::make_unique<MarvinHook>());
    } break;
  }

  return TRUE;
}