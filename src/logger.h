#pragma once
#include <string>
#include <fstream>

namespace grpc_labview 
{
	namespace logger 
	{
		enum class Level { Debug = 0, Info = 1, Error = 2 };

		void InitializeLoggerFromEnv(); // reads GRPC_LV_LOG_FILE and GRPC_LV_LOG_DEBUG
		void ShutdownLogger();

		void Log(Level lvl, const char* msg);
		inline void LogDebug(const char* msg) { Log(Level::Debug, msg); }
		inline void LogInfo(const char* msg)  { Log(Level::Info,  msg); }
		inline void LogError(const char* msg) { Log(Level::Error, msg); }

	} // namespace logger
} // namespace grpc_labview