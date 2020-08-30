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

// Test.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <API.h>
#include <cassert>

int main()
{
    auto disc_folder_path = "D:/Twinsanity Discs/Twins Full Extract PAL";
    auto iso_name = "compiled.iso"; // Can be an iso name to create locally or a path
    assert(strcmp("Put folder path here", disc_folder_path) != 0);
    Progress* pr = start_packing(disc_folder_path, iso_name);
    do {
        Progress* pr = poll_progress();
        bool write_progress = false;
        if (pr->new_state) {
            switch (pr->state)
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
            pr->new_state = false;
        }
        if (pr->new_file) {
            std::cout << "Current file: " << pr->file_name << "\n";
            write_progress = true;
            pr->new_file = false;
        }
        if (write_progress) {
            std::cout << "Progress: " << pr->progress * 100 << "%" << "\n";
        }
    } while (!pr->finished);

    std::cout << "End\n";
    return 0;
}

