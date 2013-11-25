/*
  Copyright (C) 2011, 2012, 2013 by the authors of the ASPECT code.

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
#include <aspect/adiabatic_conditions.h>
#include <aspect/initial_conditions/interface.h>
#include <aspect/compositional_initial_conditions/interface.h>

#include <deal.II/base/quadrature_lib.h>
#include <deal.II/base/function.h>

#include <deal.II/lac/full_matrix.h>
#include <deal.II/lac/constraint_matrix.h>
#include <deal.II/grid/tria_iterator.h>
#include <deal.II/dofs/dof_accessor.h>
#include <deal.II/fe/fe_values.h>
#include <deal.II/numerics/vector_tools.h>


namespace aspect
{

  template <int dim>
  void Simulator<dim>::set_initial_temperature_and_compositional_fields ()
  {
    // below, we would want to call VectorTools::interpolate on the
    // entire FESystem. there currently is no way to restrict the
    // interpolation operations to only a subset of vector
    // components (oversight in deal.II?), specifically to the
    // temperature component. this causes more work than necessary
    // but worse yet, it doesn't work for the DGP(q) pressure element
    // if we use a locally conservative formulation since there the
    // pressure element is non-interpolating (we get an exception
    // even though we are, strictly speaking, not interested in
    // interpolating the pressure; but, as mentioned, there is no way
    // to tell VectorTools::interpolate that)
    //
    // to work around this problem, the following code is essentially
    // a (simplified) copy of the code in VectorTools::interpolate
    // that only works on the temperature component

    // create a fully distributed vector since we
    // need to write into it and we can not
    // write into vectors with ghost elements
    LinearAlgebra::BlockVector initial_solution;
    bool normalize_composition = false;
    double max_sum_comp = 0.0;
    double global_max = 0.0;

    for (unsigned int n=0; n<1+parameters.n_compositional_fields; ++n)
      {
        initial_solution.reinit(system_rhs,false);

        // base element in the finite element is 2 for temperature (n=0) and 3 for
        // compositional fields (n>0)
//TODO: can we use introspection here, instead of the hard coded numbers?
        const unsigned int base_element = (n==0 ? 2 : 3);

        // get the temperature/composition support points
        const std::vector<Point<dim> > support_points
          = finite_element.base_element(base_element).get_unit_support_points();
        Assert (support_points.size() != 0,
                ExcInternalError());

        // create an FEValues object with just the temperature/composition element
        FEValues<dim> fe_values (mapping, finite_element,
                                 support_points,
                                 update_quadrature_points);

        std::vector<types::global_dof_index> local_dof_indices (finite_element.dofs_per_cell);

        for (typename DoFHandler<dim>::active_cell_iterator cell = dof_handler.begin_active();
             cell != dof_handler.end(); ++cell)
          if (cell->is_locally_owned())
            {
              fe_values.reinit (cell);

              // go through the temperature/composition dofs and set their global values
              // to the temperature/composition field interpolated at these points
              cell->get_dof_indices (local_dof_indices);
              for (unsigned int i=0; i<finite_element.base_element(base_element).dofs_per_cell; ++i)
                {
                  const unsigned int system_local_dof
                    = finite_element.component_to_system_index(/*temperature/composition component=*/dim+1+n,
                        /*dof index within component=*/i);

                  double value =
                    (base_element == 2 ?
                     initial_conditions->initial_temperature(fe_values.quadrature_point(i))
                     : compositional_initial_conditions->initial_composition(fe_values.quadrature_point(i),n-1));
                  initial_solution(local_dof_indices[system_local_dof]) = value;

                  if (base_element != 2)
                    Assert (value >= 0,
                            ExcMessage("Invalid initial conditions: Composition is negative"));

                  // if it is specified in the parameter file that the sum of all compositional fields
                  // must not exceed one, this should be checked
                  if (parameters.normalized_fields.size()>0 && n == 1)
                    {
                      double sum = 0;
                      for (unsigned int m=0; m<parameters.normalized_fields.size(); ++m)
                        sum += compositional_initial_conditions->initial_composition(fe_values.quadrature_point(i),parameters.normalized_fields[m]);
                      if (abs(sum) > 1.0+1e-6)
                        {
                          max_sum_comp = std::max(sum,max_sum_comp);
                          normalize_composition = true;
                        }
                    }

                }
            }

        initial_solution.compress(VectorOperation::insert);

        // we should not have written at all into any of the blocks with
        // the exception of the current temperature or composition block
        for (unsigned int b=0; b<initial_solution.n_blocks(); ++b)
          if (b != 2+n)
            Assert (initial_solution.block(b).l2_norm() == 0,
                    ExcInternalError());

        // if at least one processor decides that it needs
        // to normalize, do the same on all processors.
        int my_normalize_decision = normalize_composition;
        int global_dec = Utilities::MPI::max (my_normalize_decision, mpi_communicator);

        if (global_dec>0)
          {
            global_max = Utilities::MPI::max (max_sum_comp, mpi_communicator);
            if (n==1) pcout << "Sum of compositional fields is not one, fields will be normalized" << std::endl;
            for (unsigned int m=0; m<parameters.normalized_fields.size(); ++m)
              if (n-1==parameters.normalized_fields[m]) initial_solution/=global_max;
          }

        // then apply constraints and copy the
        // result into vectors with ghost elements
        constraints.distribute(initial_solution);

        // copy temperature/composition block only
        solution.block(2+n) = initial_solution.block(2+n);
        old_solution.block(2+n) = initial_solution.block(2+n);
        old_old_solution.block(2+n) = initial_solution.block(2+n);
      }
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
        LinearAlgebra::BlockVector system_tmp;
        system_tmp.reinit (system_rhs);

        // interpolate the pressure given by the adiabatic conditions
        // object onto the solution space. note that interpolate
        // wants a function that represents all components of the
        // solution vector, so create such a function object
        // that is simply zero for all velocity components
        VectorTools::interpolate (mapping, dof_handler,
                                  VectorFunctionFromScalarFunctionObject<dim> (std_cxx1x::bind (&AdiabaticConditions<dim>::pressure,
                                                                               std_cxx1x::cref (*adiabatic_conditions),
                                                                               std_cxx1x::_1),
                                                                               dim,
                                                                               dim+2+parameters.n_compositional_fields),
                                  system_tmp);

        // we may have hanging nodes, so apply constraints
        constraints.distribute (system_tmp);

        old_solution.block(1) = system_tmp.block(1);
      }
    else
      {
        // implement the local projection for the discontinuous pressure
        // element. this is only going to work if, indeed, the element
        // is discontinuous
        const FiniteElement<dim> &system_pressure_fe = finite_element.base_element(1);
        Assert (system_pressure_fe.dofs_per_face == 0,
                ExcNotImplemented());

        LinearAlgebra::BlockVector system_tmp;
        system_tmp.reinit (system_rhs);

        QGauss<dim> quadrature(parameters.stokes_velocity_degree+1);
        UpdateFlags update_flags = UpdateFlags(update_values   |
                                               update_quadrature_points |
                                               update_JxW_values);

        FEValues<dim> fe_values (mapping, finite_element, quadrature, update_flags);

        const unsigned int
        dofs_per_cell = fe_values.dofs_per_cell,
        n_q_points    = fe_values.n_quadrature_points;

        std::vector<types::global_dof_index> local_dof_indices (dofs_per_cell);
        Vector<double> cell_vector (dofs_per_cell);
        Vector<double> local_projection (dofs_per_cell);
        FullMatrix<double> local_mass_matrix (dofs_per_cell, dofs_per_cell);

        std::vector<double> rhs_values(n_q_points);

        ScalarFunctionFromFunctionObject<dim>
        adiabatic_pressure (std_cxx1x::bind (&AdiabaticConditions<dim>::pressure,
                                             std_cxx1x::cref(*adiabatic_conditions),
                                             std_cxx1x::_1));


        typename DoFHandler<dim>::active_cell_iterator
        cell = dof_handler.begin_active(),
        endc = dof_handler.end();

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
                    if (finite_element.system_to_component_index(i).first == dim)
                      cell_vector(i)
                      +=
                        rhs_values[point] *
                        fe_values[introspection.extractors.pressure].value(i,point) *
                        fe_values.JxW(point);

                    // populate the local matrix; create the pressure mass matrix
                    // in the pressure pressure block and the identity matrix
                    // for all other variables so that the whole thing remains
                    // invertible
                    for (unsigned int j=0; j<dofs_per_cell; ++j)
                      if ((finite_element.system_to_component_index(i).first == dim)
                          &&
                          (finite_element.system_to_component_index(j).first == dim))
                        local_mass_matrix(j,i) += (fe_values[introspection.extractors.pressure].value(i,point) *
                                                   fe_values[introspection.extractors.pressure].value(j,point) *
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

        old_solution.block(1) = system_tmp.block(1);
      }

    // normalize the pressure in such a way that the surface pressure
    // equals a known and desired value
    normalize_pressure(old_solution);

    // set all solution vectors to the same value as the previous solution
    solution = old_solution;
    old_old_solution = old_solution;
  }
}



// explicit instantiation of the functions we implement in this file
namespace aspect
{
#define INSTANTIATE(dim) \
  template void Simulator<dim>::set_initial_temperature_and_compositional_fields(); \
  template void Simulator<dim>::compute_initial_pressure_field();

  ASPECT_INSTANTIATE(INSTANTIATE)
}
