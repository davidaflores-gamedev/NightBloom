//------------------------------------------------------------------------------
// FileWatcherTests.cpp
//
// Hot-reload watcher (Engine/Core/FileWatcher.hpp). Timestamps are set
// explicitly with last_write_time(path, t) rather than by sleeping, so these
// don't depend on filesystem timestamp resolution or wall-clock timing.
//------------------------------------------------------------------------------
#include <gtest/gtest.h>
#include "Engine/Core/FileWatcher.hpp"

#include <chrono>
#include <fstream>

using namespace Nightbloom;
namespace fs = std::filesystem;

namespace
{
	class FileWatcherTest : public ::testing::Test
	{
	protected:
		void SetUp() override
		{
			const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
			m_Dir = fs::temp_directory_path() / (std::string("nb_filewatcher_") + info->name());
			fs::remove_all(m_Dir);
			fs::create_directories(m_Dir);
			m_File = m_Dir / "moves.json";
			Write("{}");
			m_Base = fs::last_write_time(m_File);

			// Deterministic: poll on every Update, 0.1 s debounce.
			m_Watcher.SetPollInterval(0.0f);
			m_Watcher.SetDebounce(0.1f);
		}

		void TearDown() override
		{
			std::error_code ec;
			fs::remove_all(m_Dir, ec);
		}

		void Write(const char* text)
		{
			std::ofstream(m_File, std::ios::trunc) << text;
		}

		// "Edit" the file: give it a timestamp `seconds` after the baseline.
		void Touch(int seconds)
		{
			fs::last_write_time(m_File, m_Base + std::chrono::seconds(seconds));
		}

		fs::path        m_Dir, m_File;
		fs::file_time_type m_Base;
		FileWatcher     m_Watcher;
		int             m_Fired = 0;
	};
}

TEST_F(FileWatcherTest, ExistingFileDoesNotFireOnWatch)
{
	m_Watcher.Watch(m_File, [this](const fs::path&) { ++m_Fired; });
	m_Watcher.Update(1.0f);
	m_Watcher.Update(1.0f);
	EXPECT_EQ(m_Fired, 0);
}

TEST_F(FileWatcherTest, ChangeFiresOnceAfterDebounce)
{
	fs::path seen;
	m_Watcher.Watch(m_File, [&](const fs::path& p) { ++m_Fired; seen = p; });

	Touch(10);
	m_Watcher.Update(0.016f);   // change noticed, debounce starts
	EXPECT_EQ(m_Fired, 0);
	m_Watcher.Update(0.05f);    // still inside the debounce
	EXPECT_EQ(m_Fired, 0);
	m_Watcher.Update(0.06f);    // 0.11 s held still → fire
	EXPECT_EQ(m_Fired, 1);
	EXPECT_EQ(seen, m_File);

	m_Watcher.Update(1.0f);     // no further change, no repeat
	EXPECT_EQ(m_Fired, 1);
}

TEST_F(FileWatcherTest, ContinuedWritesRestartDebounce)
{
	m_Watcher.Watch(m_File, [this](const fs::path&) { ++m_Fired; });

	Touch(10);
	m_Watcher.Update(0.016f);
	m_Watcher.Update(0.08f);
	Touch(11);                   // second half of a two-step save
	m_Watcher.Update(0.05f);     // restarts: only 0.0 s stable
	EXPECT_EQ(m_Fired, 0);
	m_Watcher.Update(0.11f);
	EXPECT_EQ(m_Fired, 1);       // one reload, for the finished file
}

TEST_F(FileWatcherTest, EachSeparateEditFires)
{
	m_Watcher.Watch(m_File, [this](const fs::path&) { ++m_Fired; });
	Touch(10); m_Watcher.Update(0.0f); m_Watcher.Update(0.2f);
	Touch(20); m_Watcher.Update(0.0f); m_Watcher.Update(0.2f);
	EXPECT_EQ(m_Fired, 2);
}

TEST_F(FileWatcherTest, DeleteIsSilentRecreateFires)
{
	m_Watcher.Watch(m_File, [this](const fs::path&) { ++m_Fired; });

	fs::remove(m_File);
	m_Watcher.Update(0.2f);
	m_Watcher.Update(0.2f);
	EXPECT_EQ(m_Fired, 0);

	Write("{ \"back\": true }");
	m_Watcher.Update(0.0f);
	m_Watcher.Update(0.2f);
	EXPECT_EQ(m_Fired, 1);
}

TEST_F(FileWatcherTest, WatchingMissingFileFiresWhenCreated)
{
	const fs::path later = m_Dir / "later.json";
	m_Watcher.Watch(later, [this](const fs::path&) { ++m_Fired; });
	m_Watcher.Update(0.2f);
	EXPECT_EQ(m_Fired, 0);

	std::ofstream(later) << "{}";
	m_Watcher.Update(0.0f);
	m_Watcher.Update(0.2f);
	EXPECT_EQ(m_Fired, 1);
}

TEST_F(FileWatcherTest, UnwatchStopsCallbacks)
{
	auto id = m_Watcher.Watch(m_File, [this](const fs::path&) { ++m_Fired; });
	m_Watcher.Unwatch(id);
	EXPECT_EQ(m_Watcher.GetWatchCount(), 0u);
	Touch(10);
	m_Watcher.Update(0.0f);
	m_Watcher.Update(0.2f);
	EXPECT_EQ(m_Fired, 0);
}

TEST_F(FileWatcherTest, CallbackMayUnwatchItself)
{
	FileWatcher::WatchId id = 0;
	id = m_Watcher.Watch(m_File, [&](const fs::path&) { ++m_Fired; m_Watcher.Unwatch(id); });
	Touch(10);
	m_Watcher.Update(0.0f);
	EXPECT_NO_FATAL_FAILURE(m_Watcher.Update(0.2f));
	EXPECT_EQ(m_Fired, 1);
	EXPECT_EQ(m_Watcher.GetWatchCount(), 0u);
}

TEST_F(FileWatcherTest, PollIntervalThrottlesStats)
{
	m_Watcher.SetPollInterval(0.5f);
	m_Watcher.SetDebounce(0.0f);
	m_Watcher.Watch(m_File, [this](const fs::path&) { ++m_Fired; });

	Touch(10);
	m_Watcher.Update(0.2f);      // not time to poll yet
	EXPECT_EQ(m_Fired, 0);
	m_Watcher.Update(0.31f);     // 0.51 s → polls, zero debounce → fires
	EXPECT_EQ(m_Fired, 1);
}
