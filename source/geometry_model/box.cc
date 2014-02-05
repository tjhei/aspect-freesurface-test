/*
  Copyright (C) 2011, 2012, 2013 by the authors of the ASPECT code.

  This file is part of ASPECT.

  ASPECT is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.

  ASPECT is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with ASPECT; see the file doc/COPYING.  If not see
  <http://www.gnu.org/licenses/>.
*/
/*  $Id$  */


#include <aspect/geometry_model/box.h>

#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria_iterator.h>
#include <deal.II/grid/tria_accessor.h>
#include <deal.II/grid/grid_tools.h>


namespace aspect
{
  namespace GeometryModel
  {
    template <int dim>
    void
    Box<dim>::
    create_coarse_mesh (parallel::distributed::Triangulation<dim> &coarse_grid) const
    {
      GridGenerator::hyper_rectangle (coarse_grid,
                                      Point<dim>(),
                                      extents);
      for (unsigned int f=0; f<GeometryInfo<dim>::faces_per_cell; ++f)
        coarse_grid.begin_active()->face(f)->set_boundary_indicator(f);

      //Tell p4est about the periodicity of the mesh.
#if (DEAL_II_MAJOR*100 + DEAL_II_MINOR) >= 801

      // If this does not compile you are probably using 8.1pre, so please update
      // to a recent svn version or to the 8.1 release.
      std::vector<GridTools::PeriodicFacePair<typename parallel::distributed::Triangulation<dim>::cell_iterator> >
                    periodicity_vector;
      for(int i=0; i<dim; ++i)
        if (periodic[i])
          GridTools::collect_periodic_faces
          ( coarse_grid, /*b_id1*/ 2*i, /*b_id2*/ 2*i+1,
              /*direction*/ i, periodicity_vector);

      if (periodicity_vector.size() > 0)
        coarse_grid.add_periodicity (periodicity_vector);
#else
      for( unsigned int i=0; i<dim; ++i)
        AssertThrow(!periodic[i],
                    ExcMessage("Please update deal.II to the latest version to get support for periodic domains."));
#endif
    }


    template <int dim>
    std::set<types::boundary_id>
    Box<dim>::
    get_used_boundary_indicators () const
    {
      // boundary indicators are zero through 2*dim-1
      std::set<types::boundary_id> s;
      for (unsigned int i=0; i<2*dim; ++i)
        s.insert (i);
      return s;
    }

    template <int dim>
    std::set< std::pair< std::pair<types::boundary_id, types::boundary_id>, unsigned int> >
    Box<dim>::
    get_periodic_boundary_pairs () const
    {
      std::set< std::pair< std::pair<types::boundary_id, types::boundary_id>, unsigned int> > periodic_boundaries;
      for( unsigned int i=0; i<dim; ++i)
        if (periodic[i])
          periodic_boundaries.insert( std::make_pair( std::pair<types::boundary_id, types::boundary_id>(2*i, 2*i+1), i) );
      return periodic_boundaries;
    }

    template <int dim>
    Point<dim>
    Box<dim>::get_extents () const
    {
      return extents;
    }


    template <int dim>
    double
    Box<dim>::
    length_scale () const
    {
      return 0.01*extents[0];
    }


    template <int dim>
    double
    Box<dim>::depth(const Point<dim> &position) const
    {
      const double d = maximal_depth()-position(dim-1);

      // if we violate the bounds, check that we do so only very slightly and
      // then just return maximal or minimal depth
      if (d < 0)
        {
          Assert (d >= -1e-14*std::fabs(maximal_depth()), ExcInternalError());
          return 0;
        }
      if (d > maximal_depth())
        {
          Assert (d <= (1.+1e-14)*maximal_depth(), ExcInternalError());
          return maximal_depth();
        }

      return d;
    }


    template <int dim>
    Point<dim>
    Box<dim>::representative_point(const double depth) const
    {
      Assert (depth >= 0,
              ExcMessage ("Given depth must be positive or zero."));
      Assert (depth <= maximal_depth(),
              ExcMessage ("Given depth must be less than or equal to the maximal depth of this geometry."));

      // choose a point on the center axis of the domain
      Point<dim> p = extents/2;
      p[dim-1] = maximal_depth() - depth;
      return p;
    }


    template <int dim>
    double
    Box<dim>::maximal_depth() const
    {
      return extents[dim-1];
    }


    template <int dim>
    void
    Box<dim>::
    declare_parameters (ParameterHandler &prm)
    {
      prm.enter_subsection("Geometry model");
      {
        prm.enter_subsection("Box");
        {
          prm.declare_entry ("X extent", "1",
                             Patterns::Double (0),
                             "Extent of the box in x-direction. Units: m.");
          prm.declare_entry ("Y extent", "1",
                             Patterns::Double (0),
                             "Extent of the box in y-direction. Units: m.");
          prm.declare_entry ("Z extent", "1",
                             Patterns::Double (0),
                             "Extent of the box in z-direction. This value is ignored "
                             "if the simulation is in 2d Units: m.");
          prm.declare_entry ("X periodic", "false",
                             Patterns::Bool (),
                             "Whether the box should be periodic in X direction");
          prm.declare_entry ("Y periodic", "false",
                             Patterns::Bool (),
                             "Whether the box should be periodic in Y direction");
          prm.declare_entry ("Z periodic", "false",
                             Patterns::Bool (),
                             "Whether the box should be periodic in Z direction");
        }
        prm.leave_subsection();
      }
      prm.leave_subsection();
    }



    template <int dim>
    void
    Box<dim>::parse_parameters (ParameterHandler &prm)
    {
      prm.enter_subsection("Geometry model");
      {
        prm.enter_subsection("Box");
        {
          extents[0] = prm.get_double ("X extent");
          periodic[0] = prm.get_bool ("X periodic");

          if (dim >= 2)
            {
              extents[1] = prm.get_double ("Y extent");
              periodic[1] = prm.get_bool ("Y periodic");
            }

          if (dim >= 3)
            {
              extents[2] = prm.get_double ("Z extent");
              periodic[2] = prm.get_bool ("Z periodic");
            }
        }
        prm.leave_subsection();
      }
      prm.leave_subsection();
    }
  }
}

// explicit instantiations
namespace aspect
{
  namespace GeometryModel
  {
    ASPECT_REGISTER_GEOMETRY_MODEL(Box,
                                   "box",
                                   "A box geometry parallel to the coordinate directions. "
                                   "The extent of the box in each coordinate direction "
                                   "is set in the parameter file. The box geometry labels its "
                                   "2*dim sides as follows: in 2d, boundary indicators 0 through 3 "
                                   "denote the left, right, bottom and top boundaries; in 3d, boundary "
                                   "indicators 0 through 5 indicate left, right, front, back, bottom "
                                   "and top boundaries. See also the documentation of the deal.II class "
                                   "``GeometryInfo''.")
  }
}
