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
const int sector_width = 16;
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

    /// Indicates the ranks we need to index into to get data from.
    /// If your rank is in the center, it will have NW, NE, SE, and SW.
    /// If the rank is on a west-side, it will have NE and SE. 
    /// For example, if a rank has NE, it will collect corner data on the
    /// rank northeast of itself, bottom data on the rank north of itself,
    /// and side data on the rank to the east.
    enum class Sector {
        NW = 0, NE = 1, SE = 2, SW = 3
    };

    /// This program wants the size of the matrix to be square. Since the
    /// Size of the matrix is dependent on the number of processes, the user
    /// must specify a perfect square for the -np. 
    /// World width is the same as the world height. 
    int world_width = sqrt(size);
    if (size != (world_width * world_width)) {
        if (rank == 0) 
            cout << "Size must be perfect square: 4, 9, 16, etc." << endl;
        MPI_Finalize();
        return 0;
    }

    /// Contains all the data about the center of this local; 1 for alive, 0 for dead.
    /// Randomize the field so there is a 1 in 6 chance of being alive.
    int center_data_size = (sector_width - 1) * (sector_width - 1); 
    byte center_data[center_data_size];

    std::srand(std::time(nullptr));
    for (int i = 0; i < sector_width * sector_width; i++) 
        if (std::rand() / ((RAND_MAX + 1u) / 6) == 0) center_data[i] = 1;
        else center_data[i] = 0;

    /// halo_bufs are all the halo buffers - form a border around the center
    /// and can be sent across processes. Will be used in MPI_Send_init and etc.
    byte** halo_bufs;
    
    // randomize halo bufs
    for (int i = 0; i < 8; i++)
        if (i < 4) 
            for (int k = 0; k < sector_width; k++)
                if (std::rand() / ((RAND_MAX + 1u) / 6) == 0) halo_bufs[i][k] = 1;
                    else halo_bufs[i][k] = 0;
        else if (std::rand() / ((RAND_MAX + 1u) / 6) == 0) halo_bufs[i][0] = 1;
            else halo_bufs[i][0] = 0;

    /// Think of the processes as a vector, or 1d array. 
    /// We need to find the index of the process in 2d, so we can
    /// find and communicate with its neighbours. 
    int row = rank / world_width;
    int col = rank % world_width;

    /// n is a temporary variable to make the code cleaner. It is the width
    /// of the world matrix minus one. It acts as 'y' in this next section.
    /// ori, or "orientation" is the vector that contains the 
    /// neighbours of this rank, and tells us where in the world it is.
    int n = world_width - 1;
    vector<Sector> ori;

    /// Search the top row of the process matrix and assign 
    /// a label if a match is found. I know that I could use
    /// if/else here, but this is cleaner and isnt much of
    /// a performance penalty. 
    if      (row == 0 && col == 0) ori = {Sector::SE}; // Northwest
    else if (row == 0 && col == n) ori = {Sector::SW}; // Northeast
    else if (row == 0) ori = {Sector::SE, Sector::SW}; // North

    /// Search the bottom rows.
    else if (row == n && col == 0) ori = {Sector::NE}; // Southwest
    else if (row == n && col == n) ori = {Sector::NW}; // Southeast
    else if (row == n) ori = {Sector::NW, Sector::NE}; // South

    /// Search everything in between.
    else if (col == 0) ori = {Sector::NE, Sector::SE}; // West
    else if (col == n) ori = {Sector::NW, Sector::SW}; // East
    else ori = {Sector::NE,Sector::NW,Sector::SW,Sector::SE}; // Center

    /// ------------------------------------------------
    /// Allocating the buffers for sends and receives.
    /// When we go to update the world, we need to send requests to
    /// our neighbours in to receive their edge or corner data, that we can
    /// use to update our cells. We need a place to store the requests, and 
    /// somewhere to store the buffers for when we send and receive the data.

    /// Figure out which directions we need to send for.
    /// Dir enum will serve as a "tag" in the MPI_Irecv and Isends that will
    /// be communicated to the neighbours. it will indicate which buffer to send back. 
    /// We use a set<Dir> so that there are no duplicates. 
    enum Dir { N = 0, S = 1, E = 2, W = 3, NW = 4, NE = 5, SW = 6, SE = 7 };
    set<Dir> cache;
    for (auto s : ori) {
        // Breaking up the sector id so it can be used as a command to
        // get buffers from its neighbours.
        if      (s == Sector::NW) { cache.insert(Dir::N); cache.insert(Dir::W); cache.insert(Dir::NW); }
        else if (s == Sector::NE) { cache.insert(Dir::N); cache.insert(Dir::E); cache.insert(Dir::NE); }
        else if (s == Sector::SW) { cache.insert(Dir::S); cache.insert(Dir::W); cache.insert(Dir::SW); }
        else if (s == Sector::SE) { cache.insert(Dir::S); cache.insert(Dir::E); cache.insert(Dir::SE); }
    }

    /// Arrays to hold send and receive requests - important since we are doing
    /// Non-blocking communication across processes. We will use these with
    /// MPI_Startall later on. For now, we need to find out how large sends and
    /// recvs needs to be. (they will be the same size but shh)
    MPI_Request* sends;
    MPI_Request* recvs;
    int req_size;

    sends = (MPI_Request*) malloc(sizeof(MPI_Request) * cache.size());
    recvs = (MPI_Request*) malloc(sizeof(MPI_Request) * cache.size());

    /// 2d-arrays that will store all of the buffers, which will be sent and received
    /// across processes. We make the size 8 and correlate the index with Dir enum, so we
    /// can index it with direction rather than raw number. 
    byte** recv_bufs = (byte**) malloc(sizeof(byte*) * 8);

    // correlate recv_bufs with cache Dir cache. 
    for (auto itr = cache.begin(); itr != cache.end(); itr++) {
        if (*itr < 5) recv_bufs[*itr] = (byte*) malloc(sizeof(byte) * sector_width);
        else recv_bufs[*itr] = (byte*) malloc(sizeof(byte));
    }

    /// -------------------------------------------
    /// The Main Loop: where it actually happens.
    /// Up until this point, we were just setting up the indexing and bufs for the game
    /// of life. Now, it's time to start the loop that actually going to run the game.

    // start and end are used in the loop
    // to make the process sleep.
    double start, end;

    /// i_ is used instead of i to specify that it is never used. This is because this for
    /// loop is only there to detect and exit when the runtime is over. 
    for (int i_ = 0; i_ < runtime; i_++) {

        // If the rank is 0, wait, if not, wait until rank 0 finishes waiting. 
        // scuffed "sleep" implementation. Inserts some time between runs so
        // we can actually see what is happening.
        if (rank == 0) {
            start = MPI_Wtime();
            while (end - start < 0.2)
                end = MPI_Wtime();
        }

        // Every process will wait here until all
        // process is here. Synchronization.
        MPI_Barrier(MPI_COMM_WORLD);

        /// ------------------------------------
        /// Create send and receive requests.
        /// Note that the size of the cache and the req_size are the same. 
        int i = 0;
        for (auto itr = cache.begin(); itr != cache.end(); itr++) {
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
            MPI_Send_init(&halo_bufs[*itr], swidth, MPI_BYTE, dest, *itr, MPI_COMM_WORLD, &send_request);
            MPI_Recv_init(&recv_bufs[*itr], swidth, MPI_BYTE, dest, *itr, MPI_COMM_WORLD, &recv_request);
            sends[i] = send_request;
            recvs[i] = recv_request;

            i++;
        }

        /// ------------------------------------
        /// Launch send and receive requests, but only
        MPI_Startall(req_size, sends);
        MPI_Startall(req_size, recvs);

        /// ------------------------------------
        /// At this point, we have all of the information we need
        /// to update the board in send_bufs. Let's 
        /// get everything updated. 

        int x;
        int y;

        int w = sector_width - 1;

        int sum;

        for (int i = 0; i < (w * w); i++) {
            sum = 0;

            x = x / i;
            y = y % i;

            // if top row (a halo row)
            if (x == 0) {
                // if nw
                if (y == 0) {
                    sum += halo_bufs[Dir::NW][0]; // target cell
                    sum += halo_bufs[Dir::W][1];  // south
                    sum += halo_bufs[Dir::N][1];  // east
                    sum += center_data[0];        // southeast
                    if (row != 0 && col == 0) {
                        sum += recv_bufs[Dir::N][0]; // north
                        sum += recv_bufs[Dir::N][1]; // northeast
                        sum += recv_bufs[Dir::NE][0];// northwest
                        sum += recv_bufs[Dir::W][0]; // west
                        sum += recv_bufs[Dir::W][1]; // southwest
                    }
                }
                // if ne
                else if (y == w) {
                    sum += halo_bufs[Dir::NE][0];    // target cell
                    sum += halo_bufs[Dir::E][1];     // south
                    sum += halo_bufs[Dir::W][w - 1]; // west
                    sum += center_data[(w - 1)];     //southwest
                    if (row != 0 && col == world_width) {
                        sum += recv_bufs[Dir::N][w];    // north
                        sum += recv_bufs[Dir::N][w - 1];// northwest
                        sum += recv_bufs[Dir::NE][0];   // northeast
                        sum += recv_bufs[Dir::E][0];    // east
                        sum += recv_bufs[Dir::E][1];    // southeast                
                    }
                }
                // if n
                else {
                    sum += halo_bufs[Dir::N][x];    // target cell
                    sum += halo_bufs[Dir::N][x - 1];// west
                    sum += halo_bufs[Dir::N][x + 1];// east
                    sum += center_data[x];          // south

                    // if on the west edge of center
                    if (y == 1) sum += halo_bufs[Dir::W][x]; // sw
                    else        sum += center_data[x - 1];

                    // if on the east edge of center
                    if (y == w - 1) sum += halo_bufs[Dir::E][x]; // se
                    else            sum += center_data[x + 1];

                    // if on the north edge of the world
                    if (row != 0) {
                        
                    }
                }
            }

        }



    }

    /// -------------------------------------------
    /// MPI Finalizations. The program has run and is now ready to sleep.
    MPI_Finalize();

}