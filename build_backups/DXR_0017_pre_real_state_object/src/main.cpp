#include "core/App.h"

// Workaround for Visual C++ FMA3/AVX illegal instruction bug
extern "C" int _set_FMA3_enable(int flag);

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
	// Enable console immediately for debugging
	AllocConsole();
	freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
	freopen_s((FILE**)stderr, "CONOUT$", "w", stderr);

	// CRITICAL FIX: Disable FMA3 to prevent illegal instruction errors on Ryzen 5950X
	_set_FMA3_enable(0);
	printf("=== PlasmaDX Starting (FMA3 disabled for CPU compatibility) ===\n");

	try {
		App app;
		printf("App created, initializing...\n");
		if (!app.initialize(hInst, nCmdShow)) {
			printf("Initialization failed!\n");
			MessageBoxA(nullptr, "Failed to initialize application", "Error", MB_OK);
			return -1;
		}
		printf("App initialized, starting run loop...\n");
		int result = app.run();
		printf("App run completed with result: %d\n", result);
		return result;
	} catch (const std::exception& e) {
		printf("Exception caught: %s\n", e.what());
		MessageBoxA(nullptr, e.what(), "Exception", MB_OK);
		return -1;
	} catch (...) {
		printf("Unknown exception caught\n");
		MessageBoxA(nullptr, "Unknown exception occurred", "Exception", MB_OK);
		return -1;
	}
}
