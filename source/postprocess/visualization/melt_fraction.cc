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
/*  $Id: thermodynamic_phase.cc 1433 2012-12-08 08:24:55Z bangerth $  */


#include <aspect/postprocess/visualization/melt_fraction.h>
#include <aspect/simulator_access.h>

#include <deal.II/numerics/data_out.h>
#include <deal.II/base/parameter_handler.h>


namespace aspect
{
  namespace Postprocess
  {
    namespace VisualizationPostprocessors
    {
      template <int dim>
      MeltFraction<dim>::
      MeltFraction ()
        :
        DataPostprocessorScalar<dim> ("melt_fraction",
                                      update_values | update_q_points)
      {}



      template <int dim>
      void
      MeltFraction<dim>::
      compute_derived_quantities_vector (const std::vector<Vector<double> >              &uh,
                                         const std::vector<std::vector<Tensor<1,dim> > > &duh,
                                         const std::vector<std::vector<Tensor<2,dim> > > &dduh,
                                         const std::vector<Point<dim> >                  &normals,
                                         const std::vector<Point<dim> >                  &evaluation_points,
                                         std::vector<Vector<double> >                    &computed_quantities) const
      {
        const unsigned int n_quadrature_points = uh.size();
        Assert (computed_quantities.size() == n_quadrature_points,    ExcInternalError());
        Assert (computed_quantities[0].size() == 1,                   ExcInternalError());
        Assert (uh[0].size() == dim+2+this->n_compositional_fields(), ExcInternalError());

        for (unsigned int q=0; q<n_quadrature_points; ++q)
          {
            const double pressure    = uh[q][dim];
            const double temperature = uh[q][dim+1];
            std::vector<double> composition(this->n_compositional_fields());

            for (unsigned int c=0; c<this->n_compositional_fields(); ++c)
              composition[c] = uh[q][dim+2+c];

            // anhydrous melting of peridotite after Katz, 2003
            const double T_solidus  = A1 + 273.15
            		                + A2 * pressure
            		                + A3 * pressure * pressure;
            const double T_lherz_liquidus = B1 + 273.15
            		                      + B2 * pressure
            		                      + B3 * pressure * pressure;
            const double T_liquidus = C1 + 273.15
            		                + C2 * pressure
            		                + C3 * pressure * pressure;

            // melt fraction for peridotite with clinopyroxene
            double peridotite_melt_fraction;
            if (temperature < T_solidus || pressure > 1.3e10)
              peridotite_melt_fraction = 0.0;
            else if (temperature > T_liquidus)
              peridotite_melt_fraction = 1.0;
            else
              peridotite_melt_fraction = std::pow((temperature - T_solidus) / (T_lherz_liquidus - T_solidus),beta);

            // melt fraction after melting of all clinopyroxene
            const double R_cpx = r1 + r2 * pressure;
            const double F_max = M_cpx / R_cpx;

            if(peridotite_melt_fraction > F_max && peridotite_melt_fraction < 1.0)
            {
              const double T_max = std::pow(F_max,1/beta) * (T_lherz_liquidus - T_solidus) + T_solidus;
              peridotite_melt_fraction = F_max + (1 - F_max) * (temperature - T_max) / (T_liquidus - T_max);
            }

            // melting of pyroxenite after Sobolev et al., 2011
            const double T_melting = D1 + 273.15
  		                           + D2 * pressure
  		                           + D3 * pressure * pressure;

            const double discriminant = E1*E1/(E2*E2*4) + (temperature-T_melting)/E2;

            double pyroxenite_melt_fraction;
            if (temperature < T_melting || pressure > 1.3e10)
              pyroxenite_melt_fraction = 0.0;
            else if (discriminant < 0)
              pyroxenite_melt_fraction = 0.5429;
            else
              pyroxenite_melt_fraction = -E1/(2*E2) - std::sqrt(discriminant);

            double melt_fraction;
            if(this->n_compositional_fields()>0)
              melt_fraction = composition[0] * pyroxenite_melt_fraction +
                              (1-composition[0]) * peridotite_melt_fraction;
            else
              melt_fraction = peridotite_melt_fraction;

            computed_quantities[q](0) = melt_fraction;
          }
      }



      template <int dim>
      void
      MeltFraction<dim>::declare_parameters (ParameterHandler &prm)
	  {
		prm.enter_subsection("Postprocess");
		{
		  prm.enter_subsection("Visualization");
		  {
			prm.enter_subsection("Melt fraction");
			{
			  prm.declare_entry ("A1", "1085.7",
							     Patterns::Double (),
							     "Constant parameter in the quadratic "
							     "function that approximates the solidus "
							     "of peridotite. "
							     "Units: $°C$.");
			  prm.declare_entry ("A2", "1.329e-7",
							     Patterns::Double (),
							     "Prefactor of the linear pressure term "
							     "in the quadratic function that approximates "
							     "the solidus of peridotite. "
							     "Units: $°C/Pa$.");
			  prm.declare_entry ("A3", "-5.1e-18",
							     Patterns::Double (),
							     "Prefactor of the quadratic pressure term "
							     "in the quadratic function that approximates "
							     "the solidus of peridotite. "
							     "Units: $°C/(Pa^2)$.");
			  prm.declare_entry ("B1", "1475.0",
							     Patterns::Double (),
							     "Constant parameter in the quadratic "
							     "function that approximates the lherzolite "
							     "liquidus used for calculating the fraction "
							     "of peridotite-derived melt. "
							     "Units: $°C$.");
			  prm.declare_entry ("B2", "8.0e-8",
							     Patterns::Double (),
							     "Prefactor of the linear pressure term "
							     "in the quadratic function that approximates "
							     "the  lherzolite liquidus used for "
							     "calculating the fraction of peridotite-"
							     "derived melt. "
							     "Units: $°C/Pa$.");
			  prm.declare_entry ("B3", "-3.2e-18",
							     Patterns::Double (),
							     "Prefactor of the quadratic pressure term "
							     "in the quadratic function that approximates "
							     "the  lherzolite liquidus used for "
							     "calculating the fraction of peridotite-"
							     "derived melt. "
							     "Units: $°C/(Pa^2)$.");
			  prm.declare_entry ("C1", "1780.0",
							     Patterns::Double (),
							     "Constant parameter in the quadratic "
							     "function that approximates the liquidus "
							     "of peridotite. "
							     "Units: $°C$.");
			  prm.declare_entry ("C2", "4.50e-8",
							     Patterns::Double (),
							     "Prefactor of the linear pressure term "
							     "in the quadratic function that approximates "
							     "the liquidus of peridotite. "
							     "Units: $°C/Pa$.");
			  prm.declare_entry ("C3", "-2.0e-18",
							     Patterns::Double (),
							     "Prefactor of the quadratic pressure term "
							     "in the quadratic function that approximates "
							     "the liquidus of peridotite. "
							     "Units: $°C/(Pa^2)$.");
			  prm.declare_entry ("r1", "0.4",
							     Patterns::Double (),
							     "Constant in the linear function that "
							     "approximates the clinopyroxene reaction "
							     "coefficient. "
							     "Units: non-dimensional.");
			  prm.declare_entry ("r2", "8e-11",
							     Patterns::Double (),
							     "Prefactor of the linear pressure term "
							     "in the linear function that approximates "
							     "the clinopyroxene reaction coefficient. "
							     "Units: $1/Pa$.");
			  prm.declare_entry ("beta", "1.5",
							     Patterns::Double (),
							     "Exponent of the melting temperature in "
							     "the melt fraction calculation. "
							     "Units: non-dimensional.");
			  prm.declare_entry ("M_cpx", "0.3",
							     Patterns::Double (),
							     "Mass fraction of clinopyroxene in the "
							     "peridotite to be molten. "
							     "Units: non-dimensional.");
			  prm.declare_entry ("D1", "976.0",
							     Patterns::Double (),
							     "Constant parameter in the quadratic "
							     "function that approximates the solidus "
							     "of pyroxenite. "
							     "Units: $°C$.");
			  prm.declare_entry ("D2", "1.23e-7",
							     Patterns::Double (),
							     "Prefactor of the linear pressure term "
							     "in the quadratic function that approximates "
							     "the solidus of pyroxenite. "
							     "Units: $°C/Pa$.");
			  prm.declare_entry ("D3", "-5.1e-18",
							     Patterns::Double (),
							     "Prefactor of the quadratic pressure term "
							     "in the quadratic function that approximates "
							     "the solidus of pyroxenite. "
							     "Units: $°C/(Pa^2)$.");
			  prm.declare_entry ("E1", "633.8",
							     Patterns::Double (),
							     "Prefactor of the linear depletion term "
							     "in the quadratic function that approximates "
							     "the melt fraction of pyroxenite. "
							     "Units: $°C/Pa$.");
			  prm.declare_entry ("E2", "-611.4",
							     Patterns::Double (),
							     "Prefactor of the quadratic depletion term "
							     "in the quadratic function that approximates "
							     "the melt fraction of pyroxenite. "
							     "Units: $°C/(Pa^2)$.");
	        }
	        prm.leave_subsection();
		  }
		  prm.leave_subsection();
		}
		prm.leave_subsection();
	  }

      template <int dim>
      void
      MeltFraction<dim>::parse_parameters (ParameterHandler &prm)
      {
  		prm.enter_subsection("Postprocess");
  		{
  		  prm.enter_subsection("Visualization");
  		  {
  			prm.enter_subsection("Melt fraction");
  			{
              A1              = prm.get_double ("A1");
              A2              = prm.get_double ("A2");
              A3              = prm.get_double ("A3");
              B1              = prm.get_double ("B1");
              B2              = prm.get_double ("B2");
              B3              = prm.get_double ("B3");
              C1              = prm.get_double ("C1");
              C2              = prm.get_double ("C2");
              C3              = prm.get_double ("C3");
              r1              = prm.get_double ("r1");
              r2              = prm.get_double ("r2");
              beta            = prm.get_double ("beta");
              M_cpx           = prm.get_double ("M_cpx");
              D1              = prm.get_double ("D1");
              D2              = prm.get_double ("D2");
              D3              = prm.get_double ("D3");
              E1              = prm.get_double ("E1");
              E2              = prm.get_double ("E2");
            }
            prm.leave_subsection();
          }
          prm.leave_subsection();
        }
        prm.leave_subsection();
      }
    }
  }
}


// explicit instantiations
namespace aspect
{
  namespace Postprocess
  {
    namespace VisualizationPostprocessors
    {
      ASPECT_REGISTER_VISUALIZATION_POSTPROCESSOR(MeltFraction,
                                                  "melt fraction",
                                                  "A visualization output object that generates output "
                                                  "for the melt fraction at the temperature and "
                                                  "pressure of the current point (batch melting). "
                                                  "Does not take into account latent heat.")
    }
  }
}
