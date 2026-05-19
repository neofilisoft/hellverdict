using UnityEngine;
using System.Collections;
using BalmungDoom.Core;
using BalmungDoom.Enemy;
using UnityEngine.UI;

namespace BalmungDoom.Gameplay
{
    public class WeaponSystem : MonoBehaviour
    {
        [SerializeField] private int ammoPerMagazine = 30;
        [SerializeField] private int maxAmmo = 120;
        [SerializeField] private float fireRate = 0.5f;
        [SerializeField] private int damagePerShot = 10;
        [SerializeField] private float range = 12f;
        [SerializeField] private LayerMask hitMask = ~0;

        private int currentAmmo;
        private int reserveAmmo;
        private float lastFireTime;
        private bool isReloading = false;
        private Animator animator;
        private Camera mainCamera;
        private Image weaponImage;
        private Sprite idleFrame;
        private Sprite[] fireFrames;
        private Sprite[] reloadFrames;
        private Coroutine animationCoroutine;

        private void Start()
        {
            currentAmmo = ammoPerMagazine;
            reserveAmmo = maxAmmo - ammoPerMagazine;
            animator = GetComponentInChildren<Animator>();
            mainCamera = Camera.main;
            weaponImage = GameObject.Find("ShotgunOverlay")?.GetComponent<Image>();
            idleFrame = LoadSprite("sprites/weapon/shotgun/0");
            fireFrames = new[] { LoadSprite("sprites/weapon/shotgun/2"), LoadSprite("sprites/weapon/shotgun/3"), idleFrame };
            reloadFrames = new[] { LoadSprite("sprites/weapon/shotgun/4"), LoadSprite("sprites/weapon/shotgun/5"), LoadSprite("sprites/weapon/shotgun/6"), idleFrame };
            SetWeaponFrame(idleFrame);
        }

        public void Fire()
        {
            if (isReloading) return;
            if (Time.time - lastFireTime < fireRate) return;

            lastFireTime = Time.time;

            AudioManager.Instance.PlayShotgunSound();
            PlayFireAnimation();
            PerformRaycast();
            FindAnyObjectByType<UIManager>()?.UpdateHUD(FindAnyObjectByType<PlayerController>()?.GetHealth() ?? 100, 999, GameManager.Instance.GetCurrentLevel());
        }

        private void PerformRaycast()
        {
            if (mainCamera == null)
                mainCamera = Camera.main;

            if (mainCamera == null)
                return;

            Ray ray = mainCamera.ViewportPointToRay(new Vector3(0.5f, 0.5f, 0f));

            if (Physics.Raycast(ray, out RaycastHit hit, range, hitMask, QueryTriggerInteraction.Ignore))
            {
                IDamageable damageable = hit.collider.GetComponentInParent<IDamageable>();
                damageable?.TakeDamage(damagePerShot);
            }
        }

        public void Reload()
        {
            if (isReloading || currentAmmo == ammoPerMagazine || reserveAmmo <= 0)
                return;

            StartCoroutine(ReloadCoroutine());
        }

        private IEnumerator ReloadCoroutine()
        {
            isReloading = true;
            PlayReloadAnimation();
            yield return new WaitForSeconds(0.75f);
            
            int ammoToReload = Mathf.Min(ammoPerMagazine - currentAmmo, reserveAmmo);
            currentAmmo += ammoToReload;
            reserveAmmo -= ammoToReload;
            isReloading = false;
        }

        private void PlayFireAnimation()
        {
            if (animator != null)
                animator.SetTrigger("Fire");

            PlayFrames(fireFrames, 0.17f);
        }

        private void PlayReloadAnimation()
        {
            if (animator != null)
                animator.SetTrigger("Reload");

            PlayFrames(reloadFrames, 0.16f);
        }

        private void PlayFrames(Sprite[] frames, float frameTime)
        {
            if (weaponImage == null || frames == null)
                return;

            if (animationCoroutine != null)
                StopCoroutine(animationCoroutine);

            animationCoroutine = StartCoroutine(AnimateFrames(frames, frameTime));
        }

        private IEnumerator AnimateFrames(Sprite[] frames, float frameTime)
        {
            foreach (Sprite frame in frames)
            {
                SetWeaponFrame(frame);
                yield return new WaitForSeconds(frameTime);
            }

            SetWeaponFrame(idleFrame);
            animationCoroutine = null;
        }

        private void SetWeaponFrame(Sprite frame)
        {
            if (weaponImage == null)
                weaponImage = GameObject.Find("ShotgunOverlay")?.GetComponent<Image>();

            if (weaponImage != null && frame != null)
                weaponImage.sprite = frame;
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

        public int GetCurrentAmmo() => 999;
        public int GetReserveAmmo() => reserveAmmo;
        public bool IsReloading() => isReloading;
    }
}
