/*
  Copyright (C) 2013 by the authors of the ASPECT code.

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


#ifndef __aspect__boundary_composition_initial_composition_h
#define __aspect__boundary_composition_initial_composition_h

#include <aspect/boundary_composition/interface.h>
#include <aspect/simulator.h>


namespace aspect
{
  namespace BoundaryComposition
  {
    /**
     * A class that implements a composition boundary condition for a spherical
     * shell geometry in which the composition at the inner and outer surfaces
     * (i.e. at the core-mantle and the mantle-lithosphere/atmosphere boundaries)
     * are constant.
     *
     * @ingroup BoundaryCompositions
     */
    template <int dim>
    class InitialComposition : public Interface<dim>, public ::aspect::SimulatorAccess<dim>
    {
      public:
        /**
         * Return the composition that is to hold at a particular location on the
         * boundary of the domain. This function returns the constant compositions
         * read from the parameter file for the inner and outer boundaries.
         *
         * @param geometry_model The geometry model that describes the domain. This may
         *   be used to determine whether the boundary composition model is implemented
         *   for this geometry.
         * @param boundary_indicator The boundary indicator of the part of the boundary
         *   of the domain on which the point is located at which we are requesting the
         *   composition.
         * @param location The location of the point at which we ask for the composition.
         **/
        virtual
        double composition (const GeometryModel::Interface<dim> &geometry_model,
                            const unsigned int                   boundary_indicator,
                            const Point<dim>                    &location,
                            const unsigned int                   compositional_field) const;

        /**
         * Return the minimal composition on that part of the boundary
         * on which Dirichlet conditions are posed.
         *
         * This value is used in computing dimensionless numbers such as the
         * Nusselt number indicating heat flux.
         */
        virtual
        double minimal_composition (const std::set<types::boundary_id>& fixed_boundary_ids) const;

        /**
         * Return the maximal composition on that part of the boundary
         * on which Dirichlet conditions are posed.
         *
         * This value is used in computing dimensionless numbers such as the
         * Nusselt number indicating heat flux.
         */
        virtual
        double maximal_composition (const std::set<types::boundary_id>& fixed_boundary_ids) const;

        /**
         * Declare the parameters this class takes through input files.
         * This class declares the inner and outer boundary compositions.
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
         * Compositions at the inner and outer boundaries.
         */
        double min_composition;
        double max_composition;
    };
  }
}


#endif
