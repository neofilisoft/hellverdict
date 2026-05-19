using UnityEngine;
using BalmungDoom.Core;

namespace BalmungDoom.Gameplay
{
    public class PlayerController : MonoBehaviour
    {
        public const string SensitivityPrefsKey = "hellverdict.mouseSensitivityPercent";

        [SerializeField] private float moveSpeed = 7f;
        [SerializeField] private float sprintMultiplier = 1.6f;
        [SerializeField] private float jumpForce = 6f;
        [SerializeField] private float gravity = 18f;
        [SerializeField] private float lookSensitivity = 2f;
        [SerializeField] private int maxHealth = 100;
        [SerializeField] private float floorY = 0.05f;

        private int currentHealth;
        private float rotationX = 0f;
        private float verticalVelocity;
        private Vector3 currentHorizontalVelocity;
        private CharacterController characterController;
        private WeaponSystem weaponSystem;
        private InputManager inputManager;
        private bool dead;

        private int healCharges = 3;
        private int killsSinceLastHealRecharge;

        private void Start()
        {
            currentHealth = maxHealth;
            ApplySensitivityPercent(PlayerPrefs.GetFloat(SensitivityPrefsKey, 100f));
            characterController = GetComponent<CharacterController>();
            weaponSystem = GetComponentInChildren<WeaponSystem>();
            inputManager = InputManager.Instance;

            Cursor.lockState = CursorLockMode.Locked;
            SnapToFloor();
        }

        private void Update()
        {
            if (dead || GameManager.Instance.IsPaused()) return;

            HandleMovement();
            HandleLook();
            HandleWeapon();
            HandleHeal();
        }

        private void HandleMovement()
        {
            Vector2 moveInput = inputManager.MovementInput;
            moveInput = Vector2.ClampMagnitude(moveInput, 1f);

            float currentSpeed = moveSpeed;
            if (inputManager.SprintInput && moveInput.y > 0.1f)
                currentSpeed *= sprintMultiplier;

            Vector3 targetVelocity = (transform.forward * moveInput.y + transform.right * moveInput.x) * currentSpeed;
            targetVelocity.y = 0f;
            currentHorizontalVelocity = Vector3.Lerp(currentHorizontalVelocity, targetVelocity, 18f * Time.deltaTime);

            bool grounded = characterController.isGrounded || transform.position.y <= floorY + 0.05f;

            if (grounded && verticalVelocity < 0f)
                verticalVelocity = -1f;

            if (grounded && inputManager.JumpInput)
                verticalVelocity = jumpForce;

            verticalVelocity -= gravity * Time.deltaTime;

            Vector3 move = currentHorizontalVelocity * Time.deltaTime;
            move.y = verticalVelocity * Time.deltaTime;
            characterController.Move(move);

            if (transform.position.y < floorY)
            {
                Vector3 pos = transform.position;
                pos.y = floorY;
                transform.position = pos;
                verticalVelocity = 0f;
            }
        }

        private void HandleLook()
        {
            Vector2 lookInput = inputManager.LookInput * lookSensitivity;
            rotationX -= lookInput.y;
            rotationX = Mathf.Clamp(rotationX, -90f, 90f);
            
            transform.Rotate(0, lookInput.x, 0);
            Camera.main.transform.localRotation = Quaternion.Euler(rotationX, 0, 0);
        }

        private void HandleWeapon()
        {
            if (inputManager.FireInput)
                weaponSystem.Fire();

            if (inputManager.ReloadInput)
                weaponSystem.Reload();
        }

        public void TakeDamage(int damage)
        {
            if (dead)
                return;

            if (GameManager.Instance != null && GameManager.Instance.GodMode)
                return;

            currentHealth -= damage;
            AudioManager.Instance.PlayPlayerPainSound();
            FindAnyObjectByType<UIManager>()?.UpdateHUD(currentHealth, weaponSystem != null ? weaponSystem.GetCurrentAmmo() : 0, GameManager.Instance.GetCurrentLevel(), healCharges);

            if (currentHealth <= 0)
                Die();
        }

        private void Die()
        {
            dead = true;
            GameManager.Instance.GameOver();
        }

        public void OnEnemyKilled()
        {
            killsSinceLastHealRecharge++;
            if (killsSinceLastHealRecharge >= 5)
            {
                killsSinceLastHealRecharge = 0;
                healCharges++;
                RefreshHUD();
            }
        }

        private void HandleHeal()
        {
            if (!inputManager.HealInput || healCharges <= 0 || currentHealth >= maxHealth) return;

            healCharges--;
            currentHealth = Mathf.Min(currentHealth + maxHealth / 2, maxHealth);
            RefreshHUD();
        }

        private void RefreshHUD()
        {
            FindAnyObjectByType<UIManager>()?.UpdateHUD(currentHealth, weaponSystem != null ? weaponSystem.GetCurrentAmmo() : 0, GameManager.Instance.GetCurrentLevel(), healCharges);
        }

        public int GetHealCharges() => healCharges;

        public void ResetForLevel(Vector3 startPosition)
        {
            currentHealth = maxHealth;
            dead = false;
            healCharges = 3;
            killsSinceLastHealRecharge = 0;
            currentHorizontalVelocity = Vector3.zero;
            verticalVelocity = 0f;
            rotationX = 0f;
            transform.SetPositionAndRotation(startPosition, Quaternion.identity);
            Camera camera = Camera.main;
            if (camera != null)
                camera.transform.localRotation = Quaternion.identity;
            FindAnyObjectByType<UIManager>()?.UpdateHUD(currentHealth, weaponSystem != null ? weaponSystem.GetCurrentAmmo() : 0, GameManager.Instance.GetCurrentLevel(), healCharges);
        }

        private void SnapToFloor()
        {
            Vector3 position = transform.position;
            if (position.y < -1f)
                position = new Vector3(2f, floorY, 2f);

            if (Mathf.Abs(position.y - floorY) > 0.001f)
            {
                position.y = floorY;
                transform.position = position;
            }
        }

        public int GetHealth() => currentHealth;
        public int GetMaxHealth() => maxHealth;

        public float GetSensitivityPercent()
        {
            return Mathf.Clamp(lookSensitivity / 2f * 100f, 25f, 200f);
        }

        public void SetSensitivityPercent(float percent)
        {
            ApplySensitivityPercent(percent);
            PlayerPrefs.SetFloat(SensitivityPrefsKey, GetSensitivityPercent());
            PlayerPrefs.Save();
        }

        private void ApplySensitivityPercent(float percent)
        {
            lookSensitivity = 2f * Mathf.Clamp(percent, 25f, 200f) / 100f;
        }
    }
}
