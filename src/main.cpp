#include "core/App.h"
#include "utils/Logger.h"
#include "utils/Env.h"
#include <chrono>
#include <iomanip>
#include <sstream>

// Workaround for Visual C++ FMA3/AVX illegal instruction bug
extern "C" int _set_FMA3_enable(int flag);

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
	// Enable console immediately for debugging
	AllocConsole();
	freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
	freopen_s((FILE**)stderr, "CONOUT$", "w", stderr);

	// Set up file logging immediately
	std::string logFile = Env::GetString("PLASMADX_LOG_FILE");
	if (logFile.empty()) {
		// Create logs directory if it doesn't exist
		CreateDirectoryA("logs", nullptr);

		// Generate timestamped filename
		auto now = std::chrono::system_clock::now();
		auto time_t = std::chrono::system_clock::to_time_t(now);
		auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

		std::ostringstream oss;
		oss << "logs/plasmadx_"
		    << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S")
		    << "_" << std::setfill('0') << std::setw(3) << ms.count() << ".log";
		logFile = oss.str();
	}
	SetLogFile(logFile);

	// CRITICAL FIX: Disable FMA3 to prevent illegal instruction errors on Ryzen 5950X
	_set_FMA3_enable(0);
	LOGI("=== PlasmaDX Starting (FMA3 disabled for CPU compatibility) ===");

	try {
		App app;
		LOGI("App created, initializing...");
		if (!app.initialize(hInst, nCmdShow)) {
			LOGE("Initialization failed!");
			MessageBoxA(nullptr, "Failed to initialize application", "Error", MB_OK);
			return -1;
		}
		LOGI("App initialized, starting run loop...");
		int result = app.run();
		LOGI("App run completed with result: " + std::to_string(result));
		return result;
	} catch (const std::exception& e) {
		LOGE(std::string("Exception caught: ") + e.what());
		MessageBoxA(nullptr, e.what(), "Exception", MB_OK);
		return -1;
	} catch (...) {
		LOGE("Unknown exception caught");
		MessageBoxA(nullptr, "Unknown exception occurred", "Exception", MB_OK);
		return -1;
	}
}
