#pragma once
#include <fstream>
#include <exception>
#include <vector>
#include <map>
struct FileTree;
struct FileTreeNode;

struct FileLocation {
	unsigned int global_sector; // starting from the top of the disc
	unsigned int local_sector; // specific to files which is their global sector - sector number of the FileSetDescriptor
	unsigned int lba; // starting from FileIdentifierDescriptor sector
};

class SectorManager
{
public:

	SectorManager(FileTree* ft);

	template<typename T>
	void write_sector(std::ofstream& f, T* data, unsigned int size = sizeof(T));
	void write_file(std::ofstream& f, std::filebuf* buf, long file_size);
	void pad_sector(std::ofstream& f, int padding_size);
	unsigned int get_total_sectors();
	long get_current_sector();
	unsigned int get_partition_start_sector();
	long get_total_files();
	long get_total_directories();
	unsigned int get_file_sector(FileTreeNode* node);
	unsigned int get_file_lba(FileTreeNode* node);
	unsigned int get_file_local_sector(FileTreeNode* node);
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
	std::vector<std::pair<FileTreeNode*, FileLocation>> file_sectors;
	std::vector<FileTreeNode*> directories;
	std::vector<FileTreeNode*> files;
};

template<typename T>
inline void SectorManager::write_sector(std::ofstream& f, T* data, unsigned int size)
{
	if (size > 2048) {
		throw std::exception("Can't write to sector more than sector's size");
	}
	
	// Write the data
	f.write(reinterpret_cast<char*>(data), size);


	pad_sector(f, 2048 - size);
	current_sector++;
}
