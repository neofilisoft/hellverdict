using System.Collections.Generic;
using BalmungDoom.Enemy;
using BalmungDoom.Gameplay;
using UnityEngine;
using UnityEngine.UI;

namespace BalmungDoom.Core
{
    public class HellVerdictBootstrap : MonoBehaviour
    {
        private readonly List<GameObject> levelObjects = new();
        private readonly HashSet<Vector2Int> blockedCells = new();
        private HashSet<Vector2Int> openCellSet;
        private ProceduralMazeGenerator mazeGenerator;
        private Vector2Int mazeDimensions;
        private Vector2Int playerStartCell;
        private Vector2Int exitCell;
        private GameObject player;
        private int liveEnemies;
        private const float MazeCellSize = 2.5f;
        private const float MazeWallHeight = 5.5f;
        public bool HasCyberDemonInCurrentLevel { get; private set; }
        public bool BalmungDoomMode { get; set; }

        private Shader cachedWorldShader;
        private Material[] cachedWallMaterials;
        private Material cachedFloorMaterial;

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.BeforeSceneLoad)]
        private static void CreateRuntime()
        {
            if (FindAnyObjectByType<HellVerdictBootstrap>() != null)
                return;

            GameObject runtime = new GameObject("hellverdict_runtime");
            DontDestroyOnLoad(runtime);
            runtime.AddComponent<InputManager>();
            runtime.AddComponent<AudioManager>();
            runtime.AddComponent<GameManager>();
            runtime.AddComponent<PhysicsManager>();
            runtime.AddComponent<HellVerdictBootstrap>();
            runtime.AddComponent<UIManager>();

            Resources.Load<Material>("materials/URPLitBase");
            Shader.WarmupAllShaders();

            HellVerdictBootstrap boot = FindAnyObjectByType<HellVerdictBootstrap>();
            if (boot != null)
                boot.PrewarmMaterials();
        }

        public void PrewarmMaterials()
        {
            cachedWorldShader = GetWorldShader();
            cachedFloorMaterial = CreateSolidMaterial(new Color(0.16f, 0.13f, 0.12f));
            cachedWallMaterials = new Material[5];
            for (int i = 0; i < 5; i++)
            {
                Texture2D texture = Resources.Load<Texture2D>($"textures/{i + 1}");
                cachedWallMaterials[i] = new Material(cachedWorldShader);
                if (texture != null)
                {
                    texture.wrapMode = TextureWrapMode.Repeat;
                    texture.filterMode = FilterMode.Point;
                    cachedWallMaterials[i].mainTexture = texture;
                    cachedWallMaterials[i].mainTextureScale = Vector2.one;
                    cachedWallMaterials[i].SetTexture("_BaseMap", texture);
                    cachedWallMaterials[i].SetColor("_BaseColor", Color.white);
                }
            }
            Shader.WarmupAllShaders();
        }

        public void BuildLevel(int level)
        {
            ClearLevel();
            Camera camera = EnsureCamera();
            player = EnsurePlayer(camera);

            BuildArena(level);
            Vector3 playerStart = mazeGenerator.CellToWorld(playerStartCell, MazeCellSize, 0.05f);
            player.GetComponent<PlayerController>()?.ResetForLevel(playerStart);
            SpawnTightSpots(level);
            SpawnDecorations(level);
            SpawnEnemies(level);
            Shader.WarmupAllShaders();
        }

        private Camera EnsureCamera()
        {
            Camera camera = Camera.main;
            if (camera == null)
            {
                GameObject cameraObj = new GameObject("Main Camera");
                cameraObj.tag = "MainCamera";
                camera = cameraObj.AddComponent<Camera>();
                cameraObj.AddComponent<AudioListener>();
            }

            camera.fieldOfView = 70f;
            camera.nearClipPlane = 0.05f;
            camera.farClipPlane = 120f;
            return camera;
        }

        private GameObject EnsurePlayer(Camera camera)
        {
            GameObject existing = GameObject.FindGameObjectWithTag("Player");
            if (existing != null)
            {
                CreateWeaponOverlay();
                return existing;
            }

            GameObject playerObj = new GameObject("Player");
            playerObj.tag = "Player";
            CharacterController controller = playerObj.AddComponent<CharacterController>();
            controller.height = 1.8f;
            controller.radius = 0.35f;
            controller.center = new Vector3(0f, 0.9f, 0f);
            controller.stepOffset = 0.05f;
            controller.slopeLimit = 0f;
            playerObj.AddComponent<PlayerController>();

            camera.transform.SetParent(playerObj.transform, false);
            camera.transform.localPosition = new Vector3(0f, 1.55f, 0f);
            camera.transform.localRotation = Quaternion.identity;

            GameObject weaponObj = new GameObject("Shotgun");
            weaponObj.transform.SetParent(playerObj.transform, false);
            weaponObj.AddComponent<WeaponSystem>();
            CreateWeaponOverlay();
            return playerObj;
        }

        private void CreateWeaponOverlay()
        {
            Canvas canvas = FindAnyObjectByType<Canvas>();
            if (canvas == null)
                return;

            GameObject existingOverlay = GameObject.Find("ShotgunOverlay");
            if (existingOverlay != null)
                Destroy(existingOverlay);

            GameObject weaponUi = new GameObject("ShotgunOverlay");
            Transform parent = FindAnyObjectByType<UIManager>()?.GetGameplayHUDTransform() ?? canvas.transform;
            weaponUi.transform.SetParent(parent, false);
            Image image = weaponUi.AddComponent<Image>();
            image.sprite = LoadSprite("sprites/weapon/shotgun/0");
            image.preserveAspect = true;

            RectTransform rect = weaponUi.GetComponent<RectTransform>();
            rect.anchorMin = new Vector2(0.5f, 0f);
            rect.anchorMax = new Vector2(0.5f, 0f);
            rect.pivot = new Vector2(0.5f, 0f);
            rect.anchoredPosition = new Vector2(0f, 0f);
            rect.sizeDelta = new Vector2(520f, 320f);
        }

        private void BuildArena(int level)
        {
            int size = 28 + Mathf.Min(level, GameManager.MaxStage) * 6;
            Material[] wallMaterials = GetCachedWallMaterials(level);
            Material floorMaterial = cachedFloorMaterial ?? CreateSolidMaterial(new Color(0.16f, 0.13f, 0.12f));

            mazeGenerator = gameObject.GetComponent<ProceduralMazeGenerator>();
            if (mazeGenerator == null)
                mazeGenerator = gameObject.AddComponent<ProceduralMazeGenerator>();

            mazeDimensions = mazeGenerator.Generate(
                size,
                size,
                MazeCellSize,
                MazeWallHeight,
                wallMaterials,
                floorMaterial,
                transform,
                levelObjects,
                7300 + level * 97);
            playerStartCell = mazeGenerator.GetStartCell();
            openCellSet = new HashSet<Vector2Int>(mazeGenerator.OpenCells);
        }

        private Material[] GetCachedWallMaterials(int level)
        {
            if (cachedWallMaterials == null)
                return CreateWallMaterials(level);
            int textureIndex = (level - 1) % 5;
            return new[] { cachedWallMaterials[textureIndex] };
        }

        private void SpawnDecorations(int level)
        {
            bool bossStage = HasBossEnemy(level);
            int count = 4 + level;
            for (int i = 0; i < count; i++)
            {
                GameObject deco = new GameObject("Candle");
                SpriteRenderer renderer = deco.AddComponent<SpriteRenderer>();
                bool staticCandelabra = i % 2 == 0;
                string animatedLight = bossStage ? "red_light" : "green_light";
                renderer.sprite = LoadSprite(staticCandelabra ? "sprites/static_sprites/candlebra" : $"sprites/animated_sprites/{animatedLight}/0");
                renderer.sortingOrder = 1;
                if (!staticCandelabra)
                {
                    SpriteAnimator animator = deco.AddComponent<SpriteAnimator>();
                    animator.frameRate = 0.12f;
                    animator.PlayAnimation($"sprites/animated_sprites/{animatedLight}");
                }

                Vector2Int cell = PickOpenCell(i + 3, 3);
                AttachSpriteToNearestWall(deco.transform, renderer, cell, staticCandelabra ? 1.9f : 1.75f, staticCandelabra ? 1.15f : 2.1f);
                levelObjects.Add(deco);
            }
        }

        private void SpawnTightSpots(int level)
        {
            Material[] wallMats = CreateWallMaterials(level);
            Material material = wallMats.Length > 0 ? wallMats[0] : CreateSolidMaterial(new Color(0.25f, 0.22f, 0.2f));
            int count = Mathf.Max(2, Mathf.RoundToInt(mazeGenerator.OpenCells.Count * (0.1f + level * 0.006f)));
            for (int i = 0; i < count; i++)
            {
                Vector2Int cell = PickOpenCell(20 + i * 5, 8);
                if (CountOpenNeighbors(cell) < 2)
                    continue;

                GameObject blocker = GameObject.CreatePrimitive(PrimitiveType.Cube);
                blocker.name = "TightSpot";
                Vector3 normal = GetNearestWallNormal(cell);
                blocker.transform.position = mazeGenerator.CellToWorld(cell, MazeCellSize, MazeWallHeight * 0.5f) + normal * (MazeCellSize * 0.34f);
                float width = MazeCellSize * 0.34f;
                blocker.transform.localScale = new Vector3(width, MazeWallHeight, MazeCellSize * 0.46f);
                if (Mathf.Abs(normal.z) > Mathf.Abs(normal.x))
                    blocker.transform.localScale = new Vector3(MazeCellSize * 0.46f, MazeWallHeight, width);
                blocker.GetComponent<Renderer>().sharedMaterial = material;
                levelObjects.Add(blocker);
            }
        }

        private void SpawnEnemies(int level)
        {
            string[] enemyTypes = { "soldier", "caco_demon", "cyber_demon" };
            int count = Mathf.Clamp(4 + level * 3, 5, 30);
            if (BalmungDoomMode) count += 2;
            liveEnemies = count;
            HasCyberDemonInCurrentLevel = false;

            for (int i = 0; i < count; i++)
            {
                int typeIndex;
                if (level <= 2)
                    typeIndex = 0; // Soldiers only (levels 1-2)
                else if (level == 3)
                    typeIndex = i % 3 == 0 ? 1 : 0; // Mostly soldiers, some caco_demons
                else if (level <= 6)
                    typeIndex = i % 3 == 0 ? 1 : 0; // Mix soldiers + caco_demons
                else
                {
                    // Levels 7-10: mix all three types
                    if (i % 5 == 0)
                        typeIndex = 2; // Cyber Demon
                    else if (i % 3 == 0)
                        typeIndex = 1; // Caco Demon
                    else
                        typeIndex = 0; // Soldier
                }

                string type = enemyTypes[typeIndex];
                HasCyberDemonInCurrentLevel |= type == "cyber_demon";
                GameObject enemy = new GameObject(type);
                Vector2Int cell = PickOpenCell(i + 8, 8 + level);
                enemy.transform.position = mazeGenerator.CellToWorld(cell, MazeCellSize, 0.9f);
                SpriteRenderer renderer = enemy.AddComponent<SpriteRenderer>();
                renderer.sprite = LoadSprite($"sprites/npc/{type}/idle/0") ?? LoadSprite($"sprites/npc/{type}/walk/0") ?? LoadSprite($"sprites/npc/{type}/0");
                renderer.sortingOrder = 2;
                enemy.AddComponent<Billboard>();
                EnemyController controller = enemy.AddComponent<EnemyController>();
                controller.Initialize(type, GetEnemyHealth(type, level, i), 4 + level, 0.75f + level * 0.08f);
                levelObjects.Add(enemy);
            }
        }

        private int GetEnemyHealth(string type, int level, int index)
        {
            int baseHp = type switch
            {
                "soldier" => 20,
                "caco_demon" => 50,
                "cyber_demon" => 80,
                _ => 20
            };
            return BalmungDoomMode ? baseHp * 2 : baseHp;
        }

        private bool HasBossEnemy(int level)
        {
            return level >= 5;
        }

        public void RegisterEnemyDeath()
        {
            liveEnemies = Mathf.Max(0, liveEnemies - 1);
            if (liveEnemies <= 0)
                StartCoroutine(AutoCompleteLevelAfterDelay());
        }

        private System.Collections.IEnumerator AutoCompleteLevelAfterDelay()
        {
            yield return new WaitForSeconds(2f);
            GameManager.Instance?.CompleteLevel();
        }

        public void DebugSpawnEnemy(string type)
        {
            if (player == null || mazeGenerator == null) return;

            Vector3 playerPos = player.transform.position;
            Vector3 forward = player.transform.forward;
            Vector3 spawnPos = playerPos + forward * 4f;
            spawnPos.y = 0.9f;

            int level = GameManager.Instance != null ? GameManager.Instance.GetCurrentLevel() : 1;
            GameObject enemy = new GameObject(type);
            enemy.transform.position = spawnPos;
            SpriteRenderer renderer = enemy.AddComponent<SpriteRenderer>();
            renderer.sprite = LoadSprite($"sprites/npc/{type}/idle/0") ?? LoadSprite($"sprites/npc/{type}/walk/0") ?? LoadSprite($"sprites/npc/{type}/0");
            renderer.sortingOrder = 2;
            enemy.AddComponent<Billboard>();
            EnemyController controller = enemy.AddComponent<EnemyController>();
            controller.Initialize(type, GetEnemyHealth(type, level, 0), 4 + level, 0.75f + level * 0.08f);
            levelObjects.Add(enemy);
            liveEnemies++;
        }

        public bool AreAllEnemiesDead() => liveEnemies <= 0;
        public int GetLiveEnemyCount() => liveEnemies;
        public Vector2Int GetMazeDimensions() => mazeDimensions;
        public Vector2Int GetExitCell() => exitCell;
        public IReadOnlyList<Vector2Int> GetOpenCells() => mazeGenerator != null ? mazeGenerator.OpenCells : System.Array.Empty<Vector2Int>();
        public Vector2Int GetPlayerCell() => player != null ? WorldToCell(player.transform.position) : playerStartCell;

        public void ClearLevel()
        {
            foreach (GameObject obj in levelObjects)
            {
                if (obj != null)
                    Destroy(obj);
            }

            levelObjects.Clear();
            blockedCells.Clear();
            liveEnemies = 0;
            HasCyberDemonInCurrentLevel = false;
        }

        public bool TryFindPath(Vector3 fromWorld, Vector3 toWorld, List<Vector3> worldPath)
        {
            worldPath.Clear();
            Vector2Int start = WorldToCell(fromWorld);
            Vector2Int goal = WorldToCell(toWorld);

            if (!IsWalkable(start) || !IsWalkable(goal))
                return false;

            Queue<Vector2Int> frontier = new();
            Dictionary<Vector2Int, Vector2Int> cameFrom = new();
            frontier.Enqueue(start);
            cameFrom[start] = start;

            Vector2Int[] directions =
            {
                Vector2Int.up,
                Vector2Int.right,
                Vector2Int.down,
                Vector2Int.left
            };

            while (frontier.Count > 0)
            {
                Vector2Int current = frontier.Dequeue();
                if (current == goal)
                    break;

                foreach (Vector2Int direction in directions)
                {
                    Vector2Int next = current + direction;
                    if (!IsWalkable(next) || cameFrom.ContainsKey(next))
                        continue;

                    frontier.Enqueue(next);
                    cameFrom[next] = current;
                }
            }

            if (!cameFrom.ContainsKey(goal))
                return false;

            List<Vector2Int> cellPath = new();
            Vector2Int pathCell = goal;
            while (pathCell != start)
            {
                cellPath.Add(pathCell);
                pathCell = cameFrom[pathCell];
            }

            cellPath.Reverse();
            foreach (Vector2Int cell in cellPath)
                worldPath.Add(mazeGenerator.CellToWorld(cell, MazeCellSize, fromWorld.y));

            return worldPath.Count > 0;
        }

        public Vector2Int WorldToCell(Vector3 worldPosition)
        {
            return new Vector2Int(
                Mathf.RoundToInt(worldPosition.x / MazeCellSize),
                Mathf.RoundToInt(worldPosition.z / MazeCellSize));
        }

        public bool IsWalkable(Vector2Int cell)
        {
            return IsOpenCell(cell) && !blockedCells.Contains(cell);
        }

        private Vector2Int PickOpenCell(int offset, int minimumDistanceFromPlayer)
        {
            if (mazeGenerator == null || mazeGenerator.OpenCells.Count == 0)
                return new Vector2Int(1, 1);

            int count = mazeGenerator.OpenCells.Count;
            for (int i = 0; i < count; i++)
            {
                Vector2Int cell = mazeGenerator.OpenCells[(offset * 11 + i * 7) % count];
                int distance = Mathf.Abs(cell.x - playerStartCell.x) + Mathf.Abs(cell.y - playerStartCell.y);
                if (distance >= minimumDistanceFromPlayer && IsWalkable(cell))
                    return cell;
            }

            return mazeGenerator.OpenCells[Mathf.Abs(offset) % count];
        }

        private void AttachSpriteToNearestWall(Transform target, SpriteRenderer renderer, Vector2Int cell, float targetHeight, float wallOffsetHeight)
        {
            Vector2Int[] directions =
            {
                Vector2Int.up,
                Vector2Int.right,
                Vector2Int.down,
                Vector2Int.left
            };

            Vector3 basePosition = mazeGenerator.CellToWorld(cell, MazeCellSize, wallOffsetHeight);
            Vector3 normal = GetNearestWallNormal(cell);
            target.position = basePosition + normal * (MazeCellSize * 0.43f);
            target.forward = -normal;

            if (renderer != null && renderer.sprite != null)
            {
                float spriteHeight = Mathf.Max(0.01f, renderer.sprite.bounds.size.y);
                float scale = targetHeight / spriteHeight;
                target.localScale = new Vector3(scale, scale, scale);
            }
        }

        public bool IsOpenCell(Vector2Int cell)
        {
            if (openCellSet != null)
                return openCellSet.Contains(cell);

            foreach (Vector2Int openCell in mazeGenerator.OpenCells)
            {
                if (openCell == cell)
                    return true;
            }

            return false;
        }

        private Vector3 GetNearestWallNormal(Vector2Int cell)
        {
            Vector2Int[] directions =
            {
                Vector2Int.up,
                Vector2Int.right,
                Vector2Int.down,
                Vector2Int.left
            };

            foreach (Vector2Int direction in directions)
            {
                if (!IsOpenCell(cell + direction))
                    return new Vector3(direction.x, 0f, direction.y).normalized;
            }

            return Vector3.forward;
        }

        private int CountOpenNeighbors(Vector2Int cell)
        {
            int count = 0;
            if (IsOpenCell(cell + Vector2Int.up)) count++;
            if (IsOpenCell(cell + Vector2Int.right)) count++;
            if (IsOpenCell(cell + Vector2Int.down)) count++;
            if (IsOpenCell(cell + Vector2Int.left)) count++;
            return count;
        }

        private Material[] CreateWallMaterials(int level)
        {
            Material[] materials = new Material[1];
            int textureIndex = ((level - 1) % 5) + 1;
            for (int i = 0; i < materials.Length; i++)
            {
                Texture2D texture = Resources.Load<Texture2D>($"textures/{textureIndex}");
                materials[i] = new Material(GetWorldShader());
                if (texture != null)
                {
                    texture.wrapMode = TextureWrapMode.Repeat;
                    texture.filterMode = FilterMode.Point;
                    materials[i].mainTexture = texture;
                    materials[i].mainTextureScale = Vector2.one;
                    materials[i].SetTexture("_BaseMap", texture);
                    materials[i].SetColor("_BaseColor", Color.white);
                }
                else
                {
                    Color c = Color.HSVToRGB(i / 5f, 0.45f, 0.65f);
                    materials[i].color = c;
                    materials[i].SetColor("_BaseColor", c);
                }
            }

            return materials;
        }

        private Material CreateSolidMaterial(Color color)
        {
            Material material = new Material(GetWorldShader());
            material.color = color;
            material.SetColor("_BaseColor", color);
            return material;
        }

        private Shader GetWorldShader()
        {
            Material refMat = Resources.Load<Material>("materials/URPLitBase");
            if (refMat != null)
                return refMat.shader;

            return Shader.Find("Universal Render Pipeline/Lit")
                ?? Shader.Find("Universal Render Pipeline/Simple Lit")
                ?? Shader.Find("Unlit/Texture")
                ?? Shader.Find("Standard")
                ?? Shader.Find("Sprites/Default")
                ?? Shader.Find("Hidden/InternalErrorShader");
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
                return null;

            texture.filterMode = FilterMode.Point;
            texture.wrapMode = TextureWrapMode.Clamp;
            return Sprite.Create(texture, new Rect(0f, 0f, texture.width, texture.height), new Vector2(0.5f, 0.5f), 100f);
        }
    }

    public class Billboard : MonoBehaviour
    {
        private void LateUpdate()
        {
            Camera camera = Camera.main;
            if (camera != null)
                transform.forward = camera.transform.forward;
        }
    }

}
