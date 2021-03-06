#pragma once

#include "util.h"
#include "highscores.h"
#include "save_file_info.h"
#include "saved_game_info.h"

class ProgressMeter;

class FileSharing {
  public:
  FileSharing(const string& uploadUrl, Options&, long long installId);

  optional<string> uploadSite(const FilePath& path, ProgressMeter&);
  struct SiteInfo {
    SavedGameInfo gameInfo;
    SaveFileInfo fileInfo;
    int totalGames;
    int wonGames;
    int version;
  };
  optional<vector<SiteInfo>> listSites();
  optional<string> download(const string& filename, const DirectoryPath& dir, ProgressMeter&);

  typedef map<string, string> GameEvent;
  void uploadGameEvent(const GameEvent&);
  void uploadHighscores(const FilePath&);

  struct BoardMessage {
    string text;
    string author;
  };
  optional<vector<BoardMessage>> getBoardMessages(int boardId);
  void uploadBoardMessage(const string& gameId, int hash, const string& author, const string& text);

  string downloadHighscores(int version);

  void cancel();
  ~FileSharing();

  private:
  string uploadUrl;
  Options& options;
  SyncQueue<function<void()>> uploadQueue;
  AsyncLoop uploadLoop;
  void uploadingLoop();
  void uploadGameEventImpl(const GameEvent&, int tries);
  long long installId;
};

