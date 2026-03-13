#pragma once
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <cstdint>
#include <filesystem>

struct sqlite3;

namespace bp {

// Embedded HTTP tile server (loopback only).
// Serves OpenStreetMap tiles from a local SQLite MBTiles cache.
// Fetches from OSM on cache miss, stores with 30-day TTL.
class TileCache {
public:
    explicit TileCache(const std::filesystem::path& db_path);
    ~TileCache();

    // Port that Leaflet should connect to
    uint16_t GetPort() const { return port_; }

    // Optional read-only fallback .mbtiles (for offline use)
    void SetFallbackMbtiles(const std::filesystem::path& path);

private:
    void OpenOrCreateDb(const std::filesystem::path& path);
    void ServerLoop(int server_fd);
    void HandleRequest(int client_fd);
    bool ParseTilePath(const std::string& path, int& z, int& x, int& y);
    std::vector<uint8_t> ServeTile(int z, int x, int y);
    std::vector<uint8_t> QueryCache(int z, int x, int y);
    void StoreInCache(int z, int x, int y, const std::vector<uint8_t>& data);
    std::vector<uint8_t> FetchFromOSM(int z, int x, int y);
    bool IsFresh(int z, int x, int y);

    sqlite3*            db_       = nullptr;
    sqlite3*            fallback_ = nullptr;
    uint16_t            port_     = 0;
    std::thread         server_thread_;
    std::atomic<bool>   running_  {false};
};

} // namespace bp
