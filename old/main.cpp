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

    int data[100];

    for (int i = 0; i < 100; i++) data[i] = rank;

    MPI_Datatype COLUMN;
    MPI_Type_vector(10, 1, 10, MPI_INT, &COLUMN);
    MPI_Type_commit(&COLUMN);

    MPI_Request reqs[2];

    int dest = rank == 0 ? 1 : 0;
    
    MPI_Send_init(&data[0], 1, COLUMN, dest, 0, MPI_COMM_WORLD, &reqs[0]);
    MPI_Recv_init(&data[0], 1, COLUMN, dest, 0, MPI_COMM_WORLD, &reqs[1]);

    MPI_Startall(2, reqs);
    
    MPI_Waitall(2, reqs, MPI_STATUS_IGNORE);

    MPI_Request_free(&reqs[0]);
    MPI_Request_free(&reqs[1]);

        double start;
        double end = 0;
        if (rank == 1) {
            start = MPI_Wtime();
            while (end - start < 0.2)
                end = MPI_Wtime();
        }

    cout << rank << ": ";
    for (int i = 0; i < 10; i++)
        cout << data[10 * i];
    cout << endl;

    MPI_Type_free(&COLUMN);

    MPI_Finalize();

}