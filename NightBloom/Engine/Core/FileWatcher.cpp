//------------------------------------------------------------------------------
// FileWatcher.cpp — see FileWatcher.hpp for the why.
//------------------------------------------------------------------------------
#include "Engine/Core/FileWatcher.hpp"

#include <algorithm>
#include <system_error>

namespace Nightbloom
{
	FileWatcher::Stamp FileWatcher::ReadStamp(const std::filesystem::path& path)
	{
		std::error_code ec;
		if (!std::filesystem::is_regular_file(path, ec) || ec)
			return std::nullopt;
		const auto t = std::filesystem::last_write_time(path, ec);
		if (ec) return std::nullopt;
		return t;
	}

	FileWatcher::WatchId FileWatcher::Watch(const std::filesystem::path& path, Callback callback)
	{
		Entry e;
		e.id = m_NextId++;
		e.path = path;
		e.callback = std::move(callback);
		e.reported = ReadStamp(path);
		m_Entries.push_back(std::move(e));
		return m_Entries.back().id;
	}

	void FileWatcher::Unwatch(WatchId id)
	{
		m_Entries.erase(std::remove_if(m_Entries.begin(), m_Entries.end(),
			[id](const Entry& e) { return e.id == id; }), m_Entries.end());
	}

	int FileWatcher::Update(float deltaSeconds)
	{
		m_Clock += deltaSeconds;
		m_SincePoll += deltaSeconds;
		if (m_SincePoll < m_PollInterval) return 0;
		m_SincePoll = 0.0f;
		return PollNow();
	}

	int FileWatcher::PollNow()
	{
		// Collect first, call after: a callback may Watch/Unwatch, which would
		// invalidate iterators into m_Entries.
		std::vector<std::pair<Callback, std::filesystem::path>> toFire;

		for (Entry& e : m_Entries)
		{
			const Stamp now = ReadStamp(e.path);

			if (!now)
			{
				// Missing (deleted, or mid-rename). Forget any pending change and
				// remember the absence, so the file coming back counts as a change.
				e.pending.reset();
				e.reported.reset();
				continue;
			}

			if (now == e.reported)
			{
				e.pending.reset();   // changed and changed back, or nothing happened
				continue;
			}

			if (now != e.pending)
			{
				// New change (or still being written): restart the debounce.
				e.pending = now;
				e.pendingSince = m_Clock;
			}

			if (m_Clock - e.pendingSince >= m_Debounce)
			{
				e.reported = now;
				e.pending.reset();
				if (e.callback) toFire.emplace_back(e.callback, e.path);
			}
		}

		for (auto& [cb, path] : toFire)
			cb(path);
		return static_cast<int>(toFire.size());
	}
}
