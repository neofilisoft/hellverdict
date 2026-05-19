using UnityEditor;

namespace BalmungDoom.Editor
{
    public sealed class PixelArtImportSettings : AssetPostprocessor
    {
        private void OnPreprocessTexture()
        {
            if (!IsPixelArtAsset(assetPath))
                return;

            TextureImporter importer = (TextureImporter)assetImporter;
            Apply(importer);
        }

        [MenuItem("Hell Verdict/Fix Pixel Art Import Settings")]
        public static void FixAllPixelArt()
        {
            string[] guids = AssetDatabase.FindAssets("t:Texture2D", new[] { "Assets/resources/sprites", "Assets/resources/textures", "Assets/resources/ui", "Assets/ui" });
            foreach (string guid in guids)
            {
                string path = AssetDatabase.GUIDToAssetPath(guid);
                TextureImporter importer = AssetImporter.GetAtPath(path) as TextureImporter;
                if (importer == null)
                    continue;

                Apply(importer);
                importer.SaveAndReimport();
            }
        }

        private static bool IsPixelArtAsset(string path)
        {
            string normalized = path.Replace('\\', '/').ToLowerInvariant();
            return normalized.StartsWith("assets/resources/sprites/")
                || normalized.StartsWith("assets/resources/textures/")
                || normalized.StartsWith("assets/resources/ui/")
                || normalized.StartsWith("assets/ui/");
        }

        private static void Apply(TextureImporter importer)
        {
            string normalized = importer.assetPath.Replace('\\', '/').ToLowerInvariant();
            importer.textureType = normalized.Contains("/sprites/") || normalized.Contains("/ui/")
                ? TextureImporterType.Sprite
                : TextureImporterType.Default;
            importer.spritePixelsPerUnit = 100f;
            importer.filterMode = UnityEngine.FilterMode.Point;
            importer.textureCompression = TextureImporterCompression.Uncompressed;
            importer.mipmapEnabled = false;
            importer.alphaIsTransparency = true;
            importer.wrapMode = UnityEngine.TextureWrapMode.Clamp;
        }
    }
}
