//------------------------------------------------------------------------------
// AudioSystem.hpp
//
// Minimal game audio on XAudio2 (ships with Windows 10+, no extra install):
// fire-and-forget one-shots with volume / pitch / pan, controllable loops
// (engine hum, music), and three buses — Sfx, Music, Voice — each with its own
// volume under a master. Mono clips pan; stereo clips (music) play as-is.
//
// Deliberately small: no 3D positioning (callers compute pan + attenuation from
// their camera, which is a couple of lines), no streaming (clips are in memory).
// Two effects, both per one-shot (PlayFx): a low-pass filter (distance
// muffling) and a send to a shared reverb that feeds the Sfx bus (space —
// distant things are mostly tail). If the device can't be opened (no audio
// hardware, a test runner, a remote session) Initialize() returns false and
// every call is a harmless no-op, so nothing upstream needs to care. If only
// the reverb fails, sends are silently dropped.
//
// LIFETIME: a clip's samples are read directly while it plays, so a clip must
// outlive every sound playing it. Keep clips as long-lived members.
//------------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Nightbloom
{
	// 16-bit PCM in memory. Mono for sound effects (they can be panned).
	struct AudioClip
	{
		std::vector<int16_t> samples;     // interleaved if channels == 2
		uint32_t sampleRate = 44100;
		int      channels = 1;

		bool  Empty() const { return samples.empty(); }
		float DurationSeconds() const
		{
			return channels > 0 && sampleRate > 0 ? static_cast<float>(samples.size()) / (channels * sampleRate) : 0.0f;
		}
	};

	enum class AudioBus : uint8_t { Sfx, Music, Voice, Count };

	using SoundHandle = uint32_t;   // loops only; 0 = invalid

	// Per-sound processing. Defaults = dry and unfiltered.
	struct PlayFx
	{
		float lowpassHz = 0.0f;   // > 0: low-pass cutoff (muffled with distance); 0 = open
		float reverb = 0.0f;      // 0..1 send to the shared reverb (Sfx bus sounds only)
	};

	class AudioSystem
	{
	public:
		AudioSystem();
		~AudioSystem();
		AudioSystem(const AudioSystem&) = delete;
		AudioSystem& operator=(const AudioSystem&) = delete;

		bool Initialize();
		void Shutdown();
		bool IsAvailable() const;

		// One-shot. volume 0..1+, pitch as a playback-rate ratio (0.05..4),
		// pan -1 (left) .. +1 (right).
		void Play(const AudioClip& clip, float volume = 1.0f, float pitch = 1.0f, float pan = 0.0f,
			AudioBus bus = AudioBus::Sfx, const PlayFx& fx = PlayFx{});

		// Looping sound you keep adjusting (engine hum, music).
		SoundHandle PlayLoop(const AudioClip& clip, float volume = 1.0f, float pitch = 1.0f, AudioBus bus = AudioBus::Sfx);
		void SetLoop(SoundHandle h, float volume, float pitch = 1.0f, float pan = 0.0f);
		void StopLoop(SoundHandle h);
		void StopAll();

		void  SetMasterVolume(float v);
		void  SetBusVolume(AudioBus bus, float v);
		float GetBusVolume(AudioBus bus) const;

		// 16-bit PCM WAV, mono or stereo. Returns false and fills `error` on failure.
		static bool LoadWav(const std::string& file, AudioClip& out, std::string& error);
		static bool SaveWav(const std::string& file, const AudioClip& clip);

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}
