// Test.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <thread>
#include <API.h>
#include <Windows.h>

int main()
{
	Progress* pr = start_packing("D:/Twinsanity Discs/Twins Full Extract PAL", "test.iso");
    do {
        if (pr->new_message) {
            std::cout << pr->message.str << "\n";
            pr->new_message = false;
        }
    } while (pr->progress < 1.0);

    std::cout << "End\n";
    return 0;
}

