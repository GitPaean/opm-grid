#include <config.h>

#include <opm/grid/common/SubGridView.hpp>

// Warning suppression for Dune includes.
#include <opm/grid/utility/platform_dependent/disable_warnings.h>

#include <dune/common/unused.hh>
#include <opm/grid/CpGrid.hpp>
#include <opm/grid/cpgrid/GridHelpers.hpp>

#include <dune/grid/io/file/vtk/vtkwriter.hh>
#include <dune/grid/yaspgrid.hh>

#include <opm/grid/cpgrid/dgfparser.hh>


#define DISABLE_DEPRECATED_METHOD_CHECK 1
using Dune::referenceElement; //grid check assume usage of Dune::Geometry
#include <dune/grid/test/gridcheck.hh>


// Re-enable warnings.
#include <opm/grid/utility/platform_dependent/reenable_warnings.h>

#include <opm/parser/eclipse/Deck/Deck.hpp>
#include <opm/parser/eclipse/Parser/Parser.hpp>

#include <cmath>
#include <iostream>


template <class GridView>
void testGridInteriorIteration( const GridView& gridView, const int nElem )
{
    typedef typename GridView::template Codim<0>::Iterator ElemIterator;
    typedef typename GridView::IntersectionIterator IsIt;
    typedef typename GridView::template Codim<0>::Geometry Geometry;

    int numElem = 0;
    ElemIterator elemIt = gridView.template begin<0>();
    ElemIterator elemEndIt = gridView.template end<0>();
    for (; elemIt != elemEndIt; ++elemIt) {
        if (elemIt->partitionType() != Dune::InteriorEntity) {
            continue;
        }
        const Geometry& elemGeom = elemIt->geometry();
        if (std::abs(elemGeom.volume() - 1.0) > 1e-8)
            std::cout << "element's " << numElem << " volume is wrong:"<<elemGeom.volume()<<"\n";

        typename Geometry::LocalCoordinate local( 0.5 );
        typename Geometry::GlobalCoordinate global = elemGeom.global( local );
        typename Geometry::GlobalCoordinate center = elemGeom.center();
        if( (center - global).two_norm() > 1e-6 )
        {
          std::cout << "center = " << center << " global( localCenter ) = " << global << std::endl;
        }


        int numIs = 0;
        IsIt isIt = gridView.ibegin(*elemIt);
        IsIt isEndIt = gridView.iend(*elemIt);
        for (; isIt != isEndIt; ++isIt, ++ numIs)
        {
            const auto& intersection = *isIt;
            const auto& isGeom = intersection.geometry();
            //std::cout << "Checking intersection id = " << localIdSet.id( intersection ) << std::endl;
            if (std::abs(isGeom.volume() - 1.0) > 1e-8)
                std::cout << "volume of intersection " << numIs << " of element " << numElem << " volume is wrong: " << isGeom.volume() << "\n";

            if (intersection.neighbor())
            {
              if( numIs != intersection.indexInInside() )
                  std::cout << "num iit = " << numIs << " indexInInside " << intersection.indexInInside() << std::endl;

              if (std::abs(intersection.outside().geometry().volume() - 1.0) > 1e-8)
                  std::cout << "outside element volume of intersection " << numIs << " of element " << numElem
                            << " volume is wrong: " << intersection.outside().geometry().volume() << std::endl;

              if (std::abs(intersection.inside().geometry().volume() - 1.0) > 1e-8)
                  std::cout << "inside element volume of intersection " << numIs << " of element " << numElem
                            << " volume is wrong: " << intersection.inside().geometry().volume() << std::endl;
            }
        }

        if (numIs != 2 * GridView::dimension )
            std::cout << "number of intersections is wrong for element " << numElem << "\n";

        ++ numElem;
    }

    if (numElem != nElem )
        std::cout << "number of elements is wrong: " << numElem << ", expected " << nElem << std::endl;
}


template <class Grid>
auto getSeeds(const Grid& grid, const std::vector<int>& indices)
{
    assert(std::is_sorted(indices.begin(), indices.end()));
    using EntitySeed = typename Grid::template Codim<0>::Entity::EntitySeed;
    std::vector<EntitySeed> seeds(indices.size());
    auto it = grid.template leafbegin<0>();
    int previous = 0;
    for (std::size_t c = 0; c < indices.size(); ++c) {
        std::advance(it, indices[c] - previous);
        seeds[c] = it->seed();
        previous = indices[c];
    }
    return seeds;
}


template <class Grid>
void testGrid(Grid& grid, const std::string& name, const std::size_t nElem, const std::size_t nVertices)
{
    typedef typename Grid::LeafGridView GridView;

    std::cout << name << std::endl;

    testGridInteriorIteration( grid.leafGridView(), nElem );

    std::cout << "create vertex mapper\n";
    Dune::MultipleCodimMultipleGeomTypeMapper<GridView> mapper(grid.leafGridView(), Dune::mcmgVertexLayout());

    std::cout << "VertexMapper.size(): " << mapper.size() << "\n";
    if (static_cast<std::size_t>(mapper.size()) != nVertices ) {
        std::cout << "Wrong size of vertex mapper. Expected " << nVertices << "!" << std::endl;
        //std::abort();
    }

    Dune::SubGridView<Grid> sgv(grid, getSeeds(grid, {0, 1, 2}));
    testGridInteriorIteration(sgv, 3);

}

int main(int argc, char** argv )
{
    // initialize MPI
    Dune::MPIHelper::instance( argc, argv );

    // ------------ Test grid from deck. ------------
#if HAVE_ECL_INPUT
    const char* deckString =
R"(
RUNSPEC
METRIC
DIMENS
2 2 2 /
GRID
DXV
2*1 /
DYV
2*1 /
DZ
8*1 /
TOPS
8*100.0 /
)";

    const auto deck = Opm::Parser{}.parseString(deckString);

    Dune::CpGrid grid;
    const int* actnum = deck.hasKeyword("ACTNUM") ? deck.getKeyword("ACTNUM").getIntData().data() : nullptr;
    Opm::EclipseGrid ecl_grid(deck , actnum);

    grid.processEclipseFormat(&ecl_grid, nullptr, false, false, false);
    testGrid( grid, "CpGrid_ecl", 8, 27 );
#endif

    // ------------ Test grid from dgf. ------------
    std::stringstream dgfFile;
    // create grid with 4 cells in each direction
    dgfFile << "DGF" << std::endl;
    dgfFile << "Interval" << std::endl;
    dgfFile << "0 0 0" << std::endl;
    dgfFile << "4 4 4" << std::endl;
    dgfFile << "4 4 4" << std::endl;
    dgfFile << "#" << std::endl;

    Dune::GridPtr< Dune::CpGrid > gridPtr( dgfFile );
    testGrid( *gridPtr, "CpGrid_dgf", 64, 125 );

    // ------------ Test YaspGrid. ------------

    Dune::YaspGrid<3, Dune::EquidistantCoordinates<double, 3>> yaspGrid({4.0, 4.0, 4.0}, {4, 4, 4});
    testGrid(yaspGrid, "YaspGrid", 64, 125);

    return 0;
}
