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


#include <aspect/global.h>
#include <aspect/initial_conditions/interface.h>

#include <deal.II/base/exceptions.h>
#include <deal.II/base/std_cxx1x/tuple.h>

#include <list>


namespace aspect
{
  namespace InitialConditions
  {
    template <int dim>
    Interface<dim>::~Interface ()
    {}


    template <int dim>
    void
    Interface<dim>::initialize (const GeometryModel::Interface<dim>       &geometry_model_,
                                const BoundaryTemperature::Interface<dim> &boundary_temperature_,
                                const AdiabaticConditions<dim>            &adiabatic_conditions_)
    {
      geometry_model       = &geometry_model_;
      boundary_temperature = &boundary_temperature_;
      adiabatic_conditions = &adiabatic_conditions_;
    }


    template <int dim>
    void
    Interface<dim>::
    declare_parameters (dealii::ParameterHandler &prm)
    {}


    template <int dim>
    void
    Interface<dim>::parse_parameters (dealii::ParameterHandler &prm)
    {}


// -------------------------------- Deal with registering initial_conditions models and automating
// -------------------------------- their setup and selection at run time

    namespace
    {
      std_cxx1x::tuple
      <void *,
      void *,
      internal::Plugins::PluginList<Interface<2> >,
      internal::Plugins::PluginList<Interface<3> > > registered_plugins;
    }



    template <int dim>
    void
    register_initial_conditions_model (const std::string &name,
                                       const std::string &description,
                                       void (*declare_parameters_function) (ParameterHandler &),
                                       Interface<dim> *(*factory_function) ())
    {
      std_cxx1x::get<dim>(registered_plugins).register_plugin (name,
                                                               description,
                                                               declare_parameters_function,
                                                               factory_function);
    }


    template <int dim>
    Interface<dim> *
    create_initial_conditions (ParameterHandler &prm,
                               const GeometryModel::Interface<dim> &geometry_model,
                               const BoundaryTemperature::Interface<dim> &boundary_temperature,
                               const AdiabaticConditions<dim>      &adiabatic_conditions)
    {
      std::string model_name;
      prm.enter_subsection ("Initial conditions");
      {
        model_name = prm.get ("Model name");
      }
      prm.leave_subsection ();

      Interface<dim> *plugin = std_cxx1x::get<dim>(registered_plugins).create_plugin (model_name,
                                                                                      "Initial conditions::Model name",
                                                                                      prm);
      plugin->initialize (geometry_model,
                          boundary_temperature,
                          adiabatic_conditions);
      return plugin;
    }



    template <int dim>
    void
    declare_parameters (ParameterHandler &prm)
    {
      // declare the entry in the parameter file
      prm.enter_subsection ("Initial conditions");
      {
        const std::string pattern_of_names
          = std_cxx1x::get<dim>(registered_plugins).get_pattern_of_names ();
        try
        {
        prm.declare_entry ("Model name", "",
                           Patterns::Selection (pattern_of_names),
                           "Select one of the following models:\n\n"
                           +
                           std_cxx1x::get<dim>(registered_plugins).get_description_string());
        }
        catch (const ParameterHandler::ExcValueDoesNotMatchPattern &)
        {
            // ignore the fact that the default value for this parameter
            // does not match the pattern
        }
      }
      prm.leave_subsection ();

      std_cxx1x::get<dim>(registered_plugins).declare_parameters (prm);
    }
  }
}

// explicit instantiations
namespace aspect
{
  namespace internal
  {
    namespace Plugins
    {
      template <>
      std::list<internal::Plugins::PluginList<InitialConditions::Interface<2> >::PluginInfo> *
      internal::Plugins::PluginList<InitialConditions::Interface<2> >::plugins = 0;
      template <>
      std::list<internal::Plugins::PluginList<InitialConditions::Interface<3> >::PluginInfo> *
      internal::Plugins::PluginList<InitialConditions::Interface<3> >::plugins = 0;
    }
  }

  namespace InitialConditions
  {
#define INSTANTIATE(dim) \
  template class Interface<dim>; \
  \
  template \
  void \
  register_initial_conditions_model<dim> (const std::string &, \
                                          const std::string &, \
                                          void ( *) (ParameterHandler &), \
                                          Interface<dim> *( *) ()); \
  \
  template  \
  void \
  declare_parameters<dim> (ParameterHandler &); \
  \
  template \
  Interface<dim> * \
  create_initial_conditions<dim> (ParameterHandler &prm, \
                                  const GeometryModel::Interface<dim> &geometry_model, \
                                  const BoundaryTemperature::Interface<dim> &boundary_temperature, \
                                  const AdiabaticConditions<dim>      &adiabatic_conditions);

    ASPECT_INSTANTIATE(INSTANTIATE)
  }
}
