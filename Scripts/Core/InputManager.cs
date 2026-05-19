using UnityEngine;
using UnityEngine.InputSystem;

namespace BalmungDoom.Core
{
    public class InputManager : MonoBehaviour
    {
        public static InputManager Instance { get; private set; }

        public Vector2 MovementInput { get; private set; }
        public Vector2 LookInput { get; private set; }
        public bool FireInput { get; private set; }
        public bool ReloadInput { get; private set; }
        public bool JumpInput { get; private set; }
        public bool SprintInput { get; private set; }
        public bool HealInput { get; private set; }

        private bool suppressFireUntilReleased;

        private void Awake()
        {
            if (Instance != null && Instance != this)
            {
                Destroy(gameObject);
                return;
            }

            Instance = this;
            DontDestroyOnLoad(gameObject);
        }

        private void Update()
        {
            Keyboard keyboard = Keyboard.current;
            Mouse mouse = Mouse.current;

            float horizontal = 0f;
            float vertical = 0f;

            if (keyboard != null)
            {
                if (keyboard.aKey.isPressed || keyboard.leftArrowKey.isPressed) horizontal -= 1f;
                if (keyboard.dKey.isPressed || keyboard.rightArrowKey.isPressed) horizontal += 1f;
                if (keyboard.wKey.isPressed || keyboard.upArrowKey.isPressed) vertical += 1f;
                if (keyboard.sKey.isPressed || keyboard.downArrowKey.isPressed) vertical -= 1f;

                ReloadInput = keyboard.rKey.wasPressedThisFrame;
                JumpInput = keyboard.spaceKey.wasPressedThisFrame;
                SprintInput = keyboard.leftShiftKey.isPressed;
                HealInput = keyboard.fKey.wasPressedThisFrame;

                if (keyboard.escapeKey.wasPressedThisFrame && GameManager.Instance != null)
                    GameManager.Instance.OpenGameplayOptions();

                if (keyboard.mKey.wasPressedThisFrame)
                    FindAnyObjectByType<UIManager>()?.ToggleMinimap();
            }
            else
            {
                ReloadInput = false;
            }

            MovementInput = Vector2.ClampMagnitude(new Vector2(horizontal, vertical), 1f);
            LookInput = mouse != null ? mouse.delta.ReadValue() : Vector2.zero;
            if (suppressFireUntilReleased)
            {
                FireInput = false;
                if (mouse == null || !mouse.leftButton.isPressed)
                    suppressFireUntilReleased = false;
            }
            else
            {
                FireInput = mouse != null && mouse.leftButton.isPressed;
            }

            if (keyboard != null && keyboard.enterKey.wasPressedThisFrame && GameManager.Instance != null)
                GameManager.Instance.TogglePause();
        }

        public void SuppressFireUntilReleased()
        {
            suppressFireUntilReleased = true;
            FireInput = false;
        }
    }
}
