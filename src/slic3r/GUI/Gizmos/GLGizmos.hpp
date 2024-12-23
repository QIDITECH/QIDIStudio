#ifndef slic3r_GLGizmos_hpp_
#define slic3r_GLGizmos_hpp_

// this describes events being passed from GLCanvas3D to SlaSupport gizmo
namespace Slic3r {
namespace GUI {

enum class SLAGizmoEventType : unsigned char {
    LeftDown = 1,
    LeftUp,
    RightDown,
    Dragging,
    Delete,
    SelectAll,
    ShiftUp,
    AltUp,
    ApplyChanges,
    DiscardChanges,
    AutomaticGeneration,
    ManualEditing,
    MouseWheelUp,
    MouseWheelDown,
    ResetClippingPlane
};

} // namespace GUI
} // namespace Slic3r

// QDS
#include "slic3r/GUI/Gizmos/GLGizmoMoveScale.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoRotate.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoFlatten.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoSlaSupports.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoFdmSupports.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoMmuSegmentation.hpp"
// QDS
#include "slic3r/GUI/Gizmos/GLGizmoAdvancedCut.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoHollow.hpp"

#endif //slic3r_GLGizmos_hpp_
