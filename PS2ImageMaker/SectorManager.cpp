#include "pch.h"
#include "SectorManager.h"
#include "Directory.h"

SectorManager::SectorManager(FileTree* ft) : current_sector(0L), data_sector(261L), total_sectors(0)
{
	auto directories = ft->get_dir_amount();
	auto files = ft->get_file_amount();
	this->directories = directories + 1; // Adding 1 because root directory must be recorded as well
	this->files = files;
	auto directory_records = this->directories;
	auto file_set_descriptors = 1;
	auto terminating_descriptors = 1;
	auto file_ident_descriptors = this->directories;
	auto file_entry_directories = this->directories;
	auto file_entry_files = this->files;
	partition_start_sector = 261 + directory_records;
	// Offset data sector by however many header sectors will be needed for files and directories
	data_sector += directory_records + file_set_descriptors + terminating_descriptors + file_ident_descriptors + file_entry_directories + file_entry_files;
	auto data_sectors = ft->get_files_size() / 2048;
	total_sectors = data_sector + data_sectors;
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
	return directories;
}
