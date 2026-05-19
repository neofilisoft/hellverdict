using UnityEngine;
using System.Collections.Generic;
using System.IO;
using BalmungDoom.Core;

public class LevelLoader : MonoBehaviour
{
    [Header("Map Settings")]
    public string mapFileName = "map_01.txt";
    public float cellSize = 4.0f;
    public float wallHeight = 4.0f;

    [Header("Asset References")]
    public Texture2D[] wallTextures; // 1.png to 5.png

    private void Start()
    {
        LoadMap();
    }

    void LoadMap()
    {
        string path = Path.Combine(Application.streamingAssetsPath, mapFileName);
        if (!File.Exists(path))
        {
            Debug.LogError($"Map file not found at {path}. Please ensure it exists in StreamingAssets.");
            return;
        }

        string[] lines = File.ReadAllLines(path);

        for (int z = 0; z < lines.Length; z++)
        {
            string line = lines[z];
            for (int x = 0; x < line.Length; x++)
            {
                char cell = line[x];
                Vector3 position = new Vector3(x * cellSize, wallHeight / 2f, z * cellSize);

                switch (cell)
                {
                    case '#': // Standard Wall (Texture 0)
                        CreateWall(position, 0);
                        break;
                    case '1': case '2': case '3': case '4': case '5': // Textured Walls
                        int texIndex = (cell == '#') ? 0 : (cell - '1');
                        CreateWall(position, texIndex);
                        break;
                    case 'E': // Enemy Spawn
                        SpawnEnemy("soldier", position);
                        break;
                    case 'P': // Player Start
                        SetPlayerStart(position);
                        break;
                }
            }
        }
    }

    void CreateWall(Vector3 pos, int textureIndex)
    {
        // Add to AsterCore Physics for collision
        PhysicsManager.Instance.CreateWall(pos, new Vector3(cellSize, wallHeight, cellSize));

        // For the renderer, we'd typically store this in a spatial hash
        // But for now, we register the wall type for the Raycaster
        MapData.RegisterWall(pos, cellSize, wallHeight, textureIndex);
    }

    void SpawnEnemy(string type, Vector3 pos)
    {
        // Implementation for EnemySpawner
        Debug.Log($"Spawning {type} at {pos}");
    }

    void SetPlayerStart(Vector3 pos)
    {
        GameObject player = GameObject.FindGameObjectWithTag("Player");
        if (player != null) player.transform.position = pos;
    }
}

public static class MapData
{
    public struct WallInfo
    {
        public Vector3 Position;
        public Vector3 Size;
        public int TextureIndex;
    }

    public static List<WallInfo> Walls = new List<WallInfo>();

    public static void RegisterWall(Vector3 pos, float size, float height, int texIndex)
    {
        Walls.Add(new WallInfo { Position = pos, Size = new Vector3(size, height, size), TextureIndex = texIndex });
    }
}
