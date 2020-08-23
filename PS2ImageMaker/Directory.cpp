#include <Windows.h>
#include <vector>
#include "pch.h"
#include "Directory.h"
#include "File.h"
#include "API.h"

void enumerate_files_recursively(FileTree* ft, std::string path);
void update_progress_message(Progress* pr, const char* message);

Directory::Directory(const char* path) : path(path) {}


FileTree* Directory::get_files(Progress* pr) {
	WIN32_FIND_DATAA file_info;
	FileTree* ft = new FileTree();
	path.append("/*");
	auto find_handle = FindFirstFileA(path.c_str(), &file_info);
	if (find_handle != INVALID_HANDLE_VALUE) {
		FindClose(find_handle);
		update_progress_message(pr, "Enumerating files...");
		enumerate_files_recursively(ft, path);
		return ft;
	}
	else { // No files or directories found
		update_progress_message(pr, "No files found");
		return nullptr;
	}
}

void enumerate_files_recursively(FileTree* ft, std::string path) {
	WIN32_FIND_DATAA file_info;
	auto find_handle = FindFirstFileA(path.c_str(), &file_info);
	if (find_handle != INVALID_HANDLE_VALUE) {
		path.pop_back();
		do {
			// Skip the navigation directories
			if (!strcmp(file_info.cFileName, ".") || !strcmp(file_info.cFileName, "..")) continue;

			auto pth = path;

			pth.append(file_info.cFileName);
			auto file = new File(file_info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY, file_info.nFileSizeLow,
				pth.c_str(), "", file_info.cFileName);
			auto node = new FileTreeNode(nullptr, file);
			if (file->IsDirectory()) {
				node->next = new FileTree();
				auto str = file->GetPath().append("/*");
				enumerate_files_recursively(node->next, str);
			}
			ft->tree.push_back(node);
		} while (FindNextFileA(find_handle, &file_info));
		FindClose(find_handle);
	}
}

long FileTree::get_dir_amount()
{
	long amount = 0L;
	for (auto node : this->tree) {
		if (node->file->IsDirectory()) {
			++amount;
			_get_dir_amount(node, amount);
		}
	}
	return amount;
}

long FileTree::get_file_amount()
{
	long amount = 0L;
	for (auto node : this->tree) {
		if (node->file->IsDirectory()) {
			_get_file_amount(node, amount);
		}
		else {
			++amount;
		}
	}
	return amount;
}

unsigned int FileTree::get_files_size()
{
	unsigned int size = 0U;
	for (auto node : this->tree) {
		if (node->file->IsDirectory()) {
			_get_files_size(node, size);
		}
		else {
			size += node->file->GetSize() + (2048 - node->file->GetSize() % 2048);
		}
	}
	return size + (2048 - size % 2048); // Align the size to the sectors
}

void FileTree::_get_dir_amount(FileTreeNode* node, long& amount)
{
	for (auto node : node->next->tree) {
		if (node->file->IsDirectory()) {
			++amount;
			_get_dir_amount(node, amount);
		}
	}
}

void FileTree::_get_file_amount(FileTreeNode* node, long& amount)
{
	for (auto node : node->next->tree) {
		if (node->file->IsDirectory()) {
			_get_file_amount(node, amount);
		}
		else {
			++amount;
		}
	}
}

void FileTree::_get_files_size(FileTreeNode* node, unsigned int& size)
{
	for (auto node : node->next->tree) {
		if (node->file->IsDirectory()) {
			_get_files_size(node, size);
		}
		else {
			size += node->file->GetSize() + (2048 - node->file->GetSize() % 2048);
		}
	}
}
