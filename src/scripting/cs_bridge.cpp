// HellVerdict — cs_bridge.cpp
// Copyright © 2026 Neofilisoft / Studio Balmung

#include "cs_bridge.hpp"
#include <iostream>

namespace HellVerdict {

#if HV_HAS_MONO

// ── IScriptHost internal calls registered with Mono ──────────────────────────
// These are called from C# IScriptHost methods which are declared as
// [MethodImpl(MethodImplOptions.InternalCall)] in Balmung.Scripting.dll.

void CsBridge::_icall_log(MonoString* msg) {
    char* str = mono_string_to_utf8(msg);
    std::cout << str << "\n";
    mono_free(str);
}

void CsBridge::_icall_play_sound(MonoString* id, float vol, bool loop) {
    char* str = mono_string_to_utf8(id);
    std::cout << "[Audio] play " << str << " vol=" << vol
              << (loop ? " loop" : "") << "\n";
    // TODO: forward to Balmung audio system
    mono_free(str);
}

void CsBridge::_icall_set_overlay(MonoString* slot, MonoString* text) {
    char* s_slot = mono_string_to_utf8(slot);
    char* s_text = mono_string_to_utf8(text);
    std::cout << "[Overlay] [" << s_slot << "] = " << s_text << "\n";
    // TODO: forward to HUD renderer
    mono_free(s_slot);
    mono_free(s_text);
}

// ── Init ─────────────────────────────────────────────────────────────────────
bool CsBridge::init(const std::string& mono_lib_dir,
                     const std::string& assembly_path)
{
    mono_set_dirs(mono_lib_dir.c_str(),
                  (mono_lib_dir + "/etc").c_str());
    mono_config_parse(nullptr);

    _domain = mono_jit_init("HellVerdictMono");
    if (!_domain) {
        std::cerr << "[CsBridge] mono_jit_init failed\n";
        return false;
    }

    // Register internal calls before loading assembly
    mono_add_internal_call(
        "Balmung.Scripting.ScriptHostImpl::Log",
        reinterpret_cast<void*>(_icall_log));
    mono_add_internal_call(
        "Balmung.Scripting.ScriptHostImpl::PlaySoundInternal",
        reinterpret_cast<void*>(_icall_play_sound));
    mono_add_internal_call(
        "Balmung.Scripting.ScriptHostImpl::SetOverlayInternal",
        reinterpret_cast<void*>(_icall_set_overlay));

    // Load assembly
    _assembly = mono_domain_assembly_open(_domain, assembly_path.c_str());
    if (!_assembly) {
        std::cerr << "[CsBridge] Cannot load: " << assembly_path << "\n";
        return false;
    }
    _image = mono_assembly_get_image(_assembly);

    // Find HellScriptContext class
    MonoClass* ctx_class = mono_class_from_name(
        _image,
        "HellVerdict.Scripts",
        "HellScriptContext");
    if (!ctx_class) {
        std::cerr << "[CsBridge] HellScriptContext class not found\n";
        return false;
    }

    // Instantiate (calls default ctor)
    _ctx_obj = mono_object_new(_domain, ctx_class);
    mono_runtime_object_init(_ctx_obj);

    // Cache method pointers
    auto get_method = [&](MonoClass* cls, const char* name, int argc) -> MonoMethod* {
        MonoMethod* m = mono_class_get_method_from_name(cls, name, argc);
        if (!m) std::cerr << "[CsBridge] Missing method: " << name << "\n";
        return m;
    };

    _m_on_attach = get_method(ctx_class, "OnAttach", 1);
    _m_on_update = get_method(ctx_class, "OnUpdate", 1);
    _m_on_event  = get_method(ctx_class, "OnEvent",  1);
    _m_on_detach = get_method(ctx_class, "OnDetach", 0);

    if (!_m_on_update || !_m_on_event) return false;

    // Find ScriptEvent struct class for creating event objects
    _event_class = mono_class_from_name(_image, "Balmung.Scripting", "ScriptEvent");

    // Call OnAttach — pass null host for now (host is implemented via internal calls)
    if (_m_on_attach) {
        void* args[1] = { nullptr };
        MonoObject* exc = nullptr;
        mono_runtime_invoke(_m_on_attach, _ctx_obj, args, &exc);
        if (exc) {
            char* msg = mono_string_to_utf8(
                mono_object_to_string(exc, nullptr));
            std::cerr << "[CsBridge] OnAttach exception: " << msg << "\n";
            mono_free(msg);
        }
    }

    _initialized = true;
    std::cout << "[CsBridge] Mono bridge ready — "
              << assembly_path << "\n";
    return true;
}

// ── Shutdown ──────────────────────────────────────────────────────────────────
void CsBridge::shutdown() {
    if (!_initialized) return;

    if (_m_on_detach && _ctx_obj) {
        MonoObject* exc = nullptr;
        mono_runtime_invoke(_m_on_detach, _ctx_obj, nullptr, &exc);
    }
    if (_domain) {
        mono_jit_cleanup(_domain);
        _domain = nullptr;
    }
    _initialized = false;
}

// ── Event dispatch ────────────────────────────────────────────────────────────
MonoObject* CsBridge::_create_script_event(const std::string& name,
                                             const std::string& json)
{
    if (!_event_class) return nullptr;

    MonoObject* obj = mono_object_new(_domain, _event_class);

    // ScriptEvent is a readonly struct with init-only properties.
    // Populate via reflection field set or a constructor.
    // Here we use the constructor that takes (string name, string json).
    MonoMethod* ctor = mono_class_get_method_from_name(_event_class, ".ctor", 2);
    if (ctor) {
        MonoString* ms_name = mono_string_new(_domain, name.c_str());
        MonoString* ms_json = mono_string_new(_domain, json.c_str());
        void* args[2] = { ms_name, ms_json };
        MonoObject* exc = nullptr;
        mono_runtime_invoke(ctor, obj, args, &exc);
    }
    return obj;
}

void CsBridge::dispatch_event(const GameEvent& ev) {
    if (!_initialized || !_m_on_event) return;

    MonoObject* ev_obj = _create_script_event(ev.name, ev.json);
    void* args[1] = { ev_obj };
    MonoObject* exc = nullptr;
    mono_runtime_invoke(_m_on_event, _ctx_obj, args, &exc);

    if (exc) {
        char* msg = mono_string_to_utf8(
            mono_object_to_string(exc, nullptr));
        std::cerr << "[CsBridge] OnEvent exception: " << msg << "\n";
        mono_free(msg);
    }
}

// ── Per-frame update ──────────────────────────────────────────────────────────
void CsBridge::update(float dt) {
    if (!_initialized || !_m_on_update) return;

    void* args[1] = { &dt };
    MonoObject* exc = nullptr;
    mono_runtime_invoke(_m_on_update, _ctx_obj, args, &exc);
    if (exc) {
        char* msg = mono_string_to_utf8(
            mono_object_to_string(exc, nullptr));
        std::cerr << "[CsBridge] OnUpdate exception: " << msg << "\n";
        mono_free(msg);
        _initialized = false;   // Disable after repeated exceptions
    }
}

#else // !HV_HAS_MONO

bool CsBridge::init(const std::string&, const std::string& path) {
    std::cout << "[CsBridge] Mono not available — C# scripting disabled\n";
    std::cout << "           (would load: " << path << ")\n";
    return true;   // Non-fatal — game runs without scripting
}
void CsBridge::shutdown()               {}
void CsBridge::dispatch_event(const GameEvent& ev) {
    std::cout << "[CsBridge:stub] event: " << ev.name << " " << ev.json << "\n";
}
void CsBridge::update(float) {}

#endif // HV_HAS_MONO

} // namespace HellVerdict
