#include <catch2/catch.hpp>

#include "libslic3r/PrintConfig.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

using namespace Slic3r;

namespace {
const std::array<const char*, 9> enum_keys{{
    "cooling_slowdown_logic", "extruder_type", "filament_metal_stickiness",
    "filament_scarf_seam_type", "nozzle_type", "nozzle_volume_type",
    "overhang_fan_threshold", "overhang_threshold_participating_cooling", "z_hop_types"
}};

const t_config_enum_values* enum_map(const ConfigOption& option)
{
    return option.nullable()
        ? dynamic_cast<const ConfigOptionEnumsGenericNullable&>(option).keys_map
        : dynamic_cast<const ConfigOptionEnumsGeneric&>(option).keys_map;
}

void check_defaults(const ConfigBase& config)
{
    for (const char* key : enum_keys) {
        INFO(key);
        const ConfigOption* option = config.option(key);
        REQUIRE(option != nullptr);
        REQUIRE(option->type() == coEnums);
        const ConfigOptionDef* definition = config.def()->get(key);
        REQUIRE(definition != nullptr);
        REQUIRE(definition->enum_keys_map != nullptr);
        const bool bound = enum_map(*option) == definition->enum_keys_map;
        CHECK(bound);
        // Baseline missing-map failures must never become an unsafe serialize.
        if (bound) {
            std::unique_ptr<ConfigOption> expected(definition->create_default_option());
            REQUIRE(enum_map(*expected) == definition->enum_keys_map);
            CHECK(option->serialize() == expected->serialize());
        }
    }
}
}

TEST_CASE("QIDI enum factories bind the nine print definition maps", "[Config][EnumMaps]")
{
    for (const char* key : enum_keys) {
        INFO(key);
        const ConfigOptionDef* definition = print_config_def.get(key);
        REQUIRE(definition != nullptr);
        REQUIRE(definition->enum_keys_map != nullptr);
        std::unique_ptr<ConfigOption> option(definition->create_default_option());
        REQUIRE(enum_map(*option) == definition->enum_keys_map);
        CHECK_FALSE(option->serialize().empty());
    }
}

TEST_CASE("QIDI default copy and apply paths retain enum maps", "[Config][EnumMaps]")
{
    SECTION("Static cache") { check_defaults(FullPrintConfig::defaults()); }
    SECTION("Static instance") { const FullPrintConfig config; check_defaults(config); }
    SECTION("Static copy") {
        const FullPrintConfig original;
        const FullPrintConfig config(original);
        check_defaults(config);
    }
    SECTION("Static assignment") {
        FullPrintConfig config;
        config = FullPrintConfig::defaults();
        check_defaults(config);
    }
    SECTION("Dynamic full defaults") { check_defaults(DynamicPrintConfig::full_print_config()); }
    SECTION("Dynamic copy") {
        const auto original = DynamicPrintConfig::full_print_config();
        const DynamicPrintConfig config(original);
        check_defaults(config);
    }
    SECTION("Dynamic assignment") {
        DynamicPrintConfig config;
        config = DynamicPrintConfig::full_print_config();
        check_defaults(config);
    }
    SECTION("Dynamic apply from static") {
        DynamicPrintConfig config;
        config.apply(FullPrintConfig::defaults());
        check_defaults(config);
    }
    SECTION("Static apply from dynamic") {
        FullPrintConfig config;
        config.apply(DynamicPrintConfig::full_print_config());
        check_defaults(config);
    }
}

TEST_CASE("QIDI resetting static defaults restores definition enum maps", "[Config][EnumMaps]")
{
    struct ResettableFullPrintConfig : FullPrintConfig {
        void reset() { PrintRegionConfig::set_defaults(); }
    } config;
    config.cooling_slowdown_logic.keys_map = nullptr;
    config.cooling_slowdown_logic.values.clear();
    config.z_hop_types.keys_map = nullptr;
    config.z_hop_types.values.clear();
    config.reset();
    check_defaults(config);
}

TEST_CASE("QIDI enum value assignment preserves the destination map", "[Config][EnumMaps]")
{
    const t_config_enum_values source_map{{"source", 0}};
    const t_config_enum_values destination_map{{"destination", 0}};
    SECTION("Nonnullable") {
        const ConfigOptionEnumsGeneric source(&source_map, 2, 0);
        ConfigOptionEnumsGeneric destination(&destination_map, 1, 0);
        destination.set(&source);
        CHECK(destination.keys_map == &destination_map);
        CHECK(destination.values == std::vector<int>{0, 0});
    }
    SECTION("Nullable") {
        const ConfigOptionEnumsGenericNullable source(&source_map, 2, 0);
        ConfigOptionEnumsGenericNullable destination(&destination_map, 1, 0);
        destination.set(&source);
        CHECK(destination.keys_map == &destination_map);
        CHECK(destination.values == std::vector<int>{0, 0});
    }
}
