using System.Collections;
using UnityEngine;
using UnityEngine.InputSystem;

namespace BalmungDoom.Core
{
    public class GameManager : MonoBehaviour
    {
        public const string GameName = "Hell Verdict";
        public const string GameSlug = "hellverdict";
        public const int MaxStage = 10;
        public static GameManager Instance { get; private set; }

        private UIManager uiManager;
        private HellVerdictBootstrap bootstrap;
        private bool paused;
        private int currentLevel = 1;
        private Coroutine stageAdvanceCoroutine;

        private int easterEggIndex;
        private float easterEggTimer;
        private bool easterEggAvailable;

        private int debugSeqIndex;
        private float debugSeqTimer;
        private bool debugMode;
        public bool GodMode { get; private set; }

        private void Awake()
        {
            if (Instance != null && Instance != this)
            {
                Destroy(gameObject);
                return;
            }

            Instance = this;
            DontDestroyOnLoad(gameObject);
            Application.targetFrameRate = 120;
            QualitySettings.vSyncCount = 0;
        }

        private void Start()
        {
            uiManager = FindAnyObjectByType<UIManager>();
            bootstrap = FindAnyObjectByType<HellVerdictBootstrap>();
            currentLevel = PlayerPrefs.GetInt(GameSlug + ".level", 1);
            ShowMainMenu();
        }

        public void StartNewGame()
        {
            StopStageAdvance();
            bootstrap ??= FindAnyObjectByType<HellVerdictBootstrap>();
            if (bootstrap != null) bootstrap.BalmungDoomMode = false;
            currentLevel = 1;
            SaveGame();
            StartLevel(currentLevel);
        }

        public void ContinueGame()
        {
            StopStageAdvance();
            currentLevel = Mathf.Clamp(PlayerPrefs.GetInt(GameSlug + ".level", 1), 1, MaxStage);
            StartLevel(currentLevel);
        }

        public void StartLevel(int level)
        {
            currentLevel = Mathf.Clamp(level, 1, MaxStage);
            paused = false;
            Time.timeScale = 1f;
            Cursor.lockState = CursorLockMode.Locked;
            Cursor.visible = false;
            InputManager.Instance?.SuppressFireUntilReleased();
            uiManager ??= FindAnyObjectByType<UIManager>();
            bootstrap ??= FindAnyObjectByType<HellVerdictBootstrap>();
            
            uiManager?.ShowLoadingScreen();
            bootstrap?.BuildLevel(currentLevel);
            uiManager?.ShowGameplayHUD();
            AudioManager.Instance?.PlayGameplayMusic(bootstrap != null && bootstrap.HasCyberDemonInCurrentLevel);

            easterEggAvailable = (currentLevel == 1) && !(bootstrap != null && bootstrap.BalmungDoomMode);
            easterEggIndex = 0;
            easterEggTimer = 0f;
        }

        private void Update()
        {
            Keyboard kb = Keyboard.current;
            if (kb == null || paused) return;

            UpdateDebugSequence(kb);
            if (debugMode)
                HandleDebugActions(kb);

            if (!easterEggAvailable) return;

            Mouse mouse = Mouse.current;
            easterEggTimer += Time.deltaTime;
            if (easterEggTimer > 10f)
            {
                easterEggIndex = 0;
                easterEggTimer = 0f;
            }

            if (easterEggIndex < 6)
            {
                bool expected = easterEggIndex switch
                {
                    0 => kb.wKey.wasPressedThisFrame,
                    1 => kb.aKey.wasPressedThisFrame,
                    2 => kb.sKey.wasPressedThisFrame,
                    3 => kb.dKey.wasPressedThisFrame,
                    4 => kb.spaceKey.wasPressedThisFrame,
                    5 => kb.spaceKey.wasPressedThisFrame,
                    _ => false
                };

                if (expected)
                {
                    easterEggIndex++;
                    easterEggTimer = 0f;
                }
                else if (kb.anyKey.wasPressedThisFrame)
                {
                    easterEggIndex = 0;
                    easterEggTimer = 0f;
                }
            }
            else
            {
                if (mouse != null && mouse.leftButton.wasPressedThisFrame)
                    ActivateBalmungDoom();
            }
        }

        private void UpdateDebugSequence(Keyboard kb)
        {
            debugSeqTimer += Time.deltaTime;
            if (debugSeqTimer > 10f)
            {
                debugSeqIndex = 0;
                debugSeqTimer = 0f;
            }

            bool expected = debugSeqIndex switch
            {
                0 => kb.upArrowKey.wasPressedThisFrame,
                1 => kb.downArrowKey.wasPressedThisFrame,
                2 => kb.upArrowKey.wasPressedThisFrame,
                3 => kb.downArrowKey.wasPressedThisFrame,
                4 => kb.leftArrowKey.wasPressedThisFrame,
                5 => kb.rightArrowKey.wasPressedThisFrame,
                6 => kb.leftArrowKey.wasPressedThisFrame,
                7 => kb.rightArrowKey.wasPressedThisFrame,
                8 => kb.aKey.wasPressedThisFrame,
                9 => kb.bKey.wasPressedThisFrame,
                _ => false
            };

            if (expected)
            {
                debugSeqIndex++;
                debugSeqTimer = 0f;
                if (debugSeqIndex >= 10)
                {
                    debugSeqIndex = 0;
                    ToggleDebugMode();
                }
            }
            else if (kb.anyKey.wasPressedThisFrame && debugSeqIndex > 0)
            {
                bool isDebugKey = kb.upArrowKey.wasPressedThisFrame || kb.downArrowKey.wasPressedThisFrame ||
                                  kb.leftArrowKey.wasPressedThisFrame || kb.rightArrowKey.wasPressedThisFrame ||
                                  kb.aKey.wasPressedThisFrame || kb.bKey.wasPressedThisFrame;
                if (!isDebugKey)
                {
                    debugSeqIndex = 0;
                    debugSeqTimer = 0f;
                }
            }
        }

        private void ToggleDebugMode()
        {
            debugMode = !debugMode;
            if (!debugMode)
                GodMode = false;
            Debug.Log($"[DEBUG MODE] {(debugMode ? "ENABLED" : "DISABLED")}");
        }

        private void HandleDebugActions(Keyboard kb)
        {
            bootstrap ??= FindAnyObjectByType<HellVerdictBootstrap>();
            if (bootstrap == null) return;

            if (kb.digit1Key.wasPressedThisFrame)
            {
                bootstrap.DebugSpawnEnemy("soldier");
                Debug.Log("[DEBUG] Spawned Soldier");
            }
            else if (kb.digit2Key.wasPressedThisFrame)
            {
                bootstrap.DebugSpawnEnemy("caco_demon");
                Debug.Log("[DEBUG] Spawned Caco Demon");
            }
            else if (kb.digit3Key.wasPressedThisFrame)
            {
                bootstrap.DebugSpawnEnemy("cyber_demon");
                Debug.Log("[DEBUG] Spawned Cyber Demon");
            }
            else if (kb.digit0Key.wasPressedThisFrame)
            {
                GodMode = !GodMode;
                Debug.Log($"[DEBUG] God Mode {(GodMode ? "ON" : "OFF")}");
            }
        }

        private void ActivateBalmungDoom()
        {
            easterEggAvailable = false;
            bootstrap ??= FindAnyObjectByType<HellVerdictBootstrap>();
            if (bootstrap != null)
                bootstrap.BalmungDoomMode = true;

            Debug.Log("[BALMUNG DOOM] Secret mode activated! Teleporting to final level...");
            currentLevel = MaxStage;
            SaveGame();
            StartLevel(currentLevel);
        }

        public void CompleteLevel()
        {
            if (stageAdvanceCoroutine != null)
                return;

            stageAdvanceCoroutine = StartCoroutine(CompleteLevelRoutine());
        }

        private IEnumerator CompleteLevelRoutine()
        {
            paused = true;
            Time.timeScale = 0f;
            Cursor.lockState = CursorLockMode.None;
            Cursor.visible = true;
            uiManager ??= FindAnyObjectByType<UIManager>();
            uiManager?.ShowStageClearScreen();

            if (currentLevel >= MaxStage)
            {
                yield return new WaitForSecondsRealtime(3f);
                stageAdvanceCoroutine = null;
                currentLevel = 1;
                SaveGame();
                ShowMainMenu();
                yield break;
            }

            yield return new WaitForSecondsRealtime(2f);
            stageAdvanceCoroutine = null;
            currentLevel++;
            SaveGame();
            StartLevel(currentLevel);
        }

        private void StopStageAdvance()
        {
            if (stageAdvanceCoroutine == null)
                return;

            StopCoroutine(stageAdvanceCoroutine);
            stageAdvanceCoroutine = null;
        }

        public void TogglePause()
        {
            if (uiManager == null)
                uiManager = FindAnyObjectByType<UIManager>();

            paused = !paused;
            Time.timeScale = paused ? 0f : 1f;
            Cursor.lockState = paused ? CursorLockMode.None : CursorLockMode.Locked;
            Cursor.visible = paused;
            uiManager?.ShowPauseMenu(paused);
        }

        public void OpenGameplayOptions()
        {
            if (uiManager == null)
                uiManager = FindAnyObjectByType<UIManager>();

            paused = true;
            Time.timeScale = 0f;
            Cursor.lockState = CursorLockMode.None;
            Cursor.visible = true;
            uiManager?.ShowGameplayOptions();
        }

        public void ShowMainMenu()
        {
            paused = true;
            Time.timeScale = 0f;
            Cursor.lockState = CursorLockMode.None;
            Cursor.visible = true;
            AudioManager.Instance?.StopGameplayMusic();
            bootstrap ??= FindAnyObjectByType<HellVerdictBootstrap>();
            bootstrap?.ClearLevel();
            uiManager ??= FindAnyObjectByType<UIManager>();
            uiManager?.ShowMainMenu();
        }

        public void SaveGame()
        {
            PlayerPrefs.SetInt(GameSlug + ".level", currentLevel);
            PlayerPrefs.Save();
        }

        public void GameOver()
        {
            paused = true;
            Time.timeScale = 0f;
            Cursor.lockState = CursorLockMode.None;
            Cursor.visible = true;
            uiManager?.ShowGameOverScreen();
        }

        public void GameWon()
        {
            paused = true;
            Time.timeScale = 0f;
            Cursor.lockState = CursorLockMode.None;
            Cursor.visible = true;
            uiManager?.ShowGameWonScreen();
        }

        public void QuitGame()
        {
            SaveGame();
            Application.Quit();
        }

        public bool IsPaused() => paused;
        public int GetCurrentLevel() => currentLevel;
    }
}
