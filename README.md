Do "make" to compile, "make clean" to clean up

Run program like below...

Start routing server:

    ./server_starter -t routing -p <port#>

Start master server:

    ./server_starter -t master -h <routing address> -p <port#>

Client should use:

    ./tsc -h <routing address> -p <port#> -u <username>


If code doesn't work or keep printing out something like "connecting... " use killing process command: pkill routing/tsc/tcd