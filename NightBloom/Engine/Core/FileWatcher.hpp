//------------------------------------------------------------------------------
// FileWatcher.hpp
//
// Hot reload for data files: watch a path, get a callback when it changes.
//
//   FileWatcher watcher;
//   watcher.Watch("Data/moves.json", [this](const auto& p) { LoadMoves(p); });
//   ...each frame:  watcher.Update(deltaTime);
//
// WHY POLLING: ReadDirectoryChangesW is faster to notice but is per-directory,
// asynchronous and Windows-only. For a handful of tuning files, stat()-ing each
// one a few times a second costs nothing measurable and keeps this portable
// and trivially testable.
//
// DEBOUNCE: many editors save in two steps (truncate, then write; or write a
// temp file, then rename), and a watcher that fires on the first change reads
// a half-written file. A change is only reported once the file's timestamp has
// held still for `debounceSeconds`. Deleting a file is not reported; it coming
// back is (as a change).
//
// Callbacks run synchronously inside Update(), on the calling thread — safe to
// touch game state from them. Time is passed in rather than read from a clock,
// so behaviour is deterministic and unit-testable.
//------------------------------------------------------------------------------
#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

namespace Nightbloom
{
	class FileWatcher
	{
	public:
		using Callback = std::function<void(const std::filesystem::path&)>;
		using WatchId = int;

		// Start watching. The current state is the baseline: an existing file
		// does NOT fire immediately (load it yourself at startup).
		WatchId Watch(const std::filesystem::path& path, Callback callback);
		void    Unwatch(WatchId id);
		void    Clear() { m_Entries.clear(); }

		// Call once per frame. Stats files at most every `pollIntervalSeconds`.
		// Returns how many callbacks fired.
		int Update(float deltaSeconds);

		// Stat every file right now, ignoring the poll interval (debounce still
		// applies). Returns how many callbacks fired.
		int PollNow();

		void SetPollInterval(float seconds) { m_PollInterval = seconds; }
		void SetDebounce(float seconds) { m_Debounce = seconds; }
		size_t GetWatchCount() const { return m_Entries.size(); }

	private:
		using Stamp = std::optional<std::filesystem::file_time_type>;

		struct Entry
		{
			WatchId               id = 0;
			std::filesystem::path path;
			Callback              callback;
			Stamp                 reported;        // last state we told the caller about
			Stamp                 pending;         // changed state waiting out the debounce
			float                 pendingSince = 0.0f;
		};

		static Stamp ReadStamp(const std::filesystem::path& path);

		std::vector<Entry> m_Entries;
		WatchId m_NextId = 1;
		float   m_Clock = 0.0f;           // accumulated Update() time
		float   m_SincePoll = 0.0f;
		float   m_PollInterval = 0.25f;
		float   m_Debounce = 0.1f;
	};
}
