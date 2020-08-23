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

