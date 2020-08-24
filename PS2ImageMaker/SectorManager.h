#pragma once
#include <fstream>
#include <exception>
struct FileTree;

class SectorManager
{
public:

	SectorManager(FileTree* ft);

	template<typename T>
	void write_sector(std::ofstream& f, T* data, int size);
	void write_file(std::ofstream& f, std::filebuf* buf, long file_size);
	void pad_sector(std::ofstream& f, int padding_size);
	unsigned int get_total_sectors();
	long get_current_sector();
	unsigned int get_partition_start_sector();
	long get_total_files();
	long get_total_directories();

private:
	long current_sector;
	long data_sector; // Sector where data starts residing
	long directories;
	long files;
	unsigned int total_sectors;
	unsigned int partition_start_sector;
};

template<typename T>
inline void SectorManager::write_sector(std::ofstream& f, T* data, int size)
{
	if (size > 2048) {
		throw std::exception("Can't write to sector more than sector's size");
	}
	
	// Write the data
	f.write(reinterpret_cast<char*>(data), size);

	pad_sector(f, 2048 - size);
	current_sector++;
}
