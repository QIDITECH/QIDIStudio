#ifndef slic3r_3DBed_hpp_
#define slic3r_3DBed_hpp_

#include "GLTexture.hpp"
#include "3DScene.hpp"
#include "GLModel.hpp"

#include <libslic3r/BuildVolume.hpp>

#include <tuple>
#include <array>

namespace Slic3r {
namespace GUI {

class GLCanvas3D;

class GeometryBuffer
{
    struct Vertex
    {
        Vec3f position{ Vec3f::Zero() };
        Vec2f tex_coords{ Vec2f::Zero() };
    };

    std::vector<Vertex> m_vertices;

public:
    bool set_from_triangles(const std::vector<Vec2f> &triangles, float z);
    bool set_from_lines(const Lines& lines, float z);
    //QDS: add APi to set from 3d lines
    bool set_from_3d_Lines(const Lines3& lines);

    const float* get_vertices_data() const;
    unsigned int get_vertices_data_size() const { return (unsigned int)m_vertices.size() * get_vertex_data_size(); }
    unsigned int get_vertex_data_size() const { return (unsigned int)(5 * sizeof(float)); }
    size_t get_position_offset() const { return 0; }
    size_t get_tex_coords_offset() const { return (size_t)(3 * sizeof(float)); }
    unsigned int get_vertices_count() const { return (unsigned int)m_vertices.size(); }
};

class Bed3D
{
public:
    static std::array<float, 4> AXIS_X_COLOR;
    static std::array<float, 4> AXIS_Y_COLOR;
    static std::array<float, 4> AXIS_Z_COLOR;

    static void update_render_colors();
    static void load_render_colors();

    class Axes
    {
    public:
        static const float DefaultStemRadius;
        static const float DefaultStemLength;
        static const float DefaultTipRadius;
        static const float DefaultTipLength;

    private:
        Vec3d m_origin{ Vec3d::Zero() };
        float m_stem_length{ DefaultStemLength };
        GLModel m_arrow;

    public:
        const Vec3d& get_origin() const { return m_origin; }
        void set_origin(const Vec3d& origin) { m_origin = origin; }
        void set_stem_length(float length) {
            m_stem_length = length;
            m_arrow.reset();
        }
        float get_total_length() const { return m_stem_length + DefaultTipLength; }
        void render() const;
    };

public:
    enum class Type : unsigned char
    {
        // The print bed model and texture are available from some printer preset.
        System,
        // The print bed model is unknown, thus it is rendered procedurally.
        Custom
    };

private:
    BuildVolume m_build_volume;
    Type m_type{ Type::System };
    //std::string m_texture_filename;
    std::string m_model_filename;
    // Print volume bounding box exteded with axes and model.
    BoundingBoxf3 m_extended_bounding_box;
    // Slightly expanded print bed polygon, for collision detection.
    //Polygon m_polygon;
    GLModel m_triangles;
    //GeometryBuffer m_gridlines;
    GLTexture m_texture;
    // temporary texture shown until the main texture has still no levels compressed
    //GLTexture m_temp_texture;
    GLModel m_model;
    Vec3d m_model_offset{ Vec3d::Zero() };
    unsigned int m_vbo_id{ 0 };
    Axes m_axes;

    float m_scale_factor{ 1.0f };
    //QDS: add part plate related logic
    Vec2d m_position{ Vec2d::Zero() };
    std::vector<Vec2d>  m_bed_shape;
    std::vector<std::vector<Vec2d>> m_extruder_shapes;
    std::vector<double> m_extruder_heights;
    bool m_is_dark = false;

public:
    Bed3D() = default;
    ~Bed3D() { release_VBOs(); }

    // Update print bed model from configuration.
    // Return true if the bed shape changed, so the calee will update the UI.
    //FIXME if the build volume max print height is updated, this function still returns zero
    // as this class does not use it, thus there is no need to update the UI.
    // QDS
    bool set_shape(const Pointfs& printable_area, const double printable_height, std::vector<Pointfs> extruder_areas, std::vector<double> extruder_heights, const std::string& custom_model, bool force_as_custom = false,
        const Vec2d position = Vec2d::Zero(), bool with_reset = true);

    void set_position(Vec2d& position);
    void set_axes_mode(bool origin);
    const Vec2d& get_position() const { return m_position; }

    // Build volume geometry for various collision detection tasks.
    const BuildVolume& build_volume() const { return m_build_volume; }

    // Was the model provided, or was it generated procedurally?
    Type get_type() const { return m_type; }
    // Was the model generated procedurally?
    bool is_custom() const { return m_type == Type::Custom; }

    // Bounding box around the print bed, axes and model, for rendering.
    const BoundingBoxf3& extended_bounding_box() const { return m_extended_bounding_box; }
    BoundingBoxf3 get_cur_bed_model_box();
    const std::string &  get_model_filename() { return m_model_filename; }
    const GLModel &      get_model() { return m_model; }
    // Check against an expanded 2d bounding box.
    //FIXME shall one check against the real build volume?
    bool contains(const Point& point) const;
    Point point_projection(const Point& point) const;

    void render(GLCanvas3D& canvas, bool bottom, float scale_factor, bool show_axes);
    //void render_for_picking(GLCanvas3D& canvas, bool bottom, float scale_factor);

    void on_change_color_mode(bool is_dark);

private:
    //QDS: add partplate related logic
    // Calculate an extended bounding box from axes and current model for visualization purposes.
    BoundingBoxf3 calc_extended_bounding_box(bool consider_model_offset = true) const;
    void calc_triangles(const ExPolygon& poly);
    void calc_gridlines(const ExPolygon& poly, const BoundingBox& bed_bbox);
    void update_model_offset() const;
    //QDS: with offset
    void update_bed_triangles();
    static std::tuple<Type, std::string, std::string> detect_type(const Pointfs& shape);
    void render_internal(GLCanvas3D& canvas, bool bottom, float scale_factor,
        bool show_axes);
    void render_axes() const;
    void render_system(GLCanvas3D& canvas, bool bottom) const;
    //void render_texture(bool bottom, GLCanvas3D& canvas) const;
    void render_model() const;
    void render_custom(GLCanvas3D& canvas, bool bottom);
    void render_default(bool bottom);
    void release_VBOs();
};

} // GUI
} // Slic3r

#endif // slic3r_3DBed_hpp_
