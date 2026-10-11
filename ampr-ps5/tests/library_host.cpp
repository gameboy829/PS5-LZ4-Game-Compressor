// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdlib>
#include <iostream>
#define STUDIO_HOST_TEST
#define STUDIO_LIBRARY_ROOT std::getenv("LZ4_TEST_LIBRARY")
#include "../standalone/library.h"
int main(){try{auto games=studio::scanFolderGames();studio::LibraryJson result=games;std::cout<<result.dump();return 0;}catch(const std::exception&e){std::cerr<<e.what();return 1;}}
