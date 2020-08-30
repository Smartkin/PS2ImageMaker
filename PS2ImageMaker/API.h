/*
PS2ImageMaker - Library for creating Playstation 2 (PS2)compatible images
Copyright(C) 2020 Vladislav Smyshlyaev(Smartkin)

This program is free software : you can redistribute it and /or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.If not, see < https://www.gnu.org/licenses/>.
*/

#pragma once
#ifndef DLLEXPORT
#define DLLEXPORT __declspec(dllexport)
#endif

enum ProgressState {
	FAILED = -1,
	ENUM_FILES,
	WRITE_SECTORS,
	WRITE_FILES,
	WRITE_END,
	FINISHED,
};

extern "C" struct DLLEXPORT Progress {
	char file_name[256];
	int size;
	ProgressState state;
	float progress;
	bool finished;
	bool new_state;
	bool new_file;
};

extern "C" DLLEXPORT Progress* start_packing(const char* game_path, const char* dest_path);

extern "C" DLLEXPORT void set_file_buffer(unsigned int buffer_size);

extern "C" DLLEXPORT Progress* poll_progress();

void update_progress(ProgressState message, float progress, const char* file_name = "", bool finished = false);

extern Progress program_progress;
extern Progress progress_copy;
extern bool progress_dirty;
extern char game_path[1024];
extern char dest_path[1024];
extern unsigned int buffer_size;