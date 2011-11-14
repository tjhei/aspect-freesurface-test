//-------------------------------------------------------------
//    $Id$
//
//    Copyright (C) 2011 by the authors of the ASPECT code
//
//-------------------------------------------------------------

#include <aspect/postprocess_base.h>
#include <aspect/simulator.h>

#include <typeinfo>


namespace aspect
{
  namespace Postprocess
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



    template <int dim>
    void
    Interface<dim>::save (std::map<std::string,std::string> &) const
    {}


    template <int dim>
    void
    Interface<dim>::load (const std::map<std::string,std::string> &)
    {}


// ------------------------------ SimulatorAccess -----------------------

    template <int dim>
    void
    SimulatorAccess<dim>::initialize (const Simulator<dim> &simulator_object)
    {
      simulator = SmartPointer<const Simulator<dim> > (&simulator_object, typeid(*this).name());
    }



    template <int dim>
    double SimulatorAccess<dim>::get_time () const
    {
      return simulator->time;
    }



    template <int dim>
    double SimulatorAccess<dim>::get_timestep () const
    {
      return simulator->time_step;
    }



    template <int dim>
    unsigned int SimulatorAccess<dim>::get_timestep_number () const
    {
      return simulator->timestep_number;
    }



    template <int dim>
    const parallel::distributed::Triangulation<dim> &
    SimulatorAccess<dim>::get_triangulation () const
    {
      return simulator->triangulation;
    }



    template <int dim>
    double
    SimulatorAccess<dim>::get_volume () const
    {
      return simulator->global_volume;
    }



    template <int dim>
    const Mapping<dim> &
    SimulatorAccess<dim>::get_mapping () const
    {
      return simulator->mapping;
    }



    template <int dim>
    const TrilinosWrappers::MPI::BlockVector &
    SimulatorAccess<dim>::get_stokes_solution () const
    {
      return simulator->stokes_solution;
    }



    template <int dim>
    const TrilinosWrappers::MPI::BlockVector &
    SimulatorAccess<dim>::get_old_stokes_solution () const
    {
      return simulator->old_stokes_solution;
    }



    template <int dim>
    const DoFHandler<dim> &
    SimulatorAccess<dim>::get_stokes_dof_handler () const
    {
      return simulator->stokes_dof_handler;
    }



    template <int dim>
    const TrilinosWrappers::MPI::Vector &
    SimulatorAccess<dim>::get_temperature_solution () const
    {
      return simulator->temperature_solution;
    }



    template <int dim>
    const TrilinosWrappers::MPI::Vector &
    SimulatorAccess<dim>::get_old_temperature_solution () const
    {
      return simulator->old_temperature_solution;
    }



    template <int dim>
    const DoFHandler<dim> &
    SimulatorAccess<dim>::get_temperature_dof_handler () const
    {
      return simulator->temperature_dof_handler;
    }



    template <int dim>
    const MaterialModel<dim> *
    SimulatorAccess<dim>::get_model_data () const
    {
      return simulator->model_data.get();
    }

// ------------------------------ Manager -----------------------------

    template <int dim>
    void
    Manager<dim>::initialize (const Simulator<dim> &simulator)
    {
      std::list<std::pair<std::string,std::string> > output_list;
      for (typename std::list<std_cxx1x::shared_ptr<Interface<dim> > >::iterator
           p = postprocessors.begin();
           p != postprocessors.end(); ++p)
        dynamic_cast<SimulatorAccess<dim>&>(**p).initialize (simulator);
    }



    template <int dim>
    std::list<std::pair<std::string,std::string> >
    Manager<dim>::execute (TableHandler &statistics)
    {
      // call the execute() functions of all postprocessor objects we have
      // here in turns
      std::list<std::pair<std::string,std::string> > output_list;
      for (typename std::list<std_cxx1x::shared_ptr<Interface<dim> > >::iterator
           p = postprocessors.begin();
           p != postprocessors.end(); ++p)
        {
          // call the execute() function. if it produces any output
          // then add it to the list
          std::pair<std::string,std::string> output
            = (*p)->execute (statistics);

          if (output.first.size() + output.second.size() > 0)
            output_list.push_back (output);
        }

      return output_list;
    }



    namespace
    {
      typedef
      std_cxx1x::tuple<std::string,
                void ( *) (ParameterHandler &),
                Interface<deal_II_dimension> * ( *) ()>
                PostprocessorInfo;

      // A pointer to a list of all postprocessors. the three elements of the tuple
      // correspond to the arguments given to the Manager::register_postprocessor
      // function.
      //
      // The object is a pointer rather than an object for the following reason:
      // objects with static initializers (such as =0) are initialized
      // before any objects for which one needs to run constructors.
      // consequently, we can be sure that this pointer is set to zero
      // before we ever try to register a postprocessor, and consequently
      // whenever we run Manager::register_postprocessor, we need not
      // worry whether we try to add something to this list before the lists's
      // constructor has successfully run
      std::list<PostprocessorInfo> *registered_postprocessors = 0;
    }

    template <int dim>
    void
    Manager<dim>::declare_parameters (ParameterHandler &prm)
    {
      Assert (registered_postprocessors != 0,
              ExcMessage ("No postprocessors registered!?"));

      // first declare the postprocessors we know about to
      // choose from
      prm.enter_subsection("Postprocess");
      {
        // construct a string for Patterns::MultipleSelection that
        // contains the names of all registered postprocessors
        std::string pattern_of_names;
        for (typename std::list<PostprocessorInfo>::const_iterator
             p = registered_postprocessors->begin();
             p != registered_postprocessors->end(); ++p)
          {
            if (pattern_of_names.size() > 0)
              pattern_of_names += "|";
            pattern_of_names += std_cxx1x::get<0>(*p);
          }
        if (pattern_of_names.size() > 0)
          pattern_of_names += "|";
        pattern_of_names += "all";

        prm.declare_entry("List of postprocessors",
                          "all",
                          Patterns::MultipleSelection(pattern_of_names),
                          "A comma separated list of postprocessor objects that should be run "
                          "at the end of each time step. Some of these postprocessors will "
                          "declare their own parameters which may, for example, include that "
                          "they will actually do something only every so many time steps or "
                          "years. Alternatively, the text 'all' indicates that all available "
                          "postprocessors should be run after each time step.");
      }
      prm.leave_subsection();

      // now declare the parameters of each of the registered
      // postprocessors in turn
      for (std::list<PostprocessorInfo>::const_iterator
           p = registered_postprocessors->begin();
           p != registered_postprocessors->end(); ++p)
        (std_cxx1x::get<1>(*p))(prm);
    }



    template <int dim>
    void
    Manager<dim>::parse_parameters (ParameterHandler &prm)
    {
      Assert (registered_postprocessors != 0,
              ExcMessage ("No postprocessors registered!?"));

      // first find out which postprocessors are requested
      std::vector<std::string> postprocessor_names;
      prm.enter_subsection("Postprocess");
      {
        postprocessor_names
          = Utilities::split_string_list(prm.get("List of postprocessors"));
      }
      prm.leave_subsection();

      // see if 'all' was selected (or is part of the list). if so
      // simply replace the list with one that contains all names
      if (std::find (postprocessor_names.begin(),
                     postprocessor_names.end(),
                     "all") != postprocessor_names.end())
        {
          postprocessor_names.clear();
          for (std::list<PostprocessorInfo>::const_iterator
               p = registered_postprocessors->begin();
               p != registered_postprocessors->end(); ++p)
            postprocessor_names.push_back (std_cxx1x::get<0>(*p));
        }

      // then go through the list, create objects and let them parse
      // their own parameters
      for (unsigned int name=0; name<postprocessor_names.size(); ++name)
        {
          // find the entry in the registered postprocessors list
          // that corresponds to this name
          for (std::list<PostprocessorInfo>::const_iterator
               p = registered_postprocessors->begin();
               p != registered_postprocessors->end(); ++p)
            if (std_cxx1x::get<0>(*p) == postprocessor_names[name])
              {
                // create such a postprocessor object using the given
                // factory function and let it parse its parameters.
                // then append it to the list this object stores
                Interface<dim> *object = (std_cxx1x::get<2>(*p))();
                object->parse_parameters (prm);
                postprocessors.push_back (std_cxx1x::shared_ptr<Interface<dim> >(object));

                goto found_it;
              }
          AssertThrow (false,
                       ExcPostprocessorNameNotFound(postprocessor_names[name]));

        found_it:
          ;
        }
    }


    template <int dim>
    void
    Manager<dim>::register_postprocessor (const std::string &name,
                                          void (*declare_parameters_function) (ParameterHandler &),
                                          Interface<dim> * (*factory_function) ())
    {
      // see if this is the first time we get into this
      // function and if so initialize the variable above
      if (registered_postprocessors == 0)
        registered_postprocessors = new std::list<PostprocessorInfo>();

      // now add one record to the list
      registered_postprocessors->push_back (PostprocessorInfo(name,
                                                              declare_parameters_function,
                                                              factory_function));
    }

  }
}


// explicit instantiations
namespace aspect
{
  namespace Postprocess
  {
    template class Interface<deal_II_dimension>;
    template class SimulatorAccess<deal_II_dimension>;
    template class Manager<deal_II_dimension>;

    Manager<deal_II_dimension> m;
  }
}