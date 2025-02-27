#pragma once

#include <string>
#include <unordered_map>

#include "../Common.h"
#include "../GameProxy.h"

namespace marvin {

class Bot;

// Match bits with the internal chat type numbers to simplify access check
enum {
  CommandAccess_Arena = (1 << 0),
  CommandAccess_Public = (1 << 2),
  CommandAccess_Private = (1 << 5),
  CommandAccess_Chat = (1 << 9),

  CommandAccess_All = (CommandAccess_Arena | CommandAccess_Public | CommandAccess_Private | CommandAccess_Chat),
};
typedef u32 CommandAccessFlags;

enum {
  CommandFlag_Lockable =  (1 << 0),
};
typedef u32 CommandFlags;

class CommandSystem;

class CommandExecutor {
 public:
  virtual void Execute(CommandSystem& cmd, Bot& bot, const std::string& sender, const std::string& arg) = 0;
  virtual CommandAccessFlags GetAccess(Bot& bot) = 0;
  virtual CommandFlags GetFlags() { return 0; }
  virtual std::vector<std::string> GetAliases() = 0;
  virtual std::string GetDescription() = 0;
  virtual int GetSecurityLevel() = 0;
};

using Operators = std::unordered_map<std::string, int>;
using Commands = std::unordered_map<std::string, std::shared_ptr<CommandExecutor>>;

class CommandSystem {
 public:
  CommandSystem();

  bool ProcessMessage(Bot& bot, ChatMessage& chat);

  void RegisterCommand(std::shared_ptr<CommandExecutor> executor) {
    for (std::string trigger : executor->GetAliases()) {
      commands_[Lowercase(trigger)] = executor;
    }
  }

  int GetSecurityLevel(const std::string& player);

  Commands& GetCommands() { return commands_; }
  const Operators& GetOperators() const;

 private:
  Commands commands_;
};

std::vector<std::string> Tokenize(std::string message, char delim);

}  // namespace marvin
