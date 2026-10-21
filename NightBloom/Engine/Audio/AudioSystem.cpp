//------------------------------------------------------------------------------
// AudioSystem.cpp — XAudio2 backend (see the header for the design).
//
// Voice model: a pool of source voices, reused by format and bus. A one-shot
// grabs an idle pooled voice (nothing queued) or creates one, up to
// kMaxVoices; past that it steals the oldest one-shot. Loops get their own
// voice for as long as they run. Each bus is a stereo submix voice feeding the
// mastering voice, so bus volume is one SetVolume.
//------------------------------------------------------------------------------
#include "Engine/Audio/AudioSystem.hpp"
#include "Engine/Core/Logger/Logger.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <xaudio2.h>
#pragma comment(lib, "xaudio2.lib")   // XAudio2 2.9 (Win10+); links without a CMake edit
#pragma comment(lib, "ole32.lib")
#endif

namespace Nightbloom
{
	namespace
	{
		constexpr size_t kMaxVoices = 64;
		constexpr float  kMaxPitch = 4.0f;
	}

#ifdef _WIN32
	struct AudioSystem::Impl
	{
		struct Voice
		{
			IXAudio2SourceVoice* voice = nullptr;
			uint32_t    rate = 0;
			int         channels = 0;
			AudioBus    bus = AudioBus::Sfx;
			bool        looping = false;
			SoundHandle handle = 0;
			uint64_t    started = 0;
		};

		IXAudio2*               xa = nullptr;
		IXAudio2MasteringVoice* master = nullptr;
		IXAudio2SubmixVoice*    buses[static_cast<int>(AudioBus::Count)] = {};
		float                   busVolume[static_cast<int>(AudioBus::Count)] = { 1.0f, 1.0f, 1.0f };
		std::vector<Voice>      voices;
		bool                    comInitialized = false;
		SoundHandle             nextHandle = 1;
		uint64_t                clock = 0;

		static bool Idle(IXAudio2SourceVoice* v)
		{
			XAUDIO2_VOICE_STATE st;
			v->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
			return st.BuffersQueued == 0;
		}

		IXAudio2SourceVoice* CreateVoice(const AudioClip& clip, AudioBus bus)
		{
			WAVEFORMATEX wf = {};
			wf.wFormatTag = WAVE_FORMAT_PCM;
			wf.nChannels = static_cast<WORD>(clip.channels);
			wf.nSamplesPerSec = clip.sampleRate;
			wf.wBitsPerSample = 16;
			wf.nBlockAlign = static_cast<WORD>(clip.channels * 2);
			wf.nAvgBytesPerSec = clip.sampleRate * wf.nBlockAlign;
			XAUDIO2_SEND_DESCRIPTOR send = { 0, buses[static_cast<int>(bus)] };
			XAUDIO2_VOICE_SENDS sends = { 1, &send };
			IXAudio2SourceVoice* v = nullptr;
			if (FAILED(xa->CreateSourceVoice(&v, &wf, 0, kMaxPitch, nullptr, &sends))) return nullptr;
			return v;
		}

		void Configure(IXAudio2SourceVoice* v, const AudioClip& clip, AudioBus bus, float volume, float pitch, float pan)
		{
			v->SetVolume(std::max(0.0f, volume));
			v->SetFrequencyRatio(std::clamp(pitch, 0.05f, kMaxPitch));
			// Equal-power pan for mono; stereo passes straight through.
			float m[4];
			if (clip.channels == 1)
			{
				const float a = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * 0.25f * 3.14159265f;
				m[0] = std::cos(a);
				m[1] = std::sin(a);
				v->SetOutputMatrix(buses[static_cast<int>(bus)], 1, 2, m);
			}
			else
			{
				m[0] = 1.0f; m[1] = 0.0f; m[2] = 0.0f; m[3] = 1.0f;
				v->SetOutputMatrix(buses[static_cast<int>(bus)], 2, 2, m);
			}
		}

		static void Submit(IXAudio2SourceVoice* v, const AudioClip& clip, bool loop)
		{
			XAUDIO2_BUFFER buf = {};
			buf.AudioBytes = static_cast<UINT32>(clip.samples.size() * sizeof(int16_t));
			buf.pAudioData = reinterpret_cast<const BYTE*>(clip.samples.data());
			buf.Flags = XAUDIO2_END_OF_STREAM;
			buf.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;
			v->SubmitSourceBuffer(&buf);
		}
	};

	AudioSystem::AudioSystem() : m_Impl(std::make_unique<Impl>()) {}
	AudioSystem::~AudioSystem() { Shutdown(); }

	bool AudioSystem::Initialize()
	{
		Impl& I = *m_Impl;
		if (I.xa) return true;
		const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		I.comInitialized = SUCCEEDED(co);   // RPC_E_CHANGED_MODE: someone else owns COM; fine
		if (FAILED(XAudio2Create(&I.xa, 0, XAUDIO2_DEFAULT_PROCESSOR)))
		{
			LOG_WARN("Audio: XAudio2 unavailable - running silent");
			I.xa = nullptr;
			return false;
		}
		if (FAILED(I.xa->CreateMasteringVoice(&I.master)))
		{
			LOG_WARN("Audio: no output device - running silent");
			I.xa->Release();
			I.xa = nullptr;
			return false;
		}
		for (int b = 0; b < static_cast<int>(AudioBus::Count); ++b)
			if (FAILED(I.xa->CreateSubmixVoice(&I.buses[b], 2, 44100)))
			{
				LOG_WARN("Audio: could not create mix bus - running silent");
				Shutdown();
				return false;
			}
		LOG_INFO("Audio: XAudio2 ready");
		return true;
	}

	void AudioSystem::Shutdown()
	{
		if (!m_Impl) return;
		Impl& I = *m_Impl;
		for (auto& v : I.voices) if (v.voice) v.voice->DestroyVoice();
		I.voices.clear();
		for (auto*& b : I.buses) if (b) { b->DestroyVoice(); b = nullptr; }
		if (I.master) { I.master->DestroyVoice(); I.master = nullptr; }
		if (I.xa) { I.xa->Release(); I.xa = nullptr; }
		if (I.comInitialized) { CoUninitialize(); I.comInitialized = false; }
	}

	bool AudioSystem::IsAvailable() const { return m_Impl && m_Impl->xa != nullptr; }

	void AudioSystem::Play(const AudioClip& clip, float volume, float pitch, float pan, AudioBus bus)
	{
		Impl& I = *m_Impl;
		if (!I.xa || clip.Empty() || volume <= 0.0f) return;

		Impl::Voice* slot = nullptr;
		for (auto& v : I.voices)
			if (!v.looping && v.rate == clip.sampleRate && v.channels == clip.channels && v.bus == bus && Impl::Idle(v.voice))
			{ slot = &v; break; }
		if (!slot && I.voices.size() < kMaxVoices)
		{
			Impl::Voice nv;
			nv.voice = I.CreateVoice(clip, bus);
			if (!nv.voice) return;
			nv.rate = clip.sampleRate;
			nv.channels = clip.channels;
			nv.bus = bus;
			I.voices.push_back(nv);
			slot = &I.voices.back();
		}
		if (!slot)
		{
			// Full: steal the oldest one-shot of the right format.
			for (auto& v : I.voices)
				if (!v.looping && v.rate == clip.sampleRate && v.channels == clip.channels && v.bus == bus
					&& (!slot || v.started < slot->started)) slot = &v;
			if (!slot) return;
		}

		slot->voice->Stop();
		slot->voice->FlushSourceBuffers();
		I.Configure(slot->voice, clip, bus, volume, pitch, pan);
		Impl::Submit(slot->voice, clip, false);
		slot->voice->Start();
		slot->started = ++I.clock;
	}

	SoundHandle AudioSystem::PlayLoop(const AudioClip& clip, float volume, float pitch, AudioBus bus)
	{
		Impl& I = *m_Impl;
		if (!I.xa || clip.Empty()) return 0;
		Impl::Voice nv;
		nv.voice = I.CreateVoice(clip, bus);
		if (!nv.voice) return 0;
		nv.rate = clip.sampleRate;
		nv.channels = clip.channels;
		nv.bus = bus;
		nv.looping = true;
		nv.handle = I.nextHandle++;
		I.Configure(nv.voice, clip, bus, volume, pitch, 0.0f);
		Impl::Submit(nv.voice, clip, true);
		nv.voice->Start();
		I.voices.push_back(nv);
		return nv.handle;
	}

	void AudioSystem::SetLoop(SoundHandle h, float volume, float pitch, float pan)
	{
		Impl& I = *m_Impl;
		for (auto& v : I.voices)
			if (v.looping && v.handle == h)
			{
				v.voice->SetVolume(std::max(0.0f, volume));
				v.voice->SetFrequencyRatio(std::clamp(pitch, 0.05f, kMaxPitch));
				if (v.channels == 1)
				{
					const float a = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * 0.25f * 3.14159265f;
					float m[2] = { std::cos(a), std::sin(a) };
					v.voice->SetOutputMatrix(I.buses[static_cast<int>(v.bus)], 1, 2, m);
				}
				return;
			}
	}

	void AudioSystem::StopLoop(SoundHandle h)
	{
		Impl& I = *m_Impl;
		for (size_t i = 0; i < I.voices.size(); ++i)
			if (I.voices[i].looping && I.voices[i].handle == h)
			{
				I.voices[i].voice->DestroyVoice();
				I.voices.erase(I.voices.begin() + static_cast<long>(i));
				return;
			}
	}

	void AudioSystem::StopAll()
	{
		Impl& I = *m_Impl;
		for (auto& v : I.voices) if (v.voice) v.voice->DestroyVoice();
		I.voices.clear();
	}

	void AudioSystem::SetMasterVolume(float v)
	{
		if (m_Impl->master) m_Impl->master->SetVolume(std::max(0.0f, v));
	}

	void AudioSystem::SetBusVolume(AudioBus bus, float v)
	{
		const int b = static_cast<int>(bus);
		m_Impl->busVolume[b] = std::max(0.0f, v);
		if (m_Impl->buses[b]) m_Impl->buses[b]->SetVolume(m_Impl->busVolume[b]);
	}

	float AudioSystem::GetBusVolume(AudioBus bus) const { return m_Impl->busVolume[static_cast<int>(bus)]; }

#else   // Non-Windows: silent stub (the engine is Windows-only today).
	struct AudioSystem::Impl { float busVolume[3] = { 1, 1, 1 }; };
	AudioSystem::AudioSystem() : m_Impl(std::make_unique<Impl>()) {}
	AudioSystem::~AudioSystem() = default;
	bool AudioSystem::Initialize() { return false; }
	void AudioSystem::Shutdown() {}
	bool AudioSystem::IsAvailable() const { return false; }
	void AudioSystem::Play(const AudioClip&, float, float, float, AudioBus) {}
	SoundHandle AudioSystem::PlayLoop(const AudioClip&, float, float, AudioBus) { return 0; }
	void AudioSystem::SetLoop(SoundHandle, float, float, float) {}
	void AudioSystem::StopLoop(SoundHandle) {}
	void AudioSystem::StopAll() {}
	void AudioSystem::SetMasterVolume(float) {}
	void AudioSystem::SetBusVolume(AudioBus bus, float v) { m_Impl->busVolume[static_cast<int>(bus)] = v; }
	float AudioSystem::GetBusVolume(AudioBus bus) const { return m_Impl->busVolume[static_cast<int>(bus)]; }
#endif

	//--------------------------------------------------------------------------
	// WAV (16-bit PCM, mono/stereo) — portable.
	//--------------------------------------------------------------------------
	bool AudioSystem::LoadWav(const std::string& file, AudioClip& out, std::string& error)
	{
		std::ifstream in(file, std::ios::binary);
		if (!in) { error = "cannot open " + file; return false; }
		std::vector<char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		auto u32 = [&](size_t o) { uint32_t v; std::memcpy(&v, &data[o], 4); return v; };
		auto u16 = [&](size_t o) { uint16_t v; std::memcpy(&v, &data[o], 2); return v; };
		if (data.size() < 12 || std::memcmp(&data[0], "RIFF", 4) != 0 || std::memcmp(&data[8], "WAVE", 4) != 0)
		{ error = file + ": not a RIFF/WAVE file"; return false; }

		uint16_t format = 0, channels = 0, bits = 0;
		uint32_t rate = 0;
		bool haveFmt = false;
		for (size_t o = 12; o + 8 <= data.size();)
		{
			const uint32_t size = u32(o + 4);
			const size_t body = o + 8;
			if (body + size > data.size()) break;
			if (std::memcmp(&data[o], "fmt ", 4) == 0 && size >= 16)
			{
				format = u16(body); channels = u16(body + 2); rate = u32(body + 4); bits = u16(body + 14);
				haveFmt = true;
			}
			else if (std::memcmp(&data[o], "data", 4) == 0)
			{
				if (!haveFmt) { error = file + ": data before fmt"; return false; }
				if (format != 1 || bits != 16 || channels < 1 || channels > 2)
				{ error = file + ": only 16-bit PCM mono/stereo WAV is supported"; return false; }
				out.channels = channels;
				out.sampleRate = rate;
				out.samples.resize(size / 2);
				std::memcpy(out.samples.data(), &data[body], out.samples.size() * 2);
				return true;
			}
			o = body + size + (size & 1);   // chunks are word-aligned
		}
		error = file + ": no audio data";
		return false;
	}

	bool AudioSystem::SaveWav(const std::string& file, const AudioClip& clip)
	{
		std::ofstream out(file, std::ios::binary);
		if (!out) return false;
		const uint32_t dataBytes = static_cast<uint32_t>(clip.samples.size() * 2);
		const uint16_t blockAlign = static_cast<uint16_t>(clip.channels * 2);
		const uint32_t byteRate = clip.sampleRate * blockAlign;
		auto w32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
		auto w16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
		out.write("RIFF", 4); w32(36 + dataBytes); out.write("WAVE", 4);
		out.write("fmt ", 4); w32(16); w16(1); w16(static_cast<uint16_t>(clip.channels));
		w32(clip.sampleRate); w32(byteRate); w16(blockAlign); w16(16);
		out.write("data", 4); w32(dataBytes);
		out.write(reinterpret_cast<const char*>(clip.samples.data()), dataBytes);
		return static_cast<bool>(out);
	}
}
