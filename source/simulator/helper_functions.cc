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


#include <aspect/simulator.h>
#include <aspect/global.h>


#include <deal.II/base/index_set.h>
#include <deal.II/base/conditional_ostream.h>
#include <deal.II/base/quadrature_lib.h>
#include <deal.II/lac/constraint_matrix.h>
#include <deal.II/lac/block_sparsity_pattern.h>
#include <deal.II/grid/grid_tools.h>

#include <deal.II/dofs/dof_renumbering.h>
#include <deal.II/dofs/dof_accessor.h>
#include <deal.II/dofs/dof_tools.h>

#include <deal.II/fe/fe_q.h>
#include <deal.II/fe/fe_dgq.h>
#include <deal.II/fe/fe_dgp.h>
#include <deal.II/fe/fe_values.h>

#include <deal.II/numerics/error_estimator.h>
#include <deal.II/numerics/vector_tools.h>

#include <deal.II/distributed/solution_transfer.h>
#include <deal.II/distributed/grid_refinement.h>

#include <fstream>
#include <iostream>
#include <iomanip>
#include <locale>
#include <string>


namespace aspect
{

  template <int dim>
  Simulator<dim>::TemperatureOrComposition::
  TemperatureOrComposition (const FieldType field_type,
                            const unsigned int compositional_variable)
    :
    field_type (field_type),
    compositional_variable (compositional_variable)
  {
    if (field_type == temperature_field)
      Assert (compositional_variable == numbers::invalid_unsigned_int,
              ExcMessage ("You can't specify a compositional variable if you "
                          "have in fact selected the temperature."));
  }



  template <int dim>
  typename Simulator<dim>::TemperatureOrComposition
  Simulator<dim>::TemperatureOrComposition::temperature ()
  {
    return TemperatureOrComposition(temperature_field);
  }



  template <int dim>
  typename Simulator<dim>::TemperatureOrComposition
  Simulator<dim>::TemperatureOrComposition::composition (const unsigned int compositional_variable)
  {
    return TemperatureOrComposition(compositional_field,
                                    compositional_variable);
  }


  template <int dim>
  bool
  Simulator<dim>::TemperatureOrComposition::is_temperature() const
  {
    return (field_type == temperature_field);
  }

  template <int dim>
  unsigned int
  Simulator<dim>::TemperatureOrComposition::block_index(const Introspection<dim> &introspection) const
  {
    if (this->is_temperature())
      return introspection.block_indices.temperature;
    else
      return introspection.block_indices.compositional_fields[compositional_variable];
  }


  template <int dim>
  void Simulator<dim>::output_program_stats()
  {
    if (!aspect::output_parallel_statistics)
      return;

    Utilities::System::MemoryStats stats;
    Utilities::System::get_memory_stats(stats);
    pcout << "VmPeak (proc0): " << stats.VmPeak/1024 << " mb" << std::endl;

    // memory consumption:
    const double mb = 1024*1024; //convert from bytes into mb
    pcout << "memory in MB:" << std::endl
          << "* tria " << triangulation.memory_consumption()/mb << std::endl
          << "  - p4est " << triangulation.memory_consumption_p4est()/mb << std::endl
          << "* DoFHandler " << dof_handler.memory_consumption()/mb <<std::endl
          << "* ConstraintMatrix " << constraints.memory_consumption()/mb << std::endl
          << "* current_constraints " << current_constraints.memory_consumption()/mb << std::endl
          << "* Matrix " << system_matrix.memory_consumption()/mb << std::endl
          << "* 5 Vectors " << 5*solution.memory_consumption()/mb << std::endl
          << "* preconditioner " << (system_preconditioner_matrix.memory_consumption()
                                     + Amg_preconditioner->memory_consumption()
                                     /*+Mp_preconditioner->memory_consumption()
                                                                      +T_preconditioner->memory_consumption()*/)/mb
          << std::endl
          << "  - matrix " << system_preconditioner_matrix.memory_consumption()/mb << std::endl
          << "  - prec vel " << Amg_preconditioner->memory_consumption()/mb << std::endl
          << "  - prec mass " << 0/*Mp_preconditioner->memory_consumption()/mb*/ << std::endl
          << "  - prec T " << 0/*T_preconditioner->memory_consumption()/mb*/ << std::endl
          << std::endl;
  }



  namespace
  {
    /**
     * A function that writes the statistics object into a file.
     *
     * @param stat_file_name The name of the file into which the result
     * should go
     * @param copy_of_table A copy of the table that we're to write. Since
     * this function is called in the background on a separate thread,
     * the actual table might be modified while we are about to write
     * it, so we need to work on a copy. This copy is deleted at the end
     * of this function.
     */
    void do_output_statistics (const std::string stat_file_name,
                               const TableHandler *copy_of_table)
    {
      // write into a temporary file for now so that we don't
      // interrupt anyone who might want to look at the real
      // statistics file while the program is still running
      const std::string tmp_file_name = stat_file_name + " tmp";

      std::ofstream stat_file (tmp_file_name.c_str());
      copy_of_table->write_text (stat_file,
                                 TableHandler::table_with_separate_column_description);
      stat_file.close();

      // now move the temporary file into place
      std::rename(tmp_file_name.c_str(), stat_file_name.c_str());

      // delete the copy now:
      delete copy_of_table;
    }
  }


  template <int dim>
  void Simulator<dim>::output_statistics()
  {
    // only write the statistics file from processor zero
    if (Utilities::MPI::this_mpi_process(mpi_communicator)!=0)
      return;

    if (parameters.convert_to_years == true)
      {
        statistics.set_scientific("Time (years)", true);
        statistics.set_scientific("Time step size (years)", true);
      }
    else
      {
        statistics.set_scientific("Time (seconds)", true);
        statistics.set_scientific("Time step size (seconds)", true);
      }

    // formatting the table we're about to output and writing the
    // actual file may take some time, so do it on a separate
    // thread. we pass a pointer to a copy of the statistics
    // object which the called function then has to destroy
    //
    // before we can start working on a new thread, we need to
    // make sure that the previous thread is done or they'll
    // stomp on each other's feet
    output_statistics_thread.join();
    output_statistics_thread = Threads::new_thread (&do_output_statistics,
                                                    parameters.output_directory+"statistics",
                                                    new TableHandler(statistics));
  }



  /**
   * Find the largest velocity throughout the domain.
   **/
  template <int dim>
  double Simulator<dim>::get_maximal_velocity (
    const LinearAlgebra::BlockVector &solution) const
  {
    // use a quadrature formula that has one point at
    // the location of each degree of freedom in the
    // velocity element
    const QIterated<dim> quadrature_formula (QTrapez<1>(),
                                             parameters.stokes_velocity_degree);
    const unsigned int n_q_points = quadrature_formula.size();


    FEValues<dim> fe_values (mapping, finite_element, quadrature_formula, update_values);
    std::vector<Tensor<1,dim> > velocity_values(n_q_points);

    double max_local_velocity = 0;

    // loop over all locally owned cells and evaluate the velocities at each
    // quadrature point (i.e. each node). keep a running tally of the largest
    // such velocity
    typename DoFHandler<dim>::active_cell_iterator
    cell = dof_handler.begin_active(),
    endc = dof_handler.end();
    for (; cell!=endc; ++cell)
      if (cell->is_locally_owned())
        {
          fe_values.reinit (cell);
          fe_values[introspection.extractors.velocities].get_function_values (solution,
                                                                              velocity_values);

          for (unsigned int q=0; q<n_q_points; ++q)
            max_local_velocity = std::max (max_local_velocity,
                                           velocity_values[q].norm());
        }

    // return the largest value over all processors
    return Utilities::MPI::max (max_local_velocity, mpi_communicator);
  }



  template <int dim>
  std::pair<double,bool> Simulator<dim>::compute_time_step () const
  {
    const QIterated<dim> quadrature_formula (QTrapez<1>(),
                                             parameters.stokes_velocity_degree);
    const unsigned int n_q_points = quadrature_formula.size();

    FEValues<dim> fe_values (mapping, finite_element, quadrature_formula, update_values | (parameters.use_conduction_timestep ? update_quadrature_points : update_default));
    std::vector<Tensor<1,dim> > velocity_values(n_q_points);
    std::vector<double> pressure_values(n_q_points), temperature_values(n_q_points);

    double new_time_step;
    bool convection_dominant;

    double max_local_speed_over_meshsize = 0;
    double min_local_conduction_timestep = std::numeric_limits<double>::max(), min_conduction_timestep;

    typename DoFHandler<dim>::active_cell_iterator
    cell = dof_handler.begin_active(),
    endc = dof_handler.end();
    for (; cell!=endc; ++cell)
      if (cell->is_locally_owned())
        {
          fe_values.reinit (cell);
          fe_values[introspection.extractors.velocities].get_function_values (solution,
                                                                              velocity_values);

          double max_local_velocity = 0;
          for (unsigned int q=0; q<n_q_points; ++q)
            max_local_velocity = std::max (max_local_velocity,
                                           velocity_values[q].norm());
          max_local_speed_over_meshsize = std::max(max_local_speed_over_meshsize,
                                                   max_local_velocity
                                                   /
                                                   cell->minimum_vertex_distance());
          if (parameters.use_conduction_timestep)
            {
              fe_values[introspection.extractors.pressure].get_function_values (solution,
                                                                                pressure_values);
              fe_values[introspection.extractors.temperature].get_function_values (solution,
                                                                                   temperature_values);
              // In the future we may want to evaluate thermal diffusivity at
              // each point in the mesh, but for now we just use the reference value
              for (unsigned int q=0; q<n_q_points; ++q)
                {
                  std::vector<double> composition_values_at_q_point (parameters.n_compositional_fields);
                  double      thermal_diffusivity;

                  // TODO: calculate composition field as well
                  thermal_diffusivity = material_model->thermal_diffusivity(temperature_values[q],
                                                                            pressure_values[q],
                                                                            composition_values_at_q_point,
                                                                            fe_values.quadrature_point(q));

                  min_local_conduction_timestep = std::min(min_local_conduction_timestep,
                                                           parameters.CFL_number*pow(cell->minimum_vertex_distance(),2)
                                                           / thermal_diffusivity);
                }
            }
        }

    const double max_global_speed_over_meshsize
      = Utilities::MPI::max (max_local_speed_over_meshsize, mpi_communicator);
    if (parameters.use_conduction_timestep)
      MPI_Allreduce (&min_local_conduction_timestep, &min_conduction_timestep, 1, MPI_DOUBLE, MPI_MIN, mpi_communicator);
    else
      min_conduction_timestep = std::numeric_limits<double>::max();

    // if the velocity is zero, then it is somewhat arbitrary what time step
    // we should choose. in that case, do as if the velocity was one
    if (max_global_speed_over_meshsize == 0 && !parameters.use_conduction_timestep)
      {
        new_time_step = (parameters.CFL_number /
                         (parameters.temperature_degree * 1));
        convection_dominant = false;
      }
    else
      {
        new_time_step = std::min(min_conduction_timestep,
                                 (parameters.CFL_number / (parameters.temperature_degree * max_global_speed_over_meshsize)));
        convection_dominant = (new_time_step < min_conduction_timestep);
      }

    return std::make_pair(new_time_step, convection_dominant);
  }


  namespace
  {
    void
    extract_composition_values_at_q_point (const std::vector<std::vector<double> > &composition_values,
                                           const unsigned int                      q,
                                           std::vector<double>                    &composition_values_at_q_point)
    {
      for (unsigned int k=0; k < composition_values_at_q_point.size(); ++k)
        composition_values_at_q_point[k] = composition_values[k][q];
    }
  }


  template <int dim>
  std::pair<double,double>
  Simulator<dim>::
  get_extrapolated_temperature_or_composition_range (const TemperatureOrComposition &temperature_or_composition) const
  {
    const QIterated<dim> quadrature_formula (QTrapez<1>(),
                                             (temperature_or_composition.is_temperature() ?
                                              parameters.temperature_degree :
                                              parameters.composition_degree));

    const unsigned int n_q_points = quadrature_formula.size();

    const FEValuesExtractors::Scalar field
      = (temperature_or_composition.is_temperature()
         ?
         introspection.extractors.temperature
         :
         introspection.extractors.compositional_fields[temperature_or_composition.compositional_variable]
        );

    FEValues<dim> fe_values (mapping, finite_element, quadrature_formula,
                             update_values);
    std::vector<double> old_field_values(n_q_points);
    std::vector<double> old_old_field_values(n_q_points);

    // This presets the minimum with a bigger
    // and the maximum with a smaller number
    // than one that is going to appear. Will
    // be overwritten in the cell loop or in
    // the communication step at the
    // latest.
    double min_local_field = std::numeric_limits<double>::max(),
           max_local_field = -std::numeric_limits<double>::max();

    if (timestep_number != 0)
      {
        typename DoFHandler<dim>::active_cell_iterator
        cell = dof_handler.begin_active(),
        endc = dof_handler.end();
        for (; cell!=endc; ++cell)
          if (cell->is_locally_owned())
            {
              fe_values.reinit (cell);
              fe_values[field].get_function_values (old_solution,
                                                    old_field_values);
              fe_values[field].get_function_values (old_old_solution,
                                                    old_old_field_values);

              for (unsigned int q=0; q<n_q_points; ++q)
                {
                  const double extrapolated_field =
                    (1. + time_step/old_time_step) * old_field_values[q]-
                    time_step/old_time_step * old_old_field_values[q];

                  min_local_field = std::min (min_local_field,
                                              extrapolated_field);
                  max_local_field = std::max (max_local_field,
                                              extrapolated_field);
                }
            }
      }
    else
      {
        typename DoFHandler<dim>::active_cell_iterator
        cell = dof_handler.begin_active(),
        endc = dof_handler.end();
        for (; cell!=endc; ++cell)
          if (cell->is_locally_owned())
            {
              fe_values.reinit (cell);
              fe_values[field].get_function_values (old_solution,
                                                    old_field_values);

              for (unsigned int q=0; q<n_q_points; ++q)
                {
                  const double extrapolated_field = old_field_values[q];

                  min_local_field = std::min (min_local_field,
                                              extrapolated_field);
                  max_local_field = std::max (max_local_field,
                                              extrapolated_field);
                }
            }
      }

    return std::make_pair(-Utilities::MPI::max (-min_local_field,
                                                mpi_communicator),
                          Utilities::MPI::max (max_local_field,
                                               mpi_communicator));
  }



  /*
   * normalize the pressure by calculating the surface integral of the pressure on the outer
   * shell and subtracting this from all pressure nodes.
   */
  template <int dim>
  void Simulator<dim>::normalize_pressure(LinearAlgebra::BlockVector &vector)
  {
    if (parameters.pressure_normalization == "no")
      return;

    double my_pressure = 0.0;
    double my_area = 0.0;
    if (parameters.pressure_normalization == "surface")
      {
        QGauss < dim - 1 > quadrature (parameters.stokes_velocity_degree + 1);

        const unsigned int n_q_points = quadrature.size();
        FEFaceValues<dim> fe_face_values (mapping, finite_element,  quadrature,
                                          update_JxW_values | update_values);

        std::vector<double> pressure_values(n_q_points);

        typename DoFHandler<dim>::active_cell_iterator
        cell = dof_handler.begin_active(),
        endc = dof_handler.end();
        for (; cell != endc; ++cell)
          if (cell->is_locally_owned())
            {
              for (unsigned int face_no = 0; face_no < GeometryInfo<dim>::faces_per_cell; ++face_no)
                {
                  const typename DoFHandler<dim>::face_iterator face = cell->face (face_no);
                  if (face->at_boundary()
                      &&
                      (geometry_model->depth (face->center()) <
                       (face->diameter() / std::sqrt(1.*dim-1) / 3)))
                    {
                      fe_face_values.reinit (cell, face_no);
                      fe_face_values[introspection.extractors.pressure].get_function_values(vector,
                                                                                            pressure_values);

                      for (unsigned int q = 0; q < n_q_points; ++q)
                        {
                          my_pressure += pressure_values[q]
                                         * fe_face_values.JxW (q);
                          my_area += fe_face_values.JxW (q);
                        }
                    }
                }
            }
      }
    else if (parameters.pressure_normalization=="volume")
      {
        const QGauss<dim> quadrature (parameters.stokes_velocity_degree + 1);

        const unsigned int n_q_points = quadrature.size();
        FEValues<dim> fe_values (mapping, finite_element,  quadrature,
                                 update_JxW_values | update_values);

        std::vector<double> pressure_values(n_q_points);

        typename DoFHandler<dim>::active_cell_iterator
        cell = dof_handler.begin_active(),
        endc = dof_handler.end();
        for (; cell != endc; ++cell)
          if (cell->is_locally_owned())
            {
              fe_values.reinit (cell);
              fe_values[introspection.extractors.pressure].get_function_values(vector,
                                                                               pressure_values);

              for (unsigned int q = 0; q < n_q_points; ++q)
                {
                  my_pressure += pressure_values[q]
                                 * fe_values.JxW (q);
                  my_area += fe_values.JxW (q);
                }
            }
      }
    else
      AssertThrow (false, ExcMessage("Invalid pressure normalization method: " +
                                     parameters.pressure_normalization));

    pressure_adjustment = 0.0;
    // sum up the integrals from each processor
    {
      const double my_temp[2] = {my_pressure, my_area};
      double temp[2];
      Utilities::MPI::sum (my_temp, mpi_communicator, temp);
      if (parameters.pressure_normalization == "surface")
        pressure_adjustment = -temp[0]/temp[1] + parameters.surface_pressure;
      else if (parameters.pressure_normalization == "volume")
//TODO: This can't be right. it should be -temp[0]/temp[1] to divide
        // by the volume. this was definitely wrong in ASPIRE
        pressure_adjustment = -temp[0];
      else
        AssertThrow(false, ExcNotImplemented());
    }

    // A complication is that we can't modify individual
    // elements of the solution vector since that one has ghost element.
    // rather, we first need to localize it and then distribute back
    LinearAlgebra::BlockVector distributed_vector (introspection.index_sets.system_partitioning,
                                                   mpi_communicator);
    distributed_vector = vector;

    if (parameters.use_locally_conservative_discretization == false)
      distributed_vector.block(1).add(pressure_adjustment);
    else
      {
        // this case is a bit more complicated: if the condition above is false
        // then we use the FE_DGP element for which the shape functions do not
        // add up to one; consequently, adding a constant to all degrees of
        // freedom does not alter the overall function by that constant, but
        // by something different
        //
        // we can work around this by using the documented property of the
        // FE_DGP element that the first shape function is constant.
        // consequently, adding the adjustment to the global function is
        // achieved by adding the adjustment to the first pressure degree
        // of freedom on each cell.
        Assert (dynamic_cast<const FE_DGP<dim>*>(&finite_element.base_element(1)) != 0,
                ExcInternalError());
        std::vector<types::global_dof_index> local_dof_indices (finite_element.dofs_per_cell);
        typename DoFHandler<dim>::active_cell_iterator
        cell = dof_handler.begin_active(),
        endc = dof_handler.end();
        for (; cell != endc; ++cell)
          if (cell->is_locally_owned())
            {
              // identify the first pressure dof
              cell->get_dof_indices (local_dof_indices);
              const unsigned int first_pressure_dof
                = finite_element.component_to_system_index (dim, 0);

              // make sure that this DoF is really owned by the current processor
              // and that it is in fact a pressure dof
              Assert (dof_handler.locally_owned_dofs().is_element(local_dof_indices[first_pressure_dof]),
                      ExcInternalError());
              Assert (local_dof_indices[first_pressure_dof] >= vector.block(0).size(),
                      ExcInternalError());

              // then adjust its value
              distributed_vector(local_dof_indices[first_pressure_dof]) += pressure_adjustment;
            }
      }

    // now get back to the original vector
    vector = distributed_vector;
  }


  /*
   * inverse to normalize_pressure.
   */
  template <int dim>
  void Simulator<dim>::denormalize_pressure (LinearAlgebra::BlockVector &vector)
  {
    if (parameters.pressure_normalization == "no")
      return;

    if (parameters.use_locally_conservative_discretization == false)
      vector.block (1).add (-1.0 * pressure_adjustment);
    else
      {
        // this case is a bit more complicated: if the condition above is false
        // then we use the FE_DGP element for which the shape functions do not
        // add up to one; consequently, adding a constant to all degrees of
        // freedom does not alter the overall function by that constant, but
        // by something different
        //
        // we can work around this by using the documented property of the
        // FE_DGP element that the first shape function is constant.
        // consequently, adding the adjustment to the global function is
        // achieved by adding the adjustment to the first pressure degree
        // of freedom on each cell.
        Assert (dynamic_cast<const FE_DGP<dim>*>(&finite_element.base_element(1)) != 0,
                ExcInternalError());
        std::vector<types::global_dof_index> local_dof_indices (finite_element.dofs_per_cell);
        typename DoFHandler<dim>::active_cell_iterator
        cell = dof_handler.begin_active(),
        endc = dof_handler.end();
        for (; cell != endc; ++cell)
          if (cell->is_locally_owned())
            {
              // identify the first pressure dof
              cell->get_dof_indices (local_dof_indices);
              const unsigned int first_pressure_dof
                = finite_element.component_to_system_index (dim, 0);

              // make sure that this DoF is really owned by the current processor
              // and that it is in fact a pressure dof
              Assert (dof_handler.locally_owned_dofs().is_element(local_dof_indices[first_pressure_dof]),
                      ExcInternalError());
              Assert (local_dof_indices[first_pressure_dof] >= vector.block(0).size(),
                      ExcInternalError());

              // then adjust its value
              vector (local_dof_indices[first_pressure_dof]) -= pressure_adjustment;
            }
      }
  }



  /**
   * This routine adjusts the second block of the right hand side of the
   * system containing the compressibility, so that the system becomes
   * compatible. See the general documentation of this class for more
   * information.
   */
  template <int dim>
  void Simulator<dim>::make_pressure_rhs_compatible(LinearAlgebra::BlockVector &vector)
  {
    if (parameters.use_locally_conservative_discretization)
      AssertThrow(false, ExcNotImplemented());

    if (do_pressure_rhs_compatibility_modification)
      {
        const double mean       = vector.block(1).mean_value();
        const double correction = -mean*vector.block(1).size()/global_volume;

        vector.block(1).add(correction, pressure_shape_function_integrals.block(1));
      }
  }


  template <int dim>
  template<class FUNCTOR>
  void Simulator<dim>::compute_depth_average(std::vector<double> &values, FUNCTOR &fctr) const
  {
    const unsigned int num_slices = 100;
    values.resize(num_slices);
    std::vector<double> volume(num_slices);

    // this yields 100 quadrature points evenly distributed in the interior of the cell.
    // We avoid points on the faces, as they would be counted more than once.
    const QIterated<dim> quadrature_formula (QMidpoint<1>(),
                                             10);
    const unsigned int n_q_points = quadrature_formula.size();
    const double max_depth = geometry_model->maximal_depth();

    FEValues<dim> fe_values (mapping,
                             finite_element,
                             quadrature_formula,
                             update_values | update_gradients | update_quadrature_points);

    std::vector<std::vector<double> > composition_values (parameters.n_compositional_fields,std::vector<double> (n_q_points));

    std::vector<double> output_values(quadrature_formula.size());

    typename DoFHandler<dim>::active_cell_iterator
    cell = dof_handler.begin_active(),
    endc = dof_handler.end();

    typename MaterialModel::Interface<dim>::MaterialModelInputs in(n_q_points,
                                                                   parameters.n_compositional_fields);
    typename MaterialModel::Interface<dim>::MaterialModelOutputs out(n_q_points,
        parameters.n_compositional_fields);

    fctr.setup(quadrature_formula.size());

    for (; cell!=endc; ++cell)
      if (cell->is_locally_owned())
        {
          fe_values.reinit (cell);
          if (fctr.need_material_properties())
            {
              fe_values[introspection.extractors.pressure].get_function_values (this->solution,
                                                                                in.pressure);
              fe_values[introspection.extractors.temperature].get_function_values (this->solution,
                                                                                   in.temperature);
              fe_values[introspection.extractors.velocities].get_function_symmetric_gradients (this->solution,
                  in.strain_rate);
              for (unsigned int c=0; c<parameters.n_compositional_fields; ++c)
                fe_values[introspection.extractors.compositional_fields[c]].get_function_values(this->solution,
                                                                                                composition_values[c]);

              for (unsigned int i=0; i<n_q_points; ++i)
                {
                  in.position[i] = fe_values.quadrature_point(i);
                  for (unsigned int c=0; c<parameters.n_compositional_fields; ++c)
                    in.composition[i][c] = composition_values[c][i];
                }
              material_model->evaluate(in, out);
            }

          fctr(in, out, fe_values, this->solution, output_values);

          for (unsigned int q = 0; q < n_q_points; ++q)
            {
              const double depth = geometry_model->depth(fe_values.quadrature_point(q));
              const unsigned int idx = static_cast<unsigned int>((depth*num_slices)/max_depth);
              Assert(idx<num_slices, ExcInternalError());

              values[idx] += output_values[q] * fe_values.JxW(q);
              volume[idx] += fe_values.JxW(q);
            }
        }

    std::vector<double> values_all(num_slices);
    std::vector<double> volume_all(num_slices);
    Utilities::MPI::sum(volume, mpi_communicator, volume_all);
    Utilities::MPI::sum(values, mpi_communicator, values_all);

    for (unsigned int i=0; i<num_slices; ++i)
      values[i] = values_all[i] / (static_cast<double>(volume_all[i])+1e-20);
  }

  namespace
  {
    template <int dim>
    class FunctorDepthAverageField
    {
      public:
        FunctorDepthAverageField(const FEValuesExtractors::Scalar &field)
          : field_(field) {}

        bool need_material_properties()
        {
          return false;
        }

        void setup(unsigned int q_points)
        {
        }

        void operator()(const typename MaterialModel::Interface<dim>::MaterialModelInputs &in,
                        const typename MaterialModel::Interface<dim>::MaterialModelOutputs &out,
                        FEValues<dim> &fe_values,
                        const LinearAlgebra::BlockVector &solution, std::vector<double> &output)
        {
          fe_values[field_].get_function_values (solution, output);
        }

        const FEValuesExtractors::Scalar field_;
    };
  }

  template <int dim>
  void Simulator<dim>::compute_depth_average_field(const TemperatureOrComposition &temperature_or_composition,
                                                   std::vector<double> &values) const
  {
    const FEValuesExtractors::Scalar field
      = (temperature_or_composition.is_temperature()
         ?
         introspection.extractors.temperature
         :
         introspection.extractors.compositional_fields[temperature_or_composition.compositional_variable]
        );



    FunctorDepthAverageField<dim> f(field);

    compute_depth_average(values, f);
  }

  namespace
  {
    template <int dim>
    class FunctorDepthAverageViscosity
    {
      public:
        bool need_material_properties()
        {
          return true;
        }

        void setup(unsigned int q_points)
        {}

        void operator()(const typename MaterialModel::Interface<dim>::MaterialModelInputs &in,
                        const typename MaterialModel::Interface<dim>::MaterialModelOutputs &out,
                        FEValues<dim> &fe_values,
                        const LinearAlgebra::BlockVector &solution, std::vector<double> &output)
        {
          output = out.viscosities;
        }
    };
  }

  template <int dim>
  void Simulator<dim>::compute_depth_average_viscosity(std::vector<double> &values) const
  {
    FunctorDepthAverageViscosity<dim> f;

    compute_depth_average(values, f);
  }

  namespace
  {
    template <int dim>
    class FunctorDepthAverageVelocityMagnitude
    {
      public:
        FunctorDepthAverageVelocityMagnitude(const FEValuesExtractors::Vector &field)
          : field_(field) {}

        bool need_material_properties()
        {
          return false;
        }

        void setup(unsigned int q_points)
        {
          velocity_values.resize(q_points);
        }

        void operator()(const typename MaterialModel::Interface<dim>::MaterialModelInputs &in,
                        const typename MaterialModel::Interface<dim>::MaterialModelOutputs &out,
                        FEValues<dim> &fe_values,
                        const LinearAlgebra::BlockVector &solution, std::vector<double> &output)
        {
          fe_values[field_].get_function_values (solution, velocity_values);
          for (unsigned int q=0; q<output.size(); ++q)
            output[q] = velocity_values[q] * velocity_values[q];
        }

        std::vector<Tensor<1,dim> > velocity_values;
        const FEValuesExtractors::Vector field_;
    };
  }


  template <int dim>
  void Simulator<dim>::compute_depth_average_velocity_magnitude(std::vector<double> &values) const
  {
    FunctorDepthAverageVelocityMagnitude<dim> f(introspection.extractors.velocities);

    compute_depth_average(values, f);
  }

  namespace
  {
    template <int dim>
    class FunctorDepthAverageSinkingVelocity
    {
      public:
        FunctorDepthAverageSinkingVelocity(const FEValuesExtractors::Vector &field, GravityModel::Interface<dim> *gravity)
          : field_(field), gravity_(gravity) {}

        bool need_material_properties()
        {
          return false;
        }

        void setup(unsigned int q_points)
        {
          velocity_values.resize(q_points);
        }

        void operator()(const typename MaterialModel::Interface<dim>::MaterialModelInputs &in,
                        const typename MaterialModel::Interface<dim>::MaterialModelOutputs &out,
                        FEValues<dim> &fe_values,
                        const LinearAlgebra::BlockVector &solution, std::vector<double> &output)
        {
          fe_values[field_].get_function_values (solution, velocity_values);
          for (unsigned int q=0; q<output.size(); ++q)
            {
              Tensor<1,dim> g = gravity_->gravity_vector(in.position[q]);
              output[q] = std::fabs(std::min(-1e-16,g*velocity_values[q]/g.norm()))*year_in_seconds;
            }
        }

        std::vector<Tensor<1,dim> > velocity_values;
        const FEValuesExtractors::Vector field_;
        GravityModel::Interface<dim> *gravity_;
    };
  }


  template <int dim>
  void Simulator<dim>::compute_depth_average_sinking_velocity(std::vector<double> &values) const
  {
    FunctorDepthAverageSinkingVelocity<dim> f(introspection.extractors.velocities,
                                              this->gravity_model.get());

    compute_depth_average(values, f);
  }

  namespace
  {
    template <int dim>
    class FunctorDepthAverageVsVp
    {
      public:
        FunctorDepthAverageVsVp(const MaterialModel::Interface<dim> *mm, bool vs)
          : material_model(mm), vs_(vs)
        {}

        bool need_material_properties()
        {
          return true;
        }

        void setup(unsigned int q_points)
        {}

        void operator()(const typename MaterialModel::Interface<dim>::MaterialModelInputs &in,
                        const typename MaterialModel::Interface<dim>::MaterialModelOutputs &out,
                        FEValues<dim> &fe_values,
                        const LinearAlgebra::BlockVector &solution, std::vector<double> &output)
        {
          if (vs_)
            for (unsigned int q=0; q<output.size(); ++q)
              output[q] = material_model->seismic_Vs(
                            in.temperature[q], in.pressure[q], in.composition[q],
                            in.position[q]);
          else
            for (unsigned int q=0; q<output.size(); ++q)
              output[q] = material_model->seismic_Vp(
                            in.temperature[q], in.pressure[q], in.composition[q],
                            in.position[q]);
        }

        const MaterialModel::Interface<dim> *material_model;
        bool vs_;
    };

  }

  template <int dim>
  void Simulator<dim>::compute_depth_average_Vs(std::vector<double> &values) const
  {
    FunctorDepthAverageVsVp<dim> f(this->material_model.get(), true /* Vs */);

    compute_depth_average(values, f);
  }

  template <int dim>
  void Simulator<dim>::compute_depth_average_Vp(std::vector<double> &values) const
  {
    FunctorDepthAverageVsVp<dim> f(this->material_model.get(), false /* Vp */);

    compute_depth_average(values, f);
  }



  template <int dim>
  bool
  Simulator<dim>::stokes_matrix_depends_on_solution() const
  {
    // currently, the only coefficient that really appears on the
    // left hand side of the Stokes equation is the viscosity. note
    // that our implementation of compressible materials makes sure
    // that the density does not appear on the lhs.
    return (material_model->viscosity_depends_on (MaterialModel::NonlinearDependence::any_variable)
            == true);
  }
}

// explicit instantiation of the functions we implement in this file
namespace aspect
{
#define INSTANTIATE(dim) \
  template class Simulator<dim>::TemperatureOrComposition; \
  template void Simulator<dim>::normalize_pressure(LinearAlgebra::BlockVector &vector); \
  template void Simulator<dim>::denormalize_pressure(LinearAlgebra::BlockVector &vector); \
  template double Simulator<dim>::get_maximal_velocity (const LinearAlgebra::BlockVector &solution) const; \
  template std::pair<double,double> Simulator<dim>::get_extrapolated_temperature_or_composition_range (const TemperatureOrComposition &temperature_or_composition) const; \
  template std::pair<double,bool> Simulator<dim>::compute_time_step () const; \
  template void Simulator<dim>::make_pressure_rhs_compatible(LinearAlgebra::BlockVector &vector); \
  template void Simulator<dim>::compute_depth_average_field(const TemperatureOrComposition &temperature_or_composition, std::vector<double> &values) const; \
  template void Simulator<dim>::compute_depth_average_viscosity(std::vector<double> &values) const; \
  template void Simulator<dim>::compute_depth_average_velocity_magnitude(std::vector<double> &values) const; \
  template void Simulator<dim>::compute_depth_average_sinking_velocity(std::vector<double> &values) const; \
  template void Simulator<dim>::compute_depth_average_Vs(std::vector<double> &values) const; \
  template void Simulator<dim>::compute_depth_average_Vp(std::vector<double> &values) const; \
  template void Simulator<dim>::output_program_stats(); \
  template void Simulator<dim>::output_statistics(); \
  template bool Simulator<dim>::stokes_matrix_depends_on_solution() const;

  ASPECT_INSTANTIATE(INSTANTIATE)
}
