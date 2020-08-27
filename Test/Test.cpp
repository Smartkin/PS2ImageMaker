// Test.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <API.h>
#include <cassert>

int main()
{
    auto disc_folder_path = "Put folder path here";
    auto iso_name = "compiled.iso";
    assert(strcmp("Put folder path here", disc_folder_path) != 0);
	Progress* pr = start_packing(disc_folder_path, iso_name);
    do {
        if (pr->new_message) {
            std::cout << pr->message.str << "\n";
            pr->new_message = false;
        }
    } while (pr->progress < 1.0);

    std::cout << "End\n";
    return 0;
}

