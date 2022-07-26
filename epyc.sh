#!/bin/bash

SBATCH --partition=general
SBATCH --ntasks=5
SBATCH --nodes=1
SBATCH --cpus-per-task=1
SBATCH --time=00:5:00
SBATCH --job-ryancey_game_of_life

module load openmpi
module load gcc/10.2.0

mpirun -np 5 ./run