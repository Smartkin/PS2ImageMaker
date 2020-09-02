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
#include <Windows.h>
#include <vector>
#include <cmath>
#include "Directory.h"
#include "SectorDescriptors.h"
#include "File.h"
#include "API.h"

void enumerate_files_recursively(FileTree* ft, FileTreeNode* parent, std::string path, int depth = 0);

Directory::Directory(const char* path) : path(path) {}


FileTree* Directory::get_files() {
	WIN32_FIND_DATAA file_info;
	FileTree* ft = new FileTree();
	path.append("/*");
	auto find_handle = FindFirstFileA(path.c_str(), &file_info);
	if (find_handle != INVALID_HANDLE_VALUE) {
		FindClose(find_handle);
		enumerate_files_recursively(ft, nullptr, path);
		return ft;
	}
	else { // No files or directories found
		return nullptr;
	}
}

void enumerate_files_recursively(FileTree* ft, FileTreeNode* parent, std::string path, int depth) {
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
			auto node = new FileTreeNode(nullptr, parent, file);
			node->depth = depth;
			if (file->IsDirectory()) {
				
				node->next = new FileTree();
				auto str = file->GetPath().append("/*");
				enumerate_files_recursively(node->next, node, str, depth + 1);
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

long FileTree::get_content_amount()
{
	return this->tree.size();
}

long FileTree::get_dir_links()
{
	long amount = 1;
	for (auto node : this->tree) {
		amount += node->file->IsDirectory();
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
			if (node->file->GetSize() % 2048 != 0) {
				size += node->file->GetSize() + (2048 - node->file->GetSize() % 2048);
			}
			else {
				size += node->file->GetSize();
			}
		}
	}
	if (size % 2048 != 0) {
		return size + (2048 - size % 2048); // Align the size to the sectors
	}
	else {
		return size;
	}
}

unsigned int FileTree::get_directory_records_amount()
{
	unsigned int amount = 0;
	auto dir_rec_len = 0x60;
	for (auto node : this->tree) {
		if (node->file->IsDirectory()) {
			_get_directory_records_amount(node, amount);
		}
		dir_rec_len += node->file->GetName().size() + (node->file->IsDirectory() ? 0x30 : 0x32) - node->file->GetName().size() % 2;
	}
	amount += std::ceil(dir_rec_len / 2048.0);
	return amount;
}

unsigned int FileTree::get_file_identifiers_amount()
{
	unsigned int amount = 0;
	auto file_ident_len = sizeof(FileIdentifierDescriptor);
	for (auto node : this->tree) {
		if (node->file->IsDirectory()) {
			_get_file_identifiers_amount(node, amount);
		}
		auto file_name_size = node->file->GetName().size();
		auto file_str_len = file_name_size * 2 + 1;
		file_ident_len += sizeof(FileIdentifierDescriptor) - 1 + file_str_len + (file_name_size % 2) * 2;
	}
	amount += std::ceil(file_ident_len / 2048.0);
	return amount;
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
			if (node->file->GetSize() % 2048 != 0) {
				size += node->file->GetSize() + (2048 - node->file->GetSize() % 2048);
			}
			else {
				size += node->file->GetSize();
			}
		}
	}
}

void FileTree::_get_directory_records_amount(FileTreeNode* node, unsigned int& amount)
{
	auto dir_rec_len = 0x60;
	for (auto node : node->next->tree) {
		if (node->file->IsDirectory()) {
			_get_directory_records_amount(node, amount);
		}
		dir_rec_len += node->file->GetName().size() + (node->file->IsDirectory() ? 0x30 : 0x32) - node->file->GetName().size() % 2;
	}
	amount += std::ceil(dir_rec_len / 2048.0);
}

void FileTree::_get_file_identifiers_amount(FileTreeNode* node, unsigned int& amount)
{
	auto file_ident_len = sizeof(FileIdentifierDescriptor);
	for (auto node : node->next->tree) {
		if (node->file->IsDirectory()) {
			_get_file_identifiers_amount(node, amount);
		}
		auto file_name_size = node->file->GetName().size();
		auto file_str_len = file_name_size * 2 + 1;
		file_ident_len += sizeof(FileIdentifierDescriptor) - 1 + file_str_len + (file_name_size % 2) * 2;
	}
	amount += std::ceil(file_ident_len / 2048.0);
}

unsigned int FileTreeNode::get_directory_records_space()
{
	unsigned int space = 0;
	if (this->file->IsDirectory()) {
		space = 0x60;
		for (auto node : this->next->tree) {
			space += node->file->GetName().size() + (node->file->IsDirectory() ? 0x30 : 0x32) - node->file->GetName().size() % 2;
		}
	}
	return std::ceil(space / 2048.0);
}
