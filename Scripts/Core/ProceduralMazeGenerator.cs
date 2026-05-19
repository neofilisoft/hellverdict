using System.Collections.Generic;
using UnityEngine;

namespace BalmungDoom.Core
{
    public class ProceduralMazeGenerator : MonoBehaviour
    {
        [Header("Optional Prefabs")]
        [SerializeField] private GameObject wallPrefab = null;
        [SerializeField] private GameObject floorPrefab = null;

        private static readonly Vector2Int[] Directions =
        {
            new(0, 2),
            new(2, 0),
            new(0, -2),
            new(-2, 0)
        };

        public IReadOnlyList<Vector2Int> OpenCells => openCells;

        private readonly List<Vector2Int> openCells = new();
        private readonly List<Vector2Int> deadEnds = new();

        public Vector2Int Generate(
            int requestedWidth,
            int requestedDepth,
            float cellSize,
            float wallHeight,
            Material[] wallMaterials,
            Material floorMaterial,
            Transform parent,
            ICollection<GameObject> generatedObjects,
            int seed)
        {
            int width = MakeOdd(Mathf.Max(7, requestedWidth));
            int depth = MakeOdd(Mathf.Max(7, requestedDepth));

            bool[,] walls = new bool[width, depth];
            for (int x = 0; x < width; x++)
            {
                for (int z = 0; z < depth; z++)
                    walls[x, z] = true;
            }

            Random.State previousState = Random.state;
            Random.InitState(seed);
            CarveMaze(walls, width, depth, new Vector2Int(1, 1));
            OpenExtraPaths(walls, width, depth);
            CreateRooms(walls, width, depth);
            Random.state = previousState;

            openCells.Clear();
            deadEnds.Clear();

            CreateFloor(width, depth, cellSize, floorMaterial, parent, generatedObjects);
            Material ceilingMaterial = wallMaterials.Length > 0 ? wallMaterials[0] : floorMaterial;
            CreateCeiling(width, depth, cellSize, wallHeight, ceilingMaterial, parent, generatedObjects);

            for (int x = 0; x < width; x++)
            {
                for (int z = 0; z < depth; z++)
                {
                    Vector2Int cell = new(x, z);
                    if (walls[x, z])
                    {
                        Material material = wallMaterials.Length > 0 ? wallMaterials[0] : null;
                        CreateWall(cell, cellSize, wallHeight, material, parent, generatedObjects);
                    }
                    else
                    {
                        openCells.Add(cell);
                        if (CountOpenNeighbors(walls, width, depth, cell) == 1)
                            deadEnds.Add(cell);
                    }
                }
            }

            return new Vector2Int(width, depth);
        }

        public Vector2Int GetStartCell()
        {
            return openCells.Count > 0 ? openCells[0] : new Vector2Int(1, 1);
        }

        public Vector2Int GetFarthestDeadEnd(Vector2Int fromCell)
        {
            IReadOnlyList<Vector2Int> candidates = deadEnds.Count > 0 ? deadEnds : openCells;
            Vector2Int best = fromCell;
            int bestDistance = -1;

            foreach (Vector2Int candidate in candidates)
            {
                int distance = Mathf.Abs(candidate.x - fromCell.x) + Mathf.Abs(candidate.y - fromCell.y);
                if (distance > bestDistance)
                {
                    best = candidate;
                    bestDistance = distance;
                }
            }

            return best;
        }

        public Vector3 CellToWorld(Vector2Int cell, float cellSize, float y)
        {
            return new Vector3(cell.x * cellSize, y, cell.y * cellSize);
        }

        private void CarveMaze(bool[,] walls, int width, int depth, Vector2Int start)
        {
            Stack<Vector2Int> stack = new();
            walls[start.x, start.y] = false;
            stack.Push(start);

            while (stack.Count > 0)
            {
                Vector2Int current = stack.Peek();
                List<Vector2Int> availableDirections = GetAvailableDirections(walls, width, depth, current);

                if (availableDirections.Count == 0)
                {
                    stack.Pop();
                    continue;
                }

                Vector2Int direction = availableDirections[Random.Range(0, availableDirections.Count)];
                Vector2Int next = current + direction;
                Vector2Int between = current + direction / 2;

                walls[between.x, between.y] = false;
                walls[next.x, next.y] = false;
                stack.Push(next);
            }
        }

        private List<Vector2Int> GetAvailableDirections(bool[,] walls, int width, int depth, Vector2Int current)
        {
            List<Vector2Int> available = new();
            foreach (Vector2Int direction in Directions)
            {
                Vector2Int next = current + direction;
                if (next.x <= 0 || next.x >= width - 1 || next.y <= 0 || next.y >= depth - 1)
                    continue;

                if (walls[next.x, next.y])
                    available.Add(direction);
            }

            return available;
        }

        private int CountOpenNeighbors(bool[,] walls, int width, int depth, Vector2Int cell)
        {
            int count = 0;
            Vector2Int[] oneStepDirections =
            {
                Vector2Int.up,
                Vector2Int.right,
                Vector2Int.down,
                Vector2Int.left
            };

            foreach (Vector2Int direction in oneStepDirections)
            {
                Vector2Int next = cell + direction;
                if (next.x >= 0 && next.x < width && next.y >= 0 && next.y < depth && !walls[next.x, next.y])
                    count++;
            }

            return count;
        }

        private void CreateFloor(int width, int depth, float cellSize, Material sourceMaterial, Transform parent, ICollection<GameObject> generatedObjects)
        {
            GameObject sourcePrefab = floorPrefab != null ? floorPrefab : Resources.Load<GameObject>("prefabs/MazeFloor");
            GameObject floor = sourcePrefab != null ? Instantiate(sourcePrefab) : GameObject.CreatePrimitive(PrimitiveType.Cube);
            floor.name = "MazeFloor";
            floor.transform.SetParent(parent, false);
            floor.transform.position = new Vector3((width - 1) * cellSize * 0.5f, -0.05f, (depth - 1) * cellSize * 0.5f);
            floor.transform.localScale = new Vector3(width * cellSize, 0.1f, depth * cellSize);
            ApplyMaterial(floor, sourceMaterial, new Vector2(width * cellSize, depth * cellSize));
            EnsureCollider(floor);
            generatedObjects?.Add(floor);
        }

        private void CreateCeiling(int width, int depth, float cellSize, float wallHeight, Material sourceMaterial, Transform parent, ICollection<GameObject> generatedObjects)
        {
            GameObject ceiling = GameObject.CreatePrimitive(PrimitiveType.Cube);
            ceiling.name = "MazeCeiling";
            ceiling.transform.SetParent(parent, false);
            ceiling.transform.position = new Vector3((width - 1) * cellSize * 0.5f, wallHeight + 0.05f, (depth - 1) * cellSize * 0.5f);
            ceiling.transform.localScale = new Vector3(width * cellSize, 0.1f, depth * cellSize);
            ApplyMaterial(ceiling, sourceMaterial, new Vector2(width * cellSize, depth * cellSize));
            generatedObjects?.Add(ceiling);
        }

        private void CreateWall(Vector2Int cell, float cellSize, float wallHeight, Material sourceMaterial, Transform parent, ICollection<GameObject> generatedObjects)
        {
            GameObject sourcePrefab = wallPrefab != null ? wallPrefab : Resources.Load<GameObject>("prefabs/MazeWall");
            GameObject wall = sourcePrefab != null ? Instantiate(sourcePrefab) : CreateWallBlock();
            wall.name = "MazeWall";
            wall.transform.SetParent(parent, false);
            wall.transform.position = new Vector3(cell.x * cellSize, wallHeight * 0.5f, cell.y * cellSize);
            wall.transform.localScale = new Vector3(cellSize, wallHeight, cellSize);
            ApplyMaterial(wall, sourceMaterial, new Vector2(cellSize, wallHeight));
            EnsureCollider(wall);
            generatedObjects?.Add(wall);
        }

        private void ApplyMaterial(GameObject target, Material sourceMaterial, Vector2 textureScale)
        {
            Renderer renderer = target.GetComponentInChildren<Renderer>();
            if (renderer != null)
                renderer.sharedMaterial = CreateTiledMaterial(sourceMaterial, textureScale);
        }

        private void EnsureCollider(GameObject target)
        {
            if (target.GetComponentInChildren<Collider>() == null)
                target.AddComponent<BoxCollider>();
        }

        private Material CreateTiledMaterial(Material sourceMaterial, Vector2 textureScale)
        {
            Material material = sourceMaterial != null ? new Material(sourceMaterial) : new Material(GetWorldShader());
            if (material.mainTexture != null)
            {
                material.mainTexture.wrapMode = TextureWrapMode.Repeat;
                material.mainTextureScale = textureScale;
            }

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

        private GameObject CreateWallBlock()
        {
            GameObject wall = new("MazeWall");
            MeshFilter meshFilter = wall.AddComponent<MeshFilter>();
            wall.AddComponent<MeshRenderer>();
            wall.AddComponent<BoxCollider>();
            meshFilter.sharedMesh = CreateTiledCubeMesh();
            return wall;
        }

        private Mesh CreateTiledCubeMesh()
        {
            Vector3[] vertices =
            {
                new(-0.5f, -0.5f, -0.5f), new(0.5f, -0.5f, -0.5f), new(0.5f, 0.5f, -0.5f), new(-0.5f, 0.5f, -0.5f),
                new(0.5f, -0.5f, 0.5f), new(-0.5f, -0.5f, 0.5f), new(-0.5f, 0.5f, 0.5f), new(0.5f, 0.5f, 0.5f),
                new(-0.5f, -0.5f, 0.5f), new(-0.5f, -0.5f, -0.5f), new(-0.5f, 0.5f, -0.5f), new(-0.5f, 0.5f, 0.5f),
                new(0.5f, -0.5f, -0.5f), new(0.5f, -0.5f, 0.5f), new(0.5f, 0.5f, 0.5f), new(0.5f, 0.5f, -0.5f),
                new(-0.5f, 0.5f, -0.5f), new(0.5f, 0.5f, -0.5f), new(0.5f, 0.5f, 0.5f), new(-0.5f, 0.5f, 0.5f),
                new(-0.5f, -0.5f, 0.5f), new(0.5f, -0.5f, 0.5f), new(0.5f, -0.5f, -0.5f), new(-0.5f, -0.5f, -0.5f)
            };

            Vector2[] uvs =
            {
                new(0f, 0f), new(1f, 0f), new(1f, 1f), new(0f, 1f),
                new(0f, 0f), new(1f, 0f), new(1f, 1f), new(0f, 1f),
                new(0f, 0f), new(1f, 0f), new(1f, 1f), new(0f, 1f),
                new(0f, 0f), new(1f, 0f), new(1f, 1f), new(0f, 1f),
                new(0f, 0f), new(1f, 0f), new(1f, 1f), new(0f, 1f),
                new(0f, 0f), new(1f, 0f), new(1f, 1f), new(0f, 1f)
            };

            int[] triangles =
            {
                0, 2, 1, 0, 3, 2,
                4, 6, 5, 4, 7, 6,
                8, 10, 9, 8, 11, 10,
                12, 14, 13, 12, 15, 14,
                16, 18, 17, 16, 19, 18,
                20, 22, 21, 20, 23, 22
            };

            Mesh mesh = new()
            {
                name = "TiledMazeWallMesh",
                vertices = vertices,
                uv = uvs,
                triangles = triangles
            };
            mesh.RecalculateNormals();
            mesh.RecalculateBounds();
            return mesh;
        }

        private void OpenExtraPaths(bool[,] walls, int width, int depth)
        {
            int removals = (width * depth) / 5;
            for (int i = 0; i < removals; i++)
            {
                int x = Random.Range(2, width - 2);
                int z = Random.Range(2, depth - 2);
                if (!walls[x, z])
                    continue;

                int openNeighbors = 0;
                if (!walls[x - 1, z]) openNeighbors++;
                if (!walls[x + 1, z]) openNeighbors++;
                if (!walls[x, z - 1]) openNeighbors++;
                if (!walls[x, z + 1]) openNeighbors++;

                if (openNeighbors >= 2)
                    walls[x, z] = false;
            }
        }

        private void CreateRooms(bool[,] walls, int width, int depth)
        {
            int roomCount = Mathf.Max(2, (width * depth) / 400);
            for (int r = 0; r < roomCount; r++)
            {
                int roomW = Random.Range(3, 6);
                int roomH = Random.Range(3, 6);
                int startX = Random.Range(2, width - roomW - 2);
                int startZ = Random.Range(2, depth - roomH - 2);

                for (int x = startX; x < startX + roomW; x++)
                {
                    for (int z = startZ; z < startZ + roomH; z++)
                        walls[x, z] = false;
                }
            }
        }

        private int MakeOdd(int value)
        {
            return value % 2 == 0 ? value + 1 : value;
        }
    }
}
