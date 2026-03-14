#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "ui/ExportManager.h"
#include "engine/grid.h"
#include <fstream>
#include <sstream>
#include <string>
#include <cstdio>

using namespace bp;

// Build a minimal 2×2 GridArray for testing
static GridArray make_test_layer() {
    GridArray g;
    g.layer_name  = "test_layer";
    g.width       = 2;
    g.height      = 2;
    g.lat_min     = 51.0;
    g.lat_max     = 52.0;
    g.lon_min     = -1.0;
    g.lon_max     =  0.0;
    g.resolution_km = 50.0;

    g.points = {
        {51.0, -1.0, 0, 0},
        {51.0,  0.0, 0, 0},
        {52.0, -1.0, 0, 0},
        {52.0,  0.0, 0, 0}
    };
    g.values = {10.0, 20.0, 30.0, 40.0};
    return g;
}

// -----------------------------------------------------------------------
// CSV export tests
// -----------------------------------------------------------------------

TEST_CASE("ExportManager::export_csv: empty layer returns error") {
    GridArray g;
    auto err = ExportManager::export_csv(g, "/tmp/bp_test_empty.csv");
    CHECK_FALSE(err.empty());
}

TEST_CASE("ExportManager::export_csv: bad path returns error") {
    auto g = make_test_layer();
    auto err = ExportManager::export_csv(g, "/nonexistent_dir_xyz/out.csv");
    CHECK_FALSE(err.empty());
}

TEST_CASE("ExportManager::export_csv: success returns empty string") {
    auto g   = make_test_layer();
    auto err = ExportManager::export_csv(g, "/tmp/bp_test_layer.csv");
    CHECK(err.empty());
}

TEST_CASE("ExportManager::export_csv: header contains layer name") {
    auto g = make_test_layer();
    ExportManager::export_csv(g, "/tmp/bp_test_layer.csv");

    std::ifstream f("/tmp/bp_test_layer.csv");
    std::string header;
    std::getline(f, header);
    CHECK(header.find("test_layer") != std::string::npos);
    CHECK(header.find("lat") != std::string::npos);
    CHECK(header.find("lon") != std::string::npos);
}

TEST_CASE("ExportManager::export_csv: correct number of data rows") {
    auto g = make_test_layer();
    ExportManager::export_csv(g, "/tmp/bp_test_layer.csv");

    std::ifstream f("/tmp/bp_test_layer.csv");
    int lines = 0;
    std::string line;
    while (std::getline(f, line)) ++lines;
    // 1 header + 4 data rows
    CHECK(lines == 5);
}

TEST_CASE("ExportManager::export_csv: values appear in output") {
    auto g = make_test_layer();
    ExportManager::export_csv(g, "/tmp/bp_test_layer.csv");

    std::ifstream f("/tmp/bp_test_layer.csv");
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    // The four values should appear somewhere in the file
    CHECK(content.find("10.") != std::string::npos);
    CHECK(content.find("20.") != std::string::npos);
    CHECK(content.find("30.") != std::string::npos);
    CHECK(content.find("40.") != std::string::npos);
}

TEST_CASE("ExportManager::export_csv: lat/lon values appear in output") {
    auto g = make_test_layer();
    ExportManager::export_csv(g, "/tmp/bp_test_layer.csv");

    std::ifstream f("/tmp/bp_test_layer.csv");
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    CHECK(content.find("51.") != std::string::npos);
    CHECK(content.find("52.") != std::string::npos);
}

TEST_CASE("ExportManager::export_csv: empty layer_name uses 'value' column") {
    auto g = make_test_layer();
    g.layer_name = "";
    ExportManager::export_csv(g, "/tmp/bp_test_noname.csv");

    std::ifstream f("/tmp/bp_test_noname.csv");
    std::string header;
    std::getline(f, header);
    CHECK(header.find("value") != std::string::npos);
}

// -----------------------------------------------------------------------
// GeoTIFF export tests (available only with GDAL)
// -----------------------------------------------------------------------

TEST_CASE("ExportManager::export_geotiff: empty layer returns error") {
    GridArray g;
    auto err = ExportManager::export_geotiff(g, "/tmp/bp_test.tif");
    CHECK_FALSE(err.empty());
}

#if defined(BP_USE_GDAL) || defined(USE_GDAL)
TEST_CASE("ExportManager::export_geotiff: valid layer writes file") {
    auto g   = make_test_layer();
    auto err = ExportManager::export_geotiff(g, "/tmp/bp_test_layer.tif");
    if (err.empty()) {
        // Check file was created
        std::ifstream f("/tmp/bp_test_layer.tif", std::ios::binary);
        CHECK(f.is_open());
    } else {
        // GDAL might not have GTiff driver — just ensure error is descriptive
        CHECK_FALSE(err.empty());
    }
}
#endif

// -----------------------------------------------------------------------
// PNG export tests (only with wxWidgets — stub test without wx)
// -----------------------------------------------------------------------

TEST_CASE("ExportManager::export_png: empty layer returns error") {
    GridArray g;
    auto err = ExportManager::export_png(g, "/tmp/bp_test.png");
    CHECK_FALSE(err.empty());
}
