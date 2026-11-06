#include <catch2/catch.hpp>

#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <boost/filesystem.hpp>
#include <fstream>
#include <iterator>
#include <string>

TEST_CASE("G-code export accepts an optional processor result", "[GCodeExport][OptionalResult]")
{
    namespace fs = boost::filesystem;
    const fs::path root = fs::temp_directory_path() / fs::unique_path("qidi-export-result-%%%%-%%%%");
    REQUIRE(fs::create_directory(root));
    const fs::path output = root / "output.gcode";
    REQUIRE(output.is_absolute());
    struct Cleanup {
        fs::path output;
        fs::path root;
        ~Cleanup() {
            boost::system::error_code ec;
            fs::remove(output.string() + ".tmp", ec);
            fs::remove(output, ec);
            fs::remove(root, ec);
        }
    } cleanup{output, root};

    Slic3r::Model model;
    Slic3r::Print print;
    for (int index = 0; index < 2; ++index) {
        Slic3r::ModelObject* object = model.add_object();
        object->name = "cube-" + std::to_string(index);
        object->add_volume(Slic3r::make_cube(10, 10, 10));
        object->add_instance()->set_offset(Slic3r::Vec3d(20 + 30 * index, 20, 0));
        object->ensure_on_bed();
        print.auto_assign_extruders(object);
    }
    print.apply(model, Slic3r::DynamicPrintConfig::full_print_config());
    REQUIRE(print.validate().string.empty());
    REQUIRE(print.num_object_instances() == 2);
    print.set_status_silent();
    print.process();

    SECTION("A null result exports without preview data") {
        REQUIRE_NOTHROW(print.export_gcode(output.string(), nullptr, nullptr));
    }
    SECTION("A provided result receives the object label flag") {
        Slic3r::GCodeProcessorResult result;
        result.label_object_enabled = false;
        REQUIRE_NOTHROW(print.export_gcode(output.string(), &result, nullptr));
        REQUIRE(result.filename == output.string());
        REQUIRE(result.label_object_enabled);
    }
    REQUIRE(fs::is_regular_file(output));
    std::ifstream stream(output.string(), std::ios::binary);
    REQUIRE(stream.is_open());
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    REQUIRE_FALSE(text.empty());
    REQUIRE(text.find("G1") != std::string::npos);
    REQUIRE_FALSE(fs::exists(output.string() + ".tmp"));
}
