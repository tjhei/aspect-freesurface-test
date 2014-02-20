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
/*  $Id: spherical_shell.cc 1651 2013-04-29 00:20:56Z heister $  */


#include <aspect/initial_conditions/harmonic_perturbation.h>
#include <aspect/geometry_model/box.h>
#include <aspect/geometry_model/spherical_shell.h>

#include <boost/math/special_functions/spherical_harmonic.hpp>

namespace aspect
{
  namespace InitialConditions
  {
    template <int dim>
    double
    HarmonicPerturbation<dim>::
    initial_temperature (const Point<dim> &position) const
    {

      // use either the user-input reference temperature as background temperature
      // (incompressible model) or the adiabatic temperature profile (compressible model)
      const double background_temperature = this->get_material_model().is_compressible() ?
                                            this->adiabatic_conditions->temperature(position) :
                                            reference_temperature;

      // s = fraction of the way from
      // the inner to the outer
      // boundary; 0<=s<=1
      const double s = this->geometry_model->depth(position) / this->geometry_model->maximal_depth();

      const double depth_perturbation = std::sin(vertical_wave_number*s*numbers::PI);


      double lateral_perturbation = 0.0;

      if (const GeometryModel::SphericalShell<dim> *
          geometry_model = dynamic_cast <const GeometryModel::SphericalShell<dim>*> (this->geometry_model))
        {
          // In case of spherical shell calculate spherical coordinates
          const Tensor<1,dim> scoord = spherical_surface_coordinates(position);

          if (dim==2)
            {
              // Use a sine as lateral perturbation that is scaled to the opening angle of the geometry.
              // This way the perturbation is alway 0 at the model boundaries.
              const double opening_angle = geometry_model->opening_angle()*numbers::PI/180.0;
              lateral_perturbation = std::sin(lateral_wave_number_1*scoord[1]*numbers::PI/opening_angle);
            }

          else if (dim==3)
            {
              // Spherical harmonics are only defined for order <= degree
        	  // and degree >= 0. Verify that it is indeed.
              Assert ( std::abs(lateral_wave_number_2) <= lateral_wave_number_1,
                       ExcMessage ("Spherical harmonics can only be computed for "
                                   "order <= degree."));
              Assert ( lateral_wave_number_1 >= 0,
                       ExcMessage ("Spherical harmonics can only be computed for "
                                   "degree >= 0."));
              // use a spherical harmonic function as lateral perturbation
              lateral_perturbation = boost::math::spherical_harmonic_r(lateral_wave_number_1,lateral_wave_number_2,scoord[2],scoord[1]);
            }
        }
      else if (const GeometryModel::Box<dim> *
               geometry_model = dynamic_cast <const GeometryModel::Box<dim>*> (this->geometry_model))
        {
          // In case of Box model use a sine as lateral perturbation
          // that is scaled to the extent of the geometry.
          // This way the perturbation is alway 0 at the model borders.
          const Point<dim> extent = geometry_model->get_extents();

          if (dim==2)
            {
              lateral_perturbation = std::sin(lateral_wave_number_1*position(0)*numbers::PI/extent(0));
            }
          else if (dim==3)
            {
              lateral_perturbation = std::sin(lateral_wave_number_1*position(0)*numbers::PI/extent(0))
                                     * std::sin(lateral_wave_number_2*position(1)*numbers::PI/extent(1));
            }
        }
      else
        AssertThrow (false,
                     ExcMessage ("Not a valid geometry model for the initial conditions model"
                                 "harmonic perturbation."));

      return background_temperature + magnitude * depth_perturbation * lateral_perturbation;
    }

    template <int dim>
    const Tensor<1,dim>
    HarmonicPerturbation<dim>::
    spherical_surface_coordinates(const Tensor<1,dim> &position) const
    {
      Tensor<1,dim> scoord;

      scoord[0] = std::sqrt(position.norm_square()); // R
      scoord[1] = std::atan2(position[1],position[0]); // Phi
      if (scoord[1] < 0.0) scoord[1] = 2*numbers::PI + scoord[1]; // correct phi to [0,2*pi]
      if (dim==3)
        scoord[2] = std::acos(position[2]/std::sqrt(position.norm_square())); // Theta

      return scoord;
    }

    template <int dim>
    void
    HarmonicPerturbation<dim>::declare_parameters (ParameterHandler &prm)
    {
      prm.enter_subsection("Initial conditions");
      {
        prm.enter_subsection("Harmonic perturbation");
        {
          prm.declare_entry ("Vertical wave number", "1",
                             Patterns::Integer (),
                             "Doubled radial wave number of the harmonic perturbation. "
                             " One equals half of a sine period over the model domain. "
                             " This allows for single up-/downswings. Negative numbers "
                             " reverse the sign of the perturbation.");
          prm.declare_entry ("Lateral wave number one", "3",
                             Patterns::Integer (),
                             "Doubled first lateral wave number of the harmonic perturbation. "
                             "Equals the spherical harmonic degree in 3D spherical shells. "
                             "In all other cases one equals half of a sine period over "
                             "the model domain. This allows for single up-/downswings. "
                             "Negative numbers reverse the sign of the perturbation but are "
                             "not allowed for the spherical harmonic case.");
          prm.declare_entry ("Lateral wave number two", "2",
                             Patterns::Integer (),
                             "Doubled second lateral wave number of the harmonic perturbation. "
                             "Equals the spherical harmonic order in 3D spherical shells. "
                             "In all other cases one equals half of a sine period over "
                             "the model domain. This allows for single up-/downswings. "
                             "Negative numbers reverse the sign of the perturbation.");
          prm.declare_entry ("Magnitude", "1.0",
                             Patterns::Double (0),
                             "The magnitude of the Harmonic perturbation.");
          prm.declare_entry ("Reference temperature", "1600.0",
                             Patterns::Double (0),
                             "The reference temperature that is perturbed by the"
                             "harmonic function. Only used in incompressible models.");
        }
        prm.leave_subsection ();
      }
      prm.leave_subsection ();
    }


    template <int dim>
    void
    HarmonicPerturbation<dim>::parse_parameters (ParameterHandler &prm)
    {
      prm.enter_subsection("Initial conditions");
      {
        prm.enter_subsection("Harmonic perturbation");
        {
          vertical_wave_number = prm.get_integer ("Vertical wave number");
          lateral_wave_number_1 = prm.get_integer ("Lateral wave number one");
          lateral_wave_number_2 = prm.get_integer ("Lateral wave number two");
          magnitude = prm.get_double ("Magnitude");
          reference_temperature = prm.get_double ("Reference temperature");
        }
        prm.leave_subsection ();
      }
      prm.leave_subsection ();
    }
  }
}

// explicit instantiations
namespace aspect
{
  namespace InitialConditions
  {
    ASPECT_REGISTER_INITIAL_CONDITIONS(HarmonicPerturbation,
                                       "harmonic perturbation",
                                       "An initial temperature field in which the temperature "
                                       "is perturbed following a harmonic function (spherical "
                                       "harmonic or sine depending on geometry and dimension) "
                                       "in lateral and radial direction from an otherwise "
                                       "constant temperature (incompressible model) or adiabatic "
                                       "reference profile (compressible model).")
  }
}
