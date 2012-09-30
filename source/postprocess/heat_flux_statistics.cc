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


#include <aspect/postprocess/heat_flux_statistics.h>
#include <aspect/geometry_model/spherical_shell.h>
#include <aspect/geometry_model/box.h>
#include <aspect/simulator.h>

#include <deal.II/base/quadrature_lib.h>
#include <deal.II/fe/fe_values.h>


namespace aspect
{
  namespace Postprocess
  {
    template <int dim>
    std::pair<std::string,std::string>
    HeatFluxStatistics<dim>::execute (TableHandler &statistics)
    {
      // create a quadrature formula based on the temperature element alone.
      // be defensive about determining that what we think is the temperature
      // element is it in fact
      Assert (this->get_dof_handler().get_fe().n_base_elements() == 3,
              ExcNotImplemented());
      const QGauss<dim-1> quadrature_formula (this->get_dof_handler().get_fe().base_element(2).degree+1);

      FEFaceValues<dim> fe_face_values (this->get_mapping(),
                                        this->get_dof_handler().get_fe(),
                                        quadrature_formula,
                                        update_gradients      | update_values |
                                        update_normal_vectors |
                                        update_q_points       | update_JxW_values);

      const FEValuesExtractors::Scalar pressure (dim);
      const FEValuesExtractors::Scalar temperature (dim+1);

      std::vector<Tensor<1,dim> > temperature_gradients (quadrature_formula.size());
      std::vector<double>         temperature_values (quadrature_formula.size());
      std::vector<double>         pressure_values (quadrature_formula.size());

      std::map<types::boundary_id_t, double> local_boundary_fluxes;

      typename DoFHandler<dim>::active_cell_iterator
      cell = this->get_dof_handler().begin_active(),
      endc = this->get_dof_handler().end();

      // for every surface face on which it makes sense to compute a
      // heat flux and that is owned by this processor,
      // integrate the normal heat flux given by the formula
      //   j =  - k * n . grad T
      //
      // for the spherical shell geometry, note that for the inner boundary,
      // the normal vector points *into* the core, i.e. we compute the flux
      // *out* of the mantle, not into it. we fix this when we add the local
      // contribution to the global flux
      for (; cell!=endc; ++cell)
        if (cell->is_locally_owned())
          for (unsigned int f=0; f<GeometryInfo<dim>::faces_per_cell; ++f)
            if (cell->at_boundary(f))
              {
                fe_face_values.reinit (cell, f);
                fe_face_values[temperature].get_function_gradients (this->get_solution(),
                                                                    temperature_gradients);
                fe_face_values[temperature].get_function_values (this->get_solution(),
                                                                 temperature_values);
                fe_face_values[pressure].get_function_values (this->get_solution(),
                                                              pressure_values);

                double local_normal_flux = 0;
                for (unsigned int q=0; q<fe_face_values.n_quadrature_points; ++q)
                  {
                    const double thermal_conductivity
                      = this->get_material_model().thermal_conductivity(temperature_values[q],
                                                                        pressure_values[q],
                                                                        fe_face_values.quadrature_point(q));

                    local_normal_flux
                    +=
                      -thermal_conductivity *
                      (temperature_gradients[q] *
                       fe_face_values.normal_vector(q)) *
                      fe_face_values.JxW(q);
                  }

                local_boundary_fluxes[cell->face(f)->boundary_indicator()]
                += local_normal_flux;
              }

      // now communicate to get the global values
      std::map<types::boundary_id_t, double> global_boundary_fluxes;
      {
        // first collect local values in the same order in which they are listed
        // in the set of boundary indicators
        const std::set<types::boundary_id_t>
        boundary_indicators
          = this->get_geometry_model().get_used_boundary_indicators ();
        std::vector<double> local_values;
        for (std::set<types::boundary_id_t>::const_iterator
             p = boundary_indicators.begin();
             p != boundary_indicators.end(); ++p)
          local_values.push_back (local_boundary_fluxes[*p]);

        // then collect contributions from all processors
        std::vector<double> global_values;
        Utilities::MPI::sum (local_values, this->get_mpi_communicator(), global_values);

        // and now take them apart into the global map again
        unsigned int index = 0;
        for (std::set<types::boundary_id_t>::const_iterator
             p = boundary_indicators.begin();
             p != boundary_indicators.end(); ++p, ++index)
          global_boundary_fluxes[*p] = global_values[index];
      }

      // now add all of the computed heat fluxes to the statistics object
      // and create a single string that can be output to the screen
      std::ostringstream screen_text;
      unsigned int index = 0;
      for (std::map<types::boundary_id_t, double>::const_iterator
           p = global_boundary_fluxes.begin();
           p != global_boundary_fluxes.end(); ++p, ++index)
        {
          const std::string name = "Outward heat flux through boundary with indicator "
                                   + Utilities::int_to_string(p->first)
                                   + " (W)";
          statistics.add_value (name, p->second);

          // also make sure that the other columns filled by the this object
          // all show up with sufficient accuracy and in scientific notation
          statistics.set_precision (name, 8);
          statistics.set_scientific (name, true);

          // finally have something for the screen
          screen_text.precision(4);
          screen_text << p->second << " W"
                      << (index == global_boundary_fluxes.size()-1 ? "" : ", ");
        }

      return std::pair<std::string, std::string> ("Heat fluxes through boundary parts:",
                                                  screen_text.str());
    }
  }
}


// explicit instantiations
namespace aspect
{
  namespace Postprocess
  {
    ASPECT_REGISTER_POSTPROCESSOR(HeatFluxStatistics,
                                  "heat flux statistics",
                                  "A postprocessor that computes some statistics about "
                                  "the heat flux across boundaries.")
  }
}
