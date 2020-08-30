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

#include "pch.h"
#include "File.h"

File::File(bool is_directory, long size, const char* path, const char* ext, const char* name) :
	is_directory(is_directory), size(size), path(path), ext(ext), name(name)
{}

bool File::IsDirectory()
{
	return is_directory;
}

std::string File::GetPath()
{
	return path;
}

std::string File::GetExt()
{
	return ext;
}

std::string File::GetName()
{
	return name;
}

long File::GetSize()
{
	return size;
}

unsigned int File::GetSectorsSpace()
{
	auto aligned_size = 0;
	if (size % 2048 != 0) {
		aligned_size = size + (2048 - size % 2048); // Align the size to the sectors
	}
	else {
		aligned_size = size;
	}
	return aligned_size / 2048;
}
