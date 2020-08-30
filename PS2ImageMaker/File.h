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
#include <string>
class File
{
public:
	File(bool is_directory, long size, const char* path, const char* ext, const char* name);
	bool IsDirectory();
	std::string GetPath();
	std::string GetExt();
	std::string GetName();
	long GetSize();

private:
	bool is_directory;
	long size;
	std::string path;
	std::string ext;
	std::string name;
};

