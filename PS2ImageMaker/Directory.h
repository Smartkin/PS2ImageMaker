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
#include <string>
#include <vector>
class File;
struct Progress;
struct FileTree;

struct FileTreeNode {
	FileTree* next;
	File* file;
	FileTreeNode* parent;
	int links; // Amount of links to another directories
	int depth;
	FileTreeNode(FileTree* next, FileTreeNode* parent, File* file) : next(next), parent(parent), file(file), depth(0), links(1) {}
};

struct FileTree {
	std::vector<FileTreeNode*> tree;

	long get_dir_amount();
	long get_file_amount();
	long get_content_amount();
	long get_dir_links();
	unsigned int get_files_size();

private:
	void _get_dir_amount(FileTreeNode* node, long& amount);
	void _get_file_amount(FileTreeNode* node, long& amount);
	void _get_files_size(FileTreeNode* node, unsigned int& size);
};

class Directory
{
public:
	Directory(const char* path);
	FileTree* get_files();

private:
	std::string path;

};

