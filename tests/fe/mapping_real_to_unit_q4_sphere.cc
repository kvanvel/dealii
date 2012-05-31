//-----------------------------------------------------------------------------
//    $Id: data_out_base_pvd.cc 25569 2012-05-30 12:53:31Z bangerth $
//    Version: $Name$
//
//    Copyright (C) 2012 by the deal.II authors
//
//    This file is subject to QPL and may not be  distributed
//    without copyright and license information. Please refer
//    to the file deal.II/doc/license.html for the  text  and
//    further information on this license.
//
//-----------------------------------------------------------------------------


// this is a variation of the _q4_curved test, with data extracted by
// Eric Heien from the Aspect code. we used to trip an assertion (the
// Newton iteration in transform_real_to_unit_cell did not converge)
// because the point we look for is outside the Q1 mapped cell but
// inside the Q4 mapped cell. as part of the algorithm, we get an
// initial guess for the inverse Q4 mapping by running an inverse Q1
// mapping, and we failed to find a solution in the latter.
//
// a redux of this test is in the mapping_real_to_unit_q4_sphere_*
// tests

#include "../tests.h"

#include <deal.II/base/utilities.h>
#include <deal.II/grid/tria.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria_boundary_lib.h>
#include <deal.II/fe/mapping_q.h>


void test_real_to_unit_cell()
{
  const unsigned int dim = 3;

				   // define a spherical cap boundary
				   // to be used for one of the faces
  const double radius = Point<dim>(1.43757e-10, 4.48023e+06, 4.48023e+06).norm();
  HyperBallBoundary<dim> boundary (Point<dim>(), radius);

				   // create the mesh: a single cell
				   // with the following coordinates:
  std::vector<Point<dim> > vertices;
  vertices.push_back (Point<dim>( 6.70384e-11, 3.17728e+06, 3.17728e+06));
  vertices.push_back (Point<dim>( -1.46060e+06, 3.99043e+06, 1.46060e+06));
  vertices.push_back (Point<dim>( -1.46060e+06, 1.46060e+06, 3.99043e+06));
  vertices.push_back (Point<dim>( -2.59424e+06, 2.59424e+06, 2.59424e+06));
  vertices.push_back (Point<dim>( 1.43757e-10, 4.48023e+06, 4.48023e+06));
  vertices.push_back (Point<dim>( -2.05956e+06, 5.62684e+06, 2.05956e+06));
  vertices.push_back (Point<dim>( -2.05956e+06, 2.05956e+06, 5.62684e+06));
  vertices.push_back (Point<dim>( -3.65809e+06, 3.65809e+06, 3.65809e+06));
				   // the points above don't show
				   // enough digits to have the same
				   // outer radius, so normalize the
				   // four outer ones
  for (unsigned int v=4; v<8; ++v)
    vertices[v] *= radius/vertices[v].norm();
  std::vector<CellData<dim> > cells;
  {
    CellData<dim> d = {{0,1,2,3,4,5,6,7},{0}};
    cells.push_back(d);
  }
  Triangulation<dim> triangulation;
  triangulation.create_triangulation (vertices, cells,
				      SubCellData());

				   // set the boundary indicator for
				   // one face and adjacent edges of
				   // the single cell
  triangulation.set_boundary (1, boundary);
  triangulation.begin_active()->face(5)->set_all_boundary_indicators (1);

				   // now try to find the coordinates
				   // of the following point in the
				   // reference coordinate system of
				   // the cell
  const Point<dim> p (-3.56413e+06, 1.74215e+06, 2.14762e+06);
  MappingQ<dim> map(4);
  typename Triangulation<dim >::active_cell_iterator
    cell = triangulation.begin_active();
  deallog << map.transform_real_to_unit_cell(cell,p) << std::endl;
}


int
main()
{
  std::ofstream logfile ("mapping_real_to_unit_q4_sphere/output");
  deallog.attach(logfile);
  deallog.depth_console(0);
  deallog.threshold_double(1.e-10);

  test_real_to_unit_cell();

  return 0;
}


