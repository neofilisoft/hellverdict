using UnityEngine;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using BalmungDoom.Core;
using BalmungDoom.Gameplay;

namespace BalmungDoom.Enemy
{
    public class EnemyController : MonoBehaviour, IDamageable
    {
        private int health;
        private int damage;
        private float speed;
        private float detectionRange = 80f;
        private float attackRange = 1.25f;
        private float attackCooldown = 2.2f;
        private float lastAttackTime;
        private float nextPathRefreshTime;

        private Transform playerTransform;
        private HellVerdictBootstrap level;
        private SpriteRenderer spriteRenderer;
        private Coroutine animationCoroutine;
        private readonly List<Vector3> path = new();
        private string enemyType = "soldier";
        private string currentAnimation;
        private int pathIndex;
        private bool isAlive = true;
        private bool isStunned;
        public bool IsAlive => isAlive;

        public void Initialize(int hp, int dmg, float spd)
        {
            health = hp;
            damage = dmg;
            speed = spd;
        }

        public void Initialize(string type, int hp, int dmg, float spd)
        {
            enemyType = type;
            if (type == "soldier")
                attackRange = 3f;
            else if (type == "caco_demon")
                attackRange = 1.5f;
            else if (type == "cyber_demon")
                attackRange = 2f;
            Initialize(hp, dmg, spd);
        }

        private void Start()
        {
            GameObject player = GameObject.FindGameObjectWithTag("Player");
            if (player != null)
                playerTransform = player.transform;

            level = FindAnyObjectByType<HellVerdictBootstrap>();
            spriteRenderer = GetComponent<SpriteRenderer>();
            ForcePointFiltering(spriteRenderer != null ? spriteRenderer.sprite : null);
            AddCollider();
            lastAttackTime = Time.time;
            PlayLoop("idle", 0.16f);
        }

        private void AddCollider()
        {
            if (GetComponent<Collider>() == null)
            {
                CapsuleCollider collider = gameObject.AddComponent<CapsuleCollider>();
                collider.center = new Vector3(0, 1, 0);
                collider.height = 2;
                collider.radius = 0.5f;
            }

            Rigidbody rb = GetComponent<Rigidbody>();
            if (rb == null)
            {
                rb = gameObject.AddComponent<Rigidbody>();
                rb.isKinematic = true;
                rb.useGravity = false;
            }
        }

        private void Update()
        {
            if (!isAlive || isStunned || playerTransform == null) return;

            float distanceToPlayer = Vector3.Distance(transform.position, playerTransform.position);

            if (distanceToPlayer <= attackRange && HasLineOfSight(distanceToPlayer))
            {
                AttackPlayer();
                return;
            }

            if (HasLineOfSight(distanceToPlayer))
                MoveToward(playerTransform.position);
            else
                FollowPathToPlayer();
        }

        private void MoveToward(Vector3 targetPosition)
        {
            Vector3 direction = (targetPosition - transform.position).normalized;
            direction.y = 0f;
            if (direction.sqrMagnitude < 0.001f) return;

            float moveDistance = speed * Time.deltaTime;
            Vector3 origin = transform.position + Vector3.up * 0.5f;
            float wallCheckDist = 0.6f;

            if (Physics.Raycast(origin, direction, out RaycastHit hit, wallCheckDist + moveDistance, ~0, QueryTriggerInteraction.Ignore))
            {
                if (hit.collider.GetComponent<EnemyController>() == null &&
                    hit.collider.GetComponentInParent<PlayerController>() == null)
                {
                    float allowedDist = Mathf.Max(0f, hit.distance - wallCheckDist);
                    if (allowedDist < 0.01f)
                    {
                        PlayLoop("idle", 0.16f);
                        return;
                    }
                    moveDistance = Mathf.Min(moveDistance, allowedDist);
                }
            }

            transform.position += direction * moveDistance;
            transform.rotation = Quaternion.LookRotation(direction, Vector3.up);
            PlayLoop("walk", 0.14f);
        }

        private void FollowPathToPlayer()
        {
            if (level == null)
                level = FindAnyObjectByType<HellVerdictBootstrap>();

            if (level == null)
            {
                PlayLoop("idle", 0.16f);
                return;
            }

            if (Time.time >= nextPathRefreshTime || pathIndex >= path.Count)
            {
                nextPathRefreshTime = Time.time + 0.35f;
                pathIndex = 0;
                level.TryFindPath(transform.position, playerTransform.position, path);
            }

            if (path.Count == 0 || pathIndex >= path.Count)
            {
                PlayLoop("idle", 0.16f);
                return;
            }

            Vector3 waypoint = path[pathIndex];
            waypoint.y = transform.position.y;
            if (Vector3.Distance(transform.position, waypoint) < 0.25f)
            {
                pathIndex++;
                if (pathIndex >= path.Count)
                    return;

                waypoint = path[pathIndex];
                waypoint.y = transform.position.y;
            }

            MoveToward(waypoint);
        }

        private bool HasLineOfSight(float distanceToPlayer)
        {
            Vector3 origin = transform.position + Vector3.up;
            Vector3 target = playerTransform.position + Vector3.up;
            Vector3 direction = (target - origin).normalized;
            if (Physics.Raycast(origin, direction, out RaycastHit hit, distanceToPlayer, ~0, QueryTriggerInteraction.Ignore))
                return hit.collider.GetComponentInParent<PlayerController>() != null;

            return true;
        }

        private void AttackPlayer()
        {
            if (Time.time - lastAttackTime < attackCooldown) return;

            lastAttackTime = Time.time;
            AudioManager.Instance.PlayEnemyAttackSound();
            PlayOnce("attack", 0.12f, () => PlayLoop("idle", 0.16f));

            PlayerController player = playerTransform.GetComponent<PlayerController>();
            if (player != null)
                player.TakeDamage(damage);
        }

        public void TakeDamage(int damageAmount)
        {
            health -= damageAmount;
            AudioManager.Instance.PlayEnemyPainSound();

            if (health <= 0)
            {
                Die();
                return;
            }

            PlayPain();
        }

        private void Die()
        {
            isAlive = false;
            StopAllCoroutines();
            AudioManager.Instance.PlayEnemyDeathSound();
            FindAnyObjectByType<HellVerdictBootstrap>()?.RegisterEnemyDeath();
            playerTransform?.GetComponent<PlayerController>()?.OnEnemyKilled();
            Collider collider = GetComponent<Collider>();
            if (collider != null)
                collider.enabled = false;
            StartCoroutine(DeathRoutine());
        }

        private IEnumerator DeathRoutine()
        {
            string[] deathPaths = GetDeathFramePaths();
            foreach (string path in deathPaths)
            {
                Sprite frame = LoadSpriteAt(path);
                if (frame != null && spriteRenderer != null)
                {
                    ForcePointFiltering(frame);
                    spriteRenderer.sprite = frame;
                }
                yield return new WaitForSeconds(0.5f);
            }

            float fadeDuration = 0.5f;
            float elapsed = 0f;
            Color startColor = spriteRenderer != null ? spriteRenderer.color : Color.white;
            while (elapsed < fadeDuration)
            {
                elapsed += Time.deltaTime;
                if (spriteRenderer != null)
                    spriteRenderer.color = new Color(startColor.r, startColor.g, startColor.b, 1f - elapsed / fadeDuration);
                yield return null;
            }

            Destroy(gameObject);
        }

        private string[] GetDeathFramePaths()
        {
            return enemyType switch
            {
                "soldier" => new[] { "sprites/npc/soldier/death/POSSN0", "sprites/npc/soldier/death/POSSS0" },
                "caco_demon" => new[] { "sprites/npc/caco_demon/death/4", "sprites/npc/caco_demon/death/5" },
                "cyber_demon" => new[] { "sprites/npc/cyber_demon/death/5", "sprites/npc/cyber_demon/death/6" },
                _ => new[] { "sprites/npc/soldier/death/POSSN0" }
            };
        }

        private Sprite LoadSpriteAt(string path)
        {
            Sprite s = Resources.Load<Sprite>(path);
            if (s != null) return s;
            Texture2D tex = Resources.Load<Texture2D>(path);
            if (tex == null) return null;
            tex.filterMode = FilterMode.Point;
            return Sprite.Create(tex, new Rect(0, 0, tex.width, tex.height), new Vector2(0.5f, 0.5f), 100f);
        }

        private void PlayPain()
        {
            if (!isAlive)
                return;

            if (animationCoroutine != null)
                StopCoroutine(animationCoroutine);

            animationCoroutine = StartCoroutine(PainRoutine());
        }

        private IEnumerator PainRoutine()
        {
            isStunned = true;
            yield return PlayFrames("pain", 0.08f, false);
            isStunned = false;
            PlayLoop("idle", 0.16f);
        }

        private void PlayLoop(string state, float frameTime)
        {
            if (currentAnimation == state)
                return;

            currentAnimation = state;
            if (animationCoroutine != null)
                StopCoroutine(animationCoroutine);

            animationCoroutine = StartCoroutine(LoopFrames(state, frameTime));
        }

        private void PlayOnce(string state, float frameTime, System.Action onComplete)
        {
            currentAnimation = state;
            if (animationCoroutine != null)
                StopCoroutine(animationCoroutine);

            animationCoroutine = StartCoroutine(PlayOnceRoutine(state, frameTime, onComplete));
        }

        private IEnumerator LoopFrames(string state, float frameTime)
        {
            while (isAlive)
                yield return PlayFrames(state, frameTime, true);
        }

        private IEnumerator PlayOnceRoutine(string state, float frameTime, System.Action onComplete)
        {
            yield return PlayFrames(state, frameTime, false);
            currentAnimation = null;
            animationCoroutine = null;
            onComplete?.Invoke();
        }

        private IEnumerator PlayFrames(string state, float frameTime, bool loop)
        {
            Sprite[] frames = LoadAnimation(state);
            if (frames.Length == 0)
                yield break;

            int count = loop ? frames.Length : Mathf.Max(1, frames.Length);
            for (int i = 0; i < count; i++)
            {
                Sprite frame = frames[i % frames.Length];
                ForcePointFiltering(frame);
                if (spriteRenderer != null)
                    spriteRenderer.sprite = frame;
                yield return new WaitForSeconds(frameTime);
            }
        }

        private Sprite[] LoadAnimation(string state)
        {
            Sprite[] frames = LoadOrderedAnimation(state);
            if (state == "walk")
            {
                Sprite[] idleFrames = LoadOrderedAnimation("idle");
                if (frames.Length > 0)
                    frames = idleFrames.Concat(frames).ToArray();
                else
                    frames = idleFrames;
            }

            if (frames.Length == 0)
                frames = LoadOrderedAnimation("idle");

            foreach (Sprite frame in frames)
                ForcePointFiltering(frame);

            return frames;
        }

        private Sprite[] LoadOrderedAnimation(string state)
        {
            Sprite[] frames = Resources.LoadAll<Sprite>($"sprites/npc/{enemyType}/{state}");
            if (enemyType == "soldier" && state == "death")
            {
                string[] deathOrder = { "POSSM0", "POSSN0", "POSSP0", "POSSQ0", "POSSR0", "POSST0", "POSSU0" };
                return deathOrder
                    .Select(name => frames.FirstOrDefault(sprite => sprite.name == name))
                    .Where(sprite => sprite != null)
                    .ToArray();
            }

            return frames
                .OrderBy(sprite => GetNumericPrefix(sprite.name))
                .ThenBy(sprite => sprite.name)
                .ToArray();
        }

        private int GetNumericPrefix(string text)
        {
            if (int.TryParse(text, out int value))
                return value;

            return int.MaxValue;
        }

        private void ForcePointFiltering(Sprite sprite)
        {
            if (sprite != null && sprite.texture != null)
            {
                sprite.texture.filterMode = FilterMode.Point;
                sprite.texture.wrapMode = TextureWrapMode.Clamp;
            }
        }
    }
}
