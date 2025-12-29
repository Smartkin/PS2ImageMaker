# Playstation 2 Image Maker
The goal of this library is to create a Playstation 2 compatible disc that can be then run on an emulator or burned on a disc to run on a console or through external media such as USB.

# Limitations
At the moment library produces only single layer DVD images so the size limit is ~4.7 GB of data. Support for dual layer and CD may be added later but at the moment not included. At the moment the library is Windows only due to using WinAPI to build file tree.

# Usage
To start the compilation of an image call `start_packing` function providing the input directory path and output image name or output path with an image name.
The library works in a separate thread so to know what is the progress at the moment call `poll_progress` function.

# Compilation
At least CMake 4.0 is required.
After cloning the repository in the console go to where you cloned it and run the command below.
```
mkdir build && cd build && cmake ..
```
Then compile for your needed system. If you are working on Linux just run `make`. On Windows you should get a ready solution for Visual Studio.
