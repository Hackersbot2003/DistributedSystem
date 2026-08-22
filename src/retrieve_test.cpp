#include "coordinator.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: retrieve_test <fileId>\n";
        return 1;
    }

    dfs::Coordinator coord({
        {"127.0.0.1", 6001},
        {"127.0.0.1", 6002},
        {"127.0.0.1", 6003}
    });

    coord.retrieveFile(argv[1], "recovered_output.txt");
    std::cout << "Done — check recovered_output.txt\n";
}