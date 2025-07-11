#include "libslic3r/libslic3r.h"
#include "libslic3r/AppConfig.hpp"

#include "Camera.hpp"
#include "GUI_App.hpp"

#include "Mouse3DController.hpp"
#include "Plater.hpp"

#include <GL/glew.h>

namespace Slic3r {
namespace GUI {

const double Camera::DefaultDistance = 1000.0;
const double Camera::DefaultZoomToBoxMarginFactor = 1.025;
const double Camera::DefaultZoomToVolumesMarginFactor = 1.025;
double Camera::FrustrumMinZRange = 50.0;
double Camera::FrustrumMinNearZ = 100.0;
double Camera::FrustrumZMargin = 10.0;
double Camera::MaxFovDeg = 60.0;
double Camera::ZoomUnit = 0.1;

std::string Camera::get_type_as_string() const
{
    switch (m_type)
    {
    case EType::Unknown:     return "unknown";
    case EType::Perspective: return "perspective";
    default:
    case EType::Ortho:       return "orthographic";
    };
}

void Camera::set_type(EType type)
{
    if (m_type != type && (type == EType::Ortho || type == EType::Perspective)) {
        m_type = type;
        if (m_update_config_on_type_change_enabled) {
            wxGetApp().app_config->set_bool("use_perspective_camera", m_type == EType::Perspective);
            wxGetApp().app_config->save();
        }
    }
}

void Camera::select_next_type()
{
    unsigned char next = (unsigned char)m_type + 1;
    if (next == (unsigned char)EType::Num_types)
        next = 1;

    set_type((EType)next);
}

void Camera::translate(const Vec3d& displacement) {
    if (!displacement.isApprox(Vec3d::Zero())) {
        m_view_matrix.translate(-displacement);
        update_target();
    }
}

void Camera::set_target(const Vec3d& target)
{
    //QDS do not check validation
    //const Vec3d new_target = validate_target(target);
    update_target();
    const Vec3d new_target = target;
    const Vec3d new_displacement = new_target - m_target;
    if (!new_displacement.isApprox(Vec3d::Zero())) {
        m_target = new_target;
        m_view_matrix.translate(-new_displacement);
    }
}

void Camera::set_zoom(double zoom)
{
    // Don't allow to zoom too far outside the scene.
    const double zoom_min = min_zoom();
    if (zoom_min > 0.0)
        zoom = std::max(zoom, zoom_min);

    // Don't allow to zoom too close to the scene.
    m_zoom = std::min(zoom, max_zoom());
}

void Camera::select_view(const std::string& direction)
{
    if (direction == "iso")
        set_iso_orientation();
    else if (direction == "iso_1")
        set_iso_orientation(ViewAngleType::Iso_1);
    else if (direction == "iso_2")
        set_iso_orientation(ViewAngleType::Iso_2);
    else if (direction == "iso_3")
        set_iso_orientation(ViewAngleType::Iso_3);
    else if (direction == "left")
        look_at(m_target - m_distance * Vec3d::UnitX(), m_target, Vec3d::UnitZ());
    else if (direction == "right")
        look_at(m_target + m_distance * Vec3d::UnitX(), m_target, Vec3d::UnitZ());
    else if (direction == "top")
        look_at(m_target + m_distance * Vec3d::UnitZ(), m_target, Vec3d::UnitY());
    else if (direction == "bottom")
        look_at(m_target - m_distance * Vec3d::UnitZ(), m_target, -Vec3d::UnitY());
    else if (direction == "front")
        look_at(m_target - m_distance * Vec3d::UnitY(), m_target, Vec3d::UnitZ());
    else if (direction == "rear")
        look_at(m_target + m_distance * Vec3d::UnitY(), m_target, Vec3d::UnitZ());
    else if (direction == "topfront" || "plate")
        look_at(m_target - 0.707 * m_distance * Vec3d::UnitY() + 0.707 * m_distance * Vec3d::UnitZ(), m_target, Vec3d::UnitY() + Vec3d::UnitZ());
}
void Camera::select_view(ViewAngleType type)
{
    switch (type) {
    case Slic3r::GUI::Camera::ViewAngleType::Iso: {
        select_view("iso");
        break;
    }
    case Slic3r::GUI::Camera::ViewAngleType::Iso_1: {
        select_view("iso_1");
        break;
    }
    case Slic3r::GUI::Camera::ViewAngleType::Iso_2: {
        select_view("iso_2");
        break;
    }
    case Slic3r::GUI::Camera::ViewAngleType::Iso_3: {
        select_view("iso_3");
        break;
    }
    case Slic3r::GUI::Camera::ViewAngleType::Top_Front: {
        select_view("topfront");
        break;
    }
    case Slic3r::GUI::Camera::ViewAngleType::Left: {
        select_view("left");
        break;
    }
    case Slic3r::GUI::Camera::ViewAngleType::Right: {
        select_view("right");
        break;
    }
    case Slic3r::GUI::Camera::ViewAngleType::Top_Plate:
    case Slic3r::GUI::Camera::ViewAngleType::Top: {
        select_view("top");
        break;
    }
    case Slic3r::GUI::Camera::ViewAngleType::Bottom: {
        select_view("bottom");
        break;
    }
    case Slic3r::GUI::Camera::ViewAngleType::Front: {
        select_view("front");
        break;
    }
    case Slic3r::GUI::Camera::ViewAngleType::Rear: {
        select_view("rear");
        break;
    }
    default: break;
    }
}

const Transform3d Camera::get_view_matrix_for_billboard() const
{
    Transform3d view_matrix_for_billboard{ Transform3d::Identity() };
    double gui_scale = get_gui_scale();
    view_matrix_for_billboard.data()[3 * 4 + 0] = 0.0f;
    view_matrix_for_billboard.data()[3 * 4 + 1] = 0.0f;
    view_matrix_for_billboard.data()[3 * 4 + 2] = -(get_near_z() + 0.10);

    view_matrix_for_billboard.data()[0 * 4 + 0] = gui_scale;
    view_matrix_for_billboard.data()[1 * 4 + 1] = gui_scale;
    view_matrix_for_billboard.data()[2 * 4 + 2] = 1.0f;

    return view_matrix_for_billboard;
}
//how to use
//BoundingBox bbox = mesh.aabb.transform(transform);
//return camera_->getFrustum().intersects(bbox);
void Camera::debug_frustum()
{
    /*ImGuiWrapper &imgui = *wxGetApp().imgui();
    imgui.begin(std::string("Camera debug_frusm"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    Vec3f              frustum_min  = m_frustum.bbox.min.cast<float>();
    Vec3f              frustum_max = m_frustum.bbox.max.cast<float>();
    Vec3f              _0_normal  = m_frustum.planes[0].getNormal().cast<float>();
    Vec3f              _0_corner    = m_frustum.corners[0].cast<float>();
    Vec3f              _1_corner    = m_frustum.corners[1].cast<float>();

    ImGui::InputFloat3("m_last_eye", m_last_eye.data(), "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat3("m_last_center", m_last_center.data(), "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat3("m_last_up", m_last_up.data(), "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat3("frustum_min", frustum_min.data(), "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat3("frustum_max", frustum_max.data(), "%.6f", ImGuiInputTextFlags_ReadOnly);
    for (size_t i = 0; i < 8; i++) {
        std::string name = "corner" + std::to_string(i);
        ImGui::InputFloat3(name.c_str(), m_frustum.corners[i].data(), "%.6f", ImGuiInputTextFlags_ReadOnly);
    }
    for (size_t i = 0; i < 6; i++) {
        std::string name = "plane_normal" + std::to_string(i);
        Vec3f       normal = m_frustum.planes[i].getNormal();
        ImGui::InputFloat3(name.c_str(), normal.data(), "%.6f", ImGuiInputTextFlags_ReadOnly);

        name   = "plane_center" + std::to_string(i);
        Vec3f center = m_frustum.planes[i].getCenter();
        ImGui::InputFloat3(name.c_str(), center.data(), "%.6f", ImGuiInputTextFlags_ReadOnly);
    }
    imgui.end();*/
}

void Camera::update_frustum()
{
    // see https://www8.cs.umu.se/kurser/5DV051/HT12/lab/plane_extraction.pdf
    const auto vp = m_projection_matrix.matrix() * m_view_matrix.matrix();
    const auto& vp_matrix = vp.eval();

    const auto vp_data = vp_matrix.data();
    // left
    float a = vp_data[0 * 4 + 3] + vp_data[0 * 4 + 0];
    float b = vp_data[1 * 4 + 3] + vp_data[1 * 4 + 0];
    float c = vp_data[2 * 4 + 3] + vp_data[2 * 4 + 0];
    float d = vp_data[3 * 4 + 3] + vp_data[3 * 4 + 0];
    m_frustum.planes[0].set_abcd(a, b, c, d);
    m_frustum.planes[0].normailze();

    // right
    a = vp_data[0 * 4 + 3] - vp_data[0 * 4 + 0];
    b = vp_data[1 * 4 + 3] - vp_data[1 * 4 + 0];
    c = vp_data[2 * 4 + 3] - vp_data[2 * 4 + 0];
    d = vp_data[3 * 4 + 3] - vp_data[3 * 4 + 0];
    m_frustum.planes[1].set_abcd(a, b, c, d);
    m_frustum.planes[1].normailze();

    // bottom
    a = vp_data[0 * 4 + 3] + vp_data[0 * 4 + 1];
    b = vp_data[1 * 4 + 3] + vp_data[1 * 4 + 1];
    c = vp_data[2 * 4 + 3] + vp_data[2 * 4 + 1];
    d = vp_data[3 * 4 + 3] + vp_data[3 * 4 + 1];
    m_frustum.planes[2].set_abcd(a, b, c, d);
    m_frustum.planes[2].normailze();

    // top
    a = vp_data[0 * 4 + 3] - vp_data[0 * 4 + 1];
    b = vp_data[1 * 4 + 3] - vp_data[1 * 4 + 1];
    c = vp_data[2 * 4 + 3] - vp_data[2 * 4 + 1];
    d = vp_data[3 * 4 + 3] - vp_data[3 * 4 + 1];
    m_frustum.planes[3].set_abcd(a, b, c, d);
    m_frustum.planes[3].normailze();

    // near
    a = vp_data[0 * 4 + 3] + vp_data[0 * 4 + 2];
    b = vp_data[1 * 4 + 3] + vp_data[1 * 4 + 2];
    c = vp_data[2 * 4 + 3] + vp_data[2 * 4 + 2];
    d = vp_data[3 * 4 + 3] + vp_data[3 * 4 + 2];
    m_frustum.planes[4].set_abcd(a, b, c, d);
    m_frustum.planes[4].normailze();

    // far
    a = vp_data[0 * 4 + 3] - vp_data[0 * 4 + 2];
    b = vp_data[1 * 4 + 3] - vp_data[1 * 4 + 2];
    c = vp_data[2 * 4 + 3] - vp_data[2 * 4 + 2];
    d = vp_data[3 * 4 + 3] - vp_data[3 * 4 + 2];
    m_frustum.planes[5].set_abcd(a, b, c, d);
    m_frustum.planes[5].normailze();
}

double Camera::get_fov() const
{
    switch (m_type)
    {
    case EType::Perspective:
        return 2.0 * Geometry::rad2deg(std::atan(1.0 / m_projection_matrix.matrix()(1, 1)));
    default:
    case EType::Ortho:
        return 0.0;
    };
}

void Camera::apply_viewport(int x, int y, unsigned int w, unsigned int h)
{
    glsafe(::glViewport(x, y, w, h));
    m_viewport[0] = x;
    m_viewport[1] = y;
    m_viewport[2] = w;
    m_viewport[3] = h;
}

void Camera::apply_projection(const BoundingBoxf3& box, double near_z, double far_z)
{
    double w = 0.0;
    double h = 0.0;

    m_frustrum_zs = calc_tight_frustrum_zs_around(box);

    if (near_z > 0.0)
        m_frustrum_zs.first = std::max(std::min(m_frustrum_zs.first, near_z), FrustrumMinNearZ);

    if (far_z > 0.0)
        m_frustrum_zs.second = std::max(m_frustrum_zs.second, far_z);

    w = 0.5 * (double)m_viewport[2];
    h = 0.5 * (double)m_viewport[3];

    const double inv_zoom = get_inv_zoom();
    w *= inv_zoom;
    h *= inv_zoom;

    switch (m_type)
    {
    default:
    case EType::Ortho:
    {
        m_gui_scale = 1.0;
        break;
    }
    case EType::Perspective:
    {
        // scale near plane to keep w and h constant on the plane at z = m_distance
        const double scale = m_frustrum_zs.first / m_distance;
        w *= scale;
        h *= scale;
        m_gui_scale = scale;
        break;
    }
    }

    switch (m_type)
    {
    default:
    case EType::Ortho:
    {
        // see https://registry.khronos.org/OpenGL-Refpages/gl2.1/xhtml/glOrtho.xml
        //glsafe(::glOrtho(-w, w, -h, h, m_frustrum_zs.first, m_frustrum_zs.second));
        m_projection_matrix(0, 0) = 2.0f / (w - (-w));
        m_projection_matrix(0, 1) = 0.0f;
        m_projection_matrix(0, 2) = 0.0f;
        m_projection_matrix(0, 3) = -(w + (-w)) / (w - (-w));
        m_projection_matrix(1, 0) = 0.0f;
        m_projection_matrix(1, 1) = 2.0f / (h - (-h));
        m_projection_matrix(1, 2) = 0.0f;
        m_projection_matrix(1, 3) = -(h + (-h)) / (h - (-h));
        m_projection_matrix(2, 0) = 0.0f;
        m_projection_matrix(2, 1) = 0.0f;
        m_projection_matrix(2, 2) = -2.0f / (m_frustrum_zs.second - m_frustrum_zs.first);
        m_projection_matrix(2, 3) = -(m_frustrum_zs.second + m_frustrum_zs.first) / (m_frustrum_zs.second - m_frustrum_zs.first);
        m_projection_matrix(3, 0) = 0.0f;
        m_projection_matrix(3, 1) = 0.0f;
        m_projection_matrix(3, 2) = 0.0f;
        m_projection_matrix(3, 3) = 1.0f;
        break;
    }
    case EType::Perspective:
    {
        // see https://registry.khronos.org/OpenGL-Refpages/gl2.1/xhtml/glFrustum.xml
        //glsafe(::glFrustum(-w, w, -h, h, m_frustrum_zs.first, m_frustrum_zs.second));
        m_projection_matrix(0, 0) = 2.0f * m_frustrum_zs.first / (w - (-w));
        m_projection_matrix(0, 1) = 0.0f;
        m_projection_matrix(0, 2) = (w + (-w)) / (w - (-w));
        m_projection_matrix(0, 3) = 0.0f;
        m_projection_matrix(1, 0) = 0.0f;
        m_projection_matrix(1, 1) = 2.0f * m_frustrum_zs.first / (h - (-h));
        m_projection_matrix(1, 2) = (h + (-h)) / (h - (-h));
        m_projection_matrix(1, 3) = 0.0f;
        m_projection_matrix(2, 0) = 0.0f;
        m_projection_matrix(2, 1) = 0.0f;
        m_projection_matrix(2, 2) = -(m_frustrum_zs.second + m_frustrum_zs.first) / (m_frustrum_zs.second - m_frustrum_zs.first);
        m_projection_matrix(2, 3) = -2.0f * m_frustrum_zs.second * m_frustrum_zs.first / (m_frustrum_zs.second - m_frustrum_zs.first);

        m_projection_matrix(3, 0) = 0.0f;
        m_projection_matrix(3, 1) = 0.0f;
        m_projection_matrix(3, 2) = -1.0f;
        m_projection_matrix(3, 3) = 0.0f;
        break;
    }
    }
}

void Camera::zoom_to_box(const BoundingBoxf3& box, double margin_factor)
{
    // Calculate the zoom factor needed to adjust the view around the given box.
    const double zoom = calc_zoom_to_bounding_box_factor(box, margin_factor);
    if (zoom > 0.0) {
        m_zoom = zoom;
        // center view around box center
        set_target(box.center());
    }
}

void Camera::zoom_to_volumes(const GLVolumePtrs& volumes, double margin_factor)
{
    Vec3d center;
    const double zoom = calc_zoom_to_volumes_factor(volumes, center, margin_factor);
    if (zoom > 0.0) {
        m_zoom = zoom;
        // center view around the calculated center
        set_target(center);
    }
}

void Camera::debug_render()
{
    ImGuiWrapper& imgui = *wxGetApp().imgui();
    imgui.begin(std::string("Camera statistics"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    std::string type = get_type_as_string();
    if (wxGetApp().plater()->get_mouse3d_controller().connected()
#ifdef SUPPORT_FREE_CAMERA
        || (wxGetApp().app_config->get("use_free_camera") == "1")
#endif
        )
        type += "/free";
    else
        type += "/constrained";

    Vec3f position = get_position().cast<float>();
    Vec3f target = m_target.cast<float>();
    float distance = (float)get_distance();
    float zenit = (float)m_zenit;
    Vec3f forward = get_dir_forward().cast<float>();
    Vec3f right = get_dir_right().cast<float>();
    Vec3f up = get_dir_up().cast<float>();
    float nearZ = (float)m_frustrum_zs.first;
    float farZ = (float)m_frustrum_zs.second;
    float deltaZ = farZ - nearZ;
    float zoom = (float)m_zoom;
    float fov = (float)get_fov();
    std::array<int, 4>viewport = get_viewport();
    float gui_scale = (float)get_gui_scale();

    ImGui::InputText("Type", type.data(), type.length(), ImGuiInputTextFlags_ReadOnly);
    ImGui::Separator();
    ImGui::InputFloat3("Position", position.data(), "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat3("Target", target.data(), "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat("Distance", &distance, 0.0f, 0.0f, "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::Separator();
    ImGui::InputFloat("Zenit", &zenit, 0.0f, 0.0f, "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::Separator();
    ImGui::InputFloat3("Forward", forward.data(), "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat3("Right", right.data(), "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat3("Up", up.data(), "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::Separator();
    ImGui::InputFloat("Near Z", &nearZ, 0.0f, 0.0f, "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat("Far Z", &farZ, 0.0f, 0.0f, "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat("Delta Z", &deltaZ, 0.0f, 0.0f, "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::Separator();
    ImGui::InputFloat("Zoom", &zoom, 0.0f, 0.0f, "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat("Fov", &fov, 0.0f, 0.0f, "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::Separator();
    ImGui::InputInt4("Viewport", viewport.data(), ImGuiInputTextFlags_ReadOnly);
    ImGui::Separator();
    ImGui::InputFloat("GUI scale", &gui_scale, 0.0f, 0.0f, "%.6f", ImGuiInputTextFlags_ReadOnly);
    imgui.end();
}

void Camera::rotate_on_sphere_with_target(double delta_azimut_rad, double delta_zenit_rad, bool apply_limits, Vec3d target)
{
    m_zenit += Geometry::rad2deg(delta_zenit_rad);
    if (apply_limits) {
        if (m_zenit > 90.0f) {
            delta_zenit_rad -= Geometry::deg2rad(m_zenit - 90.0f);
            m_zenit = 90.0f;
        }
        else if (m_zenit < -90.0f) {
            delta_zenit_rad -= Geometry::deg2rad(m_zenit + 90.0f);
            m_zenit = -90.0f;
        }
    }

    Vec3d translation = m_view_matrix.translation() + m_view_rotation * target;
    auto rot_z = Eigen::AngleAxisd(delta_azimut_rad, Vec3d::UnitZ());
    m_view_rotation *= rot_z * Eigen::AngleAxisd(delta_zenit_rad, rot_z.inverse() * get_dir_right());
    m_view_rotation.normalize();
    m_view_matrix.fromPositionOrientationScale(m_view_rotation * (-target) + translation, m_view_rotation, Vec3d(1., 1., 1.));
}


void Camera::rotate_on_sphere(double delta_azimut_rad, double delta_zenit_rad, bool apply_limits)
{
    m_zenit += Geometry::rad2deg(delta_zenit_rad);
    if (apply_limits) {
        if (m_zenit > 90.0f) {
            delta_zenit_rad -= Geometry::deg2rad(m_zenit - 90.0f);
            m_zenit = 90.0f;
        }
        else if (m_zenit < -90.0f) {
            delta_zenit_rad -= Geometry::deg2rad(m_zenit + 90.0f);
            m_zenit = -90.0f;
        }
    }

    const Vec3d translation = m_view_matrix.translation() + m_view_rotation * m_target;
    const auto rot_z = Eigen::AngleAxisd(delta_azimut_rad, Vec3d::UnitZ());
    m_view_rotation *= rot_z * Eigen::AngleAxisd(delta_zenit_rad, rot_z.inverse() * get_dir_right());
    m_view_rotation.normalize();
    m_view_matrix.fromPositionOrientationScale(m_view_rotation * (- m_target) + translation, m_view_rotation, Vec3d(1., 1., 1.));
}

//QDS rotate with target
void Camera::rotate_local_with_target(const Vec3d& rotation_rad, Vec3d target)
{
    double angle = rotation_rad.norm();
    if (std::abs(angle) > EPSILON) {
	    Vec3d translation = m_view_matrix.translation() + m_view_rotation * target;
	    Vec3d axis        = m_view_rotation.conjugate() * rotation_rad.normalized();
        m_view_rotation *= Eigen::Quaterniond(Eigen::AngleAxisd(angle, axis));
        m_view_rotation.normalize();
	    m_view_matrix.fromPositionOrientationScale(m_view_rotation * (-target) + translation, m_view_rotation, Vec3d(1., 1., 1.));
	    update_zenit();
	}
}

void Camera::calc_horizontal_rotate_rad(float &rotation_rad) {
    if (is_looking_front()) {
        auto right   = get_dir_right();
        auto temp_rotation_rad = acos(right.dot(Vec3d(1, 0, 0)));
        auto value = Vec3d(1, 0, 0).cross(right);
        if (value.z() > 0.01) {
            temp_rotation_rad = -temp_rotation_rad;
        }
        rotation_rad = temp_rotation_rad;
    }
}

// Virtual trackball, rotate around an axis, where the eucledian norm of the axis gives the rotation angle in radians.
void Camera::rotate_local_around_target(const Vec3d& rotation_rad)
{
    const double angle = rotation_rad.norm();
    if (std::abs(angle) > EPSILON) {
        const Vec3d translation = m_view_matrix.translation() + m_view_rotation * m_target;
        const Vec3d axis = m_view_rotation.conjugate() * rotation_rad.normalized();
        m_view_rotation *= Eigen::Quaterniond(Eigen::AngleAxisd(angle, axis));
        m_view_rotation.normalize();
	    m_view_matrix.fromPositionOrientationScale(m_view_rotation * (-m_target) + translation, m_view_rotation, Vec3d(1., 1., 1.));
	    update_zenit();
	}
}

void Camera::set_rotation(const Transform3d &rotation)
{
    const Vec3d translation = m_view_matrix.translation() + m_view_rotation * m_target;
    m_view_rotation         = Eigen::Quaterniond(rotation.matrix().template block<3, 3>(0, 0));
    m_view_rotation.normalize();
    m_view_matrix.fromPositionOrientationScale(m_view_rotation * (-m_target) + translation, m_view_rotation, Vec3d(1., 1., 1.));
    update_zenit();
}

std::pair<double, double> Camera::calc_tight_frustrum_zs_around(const BoundingBoxf3& box)
{
    std::pair<double, double> ret;
    auto& [near_z, far_z] = ret;
    // box in eye space
    const BoundingBoxf3 eye_box = box.transformed(m_view_matrix);
    near_z = -eye_box.max(2);
    far_z = -eye_box.min(2);

    // apply margin
    near_z -= FrustrumZMargin;
    far_z += FrustrumZMargin;

    // ensure min size
    if (far_z - near_z < FrustrumMinZRange) {
        const double mid_z = 0.5 * (near_z + far_z);
        const double half_size = 0.5 * FrustrumMinZRange;
        near_z = mid_z - half_size;
        far_z = mid_z + half_size;
    }

    if (near_z < FrustrumMinNearZ)
    {
        const double delta = FrustrumMinNearZ - near_z;
        set_distance(m_distance + delta);
        near_z += delta;
        far_z += delta;
    }

    return ret;
}

double Camera::calc_zoom_to_bounding_box_factor(const BoundingBoxf3& box, double margin_factor) const
{
    const double max_bb_size = box.max_size();
    if (max_bb_size == 0.0)
        return -1.0;

    // project the box vertices on a plane perpendicular to the camera forward axis
    // then calculates the vertices coordinate on this plane along the camera xy axes

    const Vec3d right = get_dir_right();
    const Vec3d up = get_dir_up();
    const Vec3d forward = get_dir_forward();
    const Vec3d bb_center = box.center();

    // box vertices in world space
    const std::vector<Vec3d> vertices = {
        box.min,
        { box.max(0), box.min(1), box.min(2) },
        { box.max(0), box.max(1), box.min(2) },
        { box.min(0), box.max(1), box.min(2) },
        { box.min(0), box.min(1), box.max(2) },
        { box.max(0), box.min(1), box.max(2) },
        box.max,
        { box.min(0), box.max(1), box.max(2) }
    };

    double min_x = DBL_MAX;
    double min_y = DBL_MAX;
    double max_x = -DBL_MAX;
    double max_y = -DBL_MAX;

    for (const Vec3d& v : vertices) {
        // project vertex on the plane perpendicular to camera forward axis
        const Vec3d pos = v - bb_center;
        const Vec3d proj_on_plane = pos - pos.dot(forward) * forward;

        // calculates vertex coordinate along camera xy axes
        const double x_on_plane = proj_on_plane.dot(right);
        const double y_on_plane = proj_on_plane.dot(up);

        min_x = std::min(min_x, x_on_plane);
        min_y = std::min(min_y, y_on_plane);
        max_x = std::max(max_x, x_on_plane);
        max_y = std::max(max_y, y_on_plane);
    }

    double dx = max_x - min_x;
    double dy = max_y - min_y;
    if (dx <= 0.0 || dy <= 0.0)
        return -1.0f;

    dx *= margin_factor;
    dy *= margin_factor;

    return std::min((double)m_viewport[2] / dx, (double)m_viewport[3] / dy);
}

void Camera::set_distance(double distance)
{
    if (m_distance != distance) {
        m_view_matrix.translate((distance - m_distance) * get_dir_forward());
        m_distance = distance;

        update_target();
    }
}

double Camera::calc_zoom_to_volumes_factor(const GLVolumePtrs& volumes, Vec3d& center, double margin_factor) const
{
    if (volumes.empty())
        return -1.0;

    // project the volumes vertices on a plane perpendicular to the camera forward axis
    // then calculates the vertices coordinate on this plane along the camera xy axes

    const Vec3d right = get_dir_right();
    const Vec3d up = get_dir_up();
    const Vec3d forward = get_dir_forward();

    BoundingBoxf3 box;
    for (const GLVolume* volume : volumes) {
        box.merge(volume->transformed_bounding_box());
    }
    center = box.center();

    double min_x = DBL_MAX;
    double min_y = DBL_MAX;
    double max_x = -DBL_MAX;
    double max_y = -DBL_MAX;

    for (const GLVolume* volume : volumes) {
        const Transform3d& transform = volume->world_matrix();
        const TriangleMesh* hull = volume->convex_hull();
        if (hull == nullptr)
            continue;

        for (const Vec3f& vertex : hull->its.vertices) {
            const Vec3d v = transform * vertex.cast<double>();

            // project vertex on the plane perpendicular to camera forward axis
            const Vec3d pos = v - center;
            const Vec3d proj_on_plane = pos - pos.dot(forward) * forward;

            // calculates vertex coordinate along camera xy axes
            const double x_on_plane = proj_on_plane.dot(right);
            const double y_on_plane = proj_on_plane.dot(up);

            min_x = std::min(min_x, x_on_plane);
            min_y = std::min(min_y, y_on_plane);
            max_x = std::max(max_x, x_on_plane);
            max_y = std::max(max_y, y_on_plane);
        }
    }

    center += 0.5 * (max_x + min_x) * right + 0.5 * (max_y + min_y) * up;

    const double dx = margin_factor * (max_x - min_x);
    const double dy = margin_factor * (max_y - min_y);

    if (dx <= 0.0 || dy <= 0.0)
        return -1.0f;

    return std::min((double)m_viewport[2] / dx, (double)m_viewport[3] / dy);
}

void Camera::look_at(const Vec3d& position, const Vec3d& target, const Vec3d& up)
{
    const Vec3d unit_z = (position - target).normalized();
    const Vec3d unit_x = up.cross(unit_z).normalized();
    const Vec3d unit_y = unit_z.cross(unit_x).normalized();

    m_target = target;
    m_distance = (position - target).norm();
    const Vec3d new_position = m_target + m_distance * unit_z;

    m_view_matrix(0, 0) = unit_x(0);
    m_view_matrix(0, 1) = unit_x(1);
    m_view_matrix(0, 2) = unit_x(2);
    m_view_matrix(0, 3) = -unit_x.dot(new_position);

    m_view_matrix(1, 0) = unit_y(0);
    m_view_matrix(1, 1) = unit_y(1);
    m_view_matrix(1, 2) = unit_y(2);
    m_view_matrix(1, 3) = -unit_y.dot(new_position);

    m_view_matrix(2, 0) = unit_z(0);
    m_view_matrix(2, 1) = unit_z(1);
    m_view_matrix(2, 2) = unit_z(2);
    m_view_matrix(2, 3) = -unit_z.dot(new_position);

    m_view_matrix(3, 0) = 0.0;
    m_view_matrix(3, 1) = 0.0;
    m_view_matrix(3, 2) = 0.0;
    m_view_matrix(3, 3) = 1.0;

    // Initialize the rotation quaternion from the rotation submatrix of of m_view_matrix.
    m_view_rotation = Eigen::Quaterniond(m_view_matrix.matrix().template block<3, 3>(0, 0));
    m_view_rotation.normalize();

    update_zenit();
}

void Camera::set_default_orientation()
{
    // QDS modify default orientation
    select_view("topfront");
}

void Camera::set_iso_orientation(ViewAngleType va_type)
{
    if (!(va_type == ViewAngleType::Iso || va_type == ViewAngleType::Iso_1 || va_type == ViewAngleType::Iso_2 ||va_type == ViewAngleType::Iso_3)) {
        return;
    }
    Vec3d dir = Vec3d(-0.5f, -0.5f, std::sqrt(2) / 2.f);
    m_view_rotation = Eigen::AngleAxisd(Geometry::deg2rad(-45.0), Vec3d::UnitX()) * Eigen::AngleAxisd(Geometry::deg2rad(45.0), Vec3d::UnitZ());
    if (va_type == ViewAngleType::Iso_1) {
        dir             = Vec3d(-0.5f, 0.5f, std::sqrt(2) / 2.f);
        m_view_rotation = Eigen::AngleAxisd(Geometry::deg2rad(-45.0), Vec3d::UnitX()) * Eigen::AngleAxisd(Geometry::deg2rad(135.0), Vec3d::UnitZ());
    } else if (va_type == ViewAngleType::Iso_2) {
        dir             = Vec3d(0.5f, 0.5f, std::sqrt(2) / 2.f);
        m_view_rotation = Eigen::AngleAxisd(Geometry::deg2rad(-45.0), Vec3d::UnitX()) * Eigen::AngleAxisd(Geometry::deg2rad(225.0), Vec3d::UnitZ());
    } else if (va_type == ViewAngleType::Iso_3) {
        dir             = Vec3d(0.5f, -0.5f, std::sqrt(2) / 2.f);
        m_view_rotation = Eigen::AngleAxisd(Geometry::deg2rad(-45.0), Vec3d::UnitX()) * Eigen::AngleAxisd(Geometry::deg2rad(315.0), Vec3d::UnitZ());
    }
    m_view_rotation.normalize();
    const Vec3d camera_pos = m_target + m_distance * dir;
    m_view_matrix.fromPositionOrientationScale(m_view_rotation * (-camera_pos), m_view_rotation, Vec3d::Ones());
    update_zenit();
}


Vec3d Camera::validate_target(const Vec3d& target) const
{
    BoundingBoxf3 test_box = m_scene_box;
    test_box.translate(-m_scene_box.center());
    // We may let this factor be customizable
    //QDS enlarge scene box factor
    static const double ScaleFactor = 3.0;
    test_box.scale(ScaleFactor);
    test_box.translate(m_scene_box.center());

    return { std::clamp(target(0), test_box.min(0), test_box.max(0)),
             std::clamp(target(1), test_box.min(1), test_box.max(1)),
             std::clamp(target(2), test_box.min(2), test_box.max(2)) };
}

void Camera::update_zenit()
{
    m_zenit = Geometry::rad2deg(0.5 * M_PI - std::acos(std::clamp(-get_dir_forward().dot(Vec3d::UnitZ()), -1.0, 1.0)));
}

void Camera::update_target() {
    Vec3d temptarget = get_position() + m_distance * get_dir_forward();
    if (!(temptarget-m_target).isApprox(Vec3d::Zero())){
        m_target = temptarget;
    }
}

} // GUI
} // Slic3r

