using UnityEngine;
using System.Collections.Generic;
using System.Linq;

public class SpriteAnimator : MonoBehaviour
{
    public string spritePath; // e.g., "npc/caco_demon/idle"
    public float frameRate = 0.15f;
    private SpriteRenderer spriteRenderer;
    private List<Sprite> frames = new List<Sprite>();
    private int currentFrame = 0;
    private float timer = 0f;

    void Awake()
    {
        spriteRenderer = GetComponent<SpriteRenderer>();
        LoadFrames();
    }

    void LoadFrames()
    {
        if (string.IsNullOrWhiteSpace(spritePath))
            return;

        // Load all images from the folder (0.png, 1.png, etc.)
        // This is a simplified version; in production, we use a Resource index
        Sprite[] files = Resources.LoadAll<Sprite>(spritePath).OrderBy(s => s.name).ToArray();
        foreach (Sprite sprite in files)
        {
            if (sprite.texture != null)
                sprite.texture.filterMode = FilterMode.Point;
        }
        frames.AddRange(files);
    }

    void Update()
    {
        if (frames.Count == 0) return;

        timer += Time.deltaTime;
        if (timer >= frameRate)
        {
            currentFrame = (currentFrame + 1) % frames.Count;
            spriteRenderer.sprite = frames[currentFrame];
            timer = 0;
        }
    }

    public void PlayAnimation(string newPath)
    {
        if (spritePath == newPath) return;
        spritePath = newPath;
        frames.Clear();
        LoadFrames();
        currentFrame = 0;
        if (frames.Count > 0)
            spriteRenderer.sprite = frames[0];
    }
}
