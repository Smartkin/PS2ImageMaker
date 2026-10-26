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

// Regression tests of the image's end, run as PS2ImageMakerRegressionTest <work folder> (ctest gives it one).
// The pad sectors before the end of session descriptor were only counted when the image's sectors weren't a multiple of 16 already, the
// count was whatever the packing thread's stack held otherwise: zero sectors were written until the memory ran out (each leaked 2 KB) or a
// few too many came before the descriptor, so images of the same folder differed. glibc gives a new thread the stack of one that ended, so
// every packing here gets one full of 0xA5 bytes

#include <ps2imagemaker.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

namespace {

const long SectorSize = 2048;
// One folder of one file: 261 sectors before the directories, the root's records, the file set and its terminator, the root's identifiers,
// the root's and the file's entries, then the file
const long FirstDataSector = 267;
int failures = 0;

void check(bool condition, const std::string& what)
{
	if (!condition) {
		std::printf("FAILED: %s\n", what.c_str());
		++failures;
	}
}

void make_folder(const std::string& path)
{
#ifdef _WIN32
	_mkdir(path.c_str());
#else
	mkdir(path.c_str(), 0755);
#endif
}

unsigned char data_byte(long index)
{
	return (unsigned char)(index * 7 + 3);
}

void write_data(const std::string& path, long size)
{
	std::vector<unsigned char> bytes(size);
	for (long i = 0; i < size; ++i) {
		bytes[i] = data_byte(i);
	}
	FILE* file = std::fopen(path.c_str(), "wb");
	if (file == nullptr || std::fwrite(bytes.data(), 1, bytes.size(), file) != bytes.size()) {
		std::printf("Can't write %s\n", path.c_str());
		std::exit(1);
	}
	std::fclose(file);
}

std::vector<unsigned char> read_file(const std::string& path)
{
	std::vector<unsigned char> bytes;
	FILE* file = std::fopen(path.c_str(), "rb");
	if (file == nullptr) {
		return bytes;
	}
	std::vector<unsigned char> buffer(1 << 16);
	size_t read;
	while ((read = std::fread(buffer.data(), 1, buffer.size(), file)) > 0) {
		bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + read);
	}
	std::fclose(file);
	return bytes;
}

long file_size(const std::string& path)
{
	FILE* file = std::fopen(path.c_str(), "rb");
	if (file == nullptr) {
		return -1;
	}
	std::fseek(file, 0, SEEK_END);
	long size = std::ftell(file);
	std::fclose(file);
	return size;
}

unsigned int read_u32(const std::vector<unsigned char>& bytes, size_t offset)
{
	return bytes[offset] | bytes[offset + 1] << 8 | bytes[offset + 2] << 16 | (unsigned int)bytes[offset + 3] << 24;
}

// A thread that ends with the top of its stack full of 0xA5 bytes (where the next thread's first frames go): the next thread starts on that
// stack. Less than Windows' 1 MB of stack, whose threads get stacks of their own
void leave_dirty_stack()
{
	std::thread([] {
		const size_t size = 256 << 10;
		volatile unsigned char block[size];
		for (size_t i = 0; i < size; ++i) {
			block[i] = 0xA5;
		}
	}).join();
}

// Packs the folder and waits for it. An image growing past the expected size is a runaway, ended with the process before it takes the memory
Progress pack(const std::string& folder, const std::string& image, long expected_bytes)
{
	leave_dirty_stack();
	start_packing(folder.c_str(), image.c_str());
	for (int i = 0; i < 6000; ++i) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		Progress progress = *poll_progress();
		if (progress.finished || progress.state == FAILED) {
			return progress;
		}
		long size = file_size(image);
		if (expected_bytes > 0 && size > expected_bytes + 64 * SectorSize) {
			std::printf("FAILED: the image of %s ran away: %ld bytes and growing, %ld expected\n", folder.c_str(), size, expected_bytes);
			std::fflush(stdout);
			std::_Exit(2);
		}
	}
	std::printf("FAILED: the image of %s took more than a minute\n", folder.c_str());
	std::fflush(stdout);
	std::_Exit(2);
}

// Two images of a folder of one file: the file, the pad sectors, the end of session descriptor last, 272 sectors, the same bytes both times
void check_image(const std::string& work, const std::string& name, long data_bytes, long pad_sectors)
{
	const long expected_sectors = 272;
	const std::string folder = work + "/" + name;
	make_folder(folder);
	write_data(folder + "/DATA.BIN", data_bytes);
	std::vector<unsigned char> first;
	for (int run = 0; run < 2; ++run) {
		const std::string image = work + "/" + name + "_" + std::to_string(run) + ".iso";
		Progress progress = pack(folder, image, expected_sectors * SectorSize);
		check(progress.state == FINISHED && progress.finished, name + ": the packing didn't finish");
		std::vector<unsigned char> bytes = read_file(image);
		if (bytes.size() != (size_t)(expected_sectors * SectorSize)) {
			check(false, name + ": the image has " + std::to_string(bytes.size()) + " bytes, not " + std::to_string(expected_sectors * SectorSize));
			continue;
		}

		check(read_u32(bytes, 16 * SectorSize + 80) == expected_sectors, name + ": the volume's size isn't the image's");
		bool data = true;
		for (long i = 0; i < data_bytes; ++i) {
			data &= bytes[FirstDataSector * SectorSize + i] == data_byte(i);
		}
		check(data, name + ": the file's data isn't where its entry says");
		const long last = expected_sectors - 1;
		bool zeros = true;
		for (long i = FirstDataSector * SectorSize + data_bytes; i < last * SectorSize; ++i) {
			zeros &= bytes[i] == 0;
		}
		check(zeros, name + ": what's between the file and the end of session descriptor isn't zeros");
		check(last - (FirstDataSector + (data_bytes + SectorSize - 1) / SectorSize) == pad_sectors, name + ": the pad sectors aren't counted");
		// The descriptor's tag: identifier 2, version 2, its own sector
		check(bytes[last * SectorSize] == 2 && bytes[last * SectorSize + 2] == 2 && read_u32(bytes, last * SectorSize + 12) == last,
			name + ": the last sector isn't the end of session descriptor");
		if (run == 0) {
			first = bytes;
		}
		else {
			check(bytes == first, name + ": two images of the same folder differ");
		}
	}
}

}

int main(int argc, char** argv)
{
	if (argc < 2) {
		std::printf("Usage: %s <work folder>\n", argv[0]);
		return 1;
	}
	const std::string work = argv[1];
	make_folder(work);

	// 267 sectors, 4 of the file and the descriptor: a multiple of 16, no pad sectors
	check_image(work, "sixteen", 4 * SectorSize - 192, 0);
	// 267 sectors, 1 of the file and the descriptor: 3 pad sectors
	check_image(work, "padded", 100, 3);

	Progress missing = pack(work + "/nothing here", work + "/nothing.iso", 0);
	check(missing.state == FAILED && missing.finished, "a folder that isn't there didn't fail");

	if (failures == 0) {
		std::printf("All passed\n");
	}
	return failures == 0 ? 0 : 1;
}
