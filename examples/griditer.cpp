/*
  Copyright 2025 SINTEF Digital

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <config.h>

#include <opm/grid/utility/createThreadIterators.hpp>
#include <opm/grid/utility/ElementChunks.hpp>
#include <opm/grid/utility/StopWatch.hpp>
#include <opm/grid/CpGrid.hpp>

#ifdef _OPENMP
#include <omp.h>
#endif

#include <iostream>
#include <vector>


int main(int argc, char** argv)
{
    Dune::MPIHelper::instance(argc, argv);

    std::array<int, 3> dims = { 200, 200, 100 };
    std::cout << "Creating grid with " << dims[0]*dims[1]*dims[2]/double(1000000) << "M cells." << std::endl;
    std::array<double, 3> cellsz = { 1.0, 1.0, 1.0 };
    Dune::CpGrid grid;
    grid.createCartesian(dims, cellsz);
    const auto& gv = grid.leafGridView();

    // Plain iteration
    {
        std::cout << "Running plain loop test." << std::endl;
        std::vector<double> vols(grid.size(0));
        Opm::time::StopWatch clock;
        clock.start();
        for (const auto& elem : elements(gv)) {
            vols[elem.index()] = elem.geometry().volume();
        }
        clock.stop();
        std::cout << "Time: " << clock.secsSinceLast() << std::endl;
    }

#ifdef _OPENMP

    // Iteration with chunks
    {
        const int num_threads = 5;
        std::cout << "Running chunked loop test with " << num_threads << " threads." << std::endl;
        std::vector<double> vols(grid.size(0));
        omp_set_num_threads(num_threads);
        Opm::time::StopWatch clock;
        clock.start();
        [[maybe_unused]] int chunk_size = -1;
        const auto grid_chunk_iterators = Opm::createThreadIterators(gv, num_threads, 1000, chunk_size);
        const std::size_t num_chunks = grid_chunk_iterators.size() - 1;
#pragma omp parallel for
        for (std::size_t chunk = 0; chunk < num_chunks; ++chunk) {
            for (auto it = grid_chunk_iterators[chunk]; it != grid_chunk_iterators[chunk+1]; ++it) {
                const auto& elem = *it;
                vols[elem.index()] = elem.geometry().volume();
            }
        }
        clock.stop();
        std::cout << "Time: " << clock.secsSinceLast() << std::endl;
    }

    // Iteration with chunks using helper class
    {
        const int num_threads = 5;
        std::cout << "Running chunked loop test with " << num_threads << " threads and helper class." << std::endl;
        std::vector<double> vols(grid.size(0));
        omp_set_num_threads(num_threads);
        Opm::time::StopWatch clock;
        clock.start();
        Opm::ElementChunks chunks(gv, num_threads);
#pragma omp parallel for
        for (const auto& chunk : chunks) {
            for (const auto& elem : chunk) {
                vols[elem.index()] = elem.geometry().volume();
            }
        }
        clock.stop();
        std::cout << "Time: " << clock.secsSinceLast() << std::endl;
    }

#endif // _OPENMP

}
