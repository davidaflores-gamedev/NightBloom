//------------------------------------------------------------------------------
// AudioTests.cpp
//
// The parts of audio that don't need a sound card: synthesis (Synth.hpp) and
// WAV round-tripping (AudioSystem::LoadWav / SaveWav). Playback itself is
// XAudio2 and is exercised by running the game.
//------------------------------------------------------------------------------
#include <gtest/gtest.h>
#include "Engine/Audio/Synth.hpp"

#include <filesystem>
#include <fstream>

using namespace Nightbloom;

TEST(Synth, RenderLengthAndFormat)
{
	const AudioClip c = Synth::Render(0.5f, [osc = Synth::Osc{}](float) mutable { return Synth::Sine(osc.Step(440.0f)); });
	EXPECT_EQ(c.channels, 1);
	EXPECT_EQ(c.sampleRate, Synth::kRate);
	EXPECT_EQ(c.samples.size(), static_cast<size_t>(0.5f * Synth::kRate));
	EXPECT_NEAR(c.DurationSeconds(), 0.5f, 1e-3f);
}

TEST(Synth, NormalisesToPeakWithoutClippingHard)
{
	// A quiet signal comes out near the requested peak; a huge one doesn't wrap.
	const AudioClip quiet = Synth::Render(0.1f, [osc = Synth::Osc{}](float) mutable { return 0.01f * Synth::Sine(osc.Step(220.0f)); }, 0.5f);
	const AudioClip loud  = Synth::Render(0.1f, [osc = Synth::Osc{}](float) mutable { return 50.0f * Synth::Square(osc.Step(220.0f)); }, 0.9f);
	int qMax = 0, lMax = 0;
	for (int16_t s : quiet.samples) qMax = std::max(qMax, std::abs(static_cast<int>(s)));
	for (int16_t s : loud.samples)  lMax = std::max(lMax, std::abs(static_cast<int>(s)));
	EXPECT_NEAR(qMax / 32767.0f, 0.5f, 0.05f);
	EXPECT_LE(lMax, 32767);
	EXPECT_GT(lMax, 28000);
}

TEST(Synth, SilenceStaysSilent)
{
	const AudioClip c = Synth::Render(0.05f, [](float) { return 0.0f; });
	for (int16_t s : c.samples) ASSERT_EQ(s, 0);
}

TEST(Synth, EnvelopeAndSweepShapes)
{
	EXPECT_NEAR(Synth::Env(0.0f, 0.01f, 0.1f), 0.0f, 1e-6f);
	EXPECT_NEAR(Synth::Env(0.01f, 0.01f, 0.1f), 1.0f, 1e-5f);
	EXPECT_NEAR(Synth::Env(0.11f, 0.01f, 0.1f), std::exp(-1.0f), 1e-4f);
	EXPECT_NEAR(Synth::Sweep(0.0f, 1.0f, 100.0f, 400.0f), 100.0f, 1e-3f);
	EXPECT_NEAR(Synth::Sweep(0.5f, 1.0f, 100.0f, 400.0f), 200.0f, 1e-2f);   // exponential midpoint
	EXPECT_NEAR(Synth::Sweep(1.0f, 1.0f, 100.0f, 400.0f), 400.0f, 1e-2f);
}

TEST(Synth, OneShotsEndAtZeroLoopsDont)
{
	auto tone = [](float) { return 1.0f; };   // a held DC level: worst case for an end click
	const AudioClip oneShot = Synth::Render(0.2f, tone);
	const AudioClip loop    = Synth::Render(0.2f, tone, 0.9f, Synth::kRate, false);
	EXPECT_EQ(oneShot.samples.back(), 0);
	EXPECT_GT(loop.samples.back(), 28000);
}

TEST(AudioWav, SaveLoadRoundTrip)
{
	AudioClip a = Synth::Render(0.02f, [n = Synth::Noise(7)](float) mutable { return n(); });
	const auto path = (std::filesystem::temp_directory_path() / "nightbloom_audio_roundtrip.wav").string();
	ASSERT_TRUE(AudioSystem::SaveWav(path, a));
	AudioClip b;
	std::string err;
	ASSERT_TRUE(AudioSystem::LoadWav(path, b, err)) << err;
	EXPECT_EQ(b.channels, a.channels);
	EXPECT_EQ(b.sampleRate, a.sampleRate);
	EXPECT_EQ(b.samples, a.samples);
	std::filesystem::remove(path);
}

TEST(AudioWav, RejectsNonWav)
{
	const auto path = (std::filesystem::temp_directory_path() / "nightbloom_not_a_wav.wav").string();
	{ std::ofstream f(path); f << "definitely not riff"; }
	AudioClip c;
	std::string err;
	EXPECT_FALSE(AudioSystem::LoadWav(path, c, err));
	EXPECT_FALSE(err.empty());
	std::filesystem::remove(path);
}
