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
#include <string>
#include <vector>
#include <map>
#include <stdio.h>

struct FileTree;
struct FileTreeNode;

struct FileLocation {
	unsigned int global_sector; // starting from the top of the disc
	unsigned int local_sector; // specific to files which is their global sector - sector number of the FileSetDescriptor
	unsigned int lba; // starting from FileIdentifierDescriptor sector
};

class ImageMakerException : public std::exception
{
public:
	ImageMakerException(const std::string& message, const std::string& file = "") : message(message), file(file) {}

	const char* what() const noexcept override
	{
		return message.c_str();
	}

	// The file the packing stopped at, for the progress, none when it's the image
	const std::string& get_file() const noexcept
	{
		return file;
	}

private:
	std::string message;
	std::string file;
};

class SectorManager
{
public:

	SectorManager(FileTree* ft);

	template<typename T>
	void write_sector(FILE* f, T* data, unsigned int size = sizeof(T));
	// False when the input file has fewer bytes than its size
	bool write_file(FILE* out_f, FILE* in_f, void* buf, long file_size, long buffer_size);
	void pad_sector(FILE* f, int padding_size);
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
	// The image ends at the sectors its descriptors count: writing past them is a miscount, refused
	void claim_sectors(long count);
	void write_bytes(FILE* f, const void* data, size_t size);

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
inline void SectorManager::write_sector(FILE* f, T* data, unsigned int size)
{
	if (size > 2048) {
		throw ImageMakerException("Can't write to sector more than sector's size");
	}

	claim_sectors(1);
	write_bytes(f, data, size);
	pad_sector(f, 2048 - size);
}
