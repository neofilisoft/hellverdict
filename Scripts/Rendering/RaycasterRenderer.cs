using UnityEngine;

namespace BalmungDoom.Rendering
{
    public class RaycasterRenderer : MonoBehaviour
    {
        [SerializeField] private Texture2D[] wallTextures = new Texture2D[5];
        [SerializeField] private Camera renderCamera;
        [SerializeField] private int rayCount = 320;
        [SerializeField] private float fov = 60f;

        private RenderTexture renderTexture;
        private Material raycasterMaterial;

        private void Start()
        {
            InitializeRaycaster();
        }

        private void InitializeRaycaster()
        {
            int width = Screen.width;
            int height = Screen.height;

            renderTexture = new RenderTexture(width, height, 24);
            renderTexture.filterMode = FilterMode.Point;

            raycasterMaterial = new Material(Shader.Find("Custom/Raycaster"));
            raycasterMaterial.SetInteger("_RayCount", rayCount);
            raycasterMaterial.SetFloat("_FOV", fov);

            for (int i = 0; i < wallTextures.Length && i < 5; i++)
            {
                if (wallTextures[i] != null)
                    raycasterMaterial.SetTexture($"_WallTexture{i + 1}", wallTextures[i]);
            }
        }

        private void OnRenderImage(RenderTexture source, RenderTexture destination)
        {
            if (raycasterMaterial == null)
                return;

            Camera cam = GetComponent<Camera>();
            raycasterMaterial.SetMatrix("_CameraMatrix", cam.cameraToWorldMatrix);

            Graphics.Blit(source, destination, raycasterMaterial);
        }

        public void SetWallTexture(int index, Texture2D texture)
        {
            if (index >= 0 && index < wallTextures.Length)
            {
                wallTextures[index] = texture;
                if (raycasterMaterial != null)
                    raycasterMaterial.SetTexture($"_WallTexture{index + 1}", texture);
            }
        }
    }
}
