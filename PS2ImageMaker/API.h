#pragma once
#ifndef DLLEXPORT
#define DLLEXPORT __declspec(dllexport)
#endif

extern "C" struct DLLEXPORT BasicString {
	char str[256];
	int size;
};

extern "C" struct DLLEXPORT Progress {
	BasicString current_file;
	float progress;
	bool finished;
	BasicString message;
	bool new_message;
};

extern "C" DLLEXPORT Progress* start_packing(const char* game_path, const char* dest_path);

void update_progress_message(Progress* pr, const char* message);