#include "core/App.h"
#include "utils/Logger.h"
#include "utils/Env.h"

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
		logFile = "PlasmaDX.log";
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
