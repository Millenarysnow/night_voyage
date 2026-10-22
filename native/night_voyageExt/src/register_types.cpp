#include "register_types.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>
#include "example_class.h"
#include "ui/nv_surface_style.h"
#include "ui/nv_surface.h"
#include "ui/nv_glass_panel.h"
#include "ui/nv_alpha_shadow.h"
#include "ui/nv_hit_shape.h"
#include "ui/nv_warp_profile.h"
#include "ui/nv_analog_profile.h"
#include "ui/nv_effect_viewport.h"
#include "ui/nv_clip_viewport.h"
#include "ui/nv_screen_effect_viewport.h"
#include "ui/nv_footage_effect.h"
#include "examples/nv_effects_demo.h"

using namespace godot;

void initialize_gdextension_types(ModuleInitializationLevel p_level)
{
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    GDREGISTER_CLASS(ExampleClass);
    // 运行时 UI 组件本身就是可复用的原生节点，无需 GDScript EditorPlugin 才能添加。
    // 资源在使用它的节点之前注册，以便 Inspector 识别自定义 .tres 类型。
    GDREGISTER_CLASS(NVSurfaceStyle);
    GDREGISTER_CLASS(NVHitShape);
    GDREGISTER_CLASS(NVWarpProfile);
    GDREGISTER_CLASS(NVAnalogProfile);
    GDREGISTER_CLASS(NVSurface);
    GDREGISTER_CLASS(NVGlassPanel);
    GDREGISTER_CLASS(NVAlphaShadow);
    GDREGISTER_CLASS(NVEffectViewport);
    GDREGISTER_CLASS(NVClipViewport);
    GDREGISTER_CLASS(NVScreenEffectViewport);
    GDREGISTER_CLASS(NVFootageEffect);
    GDREGISTER_CLASS(NVEffectsDemo);
}

void uninitialize_gdextension_types(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
}

extern "C"
{
    // Initialization
    GDExtensionBool GDE_EXPORT gdextension_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization)
    {
        GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
        init_obj.register_initializer(initialize_gdextension_types);
        init_obj.register_terminator(uninitialize_gdextension_types);
        init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

        return init_obj.init();
    }
}
