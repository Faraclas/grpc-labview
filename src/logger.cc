#include "logger.h"
#include <fstream>
#include <mutex>
#include <ctime>
#include <sstream>
#include <cstdlib>
#include <cctype>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

namespace grpc_labview 
{
    namespace logger 
    {

        static std::mutex g_lock;
        static std::ofstream g_ofs;
        //static bool g_debugOutput = false;
        static Level g_minLevel = Level::Debug;

        static const char* levelToString(Level l) {
            switch (l) {
            case Level::Debug: return "DEBUG";
            case Level::Info:  return "INFO";
            case Level::Error: return "ERROR";
            }
            return "UNK";
        }

        void InitializeLoggerFromEnv()
        {
            //std::lock_guard<std::mutex> lk(g_lock);
            const char* path = std::getenv("GRPC_LV_LOG_FILE");
            const char* dbg  = std::getenv("GRPC_LV_LOG_DEBUG");
            const char* lvl  = std::getenv("GRPC_LV_LOG_LEVEL");

            // determine level
            if (lvl) {
                std::string s(lvl);
                for (auto &c : s) c = (char)std::tolower((unsigned char)c);
                if (s == "info") g_minLevel = Level::Info;
                else if (s == "error") g_minLevel = Level::Error;
                else g_minLevel = Level::Debug;
            }

            // ensure parent directory exists when a path is provided
            if (path && *path) {
                try {
                    std::filesystem::path p(path);
                    if (p.has_parent_path()) {
                        std::filesystem::create_directories(p.parent_path());
                    }
                    g_ofs.open(path, std::ios::out | std::ios::app);
                    Log(Level::Debug, "we're in!!");
                } catch (const std::exception& e) {
                    Log(Level::Debug, "exception I guess");
                    // fallback: record error to stderr
                    std::string msg = std::string("logger: failed to open log file: ") + e.what();
                    fwrite(msg.c_str(), 1, msg.size(), stderr);
                }
            }
            Log(Level::Debug, "created the stuff");
            if (dbg && (*dbg == '1' || *dbg == 'y' || *dbg == 'Y'))
            {
                //g_debugOutput = true;
                Log(Level::Debug, "dbg thing worked");
            }

            // Emit a startup message so we can verify logging works
            LogInfo("Logger initialized");
            Log(Level::Debug, "startup done been done");
        }

        void ShutdownLogger()
        {
            //std::lock_guard<std::mutex> lk(g_lock);
            if (g_ofs.is_open()) g_ofs.close();
            //g_debugOutput = false;
        }

        void Log(Level lvl, const char* msg)
        {
            if (lvl < g_minLevel) return;
            //std::lock_guard<std::mutex> lk(g_lock);

            // timestamp
            std::time_t t = std::time(nullptr);
            std::tm tm;
        #if defined(_MSC_VER)
            localtime_s(&tm, &t);
        #else
            localtime_r(&t, &tm);
        #endif
            char ts[32];
            std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);

            std::ostringstream ss;
            ss << ts << " [" << levelToString(lvl) << "] " << msg << "\n";
            std::string out = ss.str();

            if (g_ofs.is_open()) {
                g_ofs << out;
                g_ofs.flush();
            }

        #ifdef _WIN32
            /*if (g_debugOutput) {
                OutputDebugStringA(out.c_str());
            }*/
        #endif

            // fallback to stderr if nothing configured
            /*if (!g_debugOutput && !g_ofs.is_open()) {
                fwrite(out.c_str(), 1, out.size(), stderr);
                fflush(stderr);
            }*/
        }

    } // namespace logger
} // namespace grpc_labview