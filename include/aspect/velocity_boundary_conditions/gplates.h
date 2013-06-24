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


#ifndef __aspect__velocity_boundary_conditions_gplates_h
#define __aspect__velocity_boundary_conditions_gplates_h

#include <aspect/velocity_boundary_conditions/interface.h>
#include <aspect/simulator_access.h>


namespace aspect
{
  namespace VelocityBoundaryConditions
  {
    using namespace dealii;

    namespace internal
    {
      /**
       * GPlatesLookup handles all kinds of tasks around looking up a certain velocity boundary
       * condition from a gplates .gpml file. This class keeps around the contents of two sets
       * of files, corresponding to two instances in time where GPlates provides us with data;
       * the boundary values at one particular time are interpolated between the two currently
       * loaded data sets.
       */

      class GPlatesLookup
      {
        public:

          /**
           * Initialize all members and the two pointers referring to the actual velocities.
           * Also calculates any necessary rotation parameters for a 2D model.
           */
          GPlatesLookup(const Tensor<1,2> &pointone, const Tensor<1,2> &pointtwo);

          /**
           * Check whether a file named filename exists.
           */
          bool fexists(const std::string &filename) const;

          /**
           * Loads a gplates .gpml velocity file. Throws an exception if
           * the file does not exist.
           */
          void load_file(const std::string &filename);

          /**
           * Returns the computed surface velocity in cartesian coordinates. Takes
           * as input the position and current time weight.
           *
           * @param position The current position to compute velocity
           * @param time_weight A weighting between
           * the two current timesteps n and n+1
           */
          template <int dim>
          Tensor<1,dim> surface_velocity(const Point<dim> &position,
                                         const double time_weight) const;

        private:

          /**
           * Tables which contain the velocities
           */
          dealii::Table<2,Tensor<1,2> > velocity_vals;
          dealii::Table<2,Tensor<1,2> > old_velocity_vals;

          /**
           * Pointers to the actual tables.
           * Used to avoid unnecessary copying
           * of values.
           */
          dealii::Table<2,Tensor<1,2> > *velocity_values;
          dealii::Table<2,Tensor<1,2> > *old_velocity_values;

          /**
           * Distances between adjacent point in the Lat/Lon grid
           */
          double delta_phi,delta_theta;

          /**
           * The rotation axis and angle around which a 2D model needs to be rotated to
           * be transformed to a plane that contains the origin and the two
           * user prescribed points. Is not used for 3D.
           */
          Tensor<1,3> rotation_axis;
          double rotation_angle;


          /**
           * A function that returns the rotated vector r' that results out of a
           * rotation from vector r around a specified rotation_axis by an defined angle
           */
          Tensor<1,3>
          rotate (const Tensor<1,3> &position,const Tensor<1,3> &rotation_axis, const double angle) const;

          /**
           * Convert a tensor of rank 1 and dimension in to rank 1 and dimension out.
           * If $out < in$ the last elements will be discarded, if $out > in$ zeroes will
           * be appended to fill the tensor.
           */
          template <int in, int out>
          Tensor<1,out> convert_tensor (Tensor<1,in> old_tensor) const;

          /**
           * Returns spherical coordinates of a cartesian position.
           */
          Tensor<1,3> spherical_surface_coordinates(const Tensor<1,3> &position) const;

          /**
           * Return the cartesian coordinates of a spherical surface position
           * defined by theta (polar angle. not geographical latitude) and phi.
           */
          Tensor<1,3>
          cartesian_surface_coordinates(const Tensor<1,2> &sposition) const;

          /**
           * Returns cartesian velocities calculated from surface velocities and position in spherical coordinates
           *
           * @param s_velocities Surface velocities in spherical coordinates (theta, phi) @param s_position Position
           * in spherical coordinates (theta,phi,radius)
           */
          Tensor<1,3> sphere_to_cart_velocity(const Tensor<1,2> &s_velocities, const Tensor<1,3> &s_position) const;

          /**
           * calculates the phi-index given a certain phi
           */
          double get_idphi(const double phi_) const;

          /**
           * calculates the theta-index given a certain polar angle
           */
          double get_idtheta(const double theta_) const;
      };
    }

    /**
     * A class that implements prescribed velocity boundary conditions
     * determined from a GPlates input files.
     *
     * @ingroup VelocityBoundaryConditionsModels
     */
    template <int dim>
    class GPlates : public Interface<dim>, public SimulatorAccess<dim>
    {
      public:
        /**
         * Empty Constructor.
         */
        GPlates ();

        /**
         * Return the boundary velocity as a function of position. For the
         * current class, this function returns value from gplates.
         */
        Tensor<1,dim>
        boundary_velocity (const Point<dim> &position) const;

        /**
         * Initialization function. This function is called once at the beginning
         * of the program and loads the first set of GPlates files describing initial
         * conditions at the start time.
         */
        void
        initialize (const GeometryModel::Interface<dim> &geometry_model);

        /**
        * A function that is called at the beginning of each time step
        * to indicate what the model time is for which the boundary
        * values will next be evaluated. Also loads the next velocity
        * files if necessary and outputs a warning if the end of the set of
        * velocity files if reached.
        */
        void
        set_current_time (const double time);

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
        void
        parse_parameters (ParameterHandler &prm);

      private:
        /**
        * A variable that stores the current time of the simulation. Derived
        * classes can query this variable. It is set at the beginning of each
        * time step.
        */
        double current_time;

        /**
         * A variable that stores the currently used velocity file of a series.
         * It gets updated if necessary by set_current_time.
         */
        unsigned int  current_time_step;

        /**
         * Time at which the velocity file with number 0 shall be loaded.
         * Previous to this time, a no-slip boundary condition is assumed.
         */
        double velocity_file_start_time;

        /**
         * Directory in which the gplates velocity are present.
         */
        std::string data_directory;

        /**
         * First part of filename of velocity files. The files have to have the pattern
         * velocity_file_name.n.gpml where n is the number of the current timestep (starts
         * from 0).
         */
        std::string velocity_file_name;

        /**
         * Time in model units (depends on other model inputs) between two velocity files.
         */
        double time_step;

        /**
         * Weight between velocity file n and n+1 while the current time is between the
         * two values t(n) and t(n+1).
         */
        double time_weight;

        /**
         * State whether we have time_dependent boundary conditions. Switched off after
         * finding no more velocity files to suppress attempts to read in new files.
         */
        bool time_dependent;

        /**
         * Two user defined points that prescribe the plane from which the 2D model takes
         * the velocity boundary condition. One can think of this, as if the model is lying
         * in this plane although no actual model coordinate is changed. The strings need to
         * have the format "a,b" where a and b are doubles and define theta and phi on a sphere.
         */
        std::string point1;
        std::string point2;

        /**
         * Pointer to an object that reads and processes data we get from gplates files.
         */
        std_cxx1x::shared_ptr<internal::GPlatesLookup> lookup;

        /**
         * Create a filename out of the name template.
         */
        std::string
        create_filename (const int timestep) const;
    };
  }
}


#endif
