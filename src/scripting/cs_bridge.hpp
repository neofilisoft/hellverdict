// HellVerdict — cs_bridge.hpp / cs_bridge.cpp (combined header)
// Copyright © 2026 Neofilisoft / Studio Balmung
// Mono/.NET 10 bridge: loads HellVerdict.Scripts.dll, routes C++ game events
// to IBalmungScriptContext::OnEvent, calls OnUpdate each frame.

#pragma once

#include "../game/hell_game.hpp"
#include <string>
#include <memory>

// Mono headers (from mono/jit/jit.h — part of Mono runtime)
#if __has_include(<mono/jit/jit.h>)
#  include <mono/jit/jit.h>
#  include <mono/metadata/assembly.h>
#  include <mono/metadata/debug-helpers.h>
#  include <mono/metadata/mono-config.h>
#  define HV_HAS_MONO 1
#else
#  define HV_HAS_MONO 0
#endif

namespace HellVerdict {

class CsBridge {
public:
    CsBridge() = default;
    ~CsBridge() { shutdown(); }

    // Load the C# assembly and instantiate HellScriptContext.
    // mono_lib_dir: path to Mono runtime libs (e.g. "/usr/lib/mono/4.5")
    // assembly_path: path to HellVerdict.Scripts.dll
    bool init(const std::string& mono_lib_dir,
              const std::string& assembly_path);

    void shutdown();
    bool is_valid() const { return _initialized; }

    // Called from HellGame::on_event — dispatches to C# OnEvent()
    void dispatch_event(const GameEvent& ev);

    // Called every render frame — calls C# OnUpdate(dt)
    void update(float dt);

private:
#if HV_HAS_MONO
    MonoDomain*   _domain   = nullptr;
    MonoAssembly* _assembly = nullptr;
    MonoImage*    _image    = nullptr;
    MonoObject*   _ctx_obj  = nullptr;   // HellScriptContext instance

    MonoMethod*   _m_on_attach = nullptr;
    MonoMethod*   _m_on_update = nullptr;
    MonoMethod*   _m_on_event  = nullptr;
    MonoMethod*   _m_on_detach = nullptr;

    MonoClass*    _event_class = nullptr;

    MonoObject*   _create_script_event(const std::string& name,
                                        const std::string& json);

    // IScriptHost C# → C++ callbacks (registered as internal calls)
    static void  _icall_log          (MonoString* msg);
    static void  _icall_play_sound   (MonoString* id, float vol, bool loop);
    static void  _icall_set_overlay  (MonoString* slot, MonoString* text);
#endif

    bool _initialized = false;
};

// ── Inline implementation (thin — real work is in cs_bridge.cpp) ─────────────

} // namespace HellVerdict
