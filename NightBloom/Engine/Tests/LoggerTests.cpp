//------------------------------------------------------------------------------
// LoggerTests.cpp
//
// Unit tests for logging system
// Copyright (c) 2024 Your Name. All rights reserved.
//------------------------------------------------------------------------------

#include <gtest/gtest.h>
#include <sstream>
#include "../Core/Logger/Logger.hpp"

namespace Nightbloom
{
	class TestLogSink : public ILogSink
	{
	public:
		virtual void Write(LogLevel level, const std::string& message) override
		{
			m_LastLevel = level;
			m_LastMessage = message;
			m_MessageCount++;
		}

		LogLevel m_LastLevel = LogLevel::None;
		std::string m_LastMessage;
		int m_MessageCount = 0;
	};

	class LoggerTest : public ::testing::Test
	{
	protected:
		void SetUp() override
		{
			// Clear any existing sinks and set up a test sink
			Logger::Get().ClearSinks();

			// Create a test sink to capture log messages
			m_TestSink = std::make_shared<TestLogSink>();
			Logger::Get().AddSink(m_TestSink);
			Logger::Get().SetLogLevel(LogLevel::Trace);
		}

		void TearDown() override
		{
			Logger::Get().ClearSinks();
		}

		std::shared_ptr<TestLogSink> m_TestSink;
	};

	TEST_F(LoggerTest, BasicLogging)
	{
		LOG_INFO("Test message");

		EXPECT_EQ(m_TestSink->m_LastLevel, LogLevel::Info);
		EXPECT_TRUE(m_TestSink->m_LastMessage.find("Test message") != std::string::npos);
		EXPECT_EQ(m_TestSink->m_MessageCount, 1);
	}

	TEST_F(LoggerTest, LogLevels)
	{
		Logger::Get().SetLogLevel(LogLevel::Warn);

		LOG_TRACE("Should not appear");
		LOG_DEBUG("Should not appear");
		LOG_INFO("Should not appear");

		EXPECT_EQ(m_TestSink->m_MessageCount, 0);

		LOG_WARN("Should appear");
		EXPECT_EQ(m_TestSink->m_MessageCount, 1);

		LOG_ERROR("Should also appear");
		EXPECT_EQ(m_TestSink->m_MessageCount, 2);
	}

	TEST_F(LoggerTest, Formatting)
	{
		int value = 42;
		float pi = 3.14f;

		LOG_INFO("Value: {}, Pi: {:.2f}", value, pi);

		EXPECT_TRUE(m_TestSink->m_LastMessage.find("Value: 42") != std::string::npos);
		EXPECT_TRUE(m_TestSink->m_LastMessage.find("Pi: 3.14") != std::string::npos);
	}
}