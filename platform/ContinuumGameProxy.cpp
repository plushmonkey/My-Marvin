#include "ContinuumGameProxy.h"

#include <thread>
#include <vector>

#include "../Bot.h"
#include "../Map.h"

namespace marvin {

ContinuumGameProxy::ContinuumGameProxy(HWND hwnd) {
  module_base_continuum_ = process_.GetModuleBase("Continuum.exe");
  module_base_menu_ = process_.GetModuleBase("menu040.dll");
  player_id_ = 0xFFFF;
  hwnd_ = hwnd;

  game_addr_ = process_.ReadU32(module_base_continuum_ + 0xC1AFC);

  position_data_ = (uint32_t*)(game_addr_ + 0x126BC);
  
  std::string path = GetServerFolder();
  // TODO: Either find this from memory or pass it in through config
  if (path == "zones\\SSCE Hyperspace") path += "\\jun2018.lvl";
  else if (path == "zones\\SSCJ Devastation") path += "\\bd.lvl";
  else if (path == "zones\\SSCU Extreme Games") path += "\\colpub20v4.lvl";
  
  map_ = Map::Load(path);

  FetchPlayers();


  for (auto& player : players_) {
    if (player.name == GetName()) {
      player_id_ = player.id;
      player_ = &player;
    }
  }
}

void ContinuumGameProxy::Update(float dt) {
  // Continuum stops processing input when it loses focus, so update the memory
  // to make it think it always has focus.
  SetWindowFocus();

  FetchPlayers();

  // Grab the address to the main player structure
  u32 player_addr = *(u32*)(game_addr_ + 0x13070);

  // Follow a pointer that leads to weapon data
  u32 ptr = *(u32*)(player_addr + 0x0C);
  u32 weapon_count = *(u32*)(ptr + 0x1DD0) + *(u32*)(ptr + 0x1DD4);
  u32 weapon_ptrs = (ptr + 0x21F4);

  weapons_.clear();

  for (size_t i = 0; i < weapon_count; ++i) {
      u32 weapon_data = *(u32*)(weapon_ptrs + i * 4);

      WeaponData* data = (WeaponData*)(weapon_data);

      weapons_.emplace_back(data);
  }
}

void ContinuumGameProxy::FetchPlayers() {
  const std::size_t kPosOffset = 0x04;
  const std::size_t kVelocityOffset = 0x10;
  const std::size_t kIdOffset = 0x18;
  const std::size_t kBountyOffset1 = 0x20;
  const std::size_t kBountyOffset2 = 0x24;
  const std::size_t kRotOffset = 0x3C;
  const std::size_t kShipOffset = 0x5C;
  const std::size_t kFreqOffset = 0x58;
  const std::size_t kStatusOffset = 0x60;
  const std::size_t kNameOffset = 0x6D;
  const std::size_t kEnergyOffset1 = 0x208;
  const std::size_t kEnergyOffset2 = 0x20C;
  


  std::size_t base_addr = game_addr_ + 0x127EC;
  std::size_t players_addr = base_addr + 0x884;
  std::size_t count_addr = base_addr + 0x1884;

  std::size_t count = process_.ReadU32(count_addr) & 0xFFFF;

  players_.clear();

  for (std::size_t i = 0; i < count; ++i) {
    std::size_t player_addr = process_.ReadU32(players_addr + (i * 4));

    if (!player_addr) continue;

    Player player;

    player.position.x =
        process_.ReadU32(player_addr + kPosOffset) / 1000.0f / 16.0f;
    player.position.y =
        process_.ReadU32(player_addr + kPosOffset + 4) / 1000.0f / 16.0f;

    player.velocity.x =
        process_.ReadI32(player_addr + kVelocityOffset) / 10.0f / 16.0f;
    player.velocity.y =
        process_.ReadI32(player_addr + kVelocityOffset + 4) / 10.0f / 16.0f;

    player.id =
        static_cast<uint16_t>(process_.ReadU32(player_addr + kIdOffset));
    player.discrete_rotation = static_cast<uint16_t>(
        process_.ReadU32(player_addr + kRotOffset) / 1000);

    player.ship =
        static_cast<uint8_t>(process_.ReadU32(player_addr + kShipOffset));
    player.frequency =
        static_cast<uint16_t>(process_.ReadU32(player_addr + kFreqOffset));

    player.status =
        static_cast<uint8_t>(process_.ReadU32(player_addr + kStatusOffset));

    player.name = process_.ReadString(player_addr + kNameOffset, 23);

    player.bounty = *(u32*)(player_addr + kBountyOffset1) + *(u32*)(player_addr + kBountyOffset2);

    player.dead = process_.ReadU32(player_addr + 0x178) == 0;

    // Energy calculation @4485FA
    if (player.id == player_id_) {
    u32 energy1 = process_.ReadU32(player_addr + kEnergyOffset1); 
    u32 energy2 = process_.ReadU32(player_addr + kEnergyOffset2); 

    u32 combined = energy1 + energy2;
    u64 energy = ((combined * (u64)0x10624DD3) >> 32) >> 6;

    player.energy = static_cast<uint16_t>(energy);
    }
    else {
        u32 first = *(u32*)(player_addr + 0x150);
        u32 second = *(u32*)(player_addr + 0x154);

        player.energy = static_cast<uint16_t>(first + second);
    }

    players_.emplace_back(player);

    if (player.id == player_id_) {
      player_ = &players_.back();
    }
  }
}

std::vector<Weapon*> ContinuumGameProxy::GetWeapons() {
    std::vector<Weapon*> weapons;

    for (std::size_t i = 0; i < weapons_.size(); ++i) {
        weapons.push_back(&weapons_[i]);
    }

    return weapons;
}

const ClientSettings& ContinuumGameProxy::GetSettings() const {
  std::size_t addr = game_addr_ + 0x127EC + 0x1AE70;  // 0x2D65C

  return *reinterpret_cast<ClientSettings*>(addr);
}

const ShipSettings& ContinuumGameProxy::GetShipSettings() const {
  return GetSettings().ShipSettings[player_->ship];
}

const ShipSettings& ContinuumGameProxy::GetShipSettings(int ship) const {
  return GetSettings().ShipSettings[ship];
}

std::string ContinuumGameProxy::GetName() const {
  const std::size_t ProfileStructSize = 2860;

  uint16_t profile_index =
      process_.ReadU32(module_base_menu_ + 0x47FA0) & 0xFFFF;
  std::size_t addr = process_.ReadU32(module_base_menu_ + 0x47A38) + 0x15;

  if (addr == 0) {
    return "";
  }

  addr += profile_index * ProfileStructSize;

  std::string name = process_.ReadString(addr, 23);

  name = name.substr(0, strlen(name.c_str()));

  return name;
}

int ContinuumGameProxy::GetEnergy() const { return player_->energy; }

Vector2f ContinuumGameProxy::GetPosition() const {
  float x = (*position_data_) / 16.0f;
  float y = (*(position_data_ + 1)) / 16.0f;

  return Vector2f(x, y);
}

const std::vector<Player>& ContinuumGameProxy::GetPlayers() const {
  return players_;
}

const Map& ContinuumGameProxy::GetMap() const { return *map_; }
const Player& ContinuumGameProxy::GetPlayer() const { return *player_; }

const Player* ContinuumGameProxy::GetPlayerById(u16 id) const {
    for (std::size_t i = 0; i < players_.size(); ++i) {
        if (players_[i].id == id) {
            return &players_[i];
        }
    }

    return nullptr;
}

// TODO: Find level data or level name in memory
std::string ContinuumGameProxy::GetServerFolder() const {
  std::size_t folder_addr = *(uint32_t*)(game_addr_ + 0x127ec + 0x5a3c) + 0x10D;
  std::string server_folder = process_.ReadString(folder_addr, 256);

  return server_folder;
}

std::string ContinuumGameProxy::Zone() {
    std::string path = GetServerFolder();
    std::string result = "";
    if (path == "zones\\SSCE Hyperspace") result = "hyperspace";
    if (path == "zones\\SSCJ Devastation") result = "devastation";
    if (path == "zones\\SSCU Extreme Games") result = "extreme games";
    return result;
}
void ContinuumGameProxy::SetFreq(int freq) {
    std::string freq_ = std::to_string(freq);
    SendMessage(hwnd_, WM_CHAR, (WPARAM)('='), 0);
    for (std::size_t i = 0; i < freq_.size(); i++) {
        char letter = freq_[i];
        SendMessage(hwnd_, WM_CHAR, (WPARAM)(letter), 0);
    }
    SendMessage(hwnd_, WM_CHAR, (WPARAM)(VK_RETURN), 0);
}

void ContinuumGameProxy::PageUp() {
    SendKey(VK_PRIOR);
}
void ContinuumGameProxy::PageDown() {
    SendKey(VK_NEXT);
}
void ContinuumGameProxy::XRadar() {
    SendKey(VK_END);
}
void ContinuumGameProxy::Burst(KeyController& keys) {
    keys.Press(VK_SHIFT);
    SendKey(VK_DELETE);
}
void ContinuumGameProxy::Repel(KeyController& keys) {
    keys.Press(VK_SHIFT);
    keys.Press(VK_CONTROL);
}

void ContinuumGameProxy::F7() {
    SendKey(VK_F7);
}

bool ContinuumGameProxy::SetShip(int ship) {
  int* menu_open_addr = (int*)(game_addr_ + 0x12F39);

  bool menu_open = *menu_open_addr;


  if (!menu_open) {
    SendKey(VK_ESCAPE);
  } else {
    SendMessageW(hwnd_, WM_CHAR, (WPARAM)('1' + ship), 0);
  }

  return menu_open;
}

void ContinuumGameProxy::Warp() { 
    SendKey(VK_INSERT);
}

void ContinuumGameProxy::Stealth() { 
    SendKey(VK_HOME);
}

void ContinuumGameProxy::Cloak(KeyController& keys) {
    keys.Press(VK_SHIFT);
    SendKey(VK_HOME);
}

void ContinuumGameProxy::MultiFire() { SendKey(VK_DELETE); }

void ContinuumGameProxy::Flag() {

    std::string message = "?flag";

    for (std::size_t i = 0; i < message.size(); i++) {
        char letter = message[i];
        SendMessage(hwnd_, WM_CHAR, (WPARAM)(letter), 0);
    }

    SendMessage(hwnd_, WM_CHAR, (WPARAM)(VK_RETURN), 0);
}

void ContinuumGameProxy::P() {
    SendMessage(hwnd_, WM_CHAR, (WPARAM)('!'), 0);
    SendMessage(hwnd_, WM_CHAR, (WPARAM)('p'), 0);
    SendMessage(hwnd_, WM_CHAR, (WPARAM)(VK_RETURN), 0);
}

bool ContinuumGameProxy::Spec() {
    int* menu_open_addr = (int*)(game_addr_ + 0x12F39);

    bool menu_open = *menu_open_addr;

    if (!menu_open) {
        SendKey(VK_ESCAPE);
    }
    else {
        SendMessage(hwnd_, WM_CHAR, (WPARAM)('s'), 0);
    }

    return menu_open;
}
void ContinuumGameProxy::Attach(std::string name) {

    std::string message = ":" + name + ":?attach";

    for (std::size_t i = 0; i < message.size(); i++) {
        char letter = message[i];
        SendMessage(hwnd_, WM_CHAR, (WPARAM)(letter), 0);
    }

    SendMessage(hwnd_, WM_CHAR, (WPARAM)(VK_RETURN), 0);
}

void ContinuumGameProxy::SetWindowFocus() {
  std::size_t focus_addr = game_addr_ + 0x3039c;

  process_.WriteU32(focus_addr, 1);
}

ExeProcess& ContinuumGameProxy::GetProcess() { return process_; }

void ContinuumGameProxy::SendKey(int vKey) {
  SendMessage(hwnd_, WM_KEYDOWN, (WPARAM)vKey, 0);
  SendMessage(hwnd_, WM_KEYUP, (WPARAM)vKey, 0);
}

void ContinuumGameProxy::SendChatMessage(const std::string& mesg) const {

    typedef void(__fastcall* ChatSendFunction)(void* This, void* thiscall_garbage, char* msg, u32* unknown1, u32* unknown2);

    if (mesg.empty()) return;

    // The address to the current text input buffer
    std::size_t chat_input_addr = game_addr_ + 0x2DD14;
    char* input = (char*)(chat_input_addr);
    memcpy(input, mesg.c_str(), mesg.length());
    input[mesg.length()] = 0;

    ChatSendFunction send_func = (ChatSendFunction)(*(u32*)(module_base_continuum_ + 0xAC30C));


    void* This = (void*)(game_addr_ + 0x2DBF0);


    // Some value that the client passes in for some reason
    u32 value = 0x4AC370;

    send_func(This, nullptr, input, &value, 0);

    // Clear the text buffer after sending the message
    input[0] = 0;
}

std::vector<std::string> ContinuumGameProxy::GetChat(int type) const {

    u32 chat_base_addr = game_addr_ + 0x2DD08;

    ChatEntry* chat_ptr = *(ChatEntry**)(chat_base_addr);
    u32 entry_count = *(u32*)(chat_base_addr + 8);

    //read the last 6 lines, starting with the most recent, if any match the type return that line
    std::vector<std::string> lines;
    for (std::size_t i = 1; i < 2; i++) {
        ChatEntry* current = chat_ptr + (entry_count - i);
        if (current->type == type) {
            lines.push_back(current->message);
        }
    }
    /*  Arena = 0,
        Public = 2,
        Private = 5,
        Channel = 9 */
    //if no lines match just return the most recent
    //ChatEntry* current = chat_ptr + (entry_count - 1);
    return lines;
}

int64_t ContinuumGameProxy::TickerPosition() {
    //this reads the same adress as get selected player but returns a number
    //used to determine if the bot should page up or page down
    u32 addr = game_addr_ + 0x2DF44;

    int64_t selected = process_.ReadU32(addr) & 0xFFFF;
    return selected;
}

const Player& ContinuumGameProxy::GetSelectedPlayer() const {
    u32 selected_index = *(u32*)(game_addr_ + 0x127EC + 0x1B758);

    return players_[selected_index];
}

}  // namespace marvin