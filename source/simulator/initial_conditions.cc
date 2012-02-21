/* $Id$ */
/* Author: Martin Kronbichler, Uppsala University,
           Wolfgang Bangerth, Texas A&M University,
     Timo Heister, University of Goettingen, 2008-2011 */
/*                                                                */
/*    Copyright (C) 2008, 2009, 2010, 2011 by the deal.II authors */
/*                                                                */
/*    This file is subject to QPL and may not be  distributed     */
/*    without copyright and license information. Please refer     */
/*    to the file deal.II/doc/license.html for the  text  and     */
/*    further information on this license.                        */

#include <aspect/simulator.h>
#include <aspect/adiabatic_conditions.h>
#include <aspect/initial_conditions/interface.h>

#include <deal.II/base/quadrature_lib.h>
#include <deal.II/base/function.h>

#include <deal.II/lac/full_matrix.h>
#include <deal.II/lac/constraint_matrix.h>
#include <deal.II/grid/tria_iterator.h>
#include <deal.II/dofs/dof_accessor.h>
#include <deal.II/fe/fe_values.h>
#include <deal.II/numerics/vectors.h>


namespace aspect
{

  template <int dim>
  void Simulator<dim>::set_initial_temperature_field ()
  {
    // create a fully distributed vector since
    // the VectorTools::interpolate function
    // needs to write into it and we can not
    // write into vectors with ghost elements
    TrilinosWrappers::MPI::BlockVector initial_solution;
    initial_solution.reinit(system_rhs);

    // interpolate the initial values
    VectorTools::interpolate (mapping,
                              system_dof_handler,
                              VectorFunctionFromScalarFunctionObject<dim>(std_cxx1x::bind (&InitialConditions::Interface<dim>::initial_temperature,
                                                                          std_cxx1x::cref(*initial_conditions),
                                                                          std_cxx1x::_1),
                                                                          dim+1,
                                                                          dim+2),
                              initial_solution);

    // then apply constraints and copy the
    // result into vectors with ghost elements
    system_constraints.distribute(initial_solution);
    
    // copy temperature block only
    system_solution.block(2) = initial_solution.block(2);
    old_system_solution.block(2) = initial_solution.block(2);
    old_old_system_solution.block(2) = initial_solution.block(2);
  }



  template <int dim>
  void Simulator<dim>::compute_initial_pressure_field ()
  {
    // we'd like to interpolate the initial pressure onto the pressure
    // variable but that's a bit involved because the pressure may either
    // be an FE_Q (for which we can interpolate) or an FE_DGP (for which
    // we can't since the element has no nodal basis.
    //
    // fortunately, in the latter case, the element is discontinuous and
    // we can compute a local projection onto the pressure space
    if (parameters.use_locally_conservative_discretization == false)
      {
        // allocate a vector that is distributed but doesn't have
        // ghost elements (vectors with ghost elements are not
        // writable); the stokes_rhs vector is a valid template for
        // this kind of thing. interpolate into it and later copy it into the
        // solution vector that does have the necessary ghost elements
        TrilinosWrappers::MPI::BlockVector system_tmp;
        system_tmp.reinit (system_rhs);

        // interpolate the pressure given by the adiabatic conditions
        // object onto the solution space. note that interpolate
        // wants a function that represents all components of the
        // solution vector, so create such a function object
        // that is simply zero for all velocity components
        VectorTools::interpolate (mapping, system_dof_handler,
                                  VectorFunctionFromScalarFunctionObject<dim> (std_cxx1x::bind (&AdiabaticConditions<dim>::pressure,
                                                                               std_cxx1x::cref (*adiabatic_conditions),
                                                                               std_cxx1x::_1),
                                                                               dim,
                                                                               dim+2),
                                  system_tmp);

        // we may have hanging nodes, so apply constraints
        system_constraints.distribute (system_tmp);

        old_system_solution.block(1) = system_tmp.block(1);
      }
    else
      {
        // implement the local projection for the discontinuous pressure
        // element. this is only going to work if, indeed, the element
        // is discontinuous
        const FiniteElement<dim> &system_pressure_fe = system_fe.base_element(1);
        Assert (system_pressure_fe.dofs_per_face == 0,
                ExcNotImplemented());

        TrilinosWrappers::MPI::BlockVector system_tmp;
        system_tmp.reinit (system_rhs);

        QGauss<dim> quadrature(parameters.stokes_velocity_degree+1);
        UpdateFlags update_flags = UpdateFlags(update_values   |
                                               update_quadrature_points |
                                               update_JxW_values);

        FEValues<dim> fe_values (mapping, system_fe, quadrature, update_flags);
        const FEValuesExtractors::Scalar pressure (dim);

        const unsigned int
        dofs_per_cell = fe_values.dofs_per_cell,
        n_q_points    = fe_values.n_quadrature_points;

        std::vector<unsigned int> local_dof_indices (dofs_per_cell);
        Vector<double> cell_vector (dofs_per_cell);
        Vector<double> local_projection (dofs_per_cell);
        FullMatrix<double> local_mass_matrix (dofs_per_cell, dofs_per_cell);

        std::vector<double> rhs_values(n_q_points);

        ScalarFunctionFromFunctionObject<dim>
        adiabatic_pressure (std_cxx1x::bind (&AdiabaticConditions<dim>::pressure,
                                             std_cxx1x::cref(*adiabatic_conditions),
                                             std_cxx1x::_1));


        typename DoFHandler<dim>::active_cell_iterator
        cell = system_dof_handler.begin_active(),
        endc = system_dof_handler.end();

        for (; cell!=endc; ++cell)
          if (cell->is_locally_owned())
            {
              cell->get_dof_indices (local_dof_indices);
              fe_values.reinit(cell);

              adiabatic_pressure.value_list (fe_values.get_quadrature_points(),
                                             rhs_values);

              cell_vector = 0;
              local_mass_matrix = 0;
              for (unsigned int point=0; point<n_q_points; ++point)
                for (unsigned int i=0; i<dofs_per_cell; ++i)
                  {
                    if (system_fe.system_to_component_index(i).first == dim)
                      cell_vector(i)
                      +=
                        rhs_values[point] *
                        fe_values[pressure].value(i,point) *
                        fe_values.JxW(point);

                    // populate the local matrix; create the pressure mass matrix
                    // in the pressure pressure block and the identity matrix
                    // for all other variables so that the whole thing remains
                    // invertible
                    for (unsigned int j=0; j<dofs_per_cell; ++j)
                      if ((system_fe.system_to_component_index(i).first == dim)
                          &&
                          (system_fe.system_to_component_index(j).first == dim))
                        local_mass_matrix(j,i) += (fe_values[pressure].value(i,point) *
                                                   fe_values[pressure].value(j,point) *
                                                   fe_values.JxW(point));
                      else if (i == j)
                        local_mass_matrix(i,j) = 1;
                  }

              // now invert the local mass matrix and multiply it with the rhs
              local_mass_matrix.gauss_jordan();
              local_mass_matrix.vmult (local_projection, cell_vector);

              // then set the global solution vector to the values just computed
              cell->set_dof_values (local_projection, system_tmp);
            }

        old_system_solution.block(1) = system_tmp.block(1);
      }

    // normalize the pressure in such a way that the surface pressure
    // equals a known and desired value
    normalize_pressure(old_system_solution);

    // set the current solution to the same value as the previous solution
    system_solution = old_system_solution;
  }
}



// explicit instantiation of the functions we implement in this file
namespace aspect
{
  template void Simulator<deal_II_dimension>::set_initial_temperature_field();
  template void Simulator<deal_II_dimension>::compute_initial_pressure_field();
}
