#include "pch.h"
#include "SectorManager.h"
#include "Directory.h"
#include "File.h"
#include <algorithm>

SectorManager::SectorManager(FileTree* ft) : current_sector(0L), data_sector(261L), total_sectors(0)
{
	auto directories = ft->get_dir_amount();
	auto files = ft->get_file_amount();
	this->directories_amount = directories + 1; // Adding 1 because root directory must be recorded as well
	this->files = files;
	auto directory_records = this->directories_amount;
	auto file_set_descriptors = 1;
	auto terminating_descriptors = 1;
	auto file_ident_descriptors = this->directories_amount;
	auto file_entry_directories = this->directories_amount;
	auto file_entry_files = this->files;
	partition_start_sector = 261 + directory_records;
	// Offset data sector by however many header sectors will be needed for files and directories
	data_sector += directory_records + file_set_descriptors + terminating_descriptors + file_ident_descriptors + file_entry_directories + file_entry_files;
	auto data_sectors = ft->get_files_size() / 2048;
	total_sectors = data_sector + data_sectors;
	// Allocate the data sectors
	_fill_file_sectors(ft, true);
	// Fill with good stuff
	for (auto p : file_sectors) {
		if (p.first->file->IsDirectory()) {
			this->directories.push_back(p.first);
		}
	}
}

void SectorManager::write_file(std::ofstream& f, std::filebuf* buf, long file_size)
{
	int sectors_needed = std::ceil(file_size / 2048.0);
	current_sector += sectors_needed;

	f << buf;

	// Pad the rest to keep being aligned
	pad_sector(f, 2048 - file_size % 2048);
}

void SectorManager::pad_sector(std::ofstream& f, int padding_size)
{
	// Pad to align the sector
	auto pad = '\0';
	auto leftover = padding_size;
	for (int i = 0; i < leftover; ++i) {
		f << pad;
	}
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
	return files;
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

std::vector<FileTreeNode*> SectorManager::get_directories()
{
	return directories;
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
	// Sort by depth
	std::sort(file_sectors.begin(), file_sectors.end(), [](std::pair<FileTreeNode*, FileLocation> f1, std::pair<FileTreeNode*, FileLocation>  f2) {
		return f1.first->depth < f2.first->depth;
	});
	unsigned int directory_record_sector = 262;
	unsigned int dir_lba = 8;
	unsigned int data_sec = data_sector;
	unsigned int data_lba = 6 + this->directories_amount;
	for (auto& file_sector : file_sectors) {
		// If it's a directory use directory records sectors
		if (file_sector.first->file->IsDirectory()) {
			file_sector.second.global_sector = directory_record_sector++;
			file_sector.second.lba = dir_lba++;
		}
		else {
			file_sector.second.global_sector = data_sec;
			file_sector.second.lba = data_lba++;
			data_sec += file_sector.first->file->GetSize() / 2048 + (file_sector.first->file->GetSize() % 2048 == 0 ? 0 : 1);
		}
	}
}
