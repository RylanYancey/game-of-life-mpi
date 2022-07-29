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
const int sector_width = 20;
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

    /// Byte data contains this fields data, and the halo.
    /// The halo will be syncronized with each surrounding fields.
    byte data[(sector_width + 1)* (sector_width + 1)];

    int field_width = sector_width - 1;
    int data_width = sector_width;

    // Index the data we need on the horizontal axis
    MPI_Datatype HORI_TYPE;
    MPI_Type_vector(field_width, 1, data_width, MPI_BYTE, &HORI_TYPE);

    // index the data we need on the vertical axis
    MPI_Datatype VERT_TYPE;
    MPI_Type_contiguous(data_width, MPI_BYTE, &VERT_TYPE);

    // register new type with MPI
    MPI_Type_commit(&HORI_TYPE);
    MPI_Type_commit(&VERT_TYPE);

    /// Cache array to hold the data while updating (so no overwrite)
    byte cache[sector_width * sector_width];

    /// fill data with random values.
    for (int i = 0; i < data_width * data_width; i++) {
        data[i] = std::rand() / ((RAND_MAX + 1u) / 6) == 0 ? 1 : 0;
    }

    /// Create MPI Requests for each possible side.
    int verts = 2, horis = 2;
    if (row == 0) verts--;
    if (col == 0) horis--;
    if (row == world_width) verts--;
    if (col == world_width) horis--;

    MPI_Request* hori_reqs = (MPI_Request*) malloc(sizeof(MPI_Request) * horis * 2);
    MPI_Request* vert_reqs = (MPI_Request*) malloc(sizeof(MPI_Request) * verts * 2);

    byte** buffer;
    int m = (sector_width + 1) * (1 + sector_width);
    if (rank == 0) {
        buffer = (byte**) malloc(sizeof(byte*) * size);
        for (int i = 0; i < size + 1; i++) {
            buffer[i] = (byte*) malloc(sizeof(byte) * m);
        }
    }

    // used within the loop
    double start;
    double end = 0;

    for (int i_ = 0; i_ < runtime; i_++) {

        // Mandatory waiting period so we can see it run. 
        // set to -1 if testing for speed. 
        if (rank == 0) {
            start = MPI_Wtime();
            while (end - start < 0.2)
                end = MPI_Wtime();
        }

        MPI_Barrier(MPI_COMM_WORLD);

        int a = 0;
        // if not on west side / send west
        // HORIZONTAL SEND / RECEIVE
        if (col != 0) {
            MPI_Request send;
            MPI_Request recv;
            MPI_Send_init(&data[data_width + 2], 1, HORI_TYPE, rank - 1, 0, MPI_COMM_WORLD, &send);
            MPI_Recv_init(&data[data_width + 1], 1, HORI_TYPE, rank - 1, 0, MPI_COMM_WORLD, &recv);
            hori_reqs[a] = send;
            hori_reqs[a + 1] = recv;
            a += 2;
        }
        // if not on east side / send east
        // HORIZONTAL SEND / RECEIVE
        if (col != world_width - 1) {
            MPI_Request send;
            MPI_Request recv;
            MPI_Send_init(&data[(data_width * 2) - 2], 1, HORI_TYPE, rank + 1, 0, MPI_COMM_WORLD, &send);
            MPI_Recv_init(&data[(data_width * 2) - 1], 1, HORI_TYPE, rank + 1, 0, MPI_COMM_WORLD, &recv);
            hori_reqs[a] = send;
            hori_reqs[a + 1] = recv;
        }

        MPI_Startall(horis, hori_reqs);
        MPI_Waitall(horis, hori_reqs, MPI_STATUS_IGNORE);

        a = 0;
        // where are we in the process world?
        // if not on north side / send north
        // VERTICAL SEND / RECEIVE
        if (row != 0) {
            MPI_Request send;
            MPI_Request recv;
            MPI_Send_init(&data[data_width], 1, VERT_TYPE, rank - world_width, 0, MPI_COMM_WORLD, &send);
            MPI_Recv_init(&data[0], 1, VERT_TYPE, rank - world_width, 0, MPI_COMM_WORLD, &recv);
            vert_reqs[a] = send;
            vert_reqs[a + 1] = recv;
            a += 2;
        }
        // if not on south side / send south
        // VERTICAL SEND / RECEIVE
        if (row != world_width - 1) {
            MPI_Request send;
            MPI_Request recv;
            MPI_Send_init(&data[data_width * (data_width - 2)], 1, VERT_TYPE, rank + world_width, 0, MPI_COMM_WORLD, &send);
            MPI_Recv_init(&data[data_width * (data_width - 1)], 1, VERT_TYPE, rank + world_width, 0, MPI_COMM_WORLD, &recv);
            vert_reqs[a] = send;
            vert_reqs[a + 1] = recv;
        }

        MPI_Startall(verts, vert_reqs);
        MPI_Waitall(verts, vert_reqs, MPI_STATUS_IGNORE);

        for (int x = 1; x < field_width; x++)
            for (int y = 1; y < field_width; y++)
                cache[(x - 1) + field_width * (y - 1)] = 
                    data[(x - 1) + data_width * y] + data[(x + 1) + data_width * y] +
                    data[x + data_width * (y - 1)] + data[x + data_width * (y + 1)] +
                    data[(x + 1) + data_width * (y - 1)] + data[(x - 1) + data_width * (y - 1)] +
                    data[(x + 1) + data_width * (y + 1)] + data[(x - 1) + data_width * (y + 1)];

        for (int x = 1; x < field_width; x++)
            for (int y = 1; y < field_width; y++) {
                byte neighbours = cache[(x - 1) + field_width * (y - 1)];
                byte alive = 0;
                if (neighbours == 3) alive = 1;
                if (neighbours != 2) data[x + field_width * y] = alive;  
            }

        for (int i = 0; i < verts; i++)
            MPI_Request_free(&vert_reqs[i]);

        for (int i = 0; i < horis; i++)
            MPI_Request_free(&hori_reqs[i]);
    }

    MPI_Type_free(&HORI_TYPE);
    MPI_Type_free(&VERT_TYPE);

    free(hori_reqs);
    free(vert_reqs);

    for (int i = 0; i < size; i++)
        free(buffer[i]);

    free(buffer);

    MPI_Finalize();

}