/*
  Copyright (C) 2012 by the authors of the ASPECT code.

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


#include <aspect/mesh_refinement/interface.h>
#include <aspect/simulator_access.h>

#include <typeinfo>


namespace aspect
{
  namespace MeshRefinement
  {
// ------------------------------ Interface -----------------------------

    template <int dim>
    Interface<dim>::~Interface ()
    {}


    template <int dim>
    void
    Interface<dim>::declare_parameters (ParameterHandler &)
    {}



    template <int dim>
    void
    Interface<dim>::parse_parameters (ParameterHandler &)
    {}



// ------------------------------ Manager -----------------------------

    template <int dim>
    Manager<dim>::~Manager()
    {}


    template <int dim>
    void
    Manager<dim>::initialize (const Simulator<dim> &simulator)
    {
      for (typename std::list<std_cxx1x::shared_ptr<Interface<dim> > >::iterator
           p = mesh_refinement_objects.begin();
           p != mesh_refinement_objects.end(); ++p)
        if (dynamic_cast<const SimulatorAccess<dim>*>(p->get()) != 0)
          dynamic_cast<SimulatorAccess<dim>&>(**p).initialize (simulator);

      // also extract the MPI communicator over which this simulator
      // operates so that we can later scale vectors that live on
      // different processors. getting at this communicator is a bit
      // indirect but requires no extra interfaces (we need the
      // intermediary MySimulatorAccess class since SimulatorAccess's
      // get_mpi_communicator function is only protected; through the
      // 'using' declaration we can promote it to a public member).
      class MySimulatorAccess : public SimulatorAccess<dim>
      {
        public:
          using SimulatorAccess<dim>::get_mpi_communicator;
      } simulator_access;
      simulator_access.initialize (simulator);
      mpi_communicator = simulator_access.get_mpi_communicator();
    }



    template <int dim>
    void
    Manager<dim>::execute (Vector<float> &error_indicators) const
    {
      Assert (mesh_refinement_objects.size() > 0, ExcInternalError());

      // call the execute() functions of all plugins we have
      // here in turns. then normalize the output vector and
      // verify that its values are non-negative numbers
      std::vector<Vector<float> > all_error_indicators (mesh_refinement_objects.size(),
                                                        Vector<float>(error_indicators.size()));
      unsigned int index = 0;
      for (typename std::list<std_cxx1x::shared_ptr<Interface<dim> > >::const_iterator
           p = mesh_refinement_objects.begin();
           p != mesh_refinement_objects.end(); ++p, ++index)
        {
          try
            {
              (*p)->execute (all_error_indicators[index]);

              for (unsigned int i=0; i<error_indicators.size(); ++i)
                Assert (all_error_indicators[index](i) >= 0,
                        ExcMessage ("Error indicators must be non-negative numbers!"));

              // see if we want to normalize the criteria
              if (normalize_criteria == true)
                {
                  const double global_max = Utilities::MPI::max (all_error_indicators[index].linfty_norm(),
                                                                 mpi_communicator);
                  if (global_max != 0)
                    all_error_indicators[index] /= global_max;
                }

              // then also scale them
              all_error_indicators[index] *= scaling_factors[index];
            }
          // plugins that throw exceptions usually do not result in
          // anything good because they result in an unwinding of the stack
          // and, if only one processor triggers an exception, the
          // destruction of objects often causes a deadlock. thus, if
          // an exception is generated, catch it, print an error message,
          // and abort the program
          catch (std::exception &exc)
            {
              std::cerr << std::endl << std::endl
                        << "----------------------------------------------------"
                        << std::endl;
              std::cerr << "Exception on MPI process <"
                        << Utilities::MPI::this_mpi_process(MPI_COMM_WORLD)
                        << "> while running mesh refinement plugin <"
                        << typeid(**p).name()
                        << ">: " << std::endl
                        << exc.what() << std::endl
                        << "Aborting!" << std::endl
                        << "----------------------------------------------------"
                        << std::endl;

              // terminate the program!
              MPI_Abort (MPI_COMM_WORLD, 1);
            }
          catch (...)
            {
              std::cerr << std::endl << std::endl
                        << "----------------------------------------------------"
                        << std::endl;
              std::cerr << "Exception on MPI process <"
                        << Utilities::MPI::this_mpi_process(MPI_COMM_WORLD)
                        << "> while running mesh refinement plugin <"
                        << typeid(**p).name()
                        << ">: " << std::endl;
              std::cerr << "Unknown exception!" << std::endl
                        << "Aborting!" << std::endl
                        << "----------------------------------------------------"
                        << std::endl;

              // terminate the program!
              MPI_Abort (MPI_COMM_WORLD, 1);
            }
        }

      // now merge the results
      switch  (merge_operation)
        {
          case plus:
          {
            for (unsigned int i=0; i<mesh_refinement_objects.size(); ++i)
              error_indicators += all_error_indicators[i];
            break;
          }

          case max:
          {
            error_indicators = all_error_indicators[0];
            for (unsigned int i=1; i<mesh_refinement_objects.size(); ++i)
              {
                Assert (error_indicators.size() == all_error_indicators[i].size(),
                        ExcInternalError());
                for (unsigned int j=0; j<error_indicators.size(); ++j)
                  error_indicators(j) = std::max (error_indicators(j),
                                                  all_error_indicators[i](j));
              }
            break;
          }

          default:
            Assert (false, ExcNotImplemented());
        }
    }


// -------------------------------- Deal with registering plugins and automating
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
    Manager<dim>::declare_parameters (ParameterHandler &prm)
    {
      // first declare the postprocessors we know about to
      // choose from
      prm.enter_subsection("Mesh refinement");
      {
        // construct a string for Patterns::MultipleSelection that
        // contains the names of all registered plugins
        const std::string pattern_of_names
          = std_cxx1x::get<dim>(registered_plugins).get_pattern_of_names ();
        prm.declare_entry("Strategy",
                          "thermal energy density",
                          Patterns::MultipleSelection(pattern_of_names),
                          "A comma separated list of mesh refinement criteria that "
                          "will be run whenever mesh refinement is required. The "
                          "results of each of these criteria will, i.e., the refinement "
                          "indicators they produce for all the cells of the mesh "
                          "will then be normalized to a range between zero and one "
                          "and the results of different criteria will then be "
                          "merged through the operation selected in this section.\n\n"
                          "The following criteria are available:\n\n"
                          +
                          std_cxx1x::get<dim>(registered_plugins).get_description_string());

        prm.declare_entry("Normalize individual refinement criteria",
                          "true",
                          Patterns::Bool(),
                          "If multiple refinement criteria are specified in the "
                          "``Strategy'' parameter, then they need to be combined "
                          "somehow to form the final refinement indicators. This "
                          "is done using the method described by the ``Refinement "
                          "criteria merge operation'' parameter which can either "
                          "operate on the raw refinement indicators returned by "
                          "each strategy (i.e., dimensional quantities) or using "
                          "normalized values where the indicators of each strategy "
                          "are first normalized to the interval $[0,1]$ (which also "
                          "makes them non-dimensional). This parameter determines "
                          "whether this normalization will happen.");
        prm.declare_entry("Refinement criteria scaling factors",
                          "",
                          Patterns::List (Patterns::Double(0)),
                          "A list of scaling factors by which every individual refinement "
                          "criterion will be multiplied by. If only a single refinement "
                          "criterion is selected (using the ``Strategy'' parameter, then "
                          "this parameter has no particular meaning. On the other hand, if "
                          "multiple criteria are chosen, then these factors are used to "
                          "weigh the various indicators relative to each other. "
                          "\n\n"
                          "If ``Normalize individual refinement criteria'' is set to true, "
                          "then the criteria will first be normalized to the interval $[0,1]$ "
                          "and then multiplied by the factors specified here. You will likely "
                          "want to choose the factors to be not too far from 1 in that case, say "
                          "between 1 and 10, to avoid essentially disabling those criteria "
                          "with small weights. On the other hand, if the criteria are not "
                          "normalized to $[0,1]$ using the parameter mentioned above, then "
                          "the factors you specify here need to take into account the relative "
                          "numerical size of refinement indicators (which in that case carry "
                          "physical units)."
                          "\n\n"
                          "You can experimentally play with these scaling factors by choosing "
                          "to output the refinement indicators into the graphical output of "
                          "a run."
                          "\n\n"
                          "If the list of indicators given in this parameter is empty, then this "
                          "indicates that they should all be chosen equal to one. If the list "
                          "is not empty then it needs to have as many entries as there are "
                          "indicators chosen in the ``Strategy'' parameter.");
        prm.declare_entry("Refinement criteria merge operation",
                          "max",
                          Patterns::Selection("plus|max"),
                          "If multiple mesh refinement criteria are computed for each cell "
                          "(by passing a list of more than element to the \\texttt{Strategy} "
                          "parameter in this section of the input file) "
                          "then one will have to decide which one should win when deciding "
                          "which cell to refine. The operation that selects from these competing "
                          "criteria is the one that is selected here. The options are:\n\n"
                          "\\begin{itemize}\n"
                          "\\item \\texttt{plus}: Add the various error indicators together and "
                          "refine those cells on which the sum of indicators is largest.\n"
                          "\\item \\texttt{max}: Take the maximum of the various error indicators and "
                          "refine those cells on which the maximal indicators is largest.\n"
                          "\\end{itemize}"
                          "The refinement indicators computed by each strategy are modified by "
                          "the ``Normalize individual refinement criteria'' and ``Refinement "
                          "criteria scale factors'' parameters.");
      }
      prm.leave_subsection();

      // now declare the parameters of each of the registered
      // plugins in turn
      std_cxx1x::get<dim>(registered_plugins).declare_parameters (prm);
    }



    template <int dim>
    void
    Manager<dim>::parse_parameters (ParameterHandler &prm)
    {
      Assert (std_cxx1x::get<dim>(registered_plugins).plugins != 0,
              ExcMessage ("No mesh refinement plugins registered!?"));

      // find out which plugins are requested and the various other
      // parameters we declare here
      std::vector<std::string> plugin_names;
      prm.enter_subsection("Mesh refinement");
      {
        plugin_names
          = Utilities::split_string_list(prm.get("Strategy"));

        normalize_criteria = prm.get_bool ("Normalize individual refinement criteria");

        scaling_factors
          = Utilities::string_to_double(
              Utilities::split_string_list(prm.get("Refinement criteria scaling factors")));
        AssertThrow (scaling_factors.size() == plugin_names.size()
                     ||
                     scaling_factors.size() == 0,
                     ExcMessage ("The number of scaling factors given here must either be "
                                 "zero or equal to the number of chosen refinement criteria."));
        if (scaling_factors.size() == 0)
          scaling_factors = std::vector<double> (plugin_names.size(), 1.0);

        if (prm.get("Refinement criteria merge operation") == "plus")
          merge_operation = plus;
        else if (prm.get("Refinement criteria merge operation") == "max")
          merge_operation = max;
        else
          AssertThrow (false, ExcNotImplemented());
      }
      prm.leave_subsection();

      // go through the list, create objects and let them parse
      // their own parameters
      AssertThrow (plugin_names.size() >= 1,
                   ExcMessage ("You need to provide at least one mesh refinement criterion in the input file!"));
      for (unsigned int name=0; name<plugin_names.size(); ++name)
        mesh_refinement_objects.push_back (std_cxx1x::shared_ptr<Interface<dim> >
                                           (std_cxx1x::get<dim>(registered_plugins)
                                            .create_plugin (plugin_names[name],
                                                            "Mesh refinement::Refinement criteria merge operation",
                                                            prm)));
    }


    template <int dim>
    void
    Manager<dim>::register_mesh_refinement_criterion (const std::string &name,
                                                      const std::string &description,
                                                      void (*declare_parameters_function) (ParameterHandler &),
                                                      Interface<dim> *(*factory_function) ())
    {
      std_cxx1x::get<dim>(registered_plugins).register_plugin (name,
                                                               description,
                                                               declare_parameters_function,
                                                               factory_function);
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
      std::list<internal::Plugins::PluginList<MeshRefinement::Interface<2> >::PluginInfo> *
      internal::Plugins::PluginList<MeshRefinement::Interface<2> >::plugins = 0;
      template <>
      std::list<internal::Plugins::PluginList<MeshRefinement::Interface<3> >::PluginInfo> *
      internal::Plugins::PluginList<MeshRefinement::Interface<3> >::plugins = 0;
    }
  }

  namespace MeshRefinement
  {
#define INSTANTIATE(dim) \
  template class Interface<dim>; \
  template class Manager<dim>;

    ASPECT_INSTANTIATE(INSTANTIATE)
  }
}
