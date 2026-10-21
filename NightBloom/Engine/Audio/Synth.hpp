//------------------------------------------------------------------------------
// Synth.hpp
//
// Tiny sound-design kit for generating sound effects in code: oscillators,
// noise, one-pole filters, envelopes, and Render() to turn a per-sample
// function into an AudioClip (peak-normalised, soft-clipped).
//
// Procedural sounds are a prototype's friend: no assets to source or license,
// every sound is a tweakable function, and they can be exported with
// AudioSystem::SaveWav to audition or replace with recordings later.
//
//   AudioClip zap = Synth::Render(0.09f, [osc = Synth::Osc{}](float t) mutable {
//       const float f = Synth::Sweep(t, 0.09f, 1800.0f, 400.0f);
//       return Synth::Square(osc.Step(f)) * Synth::Env(t, 0.002f, 0.03f);
//   });
//------------------------------------------------------------------------------
#pragma once

#include "Engine/Audio/AudioSystem.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Nightbloom
{
	namespace Synth
	{
		constexpr uint32_t kRate = 44100;
		constexpr float    kTwoPi = 6.28318530718f;

		// Phase accumulator: Step(freq) advances by one sample, returns phase in [0, 1).
		struct Osc
		{
			float phase = 0.0f;
			float Step(float freqHz, uint32_t rate = kRate)
			{
				phase += freqHz / static_cast<float>(rate);
				phase -= std::floor(phase);
				return phase;
			}
		};

		inline float Sine(float ph)   { return std::sin(kTwoPi * ph); }
		inline float Square(float ph) { return ph < 0.5f ? 1.0f : -1.0f; }
		inline float Saw(float ph)    { return 2.0f * ph - 1.0f; }
		inline float Tri(float ph)    { return 4.0f * std::abs(ph - 0.5f) - 1.0f; }

		// Deterministic white noise in [-1, 1] (xorshift32).
		struct Noise
		{
			uint32_t s = 0x9E3779B9u;
			explicit Noise(uint32_t seed = 0x9E3779B9u) : s(seed ? seed : 1u) {}
			float operator()()
			{
				s ^= s << 13; s ^= s >> 17; s ^= s << 5;
				return static_cast<float>(s) / 2147483647.5f - 1.0f;
			}
		};

		// One-pole filter. LowPass/HighPass take the cutoff per call, so it can sweep.
		struct OnePole
		{
			float z = 0.0f;
			float LowPass(float x, float cutoffHz, uint32_t rate = kRate)
			{
				const float a = 1.0f - std::exp(-kTwoPi * std::max(cutoffHz, 1.0f) / static_cast<float>(rate));
				z += a * (x - z);
				return z;
			}
			float HighPass(float x, float cutoffHz, uint32_t rate = kRate) { return x - LowPass(x, cutoffHz, rate); }
		};

		// Linear attack, then exponential decay with time constant `decay`.
		inline float Env(float t, float attack, float decay)
		{
			if (t < attack) return attack > 0.0f ? t / attack : 1.0f;
			return std::exp(-(t - attack) / std::max(decay, 1e-4f));
		}

		// Exponential sweep from f0 to f1 across `duration` (sounds even to the ear).
		inline float Sweep(float t, float duration, float f0, float f1)
		{
			const float u = std::clamp(t / std::max(duration, 1e-4f), 0.0f, 1.0f);
			return f0 * std::pow(f1 / f0, u);
		}

		// Render `seconds` of mono audio from f(t) -> sample, then peak-normalise
		// to `peak` and soft-clip. f is called once per sample, in order, so it
		// can carry oscillator/filter state in its captures (use `mutable`).
		// `fadeTail` ramps the last 5 ms to zero so a one-shot can't end on a
		// click; turn it off for loops, whose end must meet their start.
		template <typename F>
		AudioClip Render(float seconds, F&& f, float peak = 0.9f, uint32_t rate = kRate, bool fadeTail = true)
		{
			const size_t n = static_cast<size_t>(std::max(0.0f, seconds) * rate);
			std::vector<float> buf(n);
			float maxAbs = 0.0f;
			for (size_t i = 0; i < n; ++i)
			{
				buf[i] = f(static_cast<float>(i) / static_cast<float>(rate));
				maxAbs = std::max(maxAbs, std::abs(buf[i]));
			}
			// Normalise to [-1, 1], shape with a gentle tanh curve that still maps
			// 1 -> 1, then scale to the requested peak (so the peak is exact).
			const float inv = maxAbs > 1e-6f ? 1.0f / maxAbs : 0.0f;
			constexpr float kSoft = 1.1f;
			const float norm = 1.0f / std::tanh(kSoft);
			AudioClip clip;
			clip.sampleRate = rate;
			clip.channels = 1;
			clip.samples.resize(n);
			for (size_t i = 0; i < n; ++i)
			{
				float v = peak * std::tanh(kSoft * buf[i] * inv) * norm;
				const size_t fade = std::min(n / 10, static_cast<size_t>(rate / 200));   // 5 ms (or 10% of a tiny clip)
				if (fadeTail && fade > 0 && i + fade >= n) v *= static_cast<float>(n - 1 - i) / static_cast<float>(fade);
				clip.samples[i] = static_cast<int16_t>(std::clamp(v, -1.0f, 1.0f) * 32767.0f);
			}
			return clip;
		}
	}
}
