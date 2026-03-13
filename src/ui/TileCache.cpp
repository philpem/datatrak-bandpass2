#include "TileCache.h"
#include <sqlite3.h>
#include <curl/curl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <ctime>
#include <sstream>
#include <string>
#include <vector>
#include <stdexcept>

namespace bp {

namespace {
    // 30-day TTL in seconds
    constexpr int64_t TTL_SECONDS = 30 * 24 * 3600;

    // libcurl write callback
    size_t CurlWrite(void* ptr, size_t size, size_t nmemb, void* userdata) {
        auto* buf = static_cast<std::vector<uint8_t>*>(userdata);
        size_t n = size * nmemb;
        buf->insert(buf->end(), (uint8_t*)ptr, (uint8_t*)ptr + n);
        return n;
    }

    // Simple HTTP/1.0 response helper
    std::string MakeResponse(int status, const std::string& content_type,
                             const std::vector<uint8_t>& body) {
        std::ostringstream h;
        h << "HTTP/1.0 " << status << " OK\r\n"
          << "Content-Type: " << content_type << "\r\n"
          << "Content-Length: " << body.size() << "\r\n"
          << "Access-Control-Allow-Origin: *\r\n"
          << "\r\n";
        std::string hdr = h.str();
        return hdr;
    }
}

TileCache::TileCache(const std::filesystem::path& db_path) {
    OpenOrCreateDb(db_path);

    // Bind to loopback on a random port
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) throw std::runtime_error("TileCache: socket failed");

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = 0;   // OS assigns port
    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0)
        throw std::runtime_error("TileCache: bind failed");

    socklen_t len = sizeof(addr);
    getsockname(server_fd, (sockaddr*)&addr, &len);
    port_ = ntohs(addr.sin_port);

    listen(server_fd, 16);
    running_.store(true);
    server_thread_ = std::thread([this, server_fd]{ ServerLoop(server_fd); });
}

TileCache::~TileCache() {
    running_.store(false);
    // Connect to self to unblock accept()
    int wake = socket(AF_INET, SOCK_STREAM, 0);
    if (wake >= 0) {
        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port        = htons(port_);
        connect(wake, (sockaddr*)&addr, sizeof(addr));
        close(wake);
    }
    if (server_thread_.joinable()) server_thread_.join();
    if (db_)       sqlite3_close(db_);
    if (fallback_) sqlite3_close(fallback_);
}

void TileCache::OpenOrCreateDb(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    if (sqlite3_open(path.string().c_str(), &db_) != SQLITE_OK)
        throw std::runtime_error("TileCache: cannot open DB: " + path.string());

    sqlite3_exec(db_,
        "CREATE TABLE IF NOT EXISTS tiles ("
        "  zoom_level  INTEGER NOT NULL,"
        "  tile_column INTEGER NOT NULL,"
        "  tile_row    INTEGER NOT NULL,"
        "  tile_data   BLOB    NOT NULL,"
        "  fetched_at  INTEGER NOT NULL"
        ");"
        "CREATE UNIQUE INDEX IF NOT EXISTS tiles_idx "
        "  ON tiles (zoom_level, tile_column, tile_row);"
        "CREATE TABLE IF NOT EXISTS metadata (name TEXT PRIMARY KEY, value TEXT);",
        nullptr, nullptr, nullptr);
}

void TileCache::SetFallbackMbtiles(const std::filesystem::path& path) {
    if (fallback_) { sqlite3_close(fallback_); fallback_ = nullptr; }
    sqlite3_open_v2(path.string().c_str(), &fallback_,
                    SQLITE_OPEN_READONLY, nullptr);
}

void TileCache::ServerLoop(int server_fd) {
    while (running_.load()) {
        int client = accept(server_fd, nullptr, nullptr);
        if (client < 0 || !running_.load()) { if (client >= 0) close(client); break; }
        HandleRequest(client);
        close(client);
    }
    close(server_fd);
}

void TileCache::HandleRequest(int client_fd) {
    char buf[2048] = {};
    recv(client_fd, buf, sizeof(buf) - 1, 0);

    std::string request(buf);
    // Parse first line: GET /z/x/y.png HTTP/1.x
    auto line_end = request.find("\r\n");
    if (line_end == std::string::npos) return;
    std::string first_line = request.substr(0, line_end);

    auto sp1 = first_line.find(' ');
    auto sp2 = first_line.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) return;

    std::string path = first_line.substr(sp1 + 1, sp2 - sp1 - 1);

    int z, x, y;
    if (!ParseTilePath(path, z, x, y)) {
        std::string resp = "HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        send(client_fd, resp.c_str(), resp.size(), 0);
        return;
    }

    auto tile = ServeTile(z, x, y);
    std::string hdr = MakeResponse(tile.empty() ? 404 : 200, "image/png", tile);
    send(client_fd, hdr.c_str(), hdr.size(), 0);
    if (!tile.empty())
        send(client_fd, tile.data(), tile.size(), 0);
}

bool TileCache::ParseTilePath(const std::string& path, int& z, int& x, int& y) {
    // Expect /z/x/y.png
    if (path.empty() || path[0] != '/') return false;
    std::string p = path.substr(1);
    // Remove query string
    auto q = p.find('?');
    if (q != std::string::npos) p = p.substr(0, q);
    // Remove .png extension
    if (p.size() > 4 && p.substr(p.size() - 4) == ".png")
        p = p.substr(0, p.size() - 4);
    std::istringstream ss(p);
    char slash;
    if (!(ss >> z >> slash >> x >> slash >> y)) return false;
    return true;
}

std::vector<uint8_t> TileCache::ServeTile(int z, int x, int y) {
    // Check main cache first
    if (IsFresh(z, x, y)) {
        auto data = QueryCache(z, x, y);
        if (!data.empty()) return data;
    }

    // Fetch from OSM
    auto data = FetchFromOSM(z, x, y);
    if (!data.empty()) {
        StoreInCache(z, x, y, data);
        return data;
    }

    // Try fallback mbtiles (TMS row flip)
    if (fallback_) {
        int tms_y = (1 << z) - 1 - y;
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT tile_data FROM tiles WHERE zoom_level=? AND tile_column=? AND tile_row=?";
        if (sqlite3_prepare_v2(fallback_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, z);
            sqlite3_bind_int(stmt, 2, x);
            sqlite3_bind_int(stmt, 3, tms_y);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const void* blob = sqlite3_column_blob(stmt, 0);
                int size         = sqlite3_column_bytes(stmt, 0);
                std::vector<uint8_t> v((uint8_t*)blob, (uint8_t*)blob + size);
                sqlite3_finalize(stmt);
                return v;
            }
            sqlite3_finalize(stmt);
        }
    }
    return {};
}

std::vector<uint8_t> TileCache::QueryCache(int z, int x, int y) {
    // MBTiles TMS row convention: flip y
    int tms_y = (1 << z) - 1 - y;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT tile_data FROM tiles WHERE zoom_level=? AND tile_column=? AND tile_row=?";
    std::vector<uint8_t> result;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, z);
        sqlite3_bind_int(stmt, 2, x);
        sqlite3_bind_int(stmt, 3, tms_y);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const void* blob = sqlite3_column_blob(stmt, 0);
            int size         = sqlite3_column_bytes(stmt, 0);
            result.assign((uint8_t*)blob, (uint8_t*)blob + size);
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

bool TileCache::IsFresh(int z, int x, int y) {
    int tms_y = (1 << z) - 1 - y;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT fetched_at FROM tiles WHERE zoom_level=? AND tile_column=? AND tile_row=?";
    bool fresh = false;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, z);
        sqlite3_bind_int(stmt, 2, x);
        sqlite3_bind_int(stmt, 3, tms_y);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t fetched = sqlite3_column_int64(stmt, 0);
            int64_t now     = (int64_t)std::time(nullptr);
            fresh = (now - fetched) < TTL_SECONDS;
        }
        sqlite3_finalize(stmt);
    }
    return fresh;
}

void TileCache::StoreInCache(int z, int x, int y, const std::vector<uint8_t>& data) {
    int tms_y = (1 << z) - 1 - y;
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT OR REPLACE INTO tiles (zoom_level, tile_column, tile_row, tile_data, fetched_at)"
        " VALUES (?,?,?,?,?)";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt,  1, z);
        sqlite3_bind_int(stmt,  2, x);
        sqlite3_bind_int(stmt,  3, tms_y);
        sqlite3_bind_blob(stmt, 4, data.data(), (int)data.size(), SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt,5, (int64_t)std::time(nullptr));
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

std::vector<uint8_t> TileCache::FetchFromOSM(int z, int x, int y) {
    // Subdomains a/b/c for OSM tile servers
    static const char* SUBDOMAIN[] = {"a", "b", "c"};
    std::string url = std::string("https://") + SUBDOMAIN[(x + y) % 3] +
                      ".tile.openstreetmap.org/" +
                      std::to_string(z) + "/" + std::to_string(x) + "/" +
                      std::to_string(y) + ".png";

    std::vector<uint8_t> data;
    CURL* curl = curl_easy_init();
    if (!curl) return data;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "BANDPASS-II/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK || http_code != 200) return {};
    return data;
}

} // namespace bp
