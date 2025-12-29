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
#include "SectorManager.h"
#include "Directory.h"
#include "File.h"
#include <algorithm>
#include <regex>
#include <cmath>
#include <cstring>

SectorManager::SectorManager(FileTree* ft) : current_sector(0L), data_sector(261L), total_sectors(0)
{
	auto directories = ft->get_dir_amount();
	auto files = ft->get_file_amount();
	this->directories_amount = directories + 1; // Adding 1 because root directory must be recorded as well
	this->files_amount = files;
	auto directory_records = ft->get_directory_records_amount();
	auto file_set_descriptors = 1;
	auto terminating_descriptors = 1;
	auto file_ident_descriptors = ft->get_file_identifiers_amount();
	auto file_entry_directories = this->directories_amount;
	auto file_entry_files = this->files_amount;
	partition_start_sector = 261 + directory_records;
	// Offset data sector by however many header sectors will be needed for files and directories
	data_sector += directory_records + file_set_descriptors + terminating_descriptors + file_ident_descriptors + file_entry_directories + file_entry_files;
	auto data_sectors = ft->get_files_size() / 2048;
	total_sectors = data_sector + data_sectors + 1; // End of session descriptor?
	if (total_sectors % 0x10 != 0) { // Not entirely sure why but the amount of sectors needs to be a multiple of 0x10(16)
		pad_sectors = (0x10 - total_sectors % 0x10);
		total_sectors += (0x10 - total_sectors % 0x10); // however many pad sectors and 1 extra end of session descriptor?
	}
	// Allocate the data sectors
	_fill_file_sectors(ft, true);
	// Fill directories
	for (auto p : file_sectors) {
		if (p.first->file->IsDirectory()) {
			this->directories.push_back(p.first);
		}
	}
	// Fill files
	auto cur_tree = ft;
	unsigned int dir_lba = 2 + file_ident_descriptors;
	unsigned int data_sec = this->data_sector;
	unsigned int data_lba = dir_lba + this->directories_amount;
	unsigned int file_local_sector = this->data_sector - 261 - directory_records;
	for (auto& p : file_sectors) {
		if (!p.first->file->IsDirectory()) {
			this->files.push_back(p.first);
			p.second.global_sector = data_sec;
			p.second.lba = data_lba++;
			p.second.local_sector = file_local_sector;
			auto sector_space = 0;
			if (p.first->file->GetSize() % 2048 != 0) {
				sector_space = (p.first->file->GetSize() + (2048 - p.first->file->GetSize() % 2048)) / 2048;
			}
			else {
				sector_space = p.first->file->GetSize() / 2048;
			}
			data_sec += sector_space;
			file_local_sector += sector_space;
		}
	}
}

void SectorManager::write_file(FILE* out_f, FILE* in_f, void* buf, long file_size, long buffer_size)
{
	int sectors_needed = std::ceil(file_size / 2048.0);
	current_sector += sectors_needed;
	auto write_left = file_size;

	while (write_left > 0) {
		auto write_size = buffer_size;
		if (write_size > write_left) {
			write_size = write_left;
		}
		fread(buf, 1, write_size, in_f);
		fwrite(buf, 1, write_size, out_f);
		write_left -= write_size;
	}

	if (file_size % 2048 != 0) {
		// Pad the rest to keep being aligned
		pad_sector(out_f, 2048 - file_size % 2048);
	}
}

void SectorManager::pad_sector(FILE* f, int padding_size)
{
	// Pad to align the sector
	auto pad = new char[padding_size]();
	fwrite(pad, 1, padding_size, f);
}

unsigned int SectorManager::get_total_sectors()
{
	return total_sectors;
}

long SectorManager::get_current_sector()
{
	return current_sector;
}

unsigned int SectorManager::get_partition_start_sector()
{
	return partition_start_sector;
}

long SectorManager::get_total_files()
{
	return files_amount;
}

long SectorManager::get_total_directories()
{
	return directories_amount;
}

unsigned int SectorManager::get_file_sector(FileTreeNode* node)
{
	auto vec_iter = std::find_if(file_sectors.begin(), file_sectors.end(), [node](std::pair<FileTreeNode*, FileLocation> p) {
		return p.first == node;
	});
	return (*vec_iter).second.global_sector;
}

unsigned int SectorManager::get_file_lba(FileTreeNode* node)
{
	auto vec_iter = std::find_if(file_sectors.begin(), file_sectors.end(), [node](std::pair<FileTreeNode*, FileLocation> p) {
		return p.first == node;
	});
	return (*vec_iter).second.lba;
}

unsigned int SectorManager::get_file_local_sector(FileTreeNode* node)
{
	auto vec_iter = std::find_if(file_sectors.begin(), file_sectors.end(), [node](std::pair<FileTreeNode*, FileLocation> p) {
		return p.first == node;
	});
	return (*vec_iter).second.local_sector;
}

unsigned int SectorManager::get_pad_sectors()
{
	return pad_sectors;
}

std::vector<FileTreeNode*> SectorManager::get_directories()
{
	return directories;
}

std::vector<FileTreeNode*> SectorManager::get_files()
{
	return files;
}

void SectorManager::_fill_file_sectors(FileTree* ft, bool root)
{
	for (auto node : ft->tree) {
		file_sectors.push_back(std::pair<FileTreeNode*, FileLocation>(node, FileLocation()));
		if (node->file->IsDirectory()) {
			_fill_file_sectors(node->next, false);
		}
	}
	// Need to sort the map but only by the initial caller
	if (!root) return;
	// Sort by depth and name
	std::sort(file_sectors.begin(), file_sectors.end(), [](std::pair<FileTreeNode*, FileLocation> dir1, std::pair<FileTreeNode*, FileLocation> dir2) {
		auto dir1_path = dir1.first->file->GetPath();
		auto dir2_path = dir2.first->file->GetPath();

		if (dir1.first->depth == dir2.first->depth) {
			std::regex regexp("[\\\\/]");
			std::sregex_token_iterator dir1_match(dir1_path.begin(), dir1_path.end(), regexp, -1);
			std::sregex_token_iterator dir2_match(dir2_path.begin(), dir2_path.end(), regexp, -1);
			std::sregex_token_iterator end;
			while (dir1_match != end && dir2_match != end) {
				if (strcmp((*dir1_match).str().c_str(), (*dir2_match).str().c_str()) != 0) {
					return strcmp((*dir1_match).str().c_str(), (*dir2_match).str().c_str()) < 0;
				}
				dir1_match++;
				dir2_match++;
			}
		}
		return dir1.first->depth < dir2.first->depth;
	});
	unsigned int directory_record_sector = 262;
	unsigned int dir_lba = 3 + ft->get_file_identifiers_amount(); // Directory LBA starts 2 sectors from FileSetDescriptor + 1 since we record root in code later
	for (auto& file_sector : file_sectors) {
		// If it's a directory use directory records sectors
		if (file_sector.first->file->IsDirectory()) {
			file_sector.second.global_sector = directory_record_sector;
			directory_record_sector += file_sector.first->get_directory_records_space();
			file_sector.second.lba = dir_lba++;
		}
	}
}
