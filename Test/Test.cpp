// Test.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <API.h>
#include <cassert>

int main()
{
    auto disc_folder_path = "Put folder path here";
    auto iso_name = "compiled.iso"; // Can be an iso name to create locally or a path
    assert(strcmp("Put folder path here", disc_folder_path) != 0);
    Progress& pr = start_packing(disc_folder_path, iso_name);
    do {
        Progress& pr = poll_progress();
        bool write_progress = false;
        if (pr.new_state) {
            switch (pr.state)
            {
            case ProgressState::ENUM_FILES:
                std::cout << "Enumerating files..." << "\n";
                break;
            case ProgressState::FAILED:
                std::cout << "An error occured" << "\n";
                break;
            case ProgressState::WRITE_FILES:
                std::cout << "Writing files..." << "\n";
                break;
            case ProgressState::WRITE_SECTORS:
                std::cout << "Writing sectors..." << "\n";
                break;
            case ProgressState::WRITE_END:
                std::cout << "Writing ending sectors..." << "\n";
                break;
            case ProgressState::FINISHED:
                std::cout << "Finished creating the image" << "\n";
                break;
            }
            write_progress = true;
            pr.new_state = false;
        }
        if (pr.new_file) {
            std::cout << "Current file: " << pr.current_file.str << "\n";
            write_progress = true;
            pr.new_file = false;
        }
        if (write_progress) {
            std::cout << "Progress: " << pr.progress * 100 << "%" << "\n";
        }
    } while (!pr.finished);

    std::cout << "End\n";
    return 0;
}

