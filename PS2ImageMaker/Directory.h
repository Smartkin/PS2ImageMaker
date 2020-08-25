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
	int depth;
	FileTreeNode(FileTree* next, FileTreeNode* parent, File* file) : next(next), parent(parent), file(file), depth(0) {}
};

struct FileTree {
	std::vector<FileTreeNode*> tree;

	long get_dir_amount();
	long get_file_amount();
	long get_content_amount();
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

