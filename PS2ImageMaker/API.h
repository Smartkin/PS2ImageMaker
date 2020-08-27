#pragma once
#ifndef DLLEXPORT
#define DLLEXPORT __declspec(dllexport)
#endif

extern "C" struct DLLEXPORT BasicString {
	char str[256];
	int size;
};

enum ProgressState {
	FAILED = -1,
	ENUM_FILES,
	WRITE_SECTORS,
	WRITE_FILES,
	WRITE_END,
	FINISHED,
};

extern "C" struct DLLEXPORT Progress {
	BasicString current_file;
	ProgressState state;
	float progress;
	bool finished;
	bool new_state;
	bool new_file;
};

extern "C" DLLEXPORT Progress& start_packing(const char* game_path, const char* dest_path);

extern "C" DLLEXPORT Progress& poll_progress();

void update_progress(ProgressState message, float progress, const char* file_name = "", bool finished = false);

extern Progress program_progress;
extern Progress progress_copy;
extern bool progress_dirty;