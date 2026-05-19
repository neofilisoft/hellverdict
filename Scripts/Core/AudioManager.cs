using System.Collections;
using System.IO;
using UnityEngine;
using UnityEngine.Networking;

namespace BalmungDoom.Core
{
    public class AudioManager : MonoBehaviour
    {
        public static AudioManager Instance { get; private set; }

        private AudioSource bgmSource;
        private AudioSource sfxSource;
        private float masterVolume = 8f;
        private float bgmVolume = 8f;
        private float sfxVolume = 8f;

        private AudioClip themeClip;
        private AudioClip cyberThemeClip;
        private AudioClip shotgunClip;
        private AudioClip playerPainClip;
        private AudioClip npcAttackClip;
        private AudioClip npcPainClip;
        private AudioClip npcDeathClip;
        private bool wantsCyberTheme;
        private Coroutine cyberThemeLoadCoroutine;

        private void Awake()
        {
            if (Instance != null && Instance != this)
            {
                Destroy(gameObject);
                return;
            }

            Instance = this;
            DontDestroyOnLoad(gameObject);
            bgmSource = gameObject.AddComponent<AudioSource>();
            sfxSource = gameObject.AddComponent<AudioSource>();
            bgmSource.loop = true;
            LoadClips();
            ApplyVolumes();
        }

        private void LoadClips()
        {
            themeClip = Resources.Load<AudioClip>("sound/theme");
            cyberThemeClip = Resources.Load<AudioClip>("sound/theme_2");
            if (cyberThemeClip == null)
                cyberThemeLoadCoroutine = StartCoroutine(LoadExternalAudioClip("sound/theme_2.mp3"));

            bgmSource.clip = themeClip;
            shotgunClip = Resources.Load<AudioClip>("sound/shotgun");
            playerPainClip = Resources.Load<AudioClip>("sound/player_pain");
            npcAttackClip = Resources.Load<AudioClip>("sound/npc_attack");
            npcPainClip = Resources.Load<AudioClip>("sound/npc_pain");
            npcDeathClip = Resources.Load<AudioClip>("sound/npc_death");
        }

        public void PlayGameplayMusic(bool cyberDemonPresent = false)
        {
            wantsCyberTheme = cyberDemonPresent;
            AudioClip requestedClip = cyberDemonPresent && cyberThemeClip != null ? cyberThemeClip : themeClip;
            if (bgmSource.clip != requestedClip)
            {
                bgmSource.Stop();
                bgmSource.clip = requestedClip;
            }

            if (bgmSource.clip != null && !bgmSource.isPlaying)
                bgmSource.Play();
        }

        private IEnumerator LoadExternalAudioClip(string relativePath)
        {
            string filePath = FindSourceAsset(relativePath);
            if (string.IsNullOrEmpty(filePath))
            {
                cyberThemeLoadCoroutine = null;
                yield break;
            }

            using UnityWebRequest request = UnityWebRequestMultimedia.GetAudioClip(new System.Uri(filePath).AbsoluteUri, AudioType.MPEG);
            yield return request.SendWebRequest();

            if (request.result == UnityWebRequest.Result.Success)
            {
                cyberThemeClip = DownloadHandlerAudioClip.GetContent(request);
                if (cyberThemeClip != null)
                    cyberThemeClip.name = Path.GetFileNameWithoutExtension(filePath);
            }

            cyberThemeLoadCoroutine = null;
            if (wantsCyberTheme && cyberThemeClip != null)
                PlayGameplayMusic(true);
        }

        private string FindSourceAsset(string relativePath)
        {
            string dataPath = Application.dataPath;
            string appRoot = Directory.GetParent(dataPath)?.FullName;
            string projectRoot = Directory.GetParent(Directory.GetParent(appRoot ?? string.Empty)?.FullName ?? string.Empty)?.FullName;
            string repoRoot = Directory.GetParent(dataPath)?.FullName;

            string[] candidates =
            {
                Path.Combine(dataPath, "resources", relativePath),
                Path.Combine(projectRoot ?? string.Empty, "Assets", "resources", relativePath),
                Path.Combine(repoRoot ?? string.Empty, "Hell Verdict", "Assets", "resources", relativePath)
            };

            foreach (string candidate in candidates)
            {
                if (File.Exists(candidate))
                    return candidate;
            }

            return null;
        }

        public void StopGameplayMusic()
        {
            bgmSource.Stop();
        }

        public void PlayShotgunSound() => PlaySfx(shotgunClip);
        public void PlayPlayerPainSound() => PlaySfx(playerPainClip);
        public void PlayEnemyAttackSound() => PlaySfx(npcAttackClip);
        public void PlayEnemyPainSound() => PlaySfx(npcPainClip);
        public void PlayEnemyDeathSound() => PlaySfx(npcDeathClip);

        private void PlaySfx(AudioClip clip)
        {
            if (clip != null)
                sfxSource.PlayOneShot(clip, Mathf.Clamp01(masterVolume / 10f) * Mathf.Clamp01(sfxVolume / 10f));
        }

        public void SetMasterVolume(float value)
        {
            masterVolume = Mathf.Clamp(value, 0f, 10f);
            ApplyVolumes();
        }

        public void SetBGMVolume(float value)
        {
            bgmVolume = Mathf.Clamp(value, 0f, 10f);
            ApplyVolumes();
        }

        public void SetSFXVolume(float value)
        {
            sfxVolume = Mathf.Clamp(value, 0f, 10f);
            ApplyVolumes();
        }

        public float GetMasterVolume() => masterVolume;
        public float GetBGMVolume() => bgmVolume;
        public float GetSFXVolume() => sfxVolume;

        private void ApplyVolumes()
        {
            if (bgmSource != null)
                bgmSource.volume = Mathf.Clamp01(masterVolume / 10f) * Mathf.Clamp01(bgmVolume / 10f);
        }
    }
}
