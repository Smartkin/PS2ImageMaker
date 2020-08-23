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
