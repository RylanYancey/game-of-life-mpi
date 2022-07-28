// --------------------
// Standard Library 
#include <iostream>
using std::cout;
using std::endl;
#include <tgmath.h>
using std::sqrt;
#include<vector>
using std::vector;
#include<set>
using std::set;

// used for rand
#include <cstdlib>
#include <ctime>

// --------------------
// Library Includes
#include "mpi.h"

// --------------------
// Typedefs
typedef unsigned char byte;

// --------------------
// Configuration Constants
const int sector_width = 50;
const int runtime = 100;

int main(int argc, char* argv[]) {
    // ---------------------------------
    // MPI Setup 

    MPI_Init(&argc, &argv);
    
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    /// ---------------------------------
    /// Configuring this sector:
    /// This program treats the processes like a matrix.
    /// We need to figure out our position in the world, and how
    /// we relate to other processes. 

    int world_width = sqrt(size);
    if (size != (world_width * world_width)) {
        if (rank == 0) 
            cout << "Size must be perfect square: 4, 9, 16, etc." << endl;
        MPI_Finalize();
        return 0;
    }

    /// Think of the processes as a vector, or 1d array. 
    /// We need to find the index of the process in 2d, so we can
    /// find and communicate with its neighbours. 
    int row = rank / world_width;
    int col = rank % world_width;

    /// allocate space for the data in the center and for the halos (outsides)
    const int data_size = (sector_width - 1) * (sector_width - 1);
    const int halo_size = sector_width - 1;

    byte data[data_size];
    byte swap[data_size];
    byte** halo = (byte**) malloc(sizeof(byte*) * 8);

    for (int i = 0; i < 8; i++) {
        if (i < 4) halo[i] = (byte*) malloc(sizeof(byte) * halo_size);
        if (i > 3) halo[i] = (byte*) malloc(sizeof(byte) * 1);
    }

    for (int i = 0; i < data_size; i++) {
        data[i] = 0;
    }

    for (int i = 0; i < 8; i++) {
        for (int k = 0; k < halo_size; k++) {
            halo[i][k] = 0;
        }
    }

    /// Dir tells us the neighbours of this process
    /// and will inform our sends, receives, and indexing.
    enum Dir {
        N = 0, S = 1, E = 2, W = 3, NE = 4, NW = 5, SE = 6, SW = 7
    };

    /// todo: document
    set<Dir> dir_cache;
    if (row != 0) dir_cache.insert(Dir::N);
    if (row != world_width) dir_cache.insert(Dir::S);
    if (col != 0) dir_cache.insert(Dir::W);
    if (col != world_width) dir_cache.insert(Dir::E);

    if (row != 0 && col != world_width) dir_cache.insert(Dir::NE);
    if (row != 0 && col != 0) dir_cache.insert(Dir::NW);
    if (row != world_width && col != 0) dir_cache.insert(Dir::SW);
    if (row != world_width && col != world_width) dir_cache.insert(Dir::SE);

    // todo: document
    MPI_Request* sends = (MPI_Request*) malloc(sizeof(MPI_Request) * dir_cache.size());
    MPI_Request* recvs = (MPI_Request*) malloc(sizeof(MPI_Request) * dir_cache.size());

    // todo: document
    double start;
    double end = 0;

    for (int i_ = 0; i_ < runtime; i_++) {

        if (rank == 0) {
            start = MPI_Wtime();
            while (end - start < 0.2)
                end = MPI_Wtime();
        }

        MPI_Barrier(MPI_COMM_WORLD);

        /// ------------------------------------
        /// Create send and receive requests.
        /// Note that the size of the cache and the req_size are the same. 
        int i = 0;
        for (auto itr = dir_cache.begin(); itr != dir_cache.end(); itr++) {
            MPI_Request send_request;
            MPI_Request recv_request;

            /// Here, we need to find where each process we want to
            /// send and receive to is in the world. We do some simple transformations
            /// to find that. If you don't understand, try to write it out on a peice of paper.
            /// It's just 2d to 1d indexing. 
            int dest = 0, swidth = sector_width;
            switch(*itr) {
                case N:  { dest = rank - world_width; } break;
                case S:  { dest = rank + world_width; } break;
                case E:  { dest = rank + 1; } break;
                case W:  { dest = rank - 1; } break;
                case NW: { dest = rank - world_width - 1; } break;
                case NE: { dest = rank - world_width + 1; } break;
                case SW: { dest = rank + world_width - 1; } break;
                case SE: { dest = rank + world_width + 1; } break;
            }

            /// Create Send and Receive requests, but don't start them yet. We want to
            /// wait until we are all syncronized to start it. 
            if (*itr < 5) swidth = 1;
            MPI_Send_init(&halo[*itr], swidth, MPI_BYTE, dest, *itr, MPI_COMM_WORLD, &send_request);
            MPI_Recv_init(&halo[*itr], swidth, MPI_BYTE, dest, *itr, MPI_COMM_WORLD, &recv_request);
            sends[i] = send_request;
            recvs[i] = recv_request;

            i++;
        }

        // doc
        MPI_Startall(dir_cache.size(), sends);
        MPI_Startall(dir_cache.size(), recvs);

        // doc
        int sum;
        int x;
        int y;

        int w = sector_width - 1;

        // doc
        for (int i = 0; i < data_size; i++) {
            sum = 0;

            x = x / i;
            y = y % i;

            // Calculations for all N/S/E/W/ sums.
            if (x == 0) {
                sum += halo[Dir::N][x];       // halo up
                sum += data[x * w + (y + 1)]; // data down

                // if northwest, calc for NE/NW/SW/SE
                if (y == 0) {
                    sum += halo[Dir::NW][0]; // halo northwest
                    sum += halo[Dir::N][1]; // halo northeast
                    sum += halo[Dir::W][1]; // halo southwest
                    sum += data[(x + 1) * w + (y - 1)]; // data southeast
                }
                // if northeast, calc for NE/NW/SE/SW
                else if (y == w) {
                    sum += halo[Dir::NE][0]; // halo northeast
                    sum += halo[Dir::N][w - 1]; // halo northwest
                    sum += halo[Dir::E][1]; // halo southeast
                    sum += data[(x - 1) * w + (y - 1)]; // data southwest
                }
                // if between, calc for NE/NW/SE/SW
                else {
                    sum += halo[Dir::N][w - 1]; // northwest
                    sum += halo[Dir::N][w + 1]; // northeast
                    sum += data[(x + 1) * w + (y - 1)]; // southeast
                    sum += data[(x - 1) * w + (y - 1)]; // southwest
                }

            }
            else {
                sum += data[x * w + (y - 1)]; // data up
            }

            if (x == w) {
                sum += halo[Dir::S][x];       // halo down
                sum += data[x * w + (y - 1)]; // data up

                if (y == 0) {
                    sum += halo[Dir::SW][0]; // southwest
                    sum += halo[Dir::S][1]; // southeast
                    sum += halo[Dir::W][w - 1]; // northwest
                    sum += data[(x + 1) * w + (y - 1)]; // northeast
                }
                else if (y == w) {
                    sum += halo[Dir::SE][0]; // southeast
                    sum += halo[Dir::E][w - 1]; // northeast
                    sum += halo[Dir::S][w - 1]; // southwest
                    sum += data[(x - 1) * w + (y - 1)]; // northwest
                }
                else {
                    sum += halo[Dir::S][x - 1]; // Southwest
                    sum += halo[Dir::S][x + 1]; // southeast
                    sum += data[(x + 1) * w + (y - 1)]; // northeast
                    sum += data[(x - 1) * w + (y - 1)]; // northwest
                }
            }
            else {
                sum += data[x * w + (y - 1)]; // data down
            }

            if (y == 0) {
                sum += halo[Dir::W][y];       // halo west
                sum += data[x * w + (y + 1)]; // data east

                if (x != 0 && x != w) {
                    sum += halo[Dir::W][y + 1]; // northwest
                    sum += halo[Dir::W][y - 1]; // southwest
                    sum += data[(x + 1) * w + (y + 1)]; // southeast
                    sum += data[(x + 1) * w + (y - 1)]; // northeast
                }
            }
            else {
                sum += data[(x - 1) * w + y]; // data west
            }

            if (y == w) {
                sum += halo[Dir::E][y];       // halo east
                sum += data[x * w + (y - 1)]; // data west

                if (x != 0 && x != w) {
                    sum += halo[Dir::E][y + 1]; // southeast
                    sum += halo[Dir::E][y - 1]; // northeast
                    sum += data[(x - 1) * w + (y + 1)]; // southwest
                    sum += data[(x - 1) * w + (y - 1)]; // northwest
                }
            }
            else {
                sum += data[(x + 1) * w + y]; // data west
            }

            if (x != 0 && x != w && y != w && y != 0) {
                sum += data[(x - 1) * w + (y + 1)]; // southwest
                sum += data[(x - 1) * w + (y - 1)]; // northwest
                sum += data[(x + 1) * w + (y - 1)]; // northeast
                sum += data[(x + 1) * w + (y + 1)]; // southest
            }

            if (sum <= 1) swap[i] = 0;
            else if(sum >= 4) swap[i] = 0;
            else if (sum == 3) swap[i] == 1;
        }

        

        MPI_Barrier(MPI_COMM_WORLD);

    }

    MPI_Finalize();
}