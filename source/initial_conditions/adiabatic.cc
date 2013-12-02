/*
  Copyright (C) 2012, 2013 by the authors of the ASPECT code.

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


#include <aspect/initial_conditions/adiabatic.h>
#include <aspect/geometry_model/box.h>
#include <aspect/geometry_model/spherical_shell.h>

#include <cmath>

namespace aspect
{
  namespace InitialConditions
  {
    template <int dim>
    double
    Adiabatic<dim>::
    initial_temperature (const Point<dim> &position) const
    {
      // convert input ages to seconds
      const double age_top =    (this->convert_output_to_years() ? age_top_boundary_layer * year_in_seconds
                                 : age_top_boundary_layer);
      const double age_bottom = (this->convert_output_to_years() ? age_bottom_boundary_layer * year_in_seconds
                                 : age_bottom_boundary_layer);

      // first, get the temperature at the top and bottom boundary of the model
      const double T_surface = this->boundary_temperature->minimal_temperature(this->get_fixed_temperature_boundary_indicators());
      const double T_bottom = this->boundary_temperature->maximal_temperature(this->get_fixed_temperature_boundary_indicators());

      // then, get the temperature of the adiabatic profile at a representative
      // point at the top and bottom boundary of the model
      const Point<dim> surface_point = this->geometry_model->representative_point(0.0);
      const Point<dim> bottom_point = this->geometry_model->representative_point(this->geometry_model->maximal_depth());
      const double adiabatic_surface_temperature = this->adiabatic_conditions->temperature(surface_point);
      const double adiabatic_bottom_temperature = this->adiabatic_conditions->temperature(bottom_point);

      // get a representative profile of the compositional fields as an input
      // for the material model
      const double depth = this->geometry_model->depth(position);

      // look up material properties
      typename MaterialModel::Interface<dim>::MaterialModelInputs in(1, this->n_compositional_fields());
      typename MaterialModel::Interface<dim>::MaterialModelOutputs out(1, this->n_compositional_fields());
      in.position[0]=position;
      in.temperature[0]=this->adiabatic_conditions->temperature(position);
      in.pressure[0]=this->adiabatic_conditions->pressure(position);
      for (unsigned int c=0; c<this->n_compositional_fields(); ++c)
        in.composition[0][c] = function->value(Point<1>(depth),c);
      in.strain_rate[0] = SymmetricTensor<2,dim>(); // adiabat has strain=0.
      this->get_material_model().evaluate(in, out);

      const double kappa = out.thermal_conductivities[0] / (out.densities[0] * out.specific_heat[0]);

      // analytical solution for the thermal boundary layer from half-space cooling model
      const double surface_cooling_temperature = age_top > 0.0 ?
                                                 (T_surface - adiabatic_surface_temperature) *
                                                 erfc(this->geometry_model->depth(position) /
                                                      (2 * sqrt(kappa * age_top)))
                                                 : 0.0;
      const double bottom_heating_temperature = age_bottom > 0.0 ?
                                                (T_bottom - adiabatic_bottom_temperature + subadiabaticity)
                                                * erfc((this->geometry_model->maximal_depth()
                                                        - this->geometry_model->depth(position)) /
                                                       (2 * sqrt(kappa * age_bottom)))
                                                : 0.0;

      // set the initial temperature perturbation
      // first: get the center of the perturbation, then check the distance to the evaluation point
      Point<dim> mid_point;
      if (perturbation_position == "center")
        {
          if (const GeometryModel::SphericalShell<dim> *
              geometry_model = dynamic_cast <const GeometryModel::SphericalShell<dim>*> (this->geometry_model))
            {
              const double pi = 3.14159265;
              double inner_radius = geometry_model->inner_radius();
              double angle = pi/180.0 * 0.5 * geometry_model->opening_angle();
              if (dim==2)
                {
                  mid_point(0) = inner_radius * sin(angle),
                  mid_point(1) = inner_radius * cos(angle);
                }
              else if (dim==3)
                {
                  if (geometry_model->opening_angle() == 90)
            	  {
            		mid_point(0) = std::sqrt(inner_radius*inner_radius/3),
            		mid_point(1) = std::sqrt(inner_radius*inner_radius/3),
            		mid_point(2) = std::sqrt(inner_radius*inner_radius/3);
            	  }
                  else
                  {
                    mid_point(0) = inner_radius * sin(angle) * cos(angle),
                    mid_point(1) = inner_radius * sin(angle) * sin(angle),
                    mid_point(2) = inner_radius * cos(angle);
		  }
		}
	    }
          else if (const GeometryModel::Box<dim> *
                   geometry_model = dynamic_cast <const GeometryModel::Box<dim>*> (this->geometry_model))
            for (unsigned int i=0; i<dim-1; ++i)
              mid_point(i) += 0.5 * geometry_model->get_extents()[i];
          else
            AssertThrow (false,
                         ExcMessage ("Not a valid geometry model for the initial conditions model"
                                     "adiabatic."));
        }

      const double perturbation = (mid_point.distance(position) < radius) ? amplitude
                                  : 0.0;


      // add the subadiabaticity
      const double zero_depth = 0.174;
      const double nondimesional_depth = (this->geometry_model->depth(position) / this->geometry_model->maximal_depth() - zero_depth)
                                         / (1.0 - zero_depth);
      double subadiabatic_T = 0.0;
      if (nondimesional_depth > 0)
        subadiabatic_T = -subadiabaticity * nondimesional_depth * nondimesional_depth;

      // return sum of the adiabatic profile, the boundary layer temperatures and the initial
      // temperature perturbation
      return this->adiabatic_conditions->temperature(position) + surface_cooling_temperature
             + (perturbation > 0.0 ? std::max(bottom_heating_temperature + subadiabatic_T,perturbation)
                : bottom_heating_temperature + subadiabatic_T);
    }


    template <int dim>
    void
    Adiabatic<dim>::declare_parameters (ParameterHandler &prm)
    {
      prm.enter_subsection("Initial conditions");
      {
        prm.enter_subsection("Adiabatic");
        {
          prm.declare_entry ("Age top boundary layer", "0e0",
                             Patterns::Double (0),
                             "The age of the upper thermal boundary layer, used for the calculation "
                             "of the half-space cooling model temperature. Units: years if the "
                             "'Use years in output instead of seconds' parameter is set; "
                             "seconds otherwise.");
          prm.declare_entry ("Age bottom boundary layer", "0e0",
                             Patterns::Double (0),
                             "The age of the lower thermal boundary layer, used for the calculation "
                             "of the half-space cooling model temperature. Units: years if the "
                             "'Use years in output instead of seconds' parameter is set; "
                             "seconds otherwise.");
          prm.declare_entry ("Radius", "0e0",
                             Patterns::Double (0),
                             "The Radius (in m) of the initial spherical temperature perturbation "
                             "at the bottom of the model domain.");
          prm.declare_entry ("Amplitude", "0e0",
                             Patterns::Double (0),
                             "The amplitude (in K) of the initial spherical temperature perturbation "
                             "at the bottom of the model domain. This perturbation will be added to "
                             "the adiabatic temperature profile, but not to the bottom thermal "
                             "boundary layer. Instead, the maximum of the perturbation and the bottom "
                             "boundary layer temperature will be used.");
          prm.declare_entry ("Position", "center",
                             Patterns::Selection ("center|"
                                                  "boundary"),
                             "Where the initial temperature perturbation should be placed (in the "
                             "center or at the boundary of the model domain).");
          prm.declare_entry ("Subadiabaticity", "0e0",
                             Patterns::Double (0),
                             "If this value is larger than 0, the initial temperature profile will "
                             "not be adiabatic, but subadiabatic. This value gives the maximal "
                             "deviation from adiabaticity. Set to 0 for an adiabatic temperature "
                             "profile. Units: K.\n\n"
                             "The function object in the Function subsection "
                             "represents the compositional fields that will be used as a reference "
                             "profile for calculating the thermal diffusivity. "
                             "The function depends only on depth.");
          prm.enter_subsection("Function");
          {
            Functions::ParsedFunction<1>::declare_parameters (prm, 1);
          }
          prm.leave_subsection();
        }
        prm.leave_subsection ();
      }
      prm.leave_subsection ();
    }


    template <int dim>
    void
    Adiabatic<dim>::parse_parameters (ParameterHandler &prm)
    {
      // we need to get the number of compositional fields here to
      // initialize the function parser. unfortunately, we can't get it
      // via SimulatorAccess from the simulator itself because at the
      // current point the SimulatorAccess hasn't been initialized
      // yet. so get it from the parameter file directly.
      prm.enter_subsection ("Compositional fields");
      const unsigned int n_compositional_fields = prm.get_integer ("Number of fields");
      prm.leave_subsection ();

      prm.enter_subsection("Initial conditions");
      {
        prm.enter_subsection("Adiabatic");
        {
          age_top_boundary_layer = prm.get_double ("Age top boundary layer");
          age_bottom_boundary_layer = prm.get_double ("Age bottom boundary layer");
          radius = prm.get_double ("Radius");
          amplitude = prm.get_double ("Amplitude");
          perturbation_position = prm.get("Position");
          subadiabaticity = prm.get_double ("Subadiabaticity");
          if (n_compositional_fields > 0)
            {
              prm.enter_subsection("Function");
              {
                function.reset (new Functions::ParsedFunction<1>(n_compositional_fields));
                function->parse_parameters (prm);
              }
              prm.leave_subsection();
            }
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
    ASPECT_REGISTER_INITIAL_CONDITIONS(Adiabatic,
                                       "adiabatic",
                                       "Temperature is prescribed as an adiabatic "
                                       "profile with upper and lower thermal boundary layers, "
                                       "whose ages are given as input parameters.")
  }
}
