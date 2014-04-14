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


#ifndef __aspect__initial_conditions_function_h
#define __aspect__initial_conditions_function_h

#include <aspect/initial_conditions/interface.h>

#include <deal.II/base/parsed_function.h>

namespace aspect
{
  namespace InitialConditions
  {
    using namespace dealii;

    /**
     * A class that implements temperature initial conditions based on a
     * functional description provided in the input file.
     *
     * @ingroup InitialConditionsModels
     */
    template <int dim>
    class Function : public Interface<dim>
    {
      public:
        /**
         * Constructor.
         */
        Function ();

        /**
         * Return the initial temperature as a function of position.
         */
        virtual
        double initial_temperature (const Point<dim> &position) const;

        /**
         * Declare the parameters this class takes through input files. The
         * default implementation of this function does not describe any
         * parameters. Consequently, derived classes do not have to overload
         * this function if they do not take any runtime parameters.
         */
        static
        void
        declare_parameters (ParameterHandler &prm);

        /**
         * Read the parameters this class declares from the parameter file.
         * The default implementation of this function does not read any
         * parameters. Consequently, derived classes do not have to overload
         * this function if they do not take any runtime parameters.
         */
        virtual
        void
        parse_parameters (ParameterHandler &prm);

      private:
        /**
         * A function object representing the temperature.
         */
        Functions::ParsedFunction<dim> function;
    };
  }
}


#endif
