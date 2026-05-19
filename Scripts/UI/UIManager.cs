using UnityEngine;
using UnityEngine.UI;
using UnityEngine.EventSystems;
using System.Collections;
using System.IO;
using UnityEngine.InputSystem;
using UnityEngine.InputSystem.UI;
using BalmungDoom.Enemy;
using BalmungDoom.Gameplay;

namespace BalmungDoom.Core
{
    [DefaultExecutionOrder(-100)]
    public class UIManager : MonoBehaviour
    {
        [SerializeField] private Canvas mainCanvas;
        [SerializeField] private Image watermarkImage;

        private GameObject mainMenuPanel;
        private GameObject gameplayHUDPanel;
        private GameObject pauseMenuPanel;
        private GameObject optionsPanel;
        private GameObject gameOverPanel;
        private GameObject gameWonPanel;
        private GameObject stageClearPanel;
        private GameObject minimapPanel;
        private GameObject splashPanel;
        private GameObject loadingPanel;

        private Text healthText;
        private Text ammoText;
        private Text levelText;
        private Text healText;
        private Text minimapInfoText;
        private RawImage minimapImage;
        private Texture2D minimapTexture;
        private Text masterVolumeValue;
        private Text bgmVolumeValue;
        private Text sfxVolumeValue;
        private Text sensitivityValue;
        private RuntimeSensitivitySlider sensitivitySlider;
        private bool optionsOpenedFromPause;
        private float nextMinimapRefreshTime;
        private const int MinimapTextureSize = 192;

        private void Awake()
        {
            EnsureCanvas();
            CreateUIHierarchy();
            ShowSplashScreen();
        }

        private void Update()
        {
            if (minimapPanel != null && minimapPanel.activeSelf && Time.unscaledTime >= nextMinimapRefreshTime)
            {
                nextMinimapRefreshTime = Time.unscaledTime + 0.15f;
                RefreshMinimap();
            }
        }

        private void EnsureCanvas()
        {
            if (mainCanvas == null)
                mainCanvas = GetComponentInChildren<Canvas>();

            if (mainCanvas == null)
            {
                GameObject canvasObj = new GameObject("MainCanvas");
                canvasObj.transform.SetParent(transform, false);
                mainCanvas = canvasObj.AddComponent<Canvas>();
                mainCanvas.renderMode = RenderMode.ScreenSpaceOverlay;
                CanvasScaler scaler = canvasObj.AddComponent<CanvasScaler>();
                scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
                scaler.referenceResolution = new Vector2(1280f, 720f);
                scaler.matchWidthOrHeight = 0.5f;
                canvasObj.AddComponent<GraphicRaycaster>();
            }

            EventSystem eventSystem = FindAnyObjectByType<EventSystem>();
            if (eventSystem == null)
            {
                GameObject eventSystemObj = new GameObject("EventSystem");
                eventSystem = eventSystemObj.AddComponent<EventSystem>();
                DontDestroyOnLoad(eventSystemObj);
            }

            EnsureInputSystemUI(eventSystem);
        }

        private void EnsureInputSystemUI(EventSystem eventSystem)
        {
            if (eventSystem == null)
                return;

            StandaloneInputModule legacyModule = eventSystem.GetComponent<StandaloneInputModule>();
            if (legacyModule != null)
                Destroy(legacyModule);

            if (eventSystem.GetComponent<InputSystemUIInputModule>() == null)
                eventSystem.gameObject.AddComponent<InputSystemUIInputModule>();
        }

        private void CreateUIHierarchy()
        {
            mainMenuPanel = CreatePanel("MainMenuPanel", 0, true);
            gameplayHUDPanel = CreatePanel("GameplayHUDPanel", 1, false);
            pauseMenuPanel = CreatePanel("PauseMenuPanel", 2, false);
            optionsPanel = CreatePanel("OptionsPanel", 3, false);
            gameOverPanel = CreatePanel("GameOverPanel", 4, false);
            gameWonPanel = CreatePanel("GameWonPanel", 5, false);
            stageClearPanel = CreatePanel("StageClearPanel", 6, false);
            minimapPanel = CreatePanel("MinimapPanel", 7, false);
            loadingPanel = CreatePanel("LoadingPanel", 8, false);

            SetupMainMenu();
            SetupGameplayHUD();
            SetupPauseMenu();
            SetupOptions();
            SetupGameOverScreen();
            SetupGameWonScreen();
            SetupStageClearScreen();
            SetupMinimap();
            SetupLoadingScreen();
        }

        private GameObject CreatePanel(string name, int sortOrder, bool active)
        {
            GameObject panel = new GameObject(name);
            panel.transform.SetParent(mainCanvas.transform, false);
            panel.SetActive(active);

            RectTransform rectTransform = panel.AddComponent<RectTransform>();
            CanvasGroup canvasGroup = panel.AddComponent<CanvasGroup>();
            canvasGroup.blocksRaycasts = active;
            rectTransform.anchorMin = Vector2.zero;
            rectTransform.anchorMax = Vector2.one;
            rectTransform.offsetMin = Vector2.zero;
            rectTransform.offsetMax = Vector2.zero;

            return panel;
        }

        private void SetupMainMenu()
        {
            Image bg = mainMenuPanel.AddComponent<Image>();
            bg.color = new Color(0, 0, 0, 0.8f);

            Sprite titleSprite = LoadSprite("ui/hell_verdict");
            if (titleSprite != null)
            {
                GameObject titleObj = new GameObject("TitleLogo");
                titleObj.transform.SetParent(mainMenuPanel.transform, false);
                Image titleImage = titleObj.AddComponent<Image>();
                titleImage.sprite = titleSprite;
                titleImage.preserveAspect = true;
                titleImage.raycastTarget = false;
                RectTransform titleRect = titleObj.GetComponent<RectTransform>();
                titleRect.anchorMin = new Vector2(0.5f, 0.5f);
                titleRect.anchorMax = new Vector2(0.5f, 0.5f);
                titleRect.pivot = new Vector2(0.5f, 0.5f);
                titleRect.anchoredPosition = new Vector2(0f, 155f);
                titleRect.sizeDelta = new Vector2(520f, 120f);
            }
            else
            {
                Text title = CreateHUDText(mainMenuPanel, "Hell Verdict", 0, 220, TextAnchor.MiddleCenter);
                RectTransform titleRect = (RectTransform)title.transform;
                titleRect.anchoredPosition = new Vector2(0f, 150f);
                titleRect.sizeDelta = new Vector2(420f, 64f);
                title.fontSize = 42;
            }

            CreateMenuButton(mainMenuPanel, "Continue", 60, () => GameManager.Instance.ContinueGame());
            CreateMenuButton(mainMenuPanel, "New Game", 5, () => GameManager.Instance.StartNewGame());
            CreateMenuButton(mainMenuPanel, "Options", -50, () => ShowOptions());
            CreateMenuButton(mainMenuPanel, "Quit", -105, () => GameManager.Instance.QuitGame());
        }

        private void SetupGameplayHUD()
        {
            healthText = CreateHUDText(gameplayHUDPanel, "Health: 100", -480, 450, TextAnchor.UpperLeft);
            ammoText = CreateHUDText(gameplayHUDPanel, "Ammo: 30", 480, 450, TextAnchor.UpperRight);
            levelText = CreateHUDText(gameplayHUDPanel, "Level: 1", 0, 450, TextAnchor.UpperCenter);
            healText = CreateHUDText(gameplayHUDPanel, "Heal [F]: 3", -480, 420, TextAnchor.UpperLeft);
        }

        private void SetupPauseMenu()
        {
            Image bg = pauseMenuPanel.AddComponent<Image>();
            bg.color = new Color(0, 0, 0, 0.7f);

            CreateMenuButton(pauseMenuPanel, "Resume", 100, () => GameManager.Instance.TogglePause());
            CreateMenuButton(pauseMenuPanel, "Options", 0, () => ShowOptions());
            CreateMenuButton(pauseMenuPanel, "Return to Title", -100, () => GameManager.Instance.ShowMainMenu());
            CreateMenuButton(pauseMenuPanel, "Quit", -200, () => GameManager.Instance.QuitGame());
        }

        private void SetupOptions()
        {
            Image bg = optionsPanel.AddComponent<Image>();
            bg.color = new Color(0, 0, 0, 0.9f);

            Text title = CreateHUDText(optionsPanel, "Options", 0, 155, TextAnchor.MiddleCenter);
            title.fontSize = 34;

            masterVolumeValue = CreateVolumeRow("Master Volume", 75, () => AudioManager.Instance.SetMasterVolume(AudioManager.Instance.GetMasterVolume() - 1f), () => AudioManager.Instance.SetMasterVolume(AudioManager.Instance.GetMasterVolume() + 1f));
            bgmVolumeValue = CreateVolumeRow("BGM", 25, () => AudioManager.Instance.SetBGMVolume(AudioManager.Instance.GetBGMVolume() - 1f), () => AudioManager.Instance.SetBGMVolume(AudioManager.Instance.GetBGMVolume() + 1f));
            sfxVolumeValue = CreateVolumeRow("SFX", -25, () => AudioManager.Instance.SetSFXVolume(AudioManager.Instance.GetSFXVolume() - 1f), () => AudioManager.Instance.SetSFXVolume(AudioManager.Instance.GetSFXVolume() + 1f));
            sensitivityValue = CreateSensitivityRow("Mouse Sensitivity", -75);

            CreateMenuButton(optionsPanel, "Save", -140, () => GameManager.Instance.SaveGame());
            CreateMenuButton(optionsPanel, "Return", -190, () => HideOptions());
            CreateMenuButton(optionsPanel, "Title", -240, () => GameManager.Instance.ShowMainMenu());
            UpdateOptionValues();
        }

        private void SetupGameOverScreen()
        {
            Image bg = gameOverPanel.AddComponent<Image>();
            bg.color = new Color(0.6f, 0.05f, 0.05f, 0.95f);

            GameObject imageObj = new GameObject("GameOverImage");
            imageObj.transform.SetParent(gameOverPanel.transform, false);
            Image gameOverImage = imageObj.AddComponent<Image>();
            gameOverImage.sprite = LoadSprite("textures/game_over");
            gameOverImage.preserveAspect = true;
            gameOverImage.raycastTarget = false;
            gameOverImage.color = Color.white;

            RectTransform rect = imageObj.GetComponent<RectTransform>();
            rect.anchorMin = new Vector2(0.5f, 0.5f);
            rect.anchorMax = new Vector2(0.5f, 0.5f);
            rect.pivot = new Vector2(0.5f, 0.5f);
            rect.anchoredPosition = new Vector2(0, 120);
            rect.sizeDelta = new Vector2(500, 250);

            CreateMenuButton(gameOverPanel, "Return to Title", -80, () => GameManager.Instance.ShowMainMenu());
            CreateMenuButton(gameOverPanel, "Quit", -150, () => GameManager.Instance.QuitGame());
        }

        private void SetupGameWonScreen()
        {
            Image bg = gameWonPanel.AddComponent<Image>();
            bg.color = new Color(0, 0.5f, 0, 0.9f);

            CreateHUDText(gameWonPanel, "YOU WIN!", 0, 100, TextAnchor.MiddleCenter).fontSize = 60;
            CreateMenuButton(gameWonPanel, "Return to Title", -100, () => GameManager.Instance.ShowMainMenu());
            CreateMenuButton(gameWonPanel, "Quit", -200, () => GameManager.Instance.QuitGame());
        }

        private void SetupStageClearScreen()
        {
            Image bg = stageClearPanel.AddComponent<Image>();
            bg.color = new Color(0, 0, 0, 0.92f);

            GameObject imageObj = new GameObject("WinImage");
            imageObj.transform.SetParent(stageClearPanel.transform, false);
            Image winImage = imageObj.AddComponent<Image>();
            winImage.sprite = LoadSprite("textures/win");
            winImage.preserveAspect = true;
            winImage.raycastTarget = false;

            RectTransform rect = imageObj.GetComponent<RectTransform>();
            rect.anchorMin = new Vector2(0.5f, 0.5f);
            rect.anchorMax = new Vector2(0.5f, 0.5f);
            rect.pivot = new Vector2(0.5f, 0.5f);
            rect.anchoredPosition = Vector2.zero;
            rect.sizeDelta = new Vector2(620f, 360f);
        }

        private void SetupLoadingScreen()
        {
            Image bg = loadingPanel.AddComponent<Image>();
            bg.color = new Color(0, 0, 0, 0.85f);

            Text loadingText = CreateHUDText(loadingPanel, "LOADING...", 0, 0, TextAnchor.MiddleCenter);
            loadingText.fontSize = 40;
        }

        private void SetupMinimap()
        {
            CanvasGroup group = minimapPanel.GetComponent<CanvasGroup>();
            if (group != null)
                group.blocksRaycasts = false;

            GameObject mapObj = new GameObject("MinimapImage");
            mapObj.transform.SetParent(minimapPanel.transform, false);
            minimapImage = mapObj.AddComponent<RawImage>();
            minimapImage.color = Color.white;
            minimapImage.raycastTarget = false;
            minimapTexture = new Texture2D(MinimapTextureSize, MinimapTextureSize, TextureFormat.RGBA32, false);
            minimapTexture.filterMode = FilterMode.Point;
            minimapImage.texture = minimapTexture;

            RectTransform mapRect = mapObj.GetComponent<RectTransform>();
            mapRect.anchorMin = new Vector2(1f, 1f);
            mapRect.anchorMax = new Vector2(1f, 1f);
            mapRect.pivot = new Vector2(1f, 1f);
            mapRect.anchoredPosition = new Vector2(-22f, -22f);
            mapRect.sizeDelta = new Vector2(220f, 220f);

            minimapInfoText = CreateHUDText(minimapPanel, "Enemies: 0", 0, 0, TextAnchor.UpperRight);
            RectTransform textRect = (RectTransform)minimapInfoText.transform;
            textRect.anchorMin = new Vector2(1f, 1f);
            textRect.anchorMax = new Vector2(1f, 1f);
            textRect.pivot = new Vector2(1f, 1f);
            textRect.anchoredPosition = new Vector2(-22f, -248f);
            textRect.sizeDelta = new Vector2(240f, 34f);
            minimapInfoText.fontSize = 20;
        }

        private void CreateMenuButton(GameObject parent, string text, float yOffset, UnityEngine.Events.UnityAction onClick)
        {
            GameObject buttonObj = new GameObject("Button_" + text);
            buttonObj.transform.SetParent(parent.transform, false);

            RectTransform rect = buttonObj.AddComponent<RectTransform>();
            rect.anchoredPosition = new Vector2(0, yOffset);
            rect.sizeDelta = new Vector2(260, 42);

            Image image = buttonObj.AddComponent<Image>();
            image.color = new Color(0.2f, 0.2f, 0.2f);

            Button button = buttonObj.AddComponent<Button>();
            button.targetGraphic = image;
            button.navigation = new Navigation { mode = Navigation.Mode.None };
            RuntimeMenuButton menuButton = buttonObj.AddComponent<RuntimeMenuButton>();
            menuButton.Initialize(onClick, image);
            button.onClick.AddListener(menuButton.InvokeAction);

            Text buttonText = new GameObject("Text").AddComponent<Text>();
            buttonText.transform.SetParent(buttonObj.transform, false);
            buttonText.text = text;
            buttonText.alignment = TextAnchor.MiddleCenter;
            buttonText.fontSize = 22;
            buttonText.color = Color.white;
            buttonText.font = GetRuntimeFont();
            buttonText.raycastTarget = false;
            ((RectTransform)buttonText.transform).anchorMin = Vector2.zero;
            ((RectTransform)buttonText.transform).anchorMax = Vector2.one;
            ((RectTransform)buttonText.transform).offsetMin = Vector2.zero;
            ((RectTransform)buttonText.transform).offsetMax = Vector2.zero;
        }

        private Text CreateHUDText(GameObject parent, string text, float xOffset, float yOffset, TextAnchor alignment)
        {
            GameObject textObj = new GameObject("Text_" + text);
            textObj.transform.SetParent(parent.transform, false);

            RectTransform rect = textObj.AddComponent<RectTransform>();
            rect.anchoredPosition = new Vector2(xOffset, yOffset);
            rect.sizeDelta = new Vector2(360, 64);

            Text textComponent = textObj.AddComponent<Text>();
            textComponent.text = text;
            textComponent.alignment = alignment;
            textComponent.fontSize = 32;
            textComponent.color = Color.white;
            textComponent.font = GetRuntimeFont();
            textComponent.raycastTarget = false;

            return textComponent;
        }

        private void CreateOptionsLabel(GameObject parent, string text, float yOffset)
        {
            Text label = CreateHUDText(parent, text, 0, yOffset, TextAnchor.MiddleCenter);
            label.fontSize = 32;
        }

        private Text CreateVolumeRow(string label, float yOffset, UnityEngine.Events.UnityAction decrease, UnityEngine.Events.UnityAction increase)
        {
            Text labelText = CreateHUDText(optionsPanel, label, -120, yOffset, TextAnchor.MiddleLeft);
            labelText.fontSize = 20;
            ((RectTransform)labelText.transform).sizeDelta = new Vector2(170, 36);

            Text valueText = CreateHUDText(optionsPanel, "8", 65, yOffset, TextAnchor.MiddleCenter);
            valueText.fontSize = 22;
            ((RectTransform)valueText.transform).sizeDelta = new Vector2(50, 36);

            CreateSmallButton(optionsPanel, "-", 15, yOffset, () => { decrease.Invoke(); UpdateOptionValues(); });
            CreateSmallButton(optionsPanel, "+", 115, yOffset, () => { increase.Invoke(); UpdateOptionValues(); });

            return valueText;
        }

        private Text CreateSensitivityRow(string label, float yOffset)
        {
            Text labelText = CreateHUDText(optionsPanel, label, -160, yOffset, TextAnchor.MiddleLeft);
            labelText.fontSize = 20;
            ((RectTransform)labelText.transform).sizeDelta = new Vector2(230, 36);

            GameObject sliderObj = new GameObject("Slider_MouseSensitivity");
            sliderObj.transform.SetParent(optionsPanel.transform, false);
            RectTransform sliderRect = sliderObj.AddComponent<RectTransform>();
            sliderRect.anchoredPosition = new Vector2(80f, yOffset);
            sliderRect.sizeDelta = new Vector2(220f, 24f);

            GameObject backgroundObj = new GameObject("Background");
            backgroundObj.transform.SetParent(sliderObj.transform, false);
            Image background = backgroundObj.AddComponent<Image>();
            background.color = Color.white;
            RectTransform backgroundRect = backgroundObj.GetComponent<RectTransform>();
            backgroundRect.anchorMin = new Vector2(0f, 0.5f);
            backgroundRect.anchorMax = new Vector2(1f, 0.5f);
            backgroundRect.pivot = new Vector2(0.5f, 0.5f);
            backgroundRect.offsetMin = new Vector2(0f, -2f);
            backgroundRect.offsetMax = new Vector2(0f, 2f);

            GameObject handleObj = new GameObject("Handle");
            handleObj.transform.SetParent(sliderObj.transform, false);
            Image handle = handleObj.AddComponent<Image>();
            handle.sprite = CreateCircleSprite();
            handle.color = Color.white;
            RectTransform handleRect = handleObj.GetComponent<RectTransform>();
            handleRect.anchorMin = new Vector2(0.5f, 0.5f);
            handleRect.anchorMax = new Vector2(0.5f, 0.5f);
            handleRect.pivot = new Vector2(0.5f, 0.5f);
            handleRect.sizeDelta = new Vector2(12f, 12f);

            sensitivitySlider = sliderObj.AddComponent<RuntimeSensitivitySlider>();

            Text valueText = CreateHUDText(optionsPanel, "100%", 245, yOffset, TextAnchor.MiddleRight);
            valueText.fontSize = 20;
            ((RectTransform)valueText.transform).sizeDelta = new Vector2(70, 36);
            sensitivitySlider.Initialize(sliderRect, handleRect, 25f, 200f, PlayerPrefs.GetFloat(PlayerController.SensitivityPrefsKey, 100f), SetMouseSensitivity);
            return valueText;
        }

        private void CreateSmallButton(GameObject parent, string text, float xOffset, float yOffset, UnityEngine.Events.UnityAction onClick)
        {
            GameObject buttonObj = new GameObject("Button_" + text);
            buttonObj.transform.SetParent(parent.transform, false);

            RectTransform rect = buttonObj.AddComponent<RectTransform>();
            rect.anchoredPosition = new Vector2(xOffset, yOffset);
            rect.sizeDelta = new Vector2(36, 30);

            Image image = buttonObj.AddComponent<Image>();
            image.color = new Color(0.22f, 0.22f, 0.22f, 0.95f);
            buttonObj.AddComponent<RuntimeMenuButton>().Initialize(onClick, image);

            Text buttonText = new GameObject("Text").AddComponent<Text>();
            buttonText.transform.SetParent(buttonObj.transform, false);
            buttonText.text = text;
            buttonText.alignment = TextAnchor.MiddleCenter;
            buttonText.fontSize = 20;
            buttonText.color = Color.white;
            buttonText.font = GetRuntimeFont();
            buttonText.raycastTarget = false;
            ((RectTransform)buttonText.transform).anchorMin = Vector2.zero;
            ((RectTransform)buttonText.transform).anchorMax = Vector2.one;
            ((RectTransform)buttonText.transform).offsetMin = Vector2.zero;
            ((RectTransform)buttonText.transform).offsetMax = Vector2.zero;
        }

        public void ShowMainMenu()
        {
            HideAllPanels();
            SetPanelActive(mainMenuPanel, true);
        }

        public void ShowGameplayHUD()
        {
            HideAllPanels();
            SetPanelActive(gameplayHUDPanel, true);
            UpdateHUD(100, 30, GameManager.Instance.GetCurrentLevel());
        }

        public void ShowPauseMenu(bool show)
        {
            SetPanelActive(pauseMenuPanel, show);
            if (show)
                SetPanelActive(optionsPanel, false);
            SetPanelActive(gameplayHUDPanel, true);
        }

        public void ShowOptions()
        {
            optionsOpenedFromPause = pauseMenuPanel != null && pauseMenuPanel.activeSelf;
            HideAllPanels();
            if (optionsOpenedFromPause)
                SetPanelActive(gameplayHUDPanel, true);
            SetPanelActive(optionsPanel, true);
            UpdateOptionValues();
        }

        public void ShowGameplayOptions()
        {
            optionsOpenedFromPause = true;
            HideAllPanels();
            SetPanelActive(gameplayHUDPanel, true);
            SetPanelActive(optionsPanel, true);
            UpdateOptionValues();
        }

        public void HideOptions()
        {
            SetPanelActive(optionsPanel, false);
            if (!optionsOpenedFromPause)
            {
                SetPanelActive(mainMenuPanel, true);
            }
            else
            {
                SetPanelActive(pauseMenuPanel, true);
                SetPanelActive(gameplayHUDPanel, true);
            }
        }

        public void ShowGameOverScreen()
        {
            HideAllPanels();
            SetPanelActive(gameOverPanel, true);
        }

        public void ShowGameWonScreen()
        {
            HideAllPanels();
            SetPanelActive(gameWonPanel, true);
        }

        public void ShowStageClearScreen()
        {
            HideAllPanels();
            SetPanelActive(stageClearPanel, true);
        }

        public void ShowLoadingScreen()
        {
            HideAllPanels();
            SetPanelActive(loadingPanel, true);
        }

        public void ToggleMinimap()
        {
            if (gameplayHUDPanel == null || !gameplayHUDPanel.activeSelf || minimapPanel == null)
                return;

            bool show = !minimapPanel.activeSelf;
            SetPanelActive(minimapPanel, show);
            CanvasGroup group = minimapPanel.GetComponent<CanvasGroup>();
            if (group != null)
                group.blocksRaycasts = false;

            if (show)
            {
                nextMinimapRefreshTime = 0f;
                RefreshMinimap();
            }
        }

        public void UpdateHUD(int health, int ammo, int level, int healCharges = 3)
        {
            if (healthText != null) healthText.text = $"Health: {health}";
            if (ammoText != null) ammoText.text = ammo >= 999 ? "Ammo: \u221E" : $"Ammo: {ammo}";
            if (levelText != null) levelText.text = $"Level: {level}";
            if (healText != null) healText.text = $"Heal [F]: {healCharges}";
        }

        public Transform GetGameplayHUDTransform()
        {
            return gameplayHUDPanel != null ? gameplayHUDPanel.transform : mainCanvas.transform;
        }

        private void RefreshMinimap()
        {
            if (minimapTexture == null)
                return;

            HellVerdictBootstrap bootstrap = FindAnyObjectByType<HellVerdictBootstrap>();
            Vector2Int dimensions = bootstrap != null ? bootstrap.GetMazeDimensions() : Vector2Int.zero;
            if (bootstrap == null || dimensions.x <= 0 || dimensions.y <= 0)
                return;

            Color32[] pixels = new Color32[MinimapTextureSize * MinimapTextureSize];
            Color32 background = new(0, 0, 0, 210);
            for (int i = 0; i < pixels.Length; i++)
                pixels[i] = background;

            foreach (Vector2Int cell in bootstrap.GetOpenCells())
                DrawMinimapCell(pixels, cell, dimensions, new Color32(55, 55, 55, 255), 1);

            DrawMinimapCell(pixels, bootstrap.GetExitCell(), dimensions, new Color32(255, 210, 45, 255), 3);

            EnemyController[] enemies = FindObjectsByType<EnemyController>(FindObjectsInactive.Exclude);
            foreach (EnemyController enemy in enemies)
            {
                if (enemy != null && enemy.IsAlive)
                    DrawMinimapCell(pixels, bootstrap.WorldToCell(enemy.transform.position), dimensions, new Color32(220, 30, 30, 255), 2);
            }

            DrawMinimapCell(pixels, bootstrap.GetPlayerCell(), dimensions, new Color32(45, 230, 95, 255), 3);
            minimapTexture.SetPixels32(pixels);
            minimapTexture.Apply(false);

            if (minimapInfoText != null)
                minimapInfoText.text = $"Enemies: {bootstrap.GetLiveEnemyCount()}";
        }

        private void DrawMinimapCell(Color32[] pixels, Vector2Int cell, Vector2Int dimensions, Color32 color, int radius)
        {
            int x = Mathf.RoundToInt(Mathf.InverseLerp(0, Mathf.Max(1, dimensions.x - 1), cell.x) * (MinimapTextureSize - 1));
            int y = Mathf.RoundToInt(Mathf.InverseLerp(0, Mathf.Max(1, dimensions.y - 1), cell.y) * (MinimapTextureSize - 1));

            for (int py = y - radius; py <= y + radius; py++)
            {
                if (py < 0 || py >= MinimapTextureSize)
                    continue;

                for (int px = x - radius; px <= x + radius; px++)
                {
                    if (px < 0 || px >= MinimapTextureSize)
                        continue;

                    pixels[py * MinimapTextureSize + px] = color;
                }
            }
        }

        private void ShowSplashScreen()
        {
            splashPanel = CreatePanel("SplashPanel", 10, true);
            Image splash = splashPanel.AddComponent<Image>();
            splash.color = Color.black;

            GameObject watermarkObj = new GameObject("Watermark");
            watermarkObj.transform.SetParent(splashPanel.transform, false);
            Image watermark = watermarkObj.AddComponent<Image>();
            watermark.sprite = LoadSprite("ui/watermark");
            watermark.preserveAspect = true;

            RectTransform rect = watermarkObj.GetComponent<RectTransform>();
            rect.anchorMin = new Vector2(0.5f, 0.5f);
            rect.anchorMax = new Vector2(0.5f, 0.5f);
            rect.pivot = new Vector2(0.5f, 0.5f);
            rect.anchoredPosition = Vector2.zero;
            rect.sizeDelta = GetSplashSize(watermark.sprite);
            StartCoroutine(HideSplashAfterDelay());
        }

        private Vector2 GetSplashSize(Sprite sprite)
        {
            if (sprite == null)
                return new Vector2(520f, 260f);

            float maxWidth = 620f;
            float maxHeight = 260f;
            float aspect = sprite.rect.width / Mathf.Max(1f, sprite.rect.height);
            float width = maxWidth;
            float height = width / aspect;
            if (height > maxHeight)
            {
                height = maxHeight;
                width = height * aspect;
            }

            return new Vector2(width, height);
        }

        private IEnumerator HideSplashAfterDelay()
        {
            yield return new WaitForSecondsRealtime(2f);
            HideSplash();
        }

        private void HideSplash()
        {
            if (splashPanel != null)
                Destroy(splashPanel);
            SetPanelActive(mainMenuPanel, true);
        }

        private void HideAllPanels()
        {
            SetPanelActive(mainMenuPanel, false);
            SetPanelActive(gameplayHUDPanel, false);
            SetPanelActive(pauseMenuPanel, false);
            SetPanelActive(optionsPanel, false);
            SetPanelActive(gameOverPanel, false);
            SetPanelActive(gameWonPanel, false);
            SetPanelActive(stageClearPanel, false);
            SetPanelActive(minimapPanel, false);
            SetPanelActive(loadingPanel, false);
        }

        private void SetPanelActive(GameObject panel, bool active)
        {
            if (panel == null)
                return;

            panel.SetActive(active);
            CanvasGroup canvasGroup = panel.GetComponent<CanvasGroup>();
            if (canvasGroup != null)
                canvasGroup.blocksRaycasts = active;
        }

        private void UpdateOptionValues()
        {
            if (AudioManager.Instance == null)
                return;

            if (masterVolumeValue != null) masterVolumeValue.text = Mathf.RoundToInt(AudioManager.Instance.GetMasterVolume()).ToString();
            if (bgmVolumeValue != null) bgmVolumeValue.text = Mathf.RoundToInt(AudioManager.Instance.GetBGMVolume()).ToString();
            if (sfxVolumeValue != null) sfxVolumeValue.text = Mathf.RoundToInt(AudioManager.Instance.GetSFXVolume()).ToString();
            float sensitivity = PlayerPrefs.GetFloat(PlayerController.SensitivityPrefsKey, 100f);
            if (sensitivitySlider != null && !Mathf.Approximately(sensitivitySlider.Value, sensitivity))
                sensitivitySlider.SetValueWithoutNotify(sensitivity);
            if (sensitivityValue != null)
                sensitivityValue.text = $"{Mathf.RoundToInt(sensitivity)}%";
        }

        private void SetMouseSensitivity(float value)
        {
            int roundedValue = Mathf.RoundToInt(value);
            PlayerController player = FindAnyObjectByType<PlayerController>();
            if (player != null)
                player.SetSensitivityPercent(roundedValue);
            else
            {
                PlayerPrefs.SetFloat(PlayerController.SensitivityPrefsKey, roundedValue);
                PlayerPrefs.Save();
            }

            if (sensitivityValue != null)
                sensitivityValue.text = $"{roundedValue}%";
        }

        private Sprite CreateCircleSprite()
        {
            const int size = 32;
            Texture2D texture = new(size, size, TextureFormat.RGBA32, false);
            texture.filterMode = FilterMode.Point;
            Vector2 center = new((size - 1) * 0.5f, (size - 1) * 0.5f);
            float radius = size * 0.42f;

            for (int y = 0; y < size; y++)
            {
                for (int x = 0; x < size; x++)
                {
                    float distance = Vector2.Distance(new Vector2(x, y), center);
                    texture.SetPixel(x, y, distance <= radius ? Color.white : Color.clear);
                }
            }

            texture.Apply();
            return Sprite.Create(texture, new Rect(0f, 0f, size, size), new Vector2(0.5f, 0.5f), size);
        }

        private Sprite LoadSprite(string path)
        {
            Sprite sprite = Resources.Load<Sprite>(path);
            if (sprite != null)
            {
                if (sprite.texture != null)
                    sprite.texture.filterMode = FilterMode.Point;
                return sprite;
            }

            Texture2D texture = Resources.Load<Texture2D>(path);
            if (texture == null)
                texture = LoadTextureFromDisk(path);

            if (texture == null)
                return null;

            texture.filterMode = FilterMode.Point;
            return Sprite.Create(texture, new Rect(0f, 0f, texture.width, texture.height), new Vector2(0.5f, 0.5f), 100f);
        }

        private Texture2D LoadTextureFromDisk(string path)
        {
            string fileName = path + ".png";
            string dataPath = Application.dataPath;
            string appRoot = Directory.GetParent(dataPath)?.FullName;
            string projectRoot = Directory.GetParent(Directory.GetParent(appRoot ?? string.Empty)?.FullName ?? string.Empty)?.FullName;
            string repoRoot = Directory.GetParent(dataPath)?.FullName;

            string[] candidates =
            {
                Path.Combine(dataPath, fileName),
                Path.Combine(dataPath, "resources", fileName),
                Path.Combine(projectRoot ?? string.Empty, "Assets", fileName),
                Path.Combine(projectRoot ?? string.Empty, "Assets", "resources", fileName),
                Path.Combine(repoRoot ?? string.Empty, "Hell Verdict", "Assets", fileName),
                Path.Combine(repoRoot ?? string.Empty, "Hell Verdict", "Assets", "resources", fileName)
            };

            foreach (string candidate in candidates)
            {
                if (!File.Exists(candidate))
                    continue;

                Texture2D texture = new(2, 2, TextureFormat.RGBA32, false);
                if (texture.LoadImage(File.ReadAllBytes(candidate)))
                {
                    texture.name = Path.GetFileNameWithoutExtension(candidate);
                    texture.filterMode = FilterMode.Point;
                    texture.wrapMode = TextureWrapMode.Clamp;
                    return texture;
                }
            }

            return null;
        }

        private Font GetRuntimeFont()
        {
            Font font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
            if (font != null)
                return font;

            return Font.CreateDynamicFontFromOSFont(new[] { "Segoe UI", "Arial", "Tahoma" }, 32);
        }
    }

    public class RuntimeMenuButton : MonoBehaviour, IPointerClickHandler, IPointerEnterHandler, IPointerExitHandler
    {
        private UnityEngine.Events.UnityAction action;
        private RectTransform rectTransform;
        private Image image;
        private Vector3 baseScale;
        private float lastClickTime = -1f;
        private bool hovering;

        public void Initialize(UnityEngine.Events.UnityAction clickAction, Image targetImage)
        {
            action = clickAction;
            rectTransform = GetComponent<RectTransform>();
            image = targetImage;
            baseScale = transform.localScale;
        }

        private void Awake()
        {
            rectTransform = GetComponent<RectTransform>();
        }

        private void Update()
        {
            if (action == null || rectTransform == null)
                return;

            UpdateHoverState();
            Vector2 mousePosition;
            bool clicked = TryGetMouseClick(out mousePosition);
            if (clicked && RectTransformUtility.RectangleContainsScreenPoint(rectTransform, mousePosition, null))
                InvokeAction();
        }

        private bool TryGetMouseClick(out Vector2 mousePosition)
        {
            mousePosition = default;

            Mouse mouse = Mouse.current;
            if (mouse == null || !mouse.leftButton.wasPressedThisFrame)
                return false;

            mousePosition = mouse.position.ReadValue();
            return true;
        }

        public void OnPointerClick(PointerEventData eventData)
        {
            InvokeAction();
        }

        public void OnPointerEnter(PointerEventData eventData)
        {
            SetHovering(true);
        }

        public void OnPointerExit(PointerEventData eventData)
        {
            SetHovering(false);
        }

        public void InvokeAction()
        {
            if (Time.unscaledTime - lastClickTime < 0.12f)
                return;

            lastClickTime = Time.unscaledTime;
            InputManager.Instance?.SuppressFireUntilReleased();
            action?.Invoke();
        }

        private void UpdateHoverState()
        {
            Mouse mouse = Mouse.current;
            if (mouse == null)
                return;

            SetHovering(RectTransformUtility.RectangleContainsScreenPoint(rectTransform, mouse.position.ReadValue(), null));
        }

        private void SetHovering(bool isHovering)
        {
            if (hovering == isHovering)
                return;

            hovering = isHovering;
            if (image != null)
                image.color = hovering ? new Color32(0x9c, 0x0c, 0x0c, 0xff) : new Color(0.2f, 0.2f, 0.2f);

            transform.localScale = hovering ? baseScale * 1.04f : baseScale;
        }
    }

    public class RuntimeSensitivitySlider : MonoBehaviour
    {
        public float Value { get; private set; }

        private RectTransform trackRect;
        private RectTransform handleRect;
        private float minValue;
        private float maxValue;
        private UnityEngine.Events.UnityAction<float> onValueChanged;
        private bool dragging;

        public void Initialize(RectTransform track, RectTransform handle, float min, float max, float value, UnityEngine.Events.UnityAction<float> changed)
        {
            trackRect = track;
            handleRect = handle;
            minValue = min;
            maxValue = max;
            onValueChanged = changed;
            SetValue(value, false);
            onValueChanged?.Invoke(Value);
        }

        public void SetValueWithoutNotify(float value)
        {
            SetValue(value, false);
        }

        private void Update()
        {
            Mouse mouse = Mouse.current;
            if (mouse == null || trackRect == null)
                return;

            Vector2 mousePosition = mouse.position.ReadValue();
            bool inside = RectTransformUtility.RectangleContainsScreenPoint(trackRect, mousePosition, null);
            if (mouse.leftButton.wasPressedThisFrame && inside)
                dragging = true;

            if (!mouse.leftButton.isPressed)
                dragging = false;

            if (dragging)
                SetValue(ValueFromMouse(mousePosition), true);
        }

        private float ValueFromMouse(Vector2 mousePosition)
        {
            RectTransformUtility.ScreenPointToLocalPointInRectangle(trackRect, mousePosition, null, out Vector2 localPoint);
            float normalized = Mathf.InverseLerp(trackRect.rect.xMin, trackRect.rect.xMax, localPoint.x);
            return Mathf.Lerp(minValue, maxValue, normalized);
        }

        private void SetValue(float value, bool notify)
        {
            Value = Mathf.Round(Mathf.Clamp(value, minValue, maxValue));
            UpdateHandle();
            if (notify)
                onValueChanged?.Invoke(Value);
        }

        private void UpdateHandle()
        {
            if (trackRect == null || handleRect == null)
                return;

            float normalized = Mathf.InverseLerp(minValue, maxValue, Value);
            float x = Mathf.Lerp(trackRect.rect.xMin, trackRect.rect.xMax, normalized);
            handleRect.anchoredPosition = new Vector2(x, 0f);
        }
    }
}
