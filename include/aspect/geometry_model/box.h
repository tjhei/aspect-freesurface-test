/*
  Copyright (C) 2011, 2012 by the authors of the ASPECT code.

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


#ifndef __aspect__geometry_model_box_h
#define __aspect__geometry_model_box_h

#include <aspect/geometry_model/interface.h>
#include <aspect/simulator.h>


namespace aspect
{
  namespace GeometryModel
  {
    using namespace dealii;

    /**
     * A class that describes a box geometry of certain width, height, and
     * depth (in 3d).
     */
    template <int dim>
    class Box : public Interface<dim>
    {
      public:
        /**
         * Generate a coarse mesh for the geometry described by this class.
         */
        virtual
        void create_coarse_mesh (parallel::distributed::Triangulation<dim> &coarse_grid) const;

        /**
         * Return a point that denotes the upper right corner of the box domain.
         */
        Point<dim> get_extents () const;

        /**
         * Return the typical length scale one would expect of features in this geometry,
         * assuming realistic parameters.
         *
         * We return 1/100th of the diameter of the box.
         */
        virtual
        double length_scale () const;


        virtual
        double depth(const Point<dim> &position) const;

        virtual
        Point<dim> representative_point(const double depth) const;

        virtual
        double maximal_depth() const;

        /**
         * Return the set of boundary indicators that are used by this model. This
         * information is used to determine what boundary indicators can be used in
         * the input file.
         *
         * The box model uses boundary indicators zero through 2*dim-1, with the first
         * two being the faces perpendicular to the x-axis, the next two perpendicular
         * to the y-axis, etc.
         */
        virtual
        std::set<types::boundary_id>
        get_used_boundary_indicators () const;

        /**
         * Declare the parameters this class takes through input files.
         */
        static
        void
        declare_parameters (ParameterHandler &prm);

        /**
         * Read the parameters this class declares from the parameter
         * file.
         */
        virtual
        void
        parse_parameters (ParameterHandler &prm);

      private:
        /**
         * Extent of the box in x-, y-, and z-direction (in 3d).
         */
        Point<dim> extents;

        /**
         * A vector for each dimension, containing a list of step sizes for the
         * subdivisions. The step sizes have to add up to the respective extent,
         * however, if that condition is not fulfilled, an additional entry will
         * be appended in the parse_parameters function.
         */
        std::vector<std::vector<double> > step_sizes;
    };
  }
}


#endif
