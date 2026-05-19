using UnityEngine;
using BalmungDoom.Core;

namespace BalmungDoom.Core
{
    public class PhysicsManager : MonoBehaviour
    {
        public static PhysicsManager Instance { get; private set; }

        private void Awake()
        {
            if (Instance == null)
            {
                Instance = this;
                DontDestroyOnLoad(gameObject);
                NativePlugin.InitPhysics();
            }
            else
            {
                Destroy(gameObject);
            }
        }

        private void Update()
        {
            NativePlugin.UpdatePhysics(Time.deltaTime);
        }

        private void OnApplicationQuit()
        {
            NativePlugin.ShutdownPhysics();
        }

        public void CreateWall(Vector3 pos, Vector3 size)
        {
            NativePlugin.AddWall(pos.x, pos.y, pos.z, size.x, size.y, size.z);
        }

        public NativePlugin.RaycastResult Raycast(Vector3 origin, Vector3 direction)
        {
            return NativePlugin.CastRay(origin.x, origin.y, origin.z, direction.x, direction.y, direction.z);
        }
    }
}
