// HellVerdict — HellScriptContext.cs
// Copyright © 2026 Neofilisoft / Studio Balmung
// Implements IBalmungScriptContext for Hell Verdict game logic in C# / .NET 10
// Receives events from the C++ engine via the Mono bridge and drives:
//   - UI/HUD text overlays
//   - Audio cue triggers
//   - Game-state logic that doesn't need frame-level precision

using System;
using System.Collections.Generic;
using System.Text.Json;
using Balmung.Scripting;   // IBalmungScriptContext, ScriptEvent

namespace HellVerdict.Scripts
{
    /// <summary>
    /// Main C# game script — registered with CSharpScriptRuntime by main.cpp.
    /// Receives JSON events from C++ HellGame::on_event and responds via
    /// IScriptHost (audio, overlay text, etc.).
    /// </summary>
    public sealed class HellScriptContext : IBalmungScriptContext
    {
        // ── State ─────────────────────────────────────────────────────────────
        private IScriptHost _host = null!;
        private int   _score     = 0;
        private float _health    = 100f;
        private bool  _alive     = true;
        private int   _kills     = 0;
        private bool  _shotgunPickedUp = false;
        private readonly Queue<(float time, string msg)> _messages = new();

        private float _elapsed = 0f;       // total game time in seconds

        // ── IBalmungScriptContext ─────────────────────────────────────────────
        public string ContextId => "hell_verdict:main";

        public void OnAttach(IScriptHost host)
        {
            _host = host;
            _host.Log("[HellVerdict] Script context attached (.NET " +
                      Environment.Version + ")");
        }

        public void OnDetach()
        {
            _host.Log("[HellVerdict] Script context detached");
        }

        /// <summary>
        /// Called every render frame from CSharpScriptRuntime::update().
        /// dt is frame delta in seconds.
        /// </summary>
        public void OnUpdate(float dt)
        {
            _elapsed += dt;

            // Expire old messages
            while (_messages.Count > 0 && _messages.Peek().time < _elapsed)
                _messages.Dequeue();
        }

        /// <summary>
        /// Receives JSON events dispatched by HellGame::on_event (C++).
        /// Called from the Mono bridge on the main thread.
        /// </summary>
        public void OnEvent(ScriptEvent ev)
        {
            switch (ev.Name)
            {
                case "hell_verdict:game_start":   HandleGameStart(ev.Json);  break;
                case "hell_verdict:player_hit":   HandlePlayerHit(ev.Json);  break;
                case "hell_verdict:player_dead":  HandlePlayerDead(ev.Json); break;
                case "hell_verdict:enemy_hit":    HandleEnemyHit(ev.Json);   break;
                case "hell_verdict:pickup":       HandlePickup(ev.Json);     break;
                case "hell_verdict:victory":      HandleVictory(ev.Json);    break;
                default:
                    _host.Log($"[HellVerdict] Unknown event: {ev.Name}");
                    break;
            }
        }

        // ── Event handlers ────────────────────────────────────────────────────

        private void HandleGameStart(string json)
        {
            _score  = 0;
            _health = 100f;
            _alive  = true;
            _kills  = 0;
            _messages.Clear();

            try {
                using var doc = JsonDocument.Parse(json);
                int enemies = doc.RootElement.GetProperty("enemies").GetInt32();
                string level = doc.RootElement.GetProperty("level").GetString() ?? "";
                ShowMessage($"HELL VERDICT — {enemies} enemies await", 4f);
                _host.Log($"[HellVerdict] Game start: {level}, {enemies} enemies");
            } catch { /* malformed payload is non-fatal */ }

            _host.PlaySound("snd/game_start", volume: 0.8f, loop: false);
            _host.SetOverlayText("hud_title", "");
        }

        private void HandlePlayerHit(string json)
        {
            try {
                using var doc = JsonDocument.Parse(json);
                float dmg = doc.RootElement.GetProperty("damage").GetSingle();
                _health   = doc.RootElement.GetProperty("health").GetSingle();

                if (_health < 25f)
                    _host.PlaySound("snd/player_pain_low", 1.0f, false);
                else
                    _host.PlaySound("snd/player_pain", 0.9f, false);

                if (_health < 15f)
                    ShowMessage("LOW HEALTH!", 2.5f);
            } catch { }
        }

        private void HandlePlayerDead(string json)
        {
            _alive = false;
            try {
                using var doc = JsonDocument.Parse(json);
                _score = doc.RootElement.GetProperty("score").GetInt32();
            } catch { }

            _host.PlaySound("snd/player_death", 1.0f, false);
            _host.SetOverlayText("hud_title",
                $"YOU DIED\nScore: {_score}\nKills: {_kills}");
            _host.Log($"[HellVerdict] Player dead. Score={_score}, Kills={_kills}");
        }

        private void HandleEnemyHit(string json)
        {
            try {
                using var doc = JsonDocument.Parse(json);
                float dmg  = doc.RootElement.GetProperty("damage").GetSingle();
                _score     = doc.RootElement.GetProperty("score").GetInt32();

                // Distinguish kill vs wound by damage threshold
                // (enemy health not exposed here — we check via score delta)
                _kills++;
                _host.PlaySound(dmg >= 40f ? "snd/enemy_die" : "snd/enemy_hit",
                                0.75f, false);
                _host.SetOverlayText("hud_score", $"SCORE: {_score}");
            } catch { }
        }

        private void HandlePickup(string json)
        {
            try {
                using var doc = JsonDocument.Parse(json);
                string type = doc.RootElement.GetProperty("type").GetString() ?? "";

                switch (type)
                {
                    case "health":
                        int hp = doc.RootElement.GetProperty("amount").GetInt32();
                        _host.PlaySound("snd/pickup_health", 0.9f, false);
                        ShowMessage($"+{hp} HEALTH", 1.8f);
                        break;
                    case "ammo":
                        int shells = doc.RootElement.GetProperty("amount").GetInt32();
                        _host.PlaySound("snd/pickup_ammo", 0.85f, false);
                        ShowMessage($"+{shells} SHELLS", 1.5f);
                        break;
                    case "shotgun":
                        if (!_shotgunPickedUp) {
                            _shotgunPickedUp = true;
                            _host.PlaySound("snd/pickup_weapon", 1.0f, false);
                            ShowMessage("SHOTGUN ACQUIRED!", 3.0f);
                        }
                        break;
                }
            } catch { }
        }

        private void HandleVictory(string json)
        {
            try {
                using var doc = JsonDocument.Parse(json);
                _score = doc.RootElement.GetProperty("score").GetInt32();
            } catch { }

            _host.PlaySound("snd/victory", 1.0f, false);
            _host.SetOverlayText("hud_title",
                $"LEVEL COMPLETE!\nScore: {_score}\nKills: {_kills}");
            _host.Log($"[HellVerdict] Victory! Score={_score}");
        }

        // ── Helpers ───────────────────────────────────────────────────────────

        private void ShowMessage(string msg, float duration)
        {
            float expire = _elapsed + duration;
            _messages.Enqueue((expire, msg));
            // Push to HUD overlay (most recent message wins)
            _host.SetOverlayText("hud_message", msg);
            _host.Log($"[HellVerdict] MSG: {msg}");
        }

        // ── IScriptHost stub interface (declared here for documentation) ───────
        // The actual interface is defined in Balmung.Scripting (C++ Mono bridge)
    }

    // ── Minimal interface mirrors (matches IBalmungScriptContext.cs in engine) ──
    // Included here for IDE autocompletion in this file.
    // The real types are in Balmung.Scripting.dll.
    namespace Balmung.Scripting
    {
        public interface IBalmungScriptContext
        {
            string ContextId { get; }
            void OnAttach(IScriptHost host);
            void OnDetach();
            void OnUpdate(float dt);
            void OnEvent(ScriptEvent ev);
        }

        public interface IScriptHost
        {
            void Log(string message);
            void PlaySound(string soundId, float volume, bool loop);
            void SetOverlayText(string slotId, string text);
        }

        public readonly struct ScriptEvent
        {
            public string Name { get; init; }
            public string Json { get; init; }
        }
    }
}
