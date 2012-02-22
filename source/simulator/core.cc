/* $Id$ */
/* Author: Martin Kronbichler, Uppsala University,
           Wolfgang Bangerth, Texas A&M University,
     Timo Heister, University of Goettingen, 2008-2011 */
/*                                                                */
/*    Copyright (C) 2008, 2009, 2010, 2011, 2012 by the deal.II authors */
/*                                                                */
/*    This file is subject to QPL and may not be  distributed     */
/*    without copyright and license information. Please refer     */
/*    to the file deal.II/doc/license.html for the  text  and     */
/*    further information on this license.                        */

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
#include <deal.II/numerics/derivative_approximation.h>
#include <deal.II/numerics/vectors.h>

#include <deal.II/distributed/solution_transfer.h>
#include <deal.II/distributed/grid_refinement.h>

#include <fstream>
#include <iostream>
#include <iomanip>
#include <locale>
#include <string>
#include <../contrib/tbb/tbb30_104oss/include/tbb/tbb_exception.h>


using namespace dealii;


namespace aspect
{
  namespace
  {
    /**
     * Return whether t is an element of the given container object.
     */
    template <typename Container>
    bool is_element (const typename Container::value_type &t,
                     const Container                      &container)
    {
      for (typename Container::const_iterator p = container.begin();
           p != container.end();
           ++p)
        if (*p == t)
          return true;

      return false;
    }
  }

  /**
   * Constructor. Initialize all member variables.
   **/
  template <int dim>
  Simulator<dim>::Simulator (ParameterHandler &prm)
    :
    parameters (prm),
    pcout (std::cout,
           (Utilities::MPI::
            this_mpi_process(MPI_COMM_WORLD)
            == 0)),

    computing_timer (pcout, TimerOutput::summary,
                     TimerOutput::wall_times),

    geometry_model (GeometryModel::create_geometry_model<dim>(prm)),
    material_model (MaterialModel::create_material_model<dim>(prm)),
    gravity_model (GravityModel::create_gravity_model<dim>(prm)),
    boundary_temperature (BoundaryTemperature::create_boundary_temperature<dim>(prm)),
    initial_conditions (InitialConditions::create_initial_conditions (prm,
                                                                      *geometry_model,
                                                                      *boundary_temperature,
                                                                      *adiabatic_conditions)),

    time (std::numeric_limits<double>::quiet_NaN()),
    time_step (0),
    old_time_step (0),
    timestep_number (0),

    triangulation (MPI_COMM_WORLD,
                   typename Triangulation<dim>::MeshSmoothing
                   (Triangulation<dim>::smoothing_on_refinement |
                    Triangulation<dim>::smoothing_on_coarsening),
                   parallel::distributed::Triangulation<dim>::mesh_reconstruction_after_repartitioning),

    mapping (4),

    system_fe(FE_Q<dim>(parameters.stokes_velocity_degree),
              dim,
              (parameters.use_locally_conservative_discretization
               ?
               static_cast<const FiniteElement<dim> &>
               (FE_DGP<dim>(parameters.stokes_velocity_degree-1))
               :
               static_cast<const FiniteElement<dim> &>
               (FE_Q<dim>(parameters.stokes_velocity_degree-1))),
              1,
              FE_Q<dim>(parameters.temperature_degree),
              1),

    system_dof_handler (triangulation),

    rebuild_stokes_matrix (true),
    rebuild_stokes_preconditioner (true)
  {
    // first do some error checking for the parameters we got
    {
      //TODO. like making sure boundary indicators don't appear in multiple lists
    }

    // continue with initializing members that can't be initialized for one reason
    // or another in the member initializer list above
    postprocess_manager.parse_parameters (prm);
    postprocess_manager.initialize (*this);

    geometry_model->create_coarse_mesh (triangulation);
    global_Omega_diameter = GridTools::diameter (triangulation);

    adiabatic_conditions.reset (new AdiabaticConditions<dim>(*geometry_model,
                                                             *gravity_model,
                                                             *material_model));

    pressure_scaling = material_model->reference_viscosity() / geometry_model->length_scale();

    // make sure that we don't have to fill every column of the statistics
    // object in each time step.
    statistics.set_auto_fill_mode(true);

    // finally produce a record of the run-time parameters by writing
    // the currently used values into a file
    {
      std::ofstream prm_out ((parameters.output_directory + "parameters.prm").c_str());
      AssertThrow (prm_out,
                   ExcMessage (std::string("Couldn't open file <") +
                               parameters.output_directory + "parameters.prm>."));
      prm.print_parameters(prm_out, ParameterHandler::Text);
    }
    {
      std::ofstream prm_out ((parameters.output_directory + "parameters.tex").c_str());
      AssertThrow (prm_out,
                   ExcMessage (std::string("Couldn't open file <") +
                               parameters.output_directory + "parameters.tex>."));
      prm.print_parameters(prm_out, ParameterHandler::LaTeX);
    }
  }


  template <int dim>
  void
  Simulator<dim>::
  start_timestep ()
  {
    // first produce some output for the screen to show where we are
    if (parameters.convert_to_years == true)
      pcout << "*** Timestep " << timestep_number
            << ":  t=" << time/year_in_seconds
            << " years"
            << std::endl;
    else
      pcout << "*** Timestep " << timestep_number
            << ":  t=" << time
            << " seconds"
            << std::endl;

    // set global statistics about this time step
    statistics.add_value("Time step number", timestep_number);
    if (parameters.convert_to_years == true)
      statistics.add_value("Time (years)", time / year_in_seconds);
    else
      statistics.add_value("Time (seconds)", time);
    statistics.add_value("Number of mesh cells",
                         triangulation.n_global_active_cells());

    std::vector<unsigned int> system_sub_blocks (dim+2,0);
    system_sub_blocks[dim] = 1;
    system_sub_blocks[dim+1] = 2;
    std::vector<unsigned int> system_dofs_per_block (3);
    DoFTools::count_dofs_per_block (system_dof_handler, system_dofs_per_block,
                                    system_sub_blocks);

    statistics.add_value("Number of Stokes degrees of freedom",
                         system_dofs_per_block[0]+system_dofs_per_block[1]);
    statistics.add_value("Number of temperature degrees of freedom",
                         system_dofs_per_block[2]);

    // then interpolate the current boundary velocities. this adds to
    // the stokes_constraints object we already have
    {
      IndexSet system_relevant_set;
      DoFTools::extract_locally_relevant_dofs (system_dof_handler,
                                               system_relevant_set);

      current_system_constraints.clear ();
      current_system_constraints.reinit (system_relevant_set);
      current_system_constraints.merge (system_constraints);

      typedef unsigned char boundary_indicator_t;
      const std::set<boundary_indicator_t>
      prescribed_velocity_boundary_indicators (parameters.prescribed_velocity_boundary_indicators.begin(),
                                               parameters.prescribed_velocity_boundary_indicators.end());

      // do the interpolation for the prescribed velocity field
      std::vector<bool> velocity_mask (dim+2, true);
      velocity_mask[dim] = false;
      velocity_mask[dim+1] = false;
      for (std::set<boundary_indicator_t>::const_iterator
           p = prescribed_velocity_boundary_indicators.begin();
           p != prescribed_velocity_boundary_indicators.end(); ++p)
        VectorTools::interpolate_boundary_values (system_dof_handler,
                                                  *p,
                                                  ZeroFunction<dim>(dim+2),
                                                  current_system_constraints,
                                                  velocity_mask);
      current_system_constraints.close();
    }
  }



  template <int dim>
  void
  Simulator<dim>::
  setup_system_matrix (const std::vector<IndexSet> &system_partitioning)
  {
    system_matrix.clear ();

    TrilinosWrappers::BlockSparsityPattern sp (system_partitioning,
                                               MPI_COMM_WORLD);

    Table<2,DoFTools::Coupling> coupling (dim+2, dim+2);

    // TODO: determine actual non-zero blocks depending on which
    // dependencies are present in the material model
    for (unsigned int c=0; c<dim+2; ++c)
      for (unsigned int d=0; d<dim+2; ++d)
        coupling[c][d] = DoFTools::always;

    DoFTools::make_sparsity_pattern (system_dof_handler,
                                     coupling, sp,
                                     system_constraints, false,
                                     Utilities::MPI::
                                     this_mpi_process(MPI_COMM_WORLD));
    sp.compress();

    system_matrix.reinit (sp);
  }



  template <int dim>
  void Simulator<dim>::
  setup_system_preconditioner (const std::vector<IndexSet> &system_partitioning)
  {
    Amg_preconditioner.reset ();
    Mp_preconditioner.reset ();
    T_preconditioner.reset ();

    system_preconditioner_matrix.clear ();

    TrilinosWrappers::BlockSparsityPattern sp (system_partitioning,
                                               MPI_COMM_WORLD);

    Table<2,DoFTools::Coupling> coupling (dim+2, dim+2);
    for (unsigned int c=0; c<dim+2; ++c)
      for (unsigned int d=0; d<dim+2; ++d)
        if (c == d)
          coupling[c][d] = DoFTools::always;
        else
          coupling[c][d] = DoFTools::none;

    DoFTools::make_sparsity_pattern (system_dof_handler,
                                     coupling, sp,
                                     system_constraints, false,
                                     Utilities::MPI::
                                     this_mpi_process(MPI_COMM_WORLD));
    sp.compress();

    system_preconditioner_matrix.reinit (sp);
  }



  template <int dim>
  void Simulator<dim>::setup_dofs ()
  {
    computing_timer.enter_section("Setup dof systems");

    system_dof_handler.distribute_dofs(system_fe);

    // Renumber the DoFs hierarchical so that we get the
    // same numbering if we resume the computation. This
    // is because the numbering depends on the order the
    // cells are created.
    DoFRenumbering::hierarchical (system_dof_handler);
    std::vector<unsigned int> system_sub_blocks (dim+2,0);
    system_sub_blocks[dim] = 1;
    system_sub_blocks[dim+1] = 2;
    DoFRenumbering::component_wise (system_dof_handler, system_sub_blocks);

    std::vector<unsigned int> system_dofs_per_block (3);
    DoFTools::count_dofs_per_block (system_dof_handler, system_dofs_per_block,
                                    system_sub_blocks);

    const unsigned int n_u = system_dofs_per_block[0],
                       n_p = system_dofs_per_block[1],
                       n_T = system_dofs_per_block[2];

    // print dof numbers with 1000s
    // separator since they are frequently
    // large
    std::locale s = pcout.get_stream().getloc();
    // Creating std::locale with an empty string causes problems
    // on some platforms, so catch the exception and ignore
    try
      {
        pcout.get_stream().imbue(std::locale(""));
      }
    catch (std::runtime_error e)
      {
        // If the locale doesn't work, just give up
      }
    pcout << "Number of active cells: "
          << triangulation.n_global_active_cells()
          << " (on "
          << triangulation.n_levels()
          << " levels)"
          << std::endl
          << "Number of degrees of freedom: "
          << n_u + n_p + n_T
          << " (" << n_u << '+' << n_p << '+'<< n_T <<')'
          << std::endl
          << std::endl;
    pcout.get_stream().imbue(s);


    // now also compute the various partitionings between processors and blocks
    // of vectors and matrices

    std::vector<IndexSet> system_partitioning, system_relevant_partitioning;
    IndexSet system_relevant_set;
    {
      IndexSet system_index_set = system_dof_handler.locally_owned_dofs();
      system_partitioning.push_back(system_index_set.get_view(0,n_u));
      system_partitioning.push_back(system_index_set.get_view(n_u,n_u+n_p));
      system_partitioning.push_back(system_index_set.get_view(n_u+n_p,n_u+n_p+n_T));

      DoFTools::extract_locally_relevant_dofs (system_dof_handler,
                                               system_relevant_set);
      system_relevant_partitioning.push_back(system_relevant_set.get_view(0,n_u));
      system_relevant_partitioning.push_back(system_relevant_set.get_view(n_u,n_u+n_p));
      system_relevant_partitioning.push_back(system_relevant_set.get_view(n_u+n_p, n_u+n_p+n_T));
    }

    // then compute constraints for the velocity. the constraints we compute
    // here are the ones that are the same for all following time steps. in
    // addition, we may be computing constraints from boundary values for the
    // velocity that are different between time steps. these are then put
    // into current_stokes_constraints in start_timestep().
    {
      system_constraints.clear();
      system_constraints.reinit(system_relevant_set);

      DoFTools::make_hanging_node_constraints (system_dof_handler,
                                               system_constraints);

      // obtain the boundary indicators that belong to zero velocity
      // and no-normal-flux type
      typedef unsigned char boundary_indicator_t;
      const std::set<boundary_indicator_t>
      zero_boundary_indicators (parameters.zero_velocity_boundary_indicators.begin(),
                                parameters.zero_velocity_boundary_indicators.end());

      const std::set<boundary_indicator_t>
      no_normal_flux_boundary_indicators (parameters.tangential_velocity_boundary_indicators.begin(),
                                          parameters.tangential_velocity_boundary_indicators.end());

      // do the interpolation for zero velocity
      std::vector<bool> velocity_mask (dim+2, true);
      velocity_mask[dim] = false;
      velocity_mask[dim+1] = false;
      for (std::set<boundary_indicator_t>::const_iterator
           p = zero_boundary_indicators.begin();
           p != zero_boundary_indicators.end(); ++p)
        VectorTools::interpolate_boundary_values (system_dof_handler,
                                                  *p,
                                                  ZeroFunction<dim>(dim+2),
                                                  system_constraints,
                                                  velocity_mask);


      // do the same for no-normal-flux boundaries
      VectorTools::compute_no_normal_flux_constraints (system_dof_handler,
                                                       /* first_vector_component= */ 0,
                                                       no_normal_flux_boundary_indicators,
                                                       system_constraints,
                                                       mapping);
    }

    // now do the same for the temperature variable
    {

      // obtain the boundary indicators that belong to Dirichlet-type
      // temperature boundary conditions and interpolate the temperature
      // there
      std::vector<bool> temperature_mask (dim+2, false);
      temperature_mask[dim+1] = true;

      for (std::vector<int>::const_iterator
           p = parameters.fixed_temperature_boundary_indicators.begin();
           p != parameters.fixed_temperature_boundary_indicators.end(); ++p)
        {
          Assert (is_element (*p, geometry_model->get_used_boundary_indicators()),
                  ExcInternalError());
          VectorTools::interpolate_boundary_values (system_dof_handler,
                                                    *p,
                                                    VectorFunctionFromScalarFunctionObject<dim>(std_cxx1x::bind (&BoundaryTemperature::Interface<dim>::temperature,
                                                        std_cxx1x::cref(*boundary_temperature),
                                                        std_cxx1x::cref(*geometry_model),
                                                        *p,
                                                        std_cxx1x::_1),
                                                        dim+1,
                                                        dim+2),
                                                    system_constraints,
                                                    temperature_mask);

        }
      system_constraints.close();
    }

    // finally initialize vectors, matrices, etc.

    setup_system_matrix (system_partitioning);
    setup_system_preconditioner (system_partitioning);

    system_rhs.reinit(system_partitioning, MPI_COMM_WORLD);
    system_solution.reinit(system_relevant_partitioning, MPI_COMM_WORLD);
    old_system_solution.reinit(system_relevant_partitioning, MPI_COMM_WORLD);
    old_old_system_solution.reinit(system_relevant_partitioning, MPI_COMM_WORLD);

    if (material_model->is_compressible())
      pressure_shape_function_integrals.reinit (system_partitioning, MPI_COMM_WORLD);

    rebuild_stokes_matrix         = true;
    rebuild_stokes_preconditioner = true;

    computing_timer.exit_section();
  }



  template <int dim>
  void Simulator<dim>::postprocess ()
  {
    computing_timer.enter_section ("Postprocessing");
    pcout << "   Postprocessing:" << std::endl;

    // run all the postprocessing routines and then write
    // the current state of the statistics table to a file
    std::list<std::pair<std::string,std::string> >
    output_list = postprocess_manager.execute (statistics);

    std::ofstream stat_file ((parameters.output_directory+"statistics").c_str());
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

    statistics.write_text (stat_file,
                           TableHandler::table_with_separate_column_description);

    // determine the width of the first column of text so that
    // everything gets nicely aligned; then output everything
    {
      unsigned int width = 0;
      for (std::list<std::pair<std::string,std::string> >::const_iterator
           p = output_list.begin();
           p != output_list.end(); ++p)
        width = std::max<unsigned int> (width, p->first.size());

      for (std::list<std::pair<std::string,std::string> >::const_iterator
           p = output_list.begin();
           p != output_list.end(); ++p)
        pcout << "     "
              << std::left
              << std::setw(width)
              << p->first
              << " "
              << p->second
              << std::endl;
    }

    pcout << std::endl;
    computing_timer.exit_section ();
  }



// Contrary to step-32, we have found that just refining by the temperature
// works well in 2d, but only leads to refinement in the boundary layer at the
// core-mantle boundary in 3d. Consequently, we estimate the error based
// on the temperature, velocity and other criteria, see the second ASPECT paper;
// the vectors with the resulting error indicators are then normalized, and we
// take the maximum or sum of the indicators to decide whether we want to refine or
// not. In case of more complicated materials with jumps in the density
// profile, we also need to refine where the density jumps. This ensures that
// we also refine into plumes where maybe the temperature gradients aren't as
// strong as in the boundary layer but where nevertheless the gradients in the
// velocity are large.
  template <int dim>
  void Simulator<dim>::refine_mesh (const unsigned int max_grid_level)
  {
    computing_timer.enter_section ("Refine mesh structure, part 1");

    Vector<float> estimated_error_per_cell (triangulation.n_active_cells());
    Vector<float> estimated_error_per_cell_rho (triangulation.n_active_cells());
    Vector<float> estimated_error_per_cell_T (triangulation.n_active_cells());
    Vector<float> estimated_error_per_cell_u (triangulation.n_active_cells());

    // compute density error
    {
      TrilinosWrappers::MPI::BlockVector vec_distributed (this->system_rhs);

      Quadrature<dim> quadrature( system_fe.get_unit_support_points() );
      std::vector<unsigned int> local_dof_indices (system_fe.dofs_per_cell);
      FEValues<dim> fe_values (mapping, system_fe, quadrature,
                               update_quadrature_points|update_values);

      const FEValuesExtractors::Scalar pressure (dim);
      std::vector<double> pressure_values(quadrature.size());

      typename DoFHandler<dim>::active_cell_iterator
      cell = system_dof_handler.begin_active(),
      endc = system_dof_handler.end();
      for (; cell!=endc; ++cell)
        if (cell->is_locally_owned())
          {
            fe_values.reinit(cell);

            fe_values[pressure].get_function_values (system_solution,
                                                     pressure_values);

            cell->get_dof_indices (local_dof_indices);

            // look up density
            for (unsigned int i=0; i<system_fe.dofs_per_cell; ++i)
              {
                if (system_fe.system_to_component_index(i).first == dim+1)

                  vec_distributed(local_dof_indices[i])
                    = material_model->density( system_solution(local_dof_indices[i]),
                                               pressure_values[i],
                                               fe_values.quadrature_point(i));
              }
          }

      TrilinosWrappers::MPI::BlockVector vec (this->system_solution);
      vec = vec_distributed;

      DerivativeApproximation::approximate_gradient  (mapping, system_dof_handler, vec, estimated_error_per_cell_rho, dim+1);

      // Scale gradient in each cell with the
      // correct power of h. Otherwise, error
      // indicators do not reduce when
      // refined if there is a density
      // jump. We need at least order 1 for
      // the error not to grow when refining,
      // so anything >1 should work.
      {
        typename DoFHandler<dim>::active_cell_iterator
        cell = system_dof_handler.begin_active(),
        endc = system_dof_handler.end();
        unsigned int i=0;
        for (; cell!=endc; ++cell, ++i)
          if (cell->is_locally_owned())
            estimated_error_per_cell_rho(i) *= std::pow(cell->diameter(), 1+dim/2.0);
      }
    }

    // compute the errors for
    // temperature and stokes solution
    {

      std::vector<bool> temperature_mask (dim+2, false);
      temperature_mask[dim+1] = true;

      KellyErrorEstimator<dim>::estimate (system_dof_handler,
                                          QGauss<dim-1>(parameters.temperature_degree+1),
                                          typename FunctionMap<dim>::type(),
                                          system_solution,
                                          estimated_error_per_cell_T,
                                          temperature_mask,
                                          0,
                                          0,
                                          triangulation.locally_owned_subdomain());

      std::vector<bool> velocity_mask (dim+2, true);
      velocity_mask[dim] = false;
      velocity_mask[dim+1] = false;
      KellyErrorEstimator<dim>::estimate (system_dof_handler,
                                          QGauss<dim-1>(parameters.stokes_velocity_degree+1),
                                          typename FunctionMap<dim>::type(),
                                          system_solution,
                                          estimated_error_per_cell_u,
                                          velocity_mask,
                                          0,
                                          0,
                                          triangulation.locally_owned_subdomain());
    }

    // rescale errors
    {
      if (estimated_error_per_cell_T.linfty_norm() != 0)
	estimated_error_per_cell_T /= Utilities::MPI::max (estimated_error_per_cell_T.linfty_norm(),
							   MPI_COMM_WORLD);
      if (estimated_error_per_cell_u.linfty_norm() != 0)
	estimated_error_per_cell_u /= Utilities::MPI::max (estimated_error_per_cell_u.linfty_norm(),
							   MPI_COMM_WORLD);
      if (estimated_error_per_cell_rho.linfty_norm() != 0)
	estimated_error_per_cell_rho /= Utilities::MPI::max (estimated_error_per_cell_rho.linfty_norm(),
							     MPI_COMM_WORLD);

      for (unsigned int i=0; i<estimated_error_per_cell.size(); ++i)
        estimated_error_per_cell(i) = std::max(
                                        std::max (estimated_error_per_cell_T(i),
                                                  0.0f*estimated_error_per_cell_u(i)),
                                        estimated_error_per_cell_rho(i));
    }

    parallel::distributed::GridRefinement::
    refine_and_coarsen_fixed_fraction (triangulation,
                                       estimated_error_per_cell,
                                       parameters.refinement_fraction,
                                       parameters.coarsening_fraction);

    // limit maximum refinement level
    if (triangulation.n_levels() > max_grid_level)
      for (typename Triangulation<dim>::active_cell_iterator
           cell = triangulation.begin_active(max_grid_level);
           cell != triangulation.end(); ++cell)
        cell->clear_refine_flag ();

    std::vector<const TrilinosWrappers::MPI::BlockVector *> x_system (2);
    x_system[0] = &system_solution;
    x_system[1] = &old_system_solution;

    parallel::distributed::SolutionTransfer<dim,TrilinosWrappers::MPI::BlockVector>
    system_trans(system_dof_handler);

    triangulation.prepare_coarsening_and_refinement();
    system_trans.prepare_for_coarsening_and_refinement(x_system);

    triangulation.execute_coarsening_and_refinement ();
    global_volume = GridTools::volume (triangulation, mapping);
    computing_timer.exit_section();

    setup_dofs ();

    computing_timer.enter_section ("Refine mesh structure, part 2");

    {
      TrilinosWrappers::MPI::BlockVector
      distributed_system (system_rhs);
      TrilinosWrappers::MPI::BlockVector
      old_distributed_system (system_rhs);
      std::vector<TrilinosWrappers::MPI::BlockVector *> system_tmp (2);
      system_tmp[0] = &(distributed_system);
      system_tmp[1] = &(old_distributed_system);

      system_trans.interpolate (system_tmp);
      system_solution     = distributed_system;
      old_system_solution = old_distributed_system;
    }

    computing_timer.exit_section();
  }



  /**
   * This is the main function of the program, containing the overall
   * logic which function is called when.
   */
  template <int dim>
  void Simulator<dim>::run ()
  {
    // if we want to resume a computation from an earlier point
    // then reload it from a snapshot. otherwise do the basic
    // start-up
    if (parameters.resume_computation == true)
      {
        resume_from_snapshot();
      }
    else
      {
        triangulation.refine_global (parameters.initial_global_refinement);
        global_volume = GridTools::volume (triangulation, mapping);

        setup_dofs();
      }

    unsigned int max_refinement_level = parameters.initial_global_refinement +
                                        parameters.initial_adaptive_refinement;
    unsigned int pre_refinement_step = 0;

  start_time_iteration:

    if (parameters.resume_computation == false)
      {
        set_initial_temperature_field ();
        compute_initial_pressure_field ();

        time                      = parameters.start_time;
        timestep_number           = 0;
        time_step = old_time_step = 0;
      }

    // start the principal loop over time steps:
    do
      {
        start_timestep ();

        // then do the core work: assemble systems and solve
        assemble_temperature_system ();
        solve_temperature();

        assemble_stokes_system();
        build_stokes_preconditioner();
        solve_stokes();

        pcout << std::endl;

        // update the time step size
        old_time_step = time_step;
        time_step = compute_time_step();

        if (parameters.convert_to_years == true)
          statistics.add_value("Time step size (years)", time_step / year_in_seconds);
        else
          statistics.add_value("Time step size (seconds)", time_step);


        // see if we have to start over with a new refinement cycle
        // at the beginning of the simulation
        if ((timestep_number == 0) &&
            (pre_refinement_step < parameters.initial_adaptive_refinement))
          {
            refine_mesh (max_refinement_level);
            ++pre_refinement_step;
            goto start_time_iteration;
          }

        postprocess ();

        // see if this is a time step where additional refinement is requested
        // if so, then loop over as many times as this is necessary
        if ((parameters.additional_refinement_times.size() > 0)
            &&
            (parameters.additional_refinement_times.front () < time+time_step))
          {
            while ((parameters.additional_refinement_times.size() > 0)
                   &&
                   (parameters.additional_refinement_times.front () < time+time_step))
              {
                ++max_refinement_level;
                refine_mesh (max_refinement_level);

                parameters.additional_refinement_times
                .erase (parameters.additional_refinement_times.begin());
              }
          }
        else
          // see if this is a time step where regular refinement is necessary, but only
          // if the previous rule wasn't triggered
          if ((timestep_number > 0)
              &&
              (timestep_number % parameters.adaptive_refinement_interval == 0))
              refine_mesh (max_refinement_level);


        // every 100 time steps output a summary of the current
        // timing information
        if ((timestep_number > 0) && (timestep_number % 100 == 0))
          computing_timer.print_summary ();

        // increment time step by one. then prepare
        // for the next time step by shifting solution vectors
        // by one time step and presetting the current solution based
        // on an extrapolation
        time += time_step;
        ++timestep_number;
        {
          old_old_system_solution      = old_system_solution;
          old_system_solution          = system_solution;
          if (old_time_step > 0)
              system_solution.sadd (1.+time_step/old_time_step, -time_step/old_time_step,
                                    old_old_system_solution);
        }

        // periodically generate snapshots so that we can resume here
        // if the program aborts or is terminated
        if (timestep_number % 50 == 0)
          {
            create_snapshot();
            // matrices will be regenerated after a resume, so do that here too
            // to be consistent. otherwise we would get different results
            // for a restarted computation than for one that ran straight
            // through
            rebuild_stokes_matrix =
              rebuild_stokes_preconditioner = true;
          }
      }
    while (time < parameters.end_time);
  }
}



// explicit instantiation of the functions we implement in this file
namespace aspect
{
  template
  class Simulator<deal_II_dimension>;
}
