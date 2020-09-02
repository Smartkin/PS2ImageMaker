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
#include <fstream>
#include <exception>
#include <vector>
#include <map>
#include <stdio.h>
struct FileTree;
struct FileTreeNode;

struct FileLocation {
	unsigned int global_sector; // starting from the top of the disc
	unsigned int local_sector; // specific to files which is their (global sector - sector number of the FileSetDescriptor)
	unsigned int lba; // starting from FileIdentifierDescriptor sector
};

class SectorManager
{
public:

	SectorManager(FileTree* ft);

	template<typename T>
	void write_sector(HANDLE f, T* data, unsigned int size = sizeof(T));
	void write_file(HANDLE out_f, HANDLE in_f, long file_size, long buffer_size);
	void pad_sector(HANDLE f, int padding_size);
	unsigned int get_total_sectors();
	long get_current_sector();
	unsigned int get_partition_start_sector();
	long get_total_files();
	long get_total_directories();
	unsigned int get_file_sector(FileTreeNode* node);
	unsigned int get_file_lba(FileTreeNode* node);
	unsigned int get_file_local_sector(FileTreeNode* node);
	unsigned int get_pad_sectors();
	std::vector<FileTreeNode*> get_directories();
	std::vector<FileTreeNode*> get_files();

private:
	void _fill_file_sectors(FileTree* ft, bool root);

private:
	long current_sector;
	long data_sector; // Sector where data starts residing
	long directories_amount;
	long files_amount;
	unsigned int total_sectors;
	unsigned int partition_start_sector;
	unsigned int pad_sectors; // Amount of pad sectors to put in the end
	std::vector<std::pair<FileTreeNode*, FileLocation>> file_sectors;
	std::vector<FileTreeNode*> directories;
	std::vector<FileTreeNode*> files;
};

template<typename T>
inline void SectorManager::write_sector(HANDLE f, T* data, unsigned int size)
{
	if (size > 2048) {
		throw std::exception("Can't write to sector more than sector's size");
	}
	
	// Write the data
	DWORD size_written = 0;
	WriteFile(f, data, size, &size_written, NULL);
	//fwrite(data, 1, size, f);
	//f.write(reinterpret_cast<char*>(data), size);


	pad_sector(f, 2048 - size);
	current_sector++;
}
