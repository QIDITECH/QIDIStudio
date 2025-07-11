// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoText.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosCommon.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/Plater.hpp"

#include "libslic3r/Geometry/ConvexHull.hpp"
#include "libslic3r/Model.hpp"

#include "libslic3r/Shape/TextShape.hpp"

#include <numeric>
#include <codecvt>
#include <boost/log/trivial.hpp>

#include <GL/glew.h>

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui/imgui_internal.h>
#include "libslic3r/SVG.hpp"
#include <codecvt>
#include "wx/fontenum.h"
#include "FontUtils.hpp"

namespace Slic3r {
namespace GUI {

static const double PI = 3.141592653589793238;
static const wxColour FONT_TEXTURE_BG = wxColour(0, 0, 0, 0);
static const wxColour FONT_TEXTURE_FG = *wxWHITE;
static const int FONT_SIZE = 12;
static const float SELECTABLE_INNER_OFFSET = 8.0f;

static const wxFontEncoding font_encoding = wxFontEncoding::wxFONTENCODING_SYSTEM;
const std::array<float, 4>  TEXT_GRABBER_COLOR      = {1.0, 1.0, 0.0, 1.0};
const std::array<float, 4>  TEXT_GRABBER_HOVER_COLOR = {0.7, 0.7, 0.0, 1.0};
#ifdef DEBUG_TEXT
std::string                 formatFloat(float val)
{
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << val;
    return ss.str();
}
#endif
std::vector<std::string> init_face_names()
{
    std::vector<std::string> valid_font_names;
    wxArrayString            facenames = wxFontEnumerator::GetFacenames(font_encoding);
    std::vector<wxString>    bad_fonts;

    BOOST_LOG_TRIVIAL(info) << "init_fonts_names start";

    // validation lambda
    auto is_valid_font = [coding = font_encoding, bad = bad_fonts](const wxString &name) {
        if (name.empty())
            return false;

        // vertical font start with @, we will filter it out
        // Not sure if it is only in Windows so filtering is on all platforms
        if (name[0] == '@')
            return false;

        // previously detected bad font
        auto it = std::lower_bound(bad.begin(), bad.end(), name);
        if (it != bad.end() && *it == name)
            return false;

        wxFont wx_font(wxFontInfo().FaceName(name).Encoding(coding));
        // Faster chech if wx_font is loadable but not 100%
        // names could contain not loadable font
        if (!wx_font.IsOk())
            return false;

        if (!can_load(wx_font))
            return false;

        return true;
    };

    std::sort(facenames.begin(), facenames.end());
    for (const wxString &name : facenames) {
        if (is_valid_font(name)) {
            valid_font_names.push_back(name.ToStdString());
        }
        else {
            bad_fonts.emplace_back(name);
        }
    }
    assert(std::is_sorted(bad_fonts.begin(), bad_fonts.end()));

    BOOST_LOG_TRIVIAL(info) << "init_fonts_names end";

    return valid_font_names;
}

class Line_3D
{
public:
    Line_3D(Vec3d i_a, Vec3d i_b) : a(i_a), b(i_b) {}

    double length() { return (b - a).cast<double>().norm(); }

    Vec3d vector()
    {
        Vec3d new_vec = b - a;
        new_vec.normalize();
        return new_vec;
    }

    void reverse() { std::swap(this->a, this->b); }

    Vec3d a;
    Vec3d b;
};

class Polygon_3D
{
public:
    Polygon_3D(const std::vector<Vec3d> &points) : m_points(points) {}

    std::vector<Line_3D> get_lines()
    {
        std::vector<Line_3D> lines;
        lines.reserve(m_points.size());
        if (m_points.size() > 2) {
            for (int i = 0; i < m_points.size() - 1; ++i) { lines.push_back(Line_3D(m_points[i], m_points[i + 1])); }
            lines.push_back(Line_3D(m_points.back(), m_points.front()));
        }
        return lines;
    }
    std::vector<Vec3d> m_points;
};

// for debug
void export_regions_to_svg(const Point &point, const Polygons &polylines)
{
    std::string path = "D:/svg_profiles/text_poly.svg";
    //BoundingBox bbox = get_extents(polylines);
    SVG svg(path.c_str());
    svg.draw(polylines, "green");
    svg.draw(point, "red", 5e6);
}

int preNUm(unsigned char byte)
{
    unsigned char mask = 0x80;
    int           num  = 0;
    for (int i = 0; i < 8; i++) {
        if ((byte & mask) == mask) {
            mask = mask >> 1;
            num++;
        } else {
            break;
        }
    }
    return num;
}

// https://www.jianshu.com/p/a83d398e3606
bool get_utf8_sub_strings(char *data, int len, std::vector<std::string> &out_strs)
{
    out_strs.clear();
    std::string str = std::string(data);

    int num = 0;
    int i   = 0;
    while (i < len) {
        if ((data[i] & 0x80) == 0x00) {
            out_strs.emplace_back(str.substr(i, 1));
            i++;
            continue;
        } else if ((num = preNUm(data[i])) > 2) {
            int start = i;
            i++;
            for (int j = 0; j < num - 1; j++) {
                if ((data[i] & 0xc0) != 0x80) { return false; }
                i++;
            }
            out_strs.emplace_back(str.substr(start, i - start));
        } else {
            return false;
        }
    }
    return true;
}

///////////////////////
/// GLGizmoText start
GLGizmoText::GLGizmoText(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
{
}

GLGizmoText::~GLGizmoText()
{
    if (m_thread.joinable())
        m_thread.join();

    for (int i = 0; i < m_textures.size(); i++) {
        if (m_textures[i].texture != nullptr)
            delete m_textures[i].texture;
    }
}

bool GLGizmoText::on_init()
{
    m_init_texture     = false;
    m_avail_font_names = init_face_names();

    m_thread = std::thread(&GLGizmoText::update_font_status, this);

    //m_avail_font_names = init_occt_fonts();
    //update_font_texture();
    m_scale = m_imgui->get_font_size();
    m_shortcut_key = WXK_CONTROL_T;

    reset_text_info();

    m_desc["rotate_text_caption"] = _L("Shift + Mouse move up or down");
    m_desc["rotate_text"]         = _L("Rotate text");

    return true;
}

void GLGizmoText::update_font_texture()
{
    m_font_names.clear();
    for (int i = 0; i < m_textures.size(); i++) {
        if (m_textures[i].texture != nullptr)
            delete m_textures[i].texture;
    }
    m_combo_width = 0.0f;
    m_combo_height = 0.0f;
    m_textures.clear();
    m_textures.reserve(m_avail_font_names.size());
    for (int i = 0; i < m_avail_font_names.size(); i++)
    {
        GLTexture* texture = new GLTexture();
        auto face = wxString::FromUTF8(m_avail_font_names[i]);
        auto retina_scale = m_parent.get_scale();
        wxFont font { (int)round(retina_scale * FONT_SIZE), wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, face };
        int w, h, hl;
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_font_status[i] && texture->generate_texture_from_text(m_avail_font_names[i], font, w, h, hl, FONT_TEXTURE_BG, FONT_TEXTURE_FG)) {
            //if (h < m_imgui->scaled(2.f)) {
                TextureInfo info;
                info.texture = texture;
                info.w = w;
                info.h = h;
                info.hl = hl;
                info.font_name = m_avail_font_names[i];
                m_textures.push_back(info);
                m_combo_width = std::max(m_combo_width, static_cast<float>(texture->m_original_width));
                m_font_names.push_back(info.font_name);
            //}
        }
    }
    m_combo_height = m_imgui->scaled(32.f / 15.f);
}

bool GLGizmoText::is_mesh_point_clipped(const Vec3d &point, const Transform3d &trafo) const
{
    if (m_c->object_clipper()->get_position() == 0.)
        return false;

    auto  sel_info          = m_c->selection_info();
    Vec3d transformed_point = trafo * point;
    transformed_point(2) += sel_info->get_sla_shift();
    return m_c->object_clipper()->get_clipping_plane()->is_point_clipped(transformed_point);
}

BoundingBoxf3 GLGizmoText::bounding_box() const
{
    BoundingBoxf3                 ret;
    const Selection &             selection = m_parent.get_selection();
    const Selection::IndicesList &idxs      = selection.get_volume_idxs();
    for (unsigned int i : idxs) {
        const GLVolume *volume = selection.get_volume(i);
        if (!volume->is_modifier) ret.merge(volume->transformed_convex_hull_bounding_box());
    }
    return ret;
}

bool GLGizmoText::gizmo_event(SLAGizmoEventType action, const Vec2d &mouse_position, bool shift_down, bool alt_down, bool control_down)
{
    std::string text = std::string(m_text);
    if (text.empty())
        return true;

    const Selection &selection = m_parent.get_selection();
    auto mo                    = selection.get_model()->objects[m_object_idx];
    if (mo == nullptr)
        return true;

    const ModelInstance *mi        = mo->instances[selection.get_instance_idx()];
    const Camera &       camera    = wxGetApp().plater()->get_camera();

    if (action == SLAGizmoEventType::Moving) {
        if (shift_down && !alt_down && !control_down) {
            float angle = m_rotate_angle + 0.5 * (m_mouse_position - mouse_position).y();
            if (angle == 0)
                return true;

            while (angle < 0)
                angle += 360;

            while (angle >= 360)
                angle -= 360;

            m_rotate_angle = angle;
            m_shift_down   = true;
            m_need_update_text = true;
        } else {
            m_shift_down     = false;
            m_origin_mouse_position = mouse_position;
        }
        m_mouse_position = mouse_position;
    }
    else if (action == SLAGizmoEventType::LeftDown) {
        if (!selection.is_empty() && get_hover_id() != -1) {
            start_dragging();
            return true;
        }

        if (m_is_modify)
            return true;

        Plater *plater = wxGetApp().plater();
        if (!plater || m_thickness <= 0)
            return true;

        ModelObject *model_object = selection.get_model()->objects[m_object_idx];
        if (m_preview_text_volume_id > 0) {
            model_object->delete_volume(m_preview_text_volume_id);
            plater->update();
            m_preview_text_volume_id = -1;
        }

        // Precalculate transformations of individual meshes.
        std::vector<Transform3d> trafo_matrices;
        for (const ModelVolume *mv : mo->volumes) {
            if (mv->is_model_part()) { trafo_matrices.emplace_back(mi->get_transformation().get_matrix() * mv->get_matrix()); }
        }

        Vec3f  normal                       = Vec3f::Zero();
        Vec3f  hit                          = Vec3f::Zero();
        size_t facet                        = 0;
        Vec3f  closest_hit                  = Vec3f::Zero();
        Vec3f  closest_normal               = Vec3f::Zero();
        double closest_hit_squared_distance = std::numeric_limits<double>::max();
        int    closest_hit_mesh_id          = -1;

        // Cast a ray on all meshes, pick the closest hit and save it for the respective mesh
        for (int mesh_id = 0; mesh_id < int(trafo_matrices.size()); ++mesh_id) {
            MeshRaycaster mesh_raycaster = MeshRaycaster(mo->volumes[mesh_id]->mesh());

            if (mesh_raycaster.unproject_on_mesh(mouse_position, trafo_matrices[mesh_id], camera, hit, normal,
                                                                           m_c->object_clipper()->get_clipping_plane(), &facet)) {
                // In case this hit is clipped, skip it.
                if (is_mesh_point_clipped(hit.cast<double>(), trafo_matrices[mesh_id]))
                    continue;

                // Is this hit the closest to the camera so far?
                double hit_squared_distance = (camera.get_position() - trafo_matrices[mesh_id] * hit.cast<double>()).squaredNorm();
                if (hit_squared_distance < closest_hit_squared_distance) {
                    closest_hit_squared_distance = hit_squared_distance;
                    closest_hit_mesh_id          = mesh_id;
                    closest_hit                  = hit;
                    closest_normal               = normal;
                }
            }
        }

        if (closest_hit == Vec3f::Zero() && closest_normal == Vec3f::Zero())
            return true;

        m_rr = {mouse_position, closest_hit_mesh_id, closest_hit, closest_normal};//left down

        generate_text_volume(false);
        m_is_modify = true;
        plater->update();
    }

    return true;
}

void GLGizmoText::on_set_state()
{
    if (m_state == EState::On) {
        m_last_text_mv = nullptr;
        m_need_fix     = false;
        m_show_text_normal_reset_tip = false;
        load_init_text();
        if (m_last_text_mv) {
            m_reedit_text = true;
            m_need_fix    = true;
            m_load_text_tran_in_object = m_text_tran_in_object;
            if (m_really_use_surface_calc) {
                m_show_warning_regenerated = true;
                m_need_fix                 = false;
                use_fix_normal_position();
            } else if (m_fix_old_tran_flag && m_font_version == "") {
                m_show_warning_old_tran = false;
                auto  offset     = m_text_tran_in_object.get_offset();
                auto  rotation   = m_text_tran_in_object.get_rotation();
                float eps        = 0.01f;
                int   count      = 0;
                bool  has_rotation      = rotation.norm() > eps;
                count += has_rotation ? 1 : 0;
                auto scaling_factor = m_text_tran_in_object.get_scaling_factor();
                bool  has_scale         = (scaling_factor - Vec3d(1, 1, 1)).norm() > eps;
                count += has_scale ? 1 : 0;
                auto mirror     = m_text_tran_in_object.get_mirror();
                bool has_mirror = (mirror - Vec3d(1, 1, 1)).norm() > eps;
                count += has_mirror ? 1 : 0;

                Geometry::Transformation expert_text_tran_in_world;
                generate_text_tran_in_world(m_fix_text_normal_in_world.cast<double>(), m_text_position_in_world, m_rotate_angle, expert_text_tran_in_world);
                auto                     temp_expert_text_tran_in_object = m_object_cs_to_world_tran.inverse() * expert_text_tran_in_world.get_matrix();
                Geometry::Transformation expert_text_tran_in_object(temp_expert_text_tran_in_object);
                expert_text_tran_in_object.set_offset(m_text_tran_in_object.get_offset());

                if (count >= 2) {
                    m_show_warning_old_tran = true;
                }
                if (m_is_version1_10_xoy) {
                    auto rotate_tran = Geometry::assemble_transform(Vec3d::Zero(), {0.5 * M_PI, 0.0, 0.0});
                    m_load_text_tran_in_object.set_from_transform(m_load_text_tran_in_object.get_matrix() * rotate_tran);
                    m_load_text_tran_in_object.set_offset(m_load_text_tran_in_object.get_offset() + Vec3d(0, 1.65, 0)); // for size 16
                    return;
                }
                //go on
                if (has_rotation && m_show_warning_old_tran == false) {
                    m_show_warning_lost_rotate = true;
                    m_need_fix                 = false;
                    use_fix_normal_position();
                }
                //not need set set_rotation//has_rotation
                if (has_scale) {
                    expert_text_tran_in_object.set_scaling_factor(scaling_factor);
                }
                if (has_mirror) {
                    expert_text_tran_in_object.set_mirror(mirror);
                }
                m_load_text_tran_in_object.set_from_transform(expert_text_tran_in_object.get_matrix());
                if (m_is_version1_9_xoz) {
                    m_load_text_tran_in_object.set_offset(m_load_text_tran_in_object.get_offset());
                }
            }
        }
    }
    else if (m_state == EState::Off) {
        ImGui::FocusWindow(nullptr);//exit cursor
        m_reedit_text     = false;
        m_fix_old_tran_flag = false;
        close_warning_flag_after_close_or_drag();
        reset_text_info();
        delete_temp_preview_text_volume();
        m_parent.use_slope(false);
        m_parent.toggle_model_objects_visibility(true);
    }
}

void GLGizmoText::check_text_type(bool is_surface_text, bool is_keep_horizontal) {
    if (is_surface_text && is_keep_horizontal) {
        m_text_type = TextType::SURFACE_HORIZONAL;
    } else if (is_surface_text) {
        m_text_type = TextType::SURFACE;
    } else if (is_keep_horizontal) {
        m_text_type = TextType::HORIZONAL;
    } else {
        m_text_type = TextType::HORIZONAL;
    }
}

void GLGizmoText::generate_text_tran_in_world(const Vec3d &text_normal_in_world, const Vec3d &text_position_in_world,float rotate_degree, Geometry::Transformation &cur_tran)
{
    Vec3d  temp_normal        = text_normal_in_world;
    Vec3d  cut_plane_in_world = Vec3d::UnitY();
    double epson              = 1e-6;
    if (!(abs(temp_normal.x()) <= epson && abs(temp_normal.y()) <= epson && abs(temp_normal.z()) > epson)) { // temp_normal != Vec3d::UnitZ()
        Vec3d v_plane      = temp_normal.cross(Vec3d::UnitZ());
        cut_plane_in_world = v_plane.cross(temp_normal);
    }
    Vec3d z_dir       = text_normal_in_world;
    Vec3d y_dir       = cut_plane_in_world;
    Vec3d x_dir_world = y_dir.cross(z_dir);
    if (m_text_type == TextType::SURFACE_HORIZONAL && text_normal_in_world != Vec3d::UnitZ()) {
        y_dir                       = Vec3d::UnitZ();
        x_dir_world                 = y_dir.cross(z_dir);
        z_dir                       = x_dir_world.cross(y_dir);
    }
    auto temp_tran = Geometry::generate_transform(x_dir_world, y_dir, z_dir, text_position_in_world);
    Geometry::Transformation rotate_trans;
    rotate_trans.set_rotation(Vec3d(0, 0, Geometry::deg2rad(rotate_degree))); // m_rotate_angle
    cur_tran.set_matrix(temp_tran.get_matrix() * rotate_trans.get_matrix());
}

void GLGizmoText::use_fix_normal_position()
{
    m_text_normal_in_world   = m_fix_text_normal_in_world;
    m_text_position_in_world = m_fix_text_position_in_world;
}

void GLGizmoText::load_init_text()
{
    Plater *plater = wxGetApp().plater();
    if (m_parent.get_selection().is_single_volume() || m_parent.get_selection().is_single_modifier()) {
        ModelVolume *model_volume = m_parent.get_selection().get_selected_single_volume(m_object_idx, m_volume_idx);
        if (model_volume) {
            TextInfo text_info = model_volume->get_text_info();
            if (!text_info.m_text.empty()) {
                if (m_last_text_mv == model_volume) {
                    m_last_text_mv = model_volume;
                    return;
                }
                if (plater) {
                    plater->take_snapshot("enter Text");
                }
                auto box = model_volume->get_mesh_shared_ptr()->bounding_box();
                auto valid_z = text_info.m_embeded_depth + text_info.m_thickness;
                auto text_height = get_text_height(text_info.m_text);
                m_really_use_surface_calc = true;
                int index  = -1;
                for (size_t i = 0; i < 3; i++) {
                    if (abs(box.size()[i] - valid_z) < 0.1) {
                        m_really_use_surface_calc = false;
                        index  = i;
                        break;
                    }
                }
                if (abs(box.size()[1] - text_height) > 0.1) {
                    m_fix_old_tran_flag = true;
                }
                m_last_text_mv = model_volume;
                load_from_text_info(text_info);
                m_text_volume_tran = model_volume->get_matrix();
                m_text_tran_in_object.set_matrix(m_text_volume_tran);
                int                  temp_object_idx;
                auto                 mo         = m_parent.get_selection().get_selected_single_object(temp_object_idx);
                const ModelInstance *mi         = mo->instances[m_parent.get_selection().get_instance_idx()];
                m_object_cs_to_world_tran       = mi->get_transformation().get_matrix();
                auto world_tran                 = m_object_cs_to_world_tran * m_text_volume_tran;
                m_text_tran_in_world.set_matrix(world_tran);
                m_text_position_in_world = m_text_tran_in_world.get_offset();
                m_text_normal_in_world   = m_text_tran_in_world.get_matrix().linear().col(2).cast<float>();
                {
                    TriangleMesh text_attach_mesh(mo->volumes[m_rr.mesh_id]->mesh());
                    text_attach_mesh.transform(mo->volumes[m_rr.mesh_id]->get_matrix());
                    MeshRaycaster temp_ray_caster(text_attach_mesh);
                    Vec3f local_center = m_text_tran_in_object.get_offset().cast<float>();//(m_text_tran_in_object.get_matrix() * box.center()).cast<float>(); //
                    Vec3f         temp_normal;
                    Vec3f         closest_pt                    = temp_ray_caster.get_closest_point(local_center, &temp_normal);
                    m_fix_text_position_in_world = m_object_cs_to_world_tran * closest_pt.cast<double>();
                    m_fix_text_normal_in_world   = (mi->get_transformation().get_matrix_no_offset().cast<float>() * temp_normal).normalized();
                    int   face_id;
                    Vec3f direction = m_text_tran_in_world.get_matrix().linear().col(2).cast<float>();
                    if (index == 2 && abs(box.size()[1] - text_height) < 0.1) {
                        m_is_version1_9_xoz = true;
                        m_fix_old_tran_flag = true;
                    }
                    if (index == 1 && abs(box.size()[2] - text_height) < 0.1) {//for 1.10 version, xoy plane cut,just fix
                        m_is_version1_10_xoy = true;
                        direction = m_text_tran_in_world.get_matrix().linear().col(1).cast<float>();
                        if (!temp_ray_caster.get_closest_point_and_normal(local_center, direction, &closest_pt, &temp_normal, &face_id)) {
                            m_show_warning_error_mesh = true;
                        }
                    }
                    else if (!temp_ray_caster.get_closest_point_and_normal(local_center, -direction, &closest_pt, &temp_normal, &face_id)) {
                        if (!temp_ray_caster.get_closest_point_and_normal(local_center, direction, &closest_pt, &temp_normal, &face_id)) {
                            m_show_warning_error_mesh = true;
                        }
                    }
                }
                // m_rr.mesh_id
                m_need_update_text = false;
                m_is_modify        = true;
            }
        }
    }
}

void  GLGizmoText::data_changed(bool is_serializing) {
    load_init_text();
}

CommonGizmosDataID GLGizmoText::on_get_requirements() const
{
    return CommonGizmosDataID(
          int(CommonGizmosDataID::SelectionInfo)
        | int(CommonGizmosDataID::InstancesHider)
        | int(CommonGizmosDataID::Raycaster)
        | int(CommonGizmosDataID::ObjectClipper));
}

std::string GLGizmoText::on_get_name() const
{
    if (!on_is_activable() && m_state == EState::Off) {
        return _u8L("Text shape") + ":\n" + _u8L("Please select single object.");
    } else {
        return _u8L("Text shape");
    }
}

bool GLGizmoText::on_is_activable() const
{
    // This is assumed in GLCanvas3D::do_rotate, do not change this
    // without updating that function too.
    if (m_parent.get_selection().is_single_full_instance())
        return true;

    int obejct_idx, volume_idx;
    ModelVolume *model_volume = m_parent.get_selection().get_selected_single_volume(obejct_idx, volume_idx);
    if (model_volume)
        return !model_volume->get_text_info().m_text.empty();

    return false;
}

void GLGizmoText::on_render()
{
    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));
    glsafe(::glEnable(GL_DEPTH_TEST));

    std::string text = std::string(m_text);
    if (text.empty()) {
        delete_temp_preview_text_volume();
        return;
    }

    ModelObject *mo = nullptr;
    const Selection &selection = m_parent.get_selection();
    mo = selection.get_model()->objects[m_object_idx];

    if (mo == nullptr) {
        BOOST_LOG_TRIVIAL(info) << boost::format("Text: selected object is null");
        return;
    }

    const Camera& camera = wxGetApp().plater()->get_camera();
    const auto& projection_matrix = camera.get_projection_matrix();
    const auto& view_matrix = camera.get_view_matrix();
    // First check that the mouse pointer is on an object.
    const ModelInstance *mi        = mo->instances[selection.get_instance_idx()];
    Plater *plater = wxGetApp().plater();
    if (!plater)
        return;
#ifdef DEBUG_TEXT
    if (m_text_normal_in_world.norm() > 0.1) { // debug
        Geometry::Transformation tran(m_text_volume_tran);
        if (tran.get_offset().norm() > 1) {
            auto text_volume_tran_world = mi->get_transformation().get_matrix() * m_text_volume_tran;
            render_cross_mark(text_volume_tran_world, Vec3f::Zero());
        }

        render_cross_mark(m_text_tran_in_world, Vec3f::Zero());

        glsafe(::glLineWidth(2.0f));
        ::glBegin(GL_LINES);
        glsafe(::glColor3f(1.0f, 0.0f, 0.0f));

        for (size_t i = 1; i < m_cut_points_in_world.size(); i++) {//draw points
            auto target0 = m_cut_points_in_world[i - 1].cast<float>();
            auto target1 = m_cut_points_in_world[i].cast<float>();
            glsafe(::glVertex3f(target0(0), target0(1), target0(2)));
            glsafe(::glVertex3f(target1(0), target1(1), target1(2)));
        }
        glsafe(::glEnd());
    }
#endif
    if (!m_is_modify || m_shift_down) {//for temp text
        // Precalculate transformations of individual meshes.
        std::vector<Transform3d> trafo_matrices;
        for (const ModelVolume *mv : mo->volumes) {
            if (mv->is_model_part())
                trafo_matrices.emplace_back(mi->get_transformation().get_matrix() * mv->get_matrix());
        }
        // Raycast and return if there's no hit.
        Vec2d mouse_pos;
        if (m_shift_down) {
            if (m_is_modify)
                mouse_pos = m_rr.mouse_position;
            else
                mouse_pos = m_origin_mouse_position;
        }
        else {
            mouse_pos = m_parent.get_local_mouse_position();
        }

        bool position_changed = update_raycast_cache(mouse_pos, camera, trafo_matrices);

        if (m_rr.mesh_id == -1) {
            delete_temp_preview_text_volume();
            return;
        }

        if (!position_changed && !m_need_update_text && !m_shift_down)
            return;
        update_text_pos_normal();
    }

    if (m_is_modify) {
        update_text_pos_normal();
        Geometry::Transformation tran;//= m_text_tran_in_world;
        {
            double   phi;
            Vec3d    rotation_axis;
            Matrix3d rotation_matrix;
            Geometry::rotation_from_two_vectors(Vec3d::UnitZ(), m_text_normal_in_world.cast<double>(), rotation_axis, phi, &rotation_matrix);
            tran.set_matrix((Transform3d) rotation_matrix);
        }
        tran.set_offset(m_text_position_in_world);
        bool                     hover = (m_hover_id == m_move_cube_id);
        std::array<float, 4>     render_color;
        if (hover) {
            render_color = TEXT_GRABBER_HOVER_COLOR;
        } else
            render_color = TEXT_GRABBER_COLOR;
        float fullsize            = get_grabber_size();
        m_move_grabber.center     = tran.get_offset();
        Transform3d rotate_matrix = tran.get_rotation_matrix();
        Transform3d cube_mat      = Geometry::translation_transform(m_move_grabber.center) * rotate_matrix * Geometry::scale_transform(fullsize);
        m_move_grabber.set_model_matrix(cube_mat);
        render_glmodel(m_move_grabber.get_cube(), render_color, view_matrix * cube_mat, projection_matrix);
    }

    delete_temp_preview_text_volume();

    if (m_is_modify && !m_need_update_text)
        return;

    generate_text_volume();
    plater->update();
}

void GLGizmoText::on_render_for_picking()
{
    glsafe(::glDisable(GL_DEPTH_TEST));
    const auto& shader = wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;
    wxGetApp().bind_shader(shader);
    int          obejct_idx, volume_idx;
    ModelVolume *model_volume = m_parent.get_selection().get_selected_single_volume(obejct_idx, volume_idx);
    if (model_volume && !model_volume->get_text_info().m_text.empty()) {
        const Selection &selection = m_parent.get_selection();
        auto             mo        = selection.get_model()->objects[m_object_idx];
        if (mo == nullptr)
            return;
        auto color              = picking_color_component(m_move_cube_id);
        m_move_grabber.color[0] = color[0];
        m_move_grabber.color[1] = color[1];
        m_move_grabber.color[2] = color[2];
        m_move_grabber.color[3] = color[3];
        m_move_grabber.render_for_picking();
    }
    wxGetApp().unbind_shader();
}

void GLGizmoText::on_start_dragging()
{
}

void GLGizmoText::on_stop_dragging()
{
}

void GLGizmoText::on_update(const UpdateData &data)
{
    Vec2d              mouse_pos = Vec2d(data.mouse_pos.x(), data.mouse_pos.y());
    const Selection &selection = m_parent.get_selection();
    auto mo                         = selection.get_model()->objects[m_object_idx];
    if (mo == nullptr)
        return;
    const ModelInstance *mi        = mo->instances[selection.get_instance_idx()];
    const Camera &       camera    = wxGetApp().plater()->get_camera();

    std::vector<Transform3d> trafo_matrices;
    for (const ModelVolume *mv : mo->volumes) {
        if (mv->is_model_part()) { trafo_matrices.emplace_back(mi->get_transformation().get_matrix() * mv->get_matrix()); }
    }

    Vec3f  normal                       = Vec3f::Zero();
    Vec3f  hit                          = Vec3f::Zero();
    size_t facet                        = 0;
    Vec3f  closest_hit                  = Vec3f::Zero();
    Vec3f  closest_normal               = Vec3f::Zero();
    double closest_hit_squared_distance = std::numeric_limits<double>::max();
    int    closest_hit_mesh_id          = -1;

    // Cast a ray on all meshes, pick the closest hit and save it for the respective mesh
    for (int mesh_id = 0; mesh_id < int(trafo_matrices.size()); ++mesh_id) {
        if (mesh_id == m_volume_idx)
            continue;

        MeshRaycaster mesh_raycaster = MeshRaycaster(mo->volumes[mesh_id]->mesh());

        if (mesh_raycaster.unproject_on_mesh(mouse_pos, trafo_matrices[mesh_id], camera, hit, normal, m_c->object_clipper()->get_clipping_plane(),
                                                                       &facet)) {
            // In case this hit is clipped, skip it.
            if (is_mesh_point_clipped(hit.cast<double>(), trafo_matrices[mesh_id])) continue;

            // Is this hit the closest to the camera so far?
            double hit_squared_distance = (camera.get_position() - trafo_matrices[mesh_id] * hit.cast<double>()).squaredNorm();
            if (hit_squared_distance < closest_hit_squared_distance) {
                closest_hit_squared_distance = hit_squared_distance;
                closest_hit_mesh_id          = mesh_id;
                closest_hit                  = hit;
                closest_normal               = normal;
            }
        }
    }

    if (closest_hit == Vec3f::Zero() && closest_normal == Vec3f::Zero()) return;

    if (closest_hit_mesh_id != -1) {
        m_rr = {mouse_pos, closest_hit_mesh_id, closest_hit, closest_normal};//on drag
        m_need_fix         = false;
        close_warning_flag_after_close_or_drag();
        m_need_update_text = true;
    }
}
//B
void GLGizmoText::push_button_style(bool pressed) {
    if (m_is_dark_mode) {
        if (pressed) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(43 / 255.f, 64 / 255.f, 54 / 255.f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(43 / 255.f, 64 / 255.f, 54 / 255.f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(43 / 255.f, 64 / 255.f, 54 / 255.f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(68.f/ 255.f, 121 / 255.f, 251 / 255.f, 1.f));
        }
        else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(45.f / 255.f, 45.f / 255.f, 49.f / 255.f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(84 / 255.f, 84 / 255.f, 90 / 255.f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(84 / 255.f, 84 / 255.f, 90 / 255.f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(45.f / 255.f, 45.f / 255.f, 49.f / 255.f, 1.f));
        }
    }
    else {
        if (pressed) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(219 / 255.f, 253 / 255.f, 231 / 255.f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(219 / 255.f, 253 / 255.f, 231 / 255.f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(219 / 255.f, 253 / 255.f, 231 / 255.f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(68.f /255.f, 121 / 255.f, 251 / 255.f, 1.f));
        }
        else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.f, 1.f, 1.f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(238 / 255.f, 238 / 255.f, 238 / 255.f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(238 / 255.f, 238 / 255.f, 238 / 255.f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.f, 1.f, 1.f, 1.f));
        }

    }
}

void GLGizmoText::pop_button_style() {
    ImGui::PopStyleColor(4);
}

void GLGizmoText::push_combo_style(const float scale) {
    if (m_is_dark_mode) {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 1.0f * scale);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f * scale);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGuiWrapper::COL_WINDOW_BG_DARK);
        // y96
        ImGui::PushStyleColor(ImGuiCol_BorderActive, ImVec4(0.27f, 0.47f, 0.98f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.27f, 0.47f, 0.98f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.27f, 0.47f, 0.98f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.27f, 0.47f, 0.98f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImGuiWrapper::COL_WINDOW_BG_DARK);
        ImGui::PushStyleColor(ImGuiCol_Button, { 1.00f, 1.00f, 1.00f, 0.0f });
    }
    else {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 1.0f * scale);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f * scale);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGuiWrapper::COL_WINDOW_BG);
        // y96
        ImGui::PushStyleColor(ImGuiCol_BorderActive, ImVec4(0.27f, 0.47f, 0.98f, 1.00f));   // y96 change color
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.27f, 0.47f, 0.98f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.27f, 0.47f, 0.98f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.27f, 0.47f, 0.98f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImGuiWrapper::COL_WINDOW_BG);
        ImGui::PushStyleColor(ImGuiCol_Button, { 1.00f, 1.00f, 1.00f, 0.0f });
    }
}

void GLGizmoText::pop_combo_style()
{
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(7);
}

// QDS
void GLGizmoText::on_render_input_window(float x, float y, float bottom_limit)
{
    if (!m_init_texture) {
        update_font_texture();
        m_init_texture = true;
    }

    if (m_imgui->get_font_size() != m_scale) {
        m_scale = m_imgui->get_font_size();
        update_font_texture();
    }
    if (m_textures.size() == 0) {
        BOOST_LOG_TRIVIAL(info) << "GLGizmoText has no texture";
        return;
    }

    const Selection &selection = m_parent.get_selection();
    if (selection.is_single_full_instance() || selection.is_single_full_object()) {
        const GLVolume * gl_volume = selection.get_volume(*selection.get_volume_idxs().begin());
        int object_idx = gl_volume->object_idx();
        if (object_idx != m_object_idx || (object_idx == m_object_idx && m_volume_idx != -1)) {
            m_object_idx = object_idx;
            m_volume_idx = -1;
            reset_text_info();
        }
    } else if (selection.is_single_volume() || selection.is_single_modifier()) {
        int object_idx, volume_idx;
        ModelVolume *model_volume = m_parent.get_selection().get_selected_single_volume(object_idx, volume_idx);
        if ((object_idx != m_object_idx || (object_idx == m_object_idx && volume_idx != m_volume_idx))
            && model_volume) {
            m_last_text_mv     = model_volume;
            TextInfo text_info = model_volume->get_text_info();
            load_from_text_info(text_info);//mouse click down
            m_is_modify = true;
            m_volume_idx = volume_idx;
            m_object_idx = object_idx;
        }
    }

    const float win_h = ImGui::GetWindowHeight();
    y = std::min(y, bottom_limit - win_h);
    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 0.0f, 0.0f);

    static float last_y = 0.0f;
    static float last_h = 0.0f;

    const float currt_scale = m_parent.get_scale();
    ImGuiWrapper::push_toolbar_style(currt_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0,5.0) * currt_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 4.0f * currt_scale);
    GizmoImguiBegin("Text", ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
#ifdef DEBUG_TEXT
    std::string world_hit = "world hit x:" + formatFloat(m_text_position_in_world[0]) + " y:" + formatFloat(m_text_position_in_world[1]) +
                            " z:" + formatFloat(m_text_position_in_world[2]);
    std::string hit = "local hit x:" + formatFloat(m_rr.hit[0]) + " y:" + formatFloat(m_rr.hit[1]) + " z:" + formatFloat(m_rr.hit[2]);
    std::string normal = "normal x:" + formatFloat(m_rr.normal[0]) + " y:" + formatFloat(m_rr.normal[1]) + " z:" + formatFloat(m_rr.normal[2]);
    auto cut_dir = "cut_dir x:" + formatFloat(m_cut_plane_dir_in_world[0]) + " y:" + formatFloat(m_cut_plane_dir_in_world[1]) + " z:" + formatFloat(m_cut_plane_dir_in_world[2]);
    auto fix_position_str = "fix position:" + formatFloat(m_fix_text_position_in_world[0]) + " y:" + formatFloat(m_fix_text_position_in_world[1]) +
                            " z:" + formatFloat(m_fix_text_position_in_world[2]);
    auto fix_normal_str = "fix normal:" + formatFloat(m_fix_text_normal_in_world[0]) + " y:" + formatFloat(m_fix_text_normal_in_world[1]) +
                          " z:" + formatFloat(m_fix_text_normal_in_world[2]);
    m_imgui->text(world_hit);
    m_imgui->text(hit);
    m_imgui->text(normal);
    m_imgui->text(cut_dir);
    m_imgui->text(fix_position_str);
    m_imgui->text(fix_normal_str);
#endif
    float space_size = m_imgui->get_style_scaling() * 8;
    float font_cap = m_imgui->calc_text_size(_L("Font")).x;
    float size_cap = m_imgui->calc_text_size(_L("Size")).x;
    float thickness_cap = m_imgui->calc_text_size(_L("Thickness")).x;
    float input_cap = m_imgui->calc_text_size(_L("Input text")).x;
    float depth_cap = m_imgui->calc_text_size(_L("Embeded")).x;
    float caption_size  = std::max(std::max(font_cap, size_cap), std::max(depth_cap, input_cap)) + space_size + ImGui::GetStyle().WindowPadding.x;

    float input_text_size = m_imgui->scaled(10.0f);
    float button_size = ImGui::GetFrameHeight();

    ImVec2 selectable_size(std::max((input_text_size + ImGui::GetFrameHeight() * 2), m_combo_width + SELECTABLE_INNER_OFFSET * currt_scale), m_combo_height);
    float list_width = selectable_size.x + ImGui::GetStyle().ScrollbarSize + 2 * currt_scale;

    float input_size = list_width - button_size * 2 - ImGui::GetStyle().ItemSpacing.x * 4;

    ImTextureID normal_B = m_parent.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TEXT_B);
    ImTextureID normal_T = m_parent.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TEXT_T);
    ImTextureID normal_B_dark = m_parent.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TEXT_B_DARK);
    ImTextureID normal_T_dark = m_parent.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TEXT_T_DARK);

    // adjust window position to avoid overlap the view toolbar
    if (last_h != win_h || last_y != y) {
        // ask canvas for another frame to render the window in the correct position
        m_imgui->set_requires_extra_frame();
        if (last_h != win_h)
            last_h = win_h;
        if (last_y != y)
            last_y = y;
    }

    ImGui::AlignTextToFramePadding();

    m_imgui->text(_L("Font"));
    ImGui::SameLine(caption_size);
    ImGui::PushItemWidth(list_width);
    push_combo_style(currt_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 4.0f * currt_scale);

    std::vector<int> filtered_items_idx;
    bool is_filtered = false;
    if (m_imgui->qdt_combo_with_filter("##Combo_Font", m_font_names[m_curr_font_idx], m_font_names, &filtered_items_idx, &is_filtered, selectable_size.y)) {
        int show_items_count = is_filtered ? filtered_items_idx.size() : m_textures.size();

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(SELECTABLE_INNER_OFFSET, 0)* currt_scale);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
        for (int i = 0; i < show_items_count; i++)
        {
            int idx = is_filtered ? filtered_items_idx[i] : i;
            const bool is_selected = (idx == m_curr_font_idx);
            ImTextureID icon_id = (ImTextureID)(intptr_t)(m_textures[idx].texture->get_id());
            ImVec4 tint_color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            if (ImGui::QDTImageSelectable(icon_id, selectable_size, { (float)m_textures[idx].w, (float)m_textures[idx].h }, m_textures[idx].hl, tint_color, { 0, 0 }, { 1, 1 }, is_selected))
            {
                m_curr_font_idx = idx;
                m_font_name = m_textures[m_curr_font_idx].font_name;
                ImGui::CloseCurrentPopup();
                m_need_update_text = true;
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::PopStyleVar(3);
        ImGui::EndListBox();
        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(2);
    pop_combo_style();
    ImGui::AlignTextToFramePadding();
    m_imgui->text(_L("Size"));
    ImGui::SameLine(caption_size);
    ImGui::PushItemWidth(input_size);
    if (ImGui::InputFloat("###font_size", &m_font_size, 0.0f, 0.0f, "%.2f")) {
        limit_value(m_font_size, m_font_size_min, m_font_size_max);
        m_need_update_text = true;
    }
    ImGui::SameLine();

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f * currt_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {1.0f * currt_scale, 1.0f * currt_scale });
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f * currt_scale);
    push_button_style(m_bold);
    if (ImGui::ImageButton(m_is_dark_mode ? normal_B_dark : normal_B, {button_size - 2 * ImGui::GetStyle().FramePadding.x, button_size - 2 * ImGui::GetStyle().FramePadding.y})) {
        m_bold = !m_bold;
        m_need_update_text = true;
    }
    pop_button_style();
    ImGui::SameLine();
    push_button_style(m_italic);
    if (ImGui::ImageButton(m_is_dark_mode ? normal_T_dark : normal_T, {button_size - 2 * ImGui::GetStyle().FramePadding.x, button_size - 2 * ImGui::GetStyle().FramePadding.y})) {
        m_italic = !m_italic;
        m_need_update_text = true;
    }
    pop_button_style();
    ImGui::PopStyleVar(3);

    ImGui::AlignTextToFramePadding();
    m_imgui->text(_L("Thickness"));
    ImGui::SameLine(caption_size);
    ImGui::PushItemWidth(list_width);
    float old_value = m_thickness;
    ImGui::InputFloat("###text_thickness", &m_thickness, 0.0f, 0.0f, "%.2f");
    m_thickness = ImClamp(m_thickness, m_thickness_min, m_thickness_max);
    if (old_value != m_thickness)
        m_need_update_text = true;

    const float slider_icon_width = m_imgui->get_slider_icon_size().x;
    const float slider_width      = list_width - 1.5 * slider_icon_width - space_size;
    const float drag_left_width   = caption_size + slider_width + space_size;

    ImGui::AlignTextToFramePadding();
    m_imgui->text(_L("Text Gap"));
    ImGui::SameLine(caption_size);
    ImGui::PushItemWidth(slider_width);
    if (m_imgui->qdt_slider_float_style("##text_gap", &m_text_gap, -100, 100, "%.2f", 1.0f, true))
        m_need_update_text = true;
    ImGui::SameLine(drag_left_width);
    ImGui::PushItemWidth(1.5 * slider_icon_width);
    if (ImGui::QDTDragFloat("##text_gap_input", &m_text_gap, 0.05f, 0.0f, 0.0f, "%.2f"))
        m_need_update_text = true;

    ImGui::AlignTextToFramePadding();
    m_imgui->text(_L("Angle"));
    ImGui::SameLine(caption_size);
    ImGui::PushItemWidth(slider_width);
    if (m_imgui->qdt_slider_float_style("##angle", &m_rotate_angle, 0, 360, "%.2f", 1.0f, true))
        m_need_update_text = true;
    ImGui::SameLine(drag_left_width);
    ImGui::PushItemWidth(1.5 * slider_icon_width);
    if (ImGui::QDTDragFloat("##angle_input", &m_rotate_angle, 0.05f, 0.0f, 0.0f, "%.2f"))
        m_need_update_text = true;

    ImGui::AlignTextToFramePadding();
    m_imgui->text(_L("Embeded\r\ndepth"));
    ImGui::SameLine(caption_size);
    ImGui::PushItemWidth(list_width);
    old_value = m_embeded_depth;
    if (ImGui::InputFloat("###text_embeded_depth", &m_embeded_depth, 0.0f, 0.0f, "%.2f")) {
        limit_value(m_embeded_depth, 0.0f, m_embeded_depth_max);
    }
    if (old_value != m_embeded_depth)
        m_need_update_text = true;

    ImGui::AlignTextToFramePadding();
    m_imgui->text(_L("Input text"));
    ImGui::SameLine(caption_size);
    ImGui::PushItemWidth(list_width);

    if(ImGui::InputText("", m_text, sizeof(m_text)))
        m_need_update_text = true;
    std::string text = std::string(m_text);
    if (text.empty() && m_is_modify) {
        m_imgui->warning_text(_L("Warning:Input cannot be empty!"));
    }
    if (m_show_warning_text_create_fail) {
        m_imgui->warning_text(_L("Warning:create text fail."));
    }
    if (m_show_text_normal_error) {
        m_imgui->warning_text(_L("Warning:text normal is error."));
    }
    if (m_show_text_normal_reset_tip) {
        m_imgui->warning_text(_L("Warning:text normal has been reset."));
    }
    if (m_show_warning_regenerated) {
        m_imgui->warning_text(_L("Warning:Because current text does indeed use surround algorithm,\nif continue to edit, text has to regenerated according to new location."));
    }
    if (m_show_warning_old_tran) {
        m_imgui->warning_text(_L("Warning:old matrix has at least two parameters: mirroring, scaling, and rotation. \nIf you continue editing, it may not be correct. \nPlease dragging text or cancel using current pose, \nsave and reedit again."));
    }
    if (m_show_warning_error_mesh) {
        m_imgui->warning_text(_L("Error:Detecting an incorrect mesh id or an unknown error, \nregenerating text may result in incorrect outcomes.\nPlease drag text,save it then reedit it again."));
    }
    if (m_show_warning_lost_rotate) {
        m_imgui->warning_text(_L("Warning:Due to functional upgrade, rotation information \ncannot be restored. Please drag or modify text,\n save it and reedit it will ok."));
    }
    ImGui::Separator();
    if (m_need_fix && m_text_type > TextType::HORIZONAL) {
        ImGui::AlignTextToFramePadding();
        ImGui::SameLine(caption_size);
        ImGui::PushItemWidth(list_width);
        ImGui::AlignTextToFramePadding();
        if (m_imgui->qdt_checkbox(_L("Use opened text pose"), m_use_current_pose)) {
            m_need_update_text = true;
        }
    }
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 10.0f));
    float get_cur_y = ImGui::GetContentRegionMax().y + ImGui::GetFrameHeight() + y;
    show_tooltip_information(x, get_cur_y);

    float f_scale = m_parent.get_gizmos_manager().get_layout_scale();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f * f_scale));

    ImGui::SameLine(caption_size);
    ImGui::AlignTextToFramePadding();
    auto is_surface_text = m_text_type == TextType::SURFACE || m_text_type == TextType::SURFACE_HORIZONAL;
    if (m_imgui->qdt_checkbox(_L("Surface"), is_surface_text)) {
        m_need_update_text = true;
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    auto is_keep_horizontal = m_text_type == TextType::HORIZONAL || m_text_type == TextType::SURFACE_HORIZONAL;
    if (m_imgui->qdt_checkbox(_L("Horizontal text"), is_keep_horizontal)) {
        m_need_update_text = true;
        if (is_surface_text && is_keep_horizontal == false) {
            update_text_normal_in_world();
        }
    }
    check_text_type(is_surface_text, is_keep_horizontal);

    //ImGui::SameLine();
    //ImGui::AlignTextToFramePadding();
    //m_imgui->text(_L("Status:"));
    //float status_cap = m_imgui->calc_text_size(_L("Status:")).x + space_size + ImGui::GetStyle().WindowPadding.x;
    //ImGui::SameLine();
    //m_imgui->text(m_is_modify ? _L("Modify") : _L("Add"));

    ImGui::PopStyleVar(2);

#if 0
    ImGuiIO& io = ImGui::GetIO();
    ImFontAtlas* atlas = io.Fonts;
    ImVec4 tint_col = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    ImVec4 border_col = ImVec4(0.0f, 0.0f, 0.0f, 0.8f);
    m_imgui->text(wxString("") << atlas->TexWidth << " * " << atlas->TexHeight);
    ImGui::Image(atlas->TexID, ImVec2((float)atlas->TexWidth, (float)atlas->TexHeight), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint_col, border_col);
#endif

    GizmoImguiEnd();
    ImGui::PopStyleVar(2);
    ImGuiWrapper::pop_toolbar_style();
}

void GLGizmoText::show_tooltip_information(float x, float y)
{
    std::array<std::string, 1> info_array  = std::array<std::string, 1>{"rotate_text"};
    float                      caption_max = 0.f;
    for (const auto &t : info_array) { caption_max = std::max(caption_max, m_imgui->calc_text_size(m_desc[t + "_caption"]).x); }

    ImTextureID normal_id = m_parent.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_TOOLTIP);
    ImTextureID hover_id  = m_parent.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_TOOLTIP_HOVER);

    caption_max += m_imgui->calc_text_size(": ").x + 35.f;

    float  font_size   = ImGui::GetFontSize();
    ImVec2 button_size = ImVec2(font_size * 1.8, font_size * 1.3);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, ImGui::GetStyle().FramePadding.y});
    ImGui::ImageButton3(normal_id, hover_id, button_size);

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip2(ImVec2(x, y));
        auto draw_text_with_caption = [this, &caption_max](const wxString &caption, const wxString &text) {
            m_imgui->text_colored(ImGuiWrapper::COL_ACTIVE, caption);
            ImGui::SameLine(caption_max);
            m_imgui->text_colored(ImGuiWrapper::COL_WINDOW_BG, text);
        };

        for (const auto &t : info_array) draw_text_with_caption(m_desc.at(t + "_caption") + ": ", m_desc.at(t));
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar(2);
}

void GLGizmoText::reset_text_info()
{
    m_font_name     = "";
    m_font_size     = 16.f;
    m_curr_font_idx = 0;
    m_bold          = true;
    m_italic        = false;
    m_thickness     = 2.f;
    strcpy(m_text, m_font_name.c_str());
    m_embeded_depth   = 0.f;
    m_rotate_angle    = 0;
    m_text_gap        = 0.f;
    m_text_type       = TextType::SURFACE;
    m_rr              = RaycastResult();

    m_is_modify = false;
}

void GLGizmoText::update_text_pos_normal() {
    if (m_rr.mesh_id < 0) { return; }
    if (m_rr.normal.norm() < 0.1) { return; }
    const Selection &selection = m_parent.get_selection();
    auto mo                    = selection.get_model()->objects[m_object_idx];
    if (mo == nullptr) {
        BOOST_LOG_TRIVIAL(info) << boost::format("Text: selected object is null");
        return;
    }
    const ModelInstance *mi        = mo->instances[selection.get_instance_idx()];

    std::vector<Geometry::Transformation> w_matrices;
    std::vector<Geometry::Transformation> mv_trans;
    for (const ModelVolume *mv : mo->volumes) {
        if (mv->is_model_part()) {
            w_matrices.emplace_back(Geometry::Transformation(mi->get_transformation().get_matrix() * mv->get_matrix()));
            mv_trans.emplace_back(Geometry::Transformation(mv->get_matrix()));
        }
    }
#ifdef DEBUG_TEXT_VALUE
    m_rr.hit    = Vec3f(-0.58, -1.70, -12.8);
    m_rr.normal = Vec3f(0,0,-1);//just rotate cube
#endif
    m_text_position_in_world  = w_matrices[m_rr.mesh_id].get_matrix() * m_rr.hit.cast<double>();
    m_text_normal_in_world    = (w_matrices[m_rr.mesh_id].get_matrix_no_offset().cast<float>() * m_rr.normal).normalized();
}

void GLGizmoText::update_font_status()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_font_status.reserve(m_avail_font_names.size());
    for (std::string font_name : m_avail_font_names) {
        if (!can_generate_text_shape(font_name)) {
            m_font_status.emplace_back(false);
        }
        else {
            m_font_status.emplace_back(true);
        }
    }
}

float GLGizmoText::get_text_height(const std::string &text)
{
    std::wstring_convert<std::codecvt_utf8<wchar_t>> str_cnv;
    std::wstring                                     ws = boost::nowide::widen(text);
    std::vector<std::string>                         alphas;
    for (auto w : ws) {
        alphas.push_back(str_cnv.to_bytes(w));
    }
    auto  texts  = alphas ;
    float max_height = 0.f;
    for (int i = 0; i < texts.size(); ++i) {
        std::string alpha;
        if (texts[i] == " ") {
            alpha = "i";
        } else {
            alpha = texts[i];
        }
        TextResult text_result;
        load_text_shape(alpha.c_str(), m_font_name.c_str(), m_font_size, m_thickness + m_embeded_depth, m_bold, m_italic, text_result);
        auto height = text_result.text_mesh.bounding_box().size()[1];
        if (max_height < height ){
            max_height = height;
        }
    }
    return max_height;
}

void GLGizmoText::close_warning_flag_after_close_or_drag()
{
    m_show_warning_text_create_fail = false;
    m_show_warning_regenerated = false;
    m_show_warning_error_mesh  = false;
    m_show_warning_old_tran    = false;
    m_show_warning_lost_rotate      = false;
    m_show_text_normal_error   = false;
    m_show_text_normal_reset_tip = false;
    m_show_warning_lost_rotate      = false;
    m_is_version1_10_xoy            = false;
    m_is_version1_9_xoz             = false;
}

void GLGizmoText::update_text_normal_in_world()
{
    int          temp_object_idx;
    auto         mo = m_parent.get_selection().get_selected_single_object(temp_object_idx);
    if (mo && m_rr.mesh_id >= 0) {
        const ModelInstance *mi = mo->instances[m_parent.get_selection().get_instance_idx()];
        TriangleMesh text_attach_mesh(mo->volumes[m_rr.mesh_id]->mesh());
        text_attach_mesh.transform(mo->volumes[m_rr.mesh_id]->get_matrix());
        MeshRaycaster temp_ray_caster(text_attach_mesh);
        Vec3f         local_center = m_text_tran_in_object.get_offset().cast<float>(); //(m_text_tran_in_object.get_matrix() * box.center()).cast<float>(); //
        Vec3f         temp_normal;
        Vec3f         closest_pt = temp_ray_caster.get_closest_point(local_center, &temp_normal);
        m_text_normal_in_world = (mi->get_transformation().get_matrix_no_offset().cast<float>() * temp_normal).normalized();
    }
    else {
        BOOST_LOG_TRIVIAL(info) << boost::format("error: update_text_normal_in_world");
    }
}

bool GLGizmoText::update_text_positions(const std::vector<std::string>& texts)
{
    std::vector<double> text_lengths;
    for (int i = 0; i < texts.size(); ++i) {
        std::string alpha;
        if (texts[i] == " ") {
            alpha = "i";
        } else {
            alpha = texts[i];
        }
        TextResult text_result;
        load_text_shape(alpha.c_str(), m_font_name.c_str(), m_font_size, m_thickness + m_embeded_depth, m_bold, m_italic, text_result);
        double half_x_length = text_result.text_width / 2;
        text_lengths.emplace_back(half_x_length);
    }

    int text_num = texts.size();
    m_position_points.clear();
    m_normal_points.clear();

    const Selection &selection    = m_parent.get_selection();
    auto mo = selection.get_model()->objects[m_object_idx];
    if (mo == nullptr)
        return false;

    const ModelInstance *mi        = mo->instances[selection.get_instance_idx()];
    m_model_object_in_world_tran   = mi->get_transformation();
    // Precalculate transformations of individual meshes.
    std::vector<Transform3d> trafo_matrices;
    std::vector<Transform3d> rotate_trafo_matrices;
    for (const ModelVolume *mv : mo->volumes) {
        if (mv->is_model_part()) {
            trafo_matrices.emplace_back(mi->get_transformation().get_matrix() * mv->get_matrix());
            rotate_trafo_matrices.emplace_back(mi->get_transformation().get_matrix(true, false, true, true) * mv->get_matrix(true, false, true, true));
        }
    }

    if (m_rr.mesh_id == -1) {
        BOOST_LOG_TRIVIAL(info) << boost::format("Text: mrr_mesh_id is -1");
        return false;
    }

    // mouse_position_world may is error after user modified
    if (m_need_fix) {
        if (m_text_normal_in_world.norm() < 0.1) {
            m_show_text_normal_reset_tip = true;
        }
        use_fix_normal_position();
    }
    if (m_text_normal_in_world.norm() < 0.1) {
        m_show_text_normal_error = true;
        BOOST_LOG_TRIVIAL(info) << "m_text_normal_in_object is error";
        return false;
    }
    m_show_text_normal_error  = false;
    auto mouse_position_world = m_text_position_in_world.cast<double>();
    auto mouse_normal_world   = m_text_normal_in_world.cast<double>();

    TriangleMesh slice_meshs;
    int mesh_index = 0;
    int volume_index = 0;
    for (int i = 0; i < mo->volumes.size(); ++i) {
        // skip the editing text volume
        if (m_is_modify && m_volume_idx == i)
            continue;

        ModelVolume *mv = mo->volumes[i];
        if (mv->is_model_part()) {
            if (mesh_index == m_rr.mesh_id) {
                volume_index = i;
            }
            TriangleMesh vol_mesh(mv->mesh());
            vol_mesh.transform(mv->get_matrix());
            slice_meshs.merge(vol_mesh);
            mesh_index++;
        }
    }

    ModelVolume* volume = mo->volumes[volume_index];

    generate_text_tran_in_world(m_text_normal_in_world.cast<double>(), m_text_position_in_world, m_rotate_angle, m_text_tran_in_world);

    m_cut_plane_dir_in_world = m_text_tran_in_world.get_matrix().linear().col(1);
    m_text_normal_in_world   = m_text_tran_in_world.get_matrix().linear().col(2).cast<float>();

    // generate clip cs at click pos
    auto text_position_in_object = mi->get_transformation().get_matrix().inverse() * m_text_position_in_world.cast<double>();
    auto rotate_mat_inv          = mi->get_transformation().get_matrix_no_offset().inverse();

    auto text_tran_in_object = mi->get_transformation().get_matrix().inverse() * m_text_tran_in_world.get_matrix(); // Geometry::generate_transform(cs_x_dir, cs_y_dir, cs_z_dir, text_position_in_object); // todo modify by m_text_tran_in_world
    m_text_tran_in_object.set_matrix(text_tran_in_object);
    m_text_cs_to_world_tran = mi->get_transformation().get_matrix() * m_text_tran_in_object.get_matrix();
    auto rotate_tran          = Geometry::assemble_transform(Vec3d::Zero(), {-0.5 * M_PI, 0.0, 0.0});
    if (m_text_type == TextType::HORIZONAL || (m_need_fix && m_use_current_pose)) {
        m_position_points.resize(text_num);
        m_normal_points.resize(text_num);

        Vec3d pos_dir = m_cut_plane_dir_in_world.cross(mouse_normal_world);
        pos_dir.normalize();
        if (text_num % 2 == 1) {
            m_position_points[text_num / 2] = mouse_position_world;
            for (int i = 0; i < text_num / 2; ++i) {
                double left_gap = text_lengths[text_num / 2 - i - 1] + m_text_gap + text_lengths[text_num / 2 - i];
                if (left_gap < 0)
                    left_gap = 0;

                double right_gap = text_lengths[text_num / 2 + i + 1] + m_text_gap + text_lengths[text_num / 2 + i];
                if (right_gap < 0)
                    right_gap = 0;

                m_position_points[text_num / 2 - 1 - i] = m_position_points[text_num / 2 - i] - left_gap * pos_dir;
                m_position_points[text_num / 2 + 1 + i] = m_position_points[text_num / 2 + i] + right_gap * pos_dir;
            }
        } else {
            for (int i = 0; i < text_num / 2; ++i) {
                double left_gap = i == 0 ? (text_lengths[text_num / 2 - i - 1] + m_text_gap / 2) :
                    (text_lengths[text_num / 2 - i - 1] + m_text_gap + text_lengths[text_num / 2 - i]);
                if (left_gap < 0)
                    left_gap = 0;

                double right_gap = i == 0 ? (text_lengths[text_num / 2 + i] + m_text_gap / 2) :
                (text_lengths[text_num / 2 + i] + m_text_gap + text_lengths[text_num / 2 + i - 1]);
                if (right_gap < 0)
                    right_gap = 0;

                if (i == 0) {
                    m_position_points[text_num / 2 - 1 - i] = mouse_position_world - left_gap * pos_dir;
                    m_position_points[text_num / 2 + i]     = mouse_position_world + right_gap * pos_dir;
                    continue;
                }

                m_position_points[text_num / 2 - 1 - i] = m_position_points[text_num / 2 - i] - left_gap * pos_dir;
                m_position_points[text_num / 2 + i]     = m_position_points[text_num / 2 + i - 1] + right_gap * pos_dir;
            }
        }

        for (int i = 0; i < text_num; ++i) {
            m_normal_points[i] = mouse_normal_world;
        }

        return true;
    }

    MeshSlicingParams slicing_params;
    auto cut_tran    = (m_text_tran_in_object.get_matrix() * rotate_tran);
    slicing_params.trafo  = cut_tran.inverse();
    // for debug
    // its_write_obj(slice_meshs.its, "D:/debug_files/mesh.obj");
    // generate polygons
    const Polygons temp_polys = slice_mesh(slice_meshs.its, 0, slicing_params);
    Vec3d          scale_click_pt(scale_(0), scale_(0), 0);
    // for debug
    // export_regions_to_svg(Point(scale_pt.x(), scale_pt.y()), temp_polys);

    Polygons polys = union_(temp_polys);

    auto point_in_line_rectange = [](const Line &line, const Point &point, double& distance) {
        distance = line.distance_to(point);
        return distance < line.length() / 2;
    };

    int            index     = 0;
    double  min_distance = 1e12;
    Polygon        hit_ploy;
    for (const Polygon poly : polys) {
        if (poly.points.size() == 0)
            continue;

        Lines lines = poly.lines();
        for (int i = 0; i < lines.size(); ++i) {
            Line line = lines[i];
            double distance = min_distance;
            if (point_in_line_rectange(line, Point(scale_click_pt.x(), scale_click_pt.y()), distance)) {
                if (distance < min_distance) {
                    min_distance = distance;
                    index = i;
                    hit_ploy = poly;
                }
            }
        }
    }

    if (hit_ploy.points.size() == 0) {
        BOOST_LOG_TRIVIAL(info) << boost::format("Text: the hit polygon is null");
        return false;
    }

    m_cut_points_in_world.clear();
    m_cut_points_in_world.reserve(hit_ploy.points.size());
    for (int i = 0; i < hit_ploy.points.size(); ++i) {
        m_cut_points_in_world.emplace_back(m_text_cs_to_world_tran * rotate_tran * Vec3d(unscale_(hit_ploy.points[i].x()), unscale_(hit_ploy.points[i].y()), 0));
    }

    Polygon_3D new_polygon(m_cut_points_in_world);
    m_position_points.resize(text_num);
    if (text_num % 2 == 1) {
        m_position_points[text_num / 2] = Vec3d(mouse_position_world.x(), mouse_position_world.y(), mouse_position_world.z());

        std::vector<Line_3D>  lines       = new_polygon.get_lines();
        Line_3D   line        = lines[index];
        {
            int    index1      = index;
            double left_length = (mouse_position_world - line.a).cast<double>().norm();
            int    left_num    = text_num / 2;
            while (left_num > 0) {
                double gap_length = (text_lengths[left_num] + m_text_gap + text_lengths[left_num - 1]);
                if (gap_length < 0)
                    gap_length = 0;

                while (gap_length > left_length) {
                    gap_length -= left_length;
                    if (index1 == 0)
                        index1 = lines.size() - 1;
                    else
                        --index1;
                    left_length = lines[index1].length();
                }

                Vec3d direction = lines[index1].vector();
                direction.normalize();
                double distance_to_a = (left_length - gap_length);
                Line_3D   new_line      = lines[index1];

                double norm_value = direction.cast<double>().norm();
                double deta_x     = distance_to_a * direction.x() / norm_value;
                double deta_y     = distance_to_a * direction.y() / norm_value;
                double deta_z     = distance_to_a * direction.z() / norm_value;
                Vec3d  new_pos    = new_line.a + Vec3d(deta_x, deta_y, deta_z);
                left_num--;
                m_position_points[left_num] = new_pos;
                left_length                 = distance_to_a;
            }
        }

        {
            int    index2       = index;
            double right_length = (line.b - mouse_position_world).cast<double>().norm();
            int    right_num    = text_num / 2;
            while (right_num > 0) {
                double gap_length = (text_lengths[text_num - right_num] + m_text_gap + text_lengths[text_num - right_num - 1]);
                if (gap_length < 0)
                    gap_length = 0;

                while (gap_length > right_length) {
                    gap_length -= right_length;
                    if (index2 == lines.size() - 1)
                        index2 = 0;
                    else
                        ++index2;
                    right_length = lines[index2].length();
                }

                Line_3D line2 = lines[index2];
                line2.reverse();
                Vec3d direction = line2.vector();
                direction.normalize();
                double distance_to_b = (right_length - gap_length);
                Line_3D new_line         = lines[index2];

                double norm_value = direction.cast<double>().norm();
                double deta_x     = distance_to_b * direction.x() / norm_value;
                double deta_y     = distance_to_b * direction.y() / norm_value;
                double deta_z     = distance_to_b * direction.z() / norm_value;
                Vec3d new_pos = new_line.b + Vec3d(deta_x, deta_y, deta_z);
                m_position_points[text_num - right_num] = new_pos;
                right_length                            = distance_to_b;
                right_num--;
            }
        }
    }
    else {
        for (int i = 0; i < text_num / 2; ++i) {
            std::vector<Line_3D> lines = new_polygon.get_lines();
            Line_3D              line  = lines[index];
            {
                int    index1      = index;
                double left_length = (mouse_position_world - line.a).cast<double>().norm();
                int    left_num    = text_num / 2;
                for (int i = 0; i < text_num / 2; ++i) {
                    double gap_length = 0;
                    if (i == 0) {
                        gap_length = m_text_gap / 2 + text_lengths[text_num / 2 - 1 - i];
                    }
                    else {
                        gap_length = text_lengths[text_num / 2 - i] + m_text_gap + text_lengths[text_num / 2 - 1 - i];
                    }
                    if (gap_length < 0)
                        gap_length = 0;

                    while (gap_length > left_length) {
                        gap_length -= left_length;
                        if (index1 == 0)
                            index1 = lines.size() - 1;
                        else
                            --index1;
                        left_length = lines[index1].length();
                    }

                    Vec3d direction = lines[index1].vector();
                    direction.normalize();
                    double distance_to_a = (left_length - gap_length);
                    Line_3D   new_line      = lines[index1];

                    double norm_value = direction.cast<double>().norm();
                    double deta_x     = distance_to_a * direction.x() / norm_value;
                    double deta_y     = distance_to_a * direction.y() / norm_value;
                    double deta_z     = distance_to_a * direction.z() / norm_value;
                    Vec3d  new_pos    = new_line.a + Vec3d(deta_x, deta_y,deta_z);

                    m_position_points[text_num / 2 - 1 - i] = new_pos;
                    left_length                         = distance_to_a;
                }
            }

            {
                int    index2       = index;
                double right_length = (line.b - mouse_position_world).cast<double>().norm();
                int    right_num    = text_num / 2;
                double gap_length   = 0;
                for (int i = 0; i < text_num / 2; ++i) {
                    double gap_length = 0;
                    if (i == 0) {
                        gap_length = m_text_gap / 2 + text_lengths[text_num / 2 + i];
                    } else {
                        gap_length = text_lengths[text_num / 2 + i] + m_text_gap + text_lengths[text_num / 2 + i - 1];
                    }
                    if (gap_length < 0)
                        gap_length = 0;

                    while (gap_length > right_length) {
                        gap_length -= right_length;
                        if (index2 == lines.size() - 1)
                            index2 = 0;
                        else
                            ++index2;
                        right_length = lines[index2].length();
                    }

                    Line_3D line2 = lines[index2];
                    line2.reverse();
                    Vec3d direction = line2.vector();
                    direction.normalize();
                    double distance_to_b = (right_length - gap_length);
                    Line_3D   new_line      = lines[index2];

                    double norm_value                       = direction.cast<double>().norm();
                    double deta_x                           = distance_to_b * direction.x() / norm_value;
                    double deta_y                           = distance_to_b * direction.y() / norm_value;
                    double deta_z                           = distance_to_b * direction.z() / norm_value;
                    Vec3d  new_pos                          = new_line.b + Vec3d(deta_x, deta_y, deta_z);
                    m_position_points[text_num / 2 + i]     = new_pos;
                    right_length                            = distance_to_b;
                }
            }
        }
    }

    TriangleMesh mesh       = slice_meshs;
    std::vector<double> mesh_values(m_position_points.size(), 1e9);
    m_normal_points.resize(m_position_points.size());
    auto point_in_triangle_delete_area = [](const Vec3d &point, const Vec3d &point0, const Vec3d &point1, const Vec3d &point2) {
        Vec3d p0_p  = point - point0;
        Vec3d p0_p1 = point1 - point0;
        Vec3d p0_p2 = point2 - point0;
        Vec3d p_p0  = point0 - point;
        Vec3d p_p1  = point1 - point;
        Vec3d p_p2  = point2 - point;

        double s  = p0_p1.cross(p0_p2).norm();
        double s0 = p_p0.cross(p_p1).norm();
        double s1 = p_p1.cross(p_p2).norm();
        double s2 = p_p2.cross(p_p0).norm();

        return abs(s0 + s1 + s2 - s);
    };
    bool is_mirrored =m_model_object_in_world_tran.is_left_handed();
    for (int i = 0; i < m_position_points.size(); ++i) {
        for (auto indice : mesh.its.indices) {
            stl_vertex stl_point0 = mesh.its.vertices[indice[0]];
            stl_vertex stl_point1 = mesh.its.vertices[indice[1]];
            stl_vertex stl_point2 = mesh.its.vertices[indice[2]];

            Vec3d point0 = Vec3d(stl_point0[0], stl_point0[1], stl_point0[2]);
            Vec3d point1 = Vec3d(stl_point1[0], stl_point1[1], stl_point1[2]);
            Vec3d point2 = Vec3d(stl_point2[0], stl_point2[1], stl_point2[2]);

            point0 = mi->get_transformation().get_matrix() * point0;
            point1 = mi->get_transformation().get_matrix() * point1;
            point2 = mi->get_transformation().get_matrix() * point2;

            double abs_area = point_in_triangle_delete_area(m_position_points[i], point0, point1, point2);
            if (mesh_values[i] > abs_area) {
                mesh_values[i] = abs_area;

                Vec3d s1           = point1 - point0;
                Vec3d s2           = point2 - point0;
                m_normal_points[i] = s1.cross(s2);
                m_normal_points[i].normalize();
                if(is_mirrored){
                    m_normal_points[i] = -m_normal_points[i];
                }
            }
        }
    }
    return true;
}

TriangleMesh GLGizmoText::get_text_mesh(const char* text_str, const Vec3d &position, const Vec3d &normal, const Vec3d& text_up_dir)
{
    TextResult   text_result;
    load_text_shape(text_str, m_font_name.c_str(), m_font_size, m_thickness + m_embeded_depth, m_bold, m_italic, text_result);
    TriangleMesh mesh = text_result.text_mesh;


    mesh.translate(-text_result.text_width / 2, -m_font_size / 4, 0);

    double   phi;
    Vec3d    rotation_axis;
    Matrix3d rotation_matrix;
    Geometry::rotation_from_two_vectors(Vec3d::UnitZ(), normal, rotation_axis, phi, &rotation_matrix);
    mesh.rotate(phi, rotation_axis);

    auto project_on_plane = [](const Vec3d& dir, const Vec3d& plane_normal) -> Vec3d {
        return dir - (plane_normal.dot(dir) * plane_normal.dot(plane_normal)) * plane_normal;
    };

    Vec3d old_text_dir = Vec3d::UnitY();
    old_text_dir = rotation_matrix * old_text_dir;
    Vec3d new_text_dir = project_on_plane(text_up_dir, normal);
    new_text_dir.normalize();
    Geometry::rotation_from_two_vectors(old_text_dir, new_text_dir, rotation_axis, phi, &rotation_matrix);

    if (abs(phi - PI) < EPSILON)
        rotation_axis = normal;

    mesh.rotate(phi, rotation_axis);


    Vec3d offset = position - m_embeded_depth * normal;
    mesh.translate(offset.x(), offset.y(), offset.z());

    return mesh;//mesh in object cs
}

bool GLGizmoText::update_raycast_cache(const Vec2d &mouse_position, const Camera &camera, const std::vector<Transform3d> &trafo_matrices)
{
    if (m_rr.mouse_position == mouse_position) {
        return false;
    }

    if (m_is_modify)
        return false;

    Vec3f  normal                       = Vec3f::Zero();
    Vec3f  hit                          = Vec3f::Zero();
    size_t facet                        = 0;
    Vec3f  closest_hit                  = Vec3f::Zero();
    Vec3f  closest_nromal               = Vec3f::Zero();
    double closest_hit_squared_distance = std::numeric_limits<double>::max();
    int    closest_hit_mesh_id          = -1;

    // Cast a ray on all meshes, pick the closest hit and save it for the respective mesh
    for (int mesh_id = 0; mesh_id < int(trafo_matrices.size()); ++mesh_id) {
        if (m_preview_text_volume_id != -1 && mesh_id == int(trafo_matrices.size()) - 1)
            continue;

        if (m_c->raycaster()->raycasters()[mesh_id]->unproject_on_mesh(mouse_position, trafo_matrices[mesh_id], camera, hit, normal, m_c->object_clipper()->get_clipping_plane(),
                                                                       &facet)) {
            // In case this hit is clipped, skip it.
            if (is_mesh_point_clipped(hit.cast<double>(), trafo_matrices[mesh_id]))
                continue;

            double hit_squared_distance = (camera.get_position() - trafo_matrices[mesh_id] * hit.cast<double>()).squaredNorm();
            if (hit_squared_distance < closest_hit_squared_distance) {
                closest_hit_squared_distance = hit_squared_distance;
                closest_hit_mesh_id          = mesh_id;
                closest_hit                  = hit;
                closest_nromal               = normal;
            }
        }
    }

    m_rr = {mouse_position, closest_hit_mesh_id, closest_hit, closest_nromal};//update_raycast_cache berfor click down
    return true;
}

void GLGizmoText::generate_text_volume(bool is_temp)
{
    std::string text = std::string(m_text);
    if (text.empty())
        return;

    std::wstring_convert<std::codecvt_utf8<wchar_t>> str_cnv;
    std::wstring ws = boost::nowide::widen(m_text);
    std::vector<std::string> alphas;
    for (auto w : ws) {
        alphas.push_back(str_cnv.to_bytes(w));
    }

    update_text_positions(alphas);//update m_model_object_in_world_tran

    if (m_position_points.size() == 0)
        return;
    auto  inv_text_cs_in_object           = (m_model_object_in_world_tran.get_matrix() * m_text_tran_in_object.get_matrix()).inverse();
    auto         inv_text_cs_in_object_no_offset = (m_model_object_in_world_tran.get_matrix_no_offset() * m_text_tran_in_object.get_matrix_no_offset()).inverse();
    TriangleMesh mesh;
    for (int i = 0; i < alphas.size(); ++i) {
        auto         position      = inv_text_cs_in_object * m_position_points[i];
        auto         normal        = inv_text_cs_in_object_no_offset * m_normal_points[i];
        auto         cut_plane_dir = inv_text_cs_in_object_no_offset * m_cut_plane_dir_in_world;
        TriangleMesh sub_mesh      = get_text_mesh(alphas[i].c_str(), position, normal, cut_plane_dir);
        mesh.merge(sub_mesh);
    }
    if (mesh.empty())
        return;
    auto center = mesh.bounding_box().center();
    if (abs(mesh.bounding_box().size()[2] - (m_embeded_depth + m_thickness)) < 0.01) {
        mesh.translate(Vec3f(-center.x(), -center.y(), 0)); // align horizontal and vertical center
    }
    else {
        mesh.translate(Vec3f(0, -center.y(), 0)); // align vertical center
    }

    Plater *plater = wxGetApp().plater();
    if (!plater)
        return;

    TextInfo text_info = get_text_info();
    const Selection &selection    = m_parent.get_selection();
    ModelObject *    model_object = selection.get_model()->objects[m_object_idx];
    int             cur_volume_id;
    if (m_is_modify && m_need_update_text) {
        if (m_object_idx == -1 || m_volume_idx == -1) {
            BOOST_LOG_TRIVIAL(error) << boost::format("Text: selected object_idx = %1%, volume_idx = %2%") % m_object_idx % m_volume_idx;
            return;
        }

        if (!is_temp) {
            plater->take_snapshot("Modify Text");
        }
        text_info.m_font_version = CUR_FONT_VERSION;
        ModelVolume *    model_volume     = model_object->volumes[m_volume_idx];
        ModelVolume *    new_model_volume = model_object->add_volume(std::move(mesh),false);
        if (m_need_fix && // m_reedit_text//m_need_fix
            (m_text_type == TextType::HORIZONAL || (m_text_type > TextType::HORIZONAL && m_use_current_pose))) {
            new_model_volume->set_transformation(m_load_text_tran_in_object.get_matrix());
        }
        else {
            new_model_volume->set_transformation(m_text_tran_in_object.get_matrix());
        }
        new_model_volume->set_text_info(text_info);
        new_model_volume->name = model_volume->name;
        new_model_volume->set_type(model_volume->type());
        new_model_volume->config.apply(model_volume->config);
        std::swap(model_object->volumes[m_volume_idx], model_object->volumes.back());
        model_object->delete_volume(model_object->volumes.size() - 1);
        model_object->invalidate_bounding_box();
        plater->update();
        m_text_volume_tran = new_model_volume->get_matrix();
    } else {
        if (!is_temp && m_need_update_text)
            plater->take_snapshot("Add Text");
        ObjectList *obj_list = wxGetApp().obj_list();
        cur_volume_id                   = obj_list->add_text_part(mesh, "text_shape", text_info, m_text_tran_in_object.get_matrix(), is_temp);
        m_preview_text_volume_id       = is_temp ? cur_volume_id : -1;
        if (cur_volume_id >= 0) {
            m_show_warning_text_create_fail = false;
            ModelVolume *text_model_volume = model_object->volumes[cur_volume_id];
            m_text_volume_tran             = text_model_volume->get_matrix();
        }
        else {
            m_show_warning_text_create_fail = true;
        }
    }
    m_need_update_text = false;
    m_show_warning_lost_rotate = false;
}

void GLGizmoText::delete_temp_preview_text_volume()
{
    const Selection &selection = m_parent.get_selection();
    if (m_preview_text_volume_id > 0) {
        ModelObject *model_object = selection.get_model()->objects[m_object_idx];
        if (m_preview_text_volume_id < model_object->volumes.size()) {
            Plater *plater = wxGetApp().plater();
            if (!plater)
                return;

            model_object->delete_volume(m_preview_text_volume_id);

            plater->update();
        }
        m_preview_text_volume_id = -1;
    }
}

TextInfo GLGizmoText::get_text_info()
{
    TextInfo text_info;
    text_info.m_font_name     = m_font_name;
    text_info.m_font_version  = m_font_version;
    text_info.m_font_size     = m_font_size;
    text_info.m_curr_font_idx = m_curr_font_idx;
    text_info.m_bold          = m_bold;
    text_info.m_italic        = m_italic;
    text_info.m_thickness     = m_thickness;
    text_info.m_text          = m_text;
    text_info.m_rr.mesh_id    = m_rr.mesh_id;
    text_info.m_embeded_depth = m_embeded_depth;
    text_info.m_rotate_angle  = m_rotate_angle;
    text_info.m_text_gap      = m_text_gap;
    text_info.m_is_surface_text = m_text_type == TextType::SURFACE || m_text_type == TextType::SURFACE_HORIZONAL;
    text_info.m_keep_horizontal = m_text_type == TextType::HORIZONAL || m_text_type == TextType::SURFACE_HORIZONAL;
    return text_info;
}

void GLGizmoText::load_from_text_info(const TextInfo &text_info)
{
    m_font_name     = text_info.m_font_name;
    m_font_version = text_info.m_font_version;
    m_font_size     = text_info.m_font_size;
    // from other user's computer may exist case:font library size is different
    if (text_info.m_curr_font_idx < m_font_names.size()) {
        m_curr_font_idx = text_info.m_curr_font_idx;
    }
    else {
        m_curr_font_idx = 0;
    }
    m_bold          = text_info.m_bold;
    m_italic        = text_info.m_italic;
    m_thickness     = text_info.m_thickness;
    strcpy(m_text, text_info.m_text.c_str());
    m_rr.mesh_id    = text_info.m_rr.mesh_id;
    m_embeded_depth = text_info.m_embeded_depth;
    m_rotate_angle  = text_info.m_rotate_angle;
    m_text_gap      = text_info.m_text_gap;
    check_text_type(text_info.m_is_surface_text, text_info.m_keep_horizontal);
}

} // namespace GUI
} // namespace Slic3r
