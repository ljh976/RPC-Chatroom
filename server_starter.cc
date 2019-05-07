#include <iostream>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <string>


int main(int argc, char **argv)
{
    std::string arg = "master";
    std::string hostname = "localhost";
    std::string port = "3010";
    int opt = 0;
    while ((opt = getopt(argc, argv, "t:h:p:")) != -1){
        switch(opt) {
            case 't':
                arg = optarg;break;
            case 'h':
                hostname = optarg;break;
            case 'p':
                port = optarg;break;
            default:
                std::cerr << "Invalid Command Line Argument\n";
        }
    }

    if (arg == "routing")
    {
        std::string runRoutingServer = "./routing -h " + hostname + " -p " + port;
        if(fork() == 0)
            std::system(runRoutingServer.c_str());
    }

    else if (arg == "master")
    {
        std::string runMasterServer = "./tsd -h " + hostname + " -p " + port;
        if(fork() == 0)
            std::system(runMasterServer.c_str());
    }

    else
    {
        std::cerr << "Error: invalid argument. " << std::endl;
        return -1;
    }
    return 0;
}