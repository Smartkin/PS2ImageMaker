#pragma once
#include <string>
#include <vector>
class File;
struct Progress;
struct FileTree;

struct FileTreeNode {
	FileTree* next;
	File* file;
	FileTreeNode(FileTree* next, File* file) : next(next), file(file) {}
};

struct FileTree {
	std::vector<FileTreeNode*> tree;

	long get_dir_amount();
	long get_file_amount();
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
	FileTree* get_files(Progress* pr);

private:
	std::string path;

};

