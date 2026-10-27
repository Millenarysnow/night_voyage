#include "register_types.h"

#include "nv_timeline.h"
#include "nv_timeline_editor.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void initialize_nv_timeline_types(ModuleInitializationLevel p_level) {
    if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
        GDREGISTER_CLASS(NVTimelineDocument);
        GDREGISTER_CLASS(NVTimelineClock);
        GDREGISTER_CLASS(NVTimelineEventManager);
        GDREGISTER_CLASS(NVTimelineRuntime);
        GDREGISTER_CLASS(NVTimelineService);
    } else if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
        // 原生画布先注册，使编辑器窗口构造时可以安全地 memnew 轨道控件。
        GDREGISTER_CLASS(NVTimelineCanvas);
        GDREGISTER_CLASS(NVTimelineEditorWindow);
    }
}

void uninitialize_nv_timeline_types(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
}

extern "C" {

// Godot 会在加载 .gdextension 时调用该符号；其余对象注册交由 godot-cpp 的
// InitObject 统一安排，确保卸载顺序与初始化级别一致。
GDExtensionBool GDE_EXPORT nv_timeline_init(
        GDExtensionInterfaceGetProcAddress p_get_proc_address,
        GDExtensionClassLibraryPtr p_library,
        GDExtensionInitialization *r_initialization) {
    GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
    init_obj.register_initializer(initialize_nv_timeline_types);
    init_obj.register_terminator(uninitialize_nv_timeline_types);
    init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return init_obj.init();
}

}
