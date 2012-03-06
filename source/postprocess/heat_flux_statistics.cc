//-------------------------------------------------------------
//    $Id$
//
//    Copyright (C) 2011, 2012 by the authors of the ASPECT code
//
//-------------------------------------------------------------

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
//TODO: think about whether it would be useful to only get the degree of the temperature component of the FESystem
      const QGauss<dim-1> quadrature_formula (this->get_dof_handler().get_fe().degree+1);

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
          =
            this->get_geometry_model().get_used_boundary_indicators ();
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
          global_boundary_fluxes[*p] = local_values[index];
      }


//TODO: This doesn't scale to more geometry models. simply output the data as is,
// i.e. with association from boundary id to heat flux.
      // record results and have something for the output. this depends
      // on the interpretation of what boundary is which, which we can
      // only do knowing what the geometry is
      if (dynamic_cast<const GeometryModel::SphericalShell<dim> *>(&this->get_geometry_model())
          != 0)
        {
          // we have a spherical shell. note that in that case we want
          // to invert the sign of the core-mantle flux because we
          // have computed the flux with a normal pointing from
          // the mantle into the core, and not the other way around

          statistics.add_value ("Core-mantle heat flux (W)", -global_boundary_fluxes[0]);
          statistics.add_value ("Surface heat flux (W)",     global_boundary_fluxes[1]);

          // also make sure that the other columns filled by the this object
          // all show up with sufficient accuracy and in scientific notation
          statistics.set_precision ("Core-mantle heat flux (W)", 8);
          statistics.set_scientific ("Core-mantle heat flux (W)", true);
          statistics.set_precision ("Surface heat flux (W)", 8);
          statistics.set_scientific ("Surface heat flux (W)", true);

          // finally have something for the screen
          std::ostringstream output;
          output.precision(4);
          output << -global_boundary_fluxes[0] << " W, "
                 << global_boundary_fluxes[1] << " W";

          return std::pair<std::string, std::string> ("Inner/outer heat fluxes:",
                                                      output.str());
        }
      else if (dynamic_cast<const GeometryModel::Box<dim> *>(&this->get_geometry_model())
               != 0)
        {
          // for the box geometry we can associate boundary indicators 0 and 1
          // to left and right boundaries
          statistics.add_value ("Left boundary heat flux (W)", global_boundary_fluxes[0]);
          statistics.add_value ("Right boundary heat flux (W)", global_boundary_fluxes[1]);

          // also make sure that the other columns filled by the this object
          // all show up with sufficient accuracy and in scientific notation
          statistics.set_precision ("Left boundary heat flux (W)", 8);
          statistics.set_scientific ("Left boundary heat flux (W)", true);
          statistics.set_precision ("Right boundary heat flux (W)", 8);
          statistics.set_scientific ("Right boundary heat flux (W)", true);

          // finally have something for the screen
          std::ostringstream output;
          output.precision(4);
          output << global_boundary_fluxes[0] << " W, "
                 << global_boundary_fluxes[1] << " W";

          return std::pair<std::string, std::string> ("Left/right heat fluxes:",
                                                      output.str());
        }
      else
        AssertThrow (false, ExcNotImplemented());

      return std::pair<std::string, std::string> ("",
                                                  "");
    }
  }
}


// explicit instantiations
namespace aspect
{
  namespace Postprocess
  {
    template class HeatFluxStatistics<deal_II_dimension>;

    ASPECT_REGISTER_POSTPROCESSOR(HeatFluxStatistics,
                                  "heat flux statistics",
                                  "A postprocessor that computes some statistics about "
                                  "the heat flux across boundaries.")
  }
}
