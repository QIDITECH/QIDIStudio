#include <catch2/catch.hpp>

#include <cstdlib>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "libslic3r/Fill/Fill.hpp"
#include "libslic3r/Gpu/VulkanSlicer.hpp"
#include "libslic3r/Surface.hpp"
#include "libslic3r/Utils.hpp"

using namespace Slic3r;

namespace {

using Backend = Gpu::VulkanSlicerBackend;

struct RestoreGpuSettings {
    std::string resources = resources_dir();
    bool had_environment = std::getenv("SLIC3R_GPU_BACKEND") != nullptr;
    std::string environment = had_environment ? std::getenv("SLIC3R_GPU_BACKEND") : "";
    bool compute = Backend::compute_enabled();
    bool cuda = Backend::cuda_enabled();
    bool priority = Backend::gpu_priority_enabled();
    bool strict = Backend::strict_validation();
    uint32_t batch_size = Backend::batch_size();
    Gpu::ComputeBackendPreference preference = Backend::backend_preference();

    RestoreGpuSettings()
    {
        if (const char* directory = std::getenv("SLIC3R_GPU_TEST_RESOURCES"))
            set_resources_dir(directory);
    }

    ~RestoreGpuSettings()
    {
        set_resources_dir(resources);
#ifdef _WIN32
        _putenv_s("SLIC3R_GPU_BACKEND", had_environment ? environment.c_str() : "");
#else
        if (had_environment) setenv("SLIC3R_GPU_BACKEND", environment.c_str(), 1);
        else unsetenv("SLIC3R_GPU_BACKEND");
#endif
        Backend::set_compute_enabled(compute);
        Backend::set_cuda_enabled(cuda);
        Backend::set_gpu_priority_enabled(priority);
        Backend::set_batch_size(batch_size);
        Backend::set_strict_validation(strict);
        Backend::set_backend_preference(preference);
    }
};

void select_backend(Gpu::ComputeBackendPreference preference)
{
    const char* name = preference == Gpu::ComputeBackendPreference::Cuda ? "cuda" :
                       preference == Gpu::ComputeBackendPreference::Vulkan ? "vulkan" : "cpu";
#ifdef _WIN32
    REQUIRE(_putenv_s("SLIC3R_GPU_BACKEND", name) == 0);
#else
    REQUIRE(setenv("SLIC3R_GPU_BACKEND", name, 1) == 0);
#endif
    Backend::set_backend_preference(preference);
    Backend::set_compute_enabled(preference == Gpu::ComputeBackendPreference::Vulkan);
    Backend::set_cuda_enabled(preference == Gpu::ComputeBackendPreference::Cuda);
    Backend::set_gpu_priority_enabled(true);
    Backend::set_batch_size(64);
    Backend::set_strict_validation(true);
    Backend::begin_slicing_session();
}

Polylines fill_fixture(const ExPolygon& polygon, double angle)
{
    std::unique_ptr<Fill> filler(Fill::new_from_type(ipRectilinear));
    REQUIRE(filler != nullptr);
    filler->bounding_box = get_extents(polygon.contour);
    filler->spacing = 0.45;
    filler->angle = float(angle);
    FillParams params;
    params.dont_adjust = true;
    params.density = 0.35f;
    Surface surface(stTop, polygon);
    return filler->fill_surface(&surface, params);
}

void require_identical_paths(const Polylines& expected, const Polylines& actual)
{
    REQUIRE(actual.size() == expected.size());
    for (size_t path = 0; path < expected.size(); ++path) {
        REQUIRE(actual[path].points.size() == expected[path].points.size());
        for (size_t point = 0; point < expected[path].points.size(); ++point) {
            CAPTURE(path, point);
            REQUIRE(actual[path].points[point].x() == expected[path].points[point].x());
            REQUIRE(actual[path].points[point].y() == expected[path].points[point].y());
        }
    }
}

ExPolygons fixtures()
{
    Polygon outer({Point::new_scale(0, 0), Point::new_scale(120, 0),
                   Point::new_scale(120, 100), Point::new_scale(0, 100)});
    Polygon hole({Point::new_scale(30, 25), Point::new_scale(90, 25),
                  Point::new_scale(90, 75), Point::new_scale(30, 75)});
    hole.make_clockwise();
    ExPolygon with_hole(outer);
    with_hole.holes.push_back(hole);

    // Negative coordinates, vertical/horizontal edges and a tangent notch.
    Polygon notch({Point::new_scale(-80, -40), Point::new_scale(-40, -40),
                   Point::new_scale(-40, -10), Point::new_scale(0, 0),
                   Point::new_scale(40, -10), Point::new_scale(40, -40),
                   Point::new_scale(80, -40), Point::new_scale(80, 40),
                   Point::new_scale(-80, 40)});
    return {ExPolygon(outer), with_hole, ExPolygon(notch)};
}

} // namespace

TEST_CASE("Rectilinear compute selection preserves exact CPU paths", "[Fill][Gpu]")
{
    RestoreGpuSettings restore;
    for (const ExPolygon& polygon : fixtures()) {
        for (double angle : {0.0, -PI / 2.0, -PI / 4.0}) {
            CAPTURE(angle);
            select_backend(Gpu::ComputeBackendPreference::Cpu);
            const Polylines cpu = fill_fixture(polygon, angle);
            REQUIRE_FALSE(cpu.empty());
            REQUIRE(Backend::query_runtime_stats().queue_submissions == 0);

            for (auto preference : {Gpu::ComputeBackendPreference::Cuda,
                                    Gpu::ComputeBackendPreference::Vulkan}) {
                select_backend(preference);
                const Polylines selected = fill_fixture(polygon, angle);
                require_identical_paths(cpu, selected);
                require_identical_paths(cpu, fill_fixture(polygon, angle));
                // A missing/uncompiled device legitimately exercises fallback.
                // Only the hidden hardware test below requires real dispatch.
            }
        }
    }
}

TEST_CASE("Rectilinear selected GPU dispatch matches CPU paths", "[.gpu-hardware][Fill][Gpu]")
{
    RestoreGpuSettings restore;
    REQUIRE_FALSE(resources_dir().empty());
    const char* selected = std::getenv("SLIC3R_GPU_BACKEND");
    REQUIRE(selected != nullptr);
    const std::string name(selected);
    REQUIRE((name == "cuda" || name == "vulkan"));
    const auto preference = name == "cuda" ? Gpu::ComputeBackendPreference::Cuda :
                                            Gpu::ComputeBackendPreference::Vulkan;
    const ExPolygon polygon = fixtures()[1];
    select_backend(Gpu::ComputeBackendPreference::Cpu);
    const Polylines cpu = fill_fixture(polygon, -PI / 4.0);
    REQUIRE_FALSE(cpu.empty());

    select_backend(preference);
    require_identical_paths(cpu, fill_fixture(polygon, -PI / 4.0));
    const auto stats = Backend::query_runtime_stats();
    CAPTURE(stats.active_backend, stats.last_diagnostic, stats.queue_submissions);
    REQUIRE(stats.active_backend == (name == "cuda" ? "CUDA" : "Vulkan"));
    REQUIRE(stats.queue_submissions > 0);
    REQUIRE(stats.accepted_gpu_intersections > 0);
    REQUIRE(stats.validation_failures == 0);
}
