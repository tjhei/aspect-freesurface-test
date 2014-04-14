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
/*  $Id: density.h 1433 2012-12-08 08:24:55Z bangerth $  */


#ifndef __aspect__mesh_refinement_viscosity_h
#define __aspect__mesh_refinement_viscosity_h

#include <aspect/mesh_refinement/interface.h>
#include <aspect/simulator_access.h>

namespace aspect
{
  namespace MeshRefinement
  {

    /**
     * A class that implements a mesh refinement criterion based on a field
     * representing the viscosity $\eta$ as a spatially variable function and
     * that we compute from the solution vector (assuming the viscosity
     * actually depends on the solution).
     *
     * @ingroup MeshRefinement
     */
    template <int dim>
    class Viscosity : public Interface<dim>,
      public SimulatorAccess<dim>
    {
      public:
        /**
         * Execute this mesh refinement criterion.
         *
         * @param[out] error_indicators A vector that for every active cell of
         * the current mesh (which may be a partition of a distributed mesh)
         * provides an error indicator. This vector will already have the
         * correct size when the function is called.
         */
        virtual
        void
        execute (Vector<float> &error_indicators) const;
    };
  }
}

#endif
