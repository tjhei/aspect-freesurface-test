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


#ifndef __aspect__postprocess_visualization_h
#define __aspect__postprocess_visualization_h

#include <aspect/postprocess/interface.h>
#include <aspect/plugins.h>

#include <deal.II/base/thread_management.h>
#include <deal.II/numerics/data_postprocessor.h>
#include <deal.II/base/data_out_base.h>

namespace aspect
{
  namespace Postprocess
  {
    namespace VisualizationPostprocessors
    {
      /**
       * This class declares the public interface of visualization postprocessors.
       * Visualization postprocessors are used to compute derived data, e.g.
       * wave speeds, friction heating terms, etc, to be put into graphical
       * output files. They are plugins for the aspect::Postprocess::Visualization
       * class.
       *
       * Classes derived from this type must implement the functions that save the state
       * of the object and restore it (for checkpoint/restart capabilities) as well as
       * functions that declare and read parameters. However, this class also already
       * provides default implementations of these functions that simply do nothing.
       * This is appropriate for objects that are stateless, as is commonly the case
       * for visualization postprocessors.
       *
       * Access to the data of the simulator is granted by the @p protected member functions
       * of the SimulatorAccessor class, i.e., classes implementing this interface will
       * in general want to derive from both this Interface class as well as from the
       * SimulatorAccess class. Furthermore, in order to do what they are intended to,
       * derived classes need to also derive from the deal.II DataPostprocessor or any of
       * the classes like DataPostprocessorScalar or DataPostprocessorVector.
       * A typical class derived from the current class would then have the following
       * base classes:
       * - aspect::Postprocess::VisualizationPostprocessors::Interface
       * - aspect::Postprocess::SimulatorAccess
       * - deal::DataPostprocessor or any of the other ones listed above
       *
       * @ingroup Postprocessing
       * @ingroup Visualization
       */
      template <int dim>
      class Interface
      {
        public:
          /**
           * Destructor. Does nothing but is virtual so that derived classes
           * destructors are also virtual.
           **/
          virtual
          ~Interface ();

          /**
           * Declare the parameters this class takes through input files.
           * Derived classes should overload this function if they actually
           * do take parameters; this class declares a fall-back function
           * that does nothing, so that postprocessor classes that do not
           * take any parameters do not have to do anything at all.
           *
           * This function is static (and needs to be static in derived
           * classes) so that it can be called without creating actual
           * objects (because declaring parameters happens before we read
           * the input file and thus at a time when we don't even know yet
           * which postprocessor objects we need).
           */
          static
          void
          declare_parameters (ParameterHandler &prm);

          /**
           * Read the parameters this class declares from the parameter
           * file. The default implementation in this class does nothing,
           * so that derived classes that do not need any parameters do
           * not need to implement it.
           */
          virtual
          void
          parse_parameters (ParameterHandler &prm);


          /**
           * Save the state of this object to the argument given to this function.
           * This function is in support of checkpoint/restart functionality.
           *
           * Derived classes can implement this function and should store their
           * state in a string that is deposited under a key in the map through
           * which the respective class can later find the status again when the
           * program is restarted. A legitimate key to store data under is
           * <code>typeid(*this).name()</code>. It is up to derived classes to
           * decide how they want to encode their state.
           *
           * The default implementation of this function does nothing, i.e., it
           * represents a stateless object for which nothing needs to be stored
           * at checkpoint time and nothing needs to be restored at restart time.
           *
           * @param[in,out] status_strings The object into which implementations
           * in derived classes can place their status under a key that they can
           * use to retrieve the data.
           **/
          virtual
          void save (std::map<std::string, std::string> &status_strings) const;

          /**
           * Restore the state of the object by looking up a description of the
           * state in the passed argument under the same key under which it
           * was previously stored.
           *
           * The default implementation does nothing.
           *
           * @param[in] status_strings The object from which the status will
           * be restored by looking up the value for a key specific to this
           * derived class.
           **/
          virtual
          void load (const std::map<std::string, std::string> &status_strings);
      };
    }


    /**
     * A postprocessor that generates graphical output in periodic intervals
     * or every time step. The time interval between generating graphical
     * output is obtained from the parameter file.
     *
     * While this class acts as a plugin, i.e. as a postprocessor that can
     * be registered with the postprocessing manager class
     * aspect::Postprocess::Manager, this class at the same time also acts
     * as a manager for plugins itself, namely for classes derived from the
     * VisualizationPostprocessors::Interface class that are used to output different
     * aspects of the solution, such as for example a computed seismic wave
     * speed from temperature, velocity and pressure.
     *
     * @ingroup Postprocessing
     */
    template <int dim>
    class Visualization : public Interface<dim>, public SimulatorAccess<dim>
    {
      public:
        /**
         * Constructor.
         */
        Visualization ();

        /**
         * Destructor. Makes sure that any background thread that may still be
         * running writing data to disk finishes before the current object
         * is fully destroyed.
         */
        ~Visualization ();

        /**
         * Generate graphical output from the current solution.
         **/
        virtual
        std::pair<std::string,std::string>
        execute (TableHandler &statistics);

        /**
         * Initialize this class for a given simulator. In addition to
         * calling the respective function from the base class, this
         * function also initializes all the visualization postprocessor
         * plugins.
         *
         * @param simulator A reference to the main simulator object to which the
         * postprocessor implemented in the derived class should be applied.
         **/
        virtual void initialize (const Simulator<dim> &simulator);


        /**
         * A function that is used to register visualization postprocessor objects
         * in such a way that the Manager can deal with all of them
         * without having to know them by name. This allows the files
         * in which individual postprocessors are implement to register
         * these postprocessors, rather than also having to modify the
         * Manage class by adding the new postprocessor class.
         *
         * @param name The name under which this visualization postprocessor is to
         * be called in parameter files.
         * @param description A text description of what this visualization plugin
         * does and that will be listed in the documentation of
         * the parameter file.
         * @param declare_parameters_function A pointer to a function
         * that declares the parameters for this postprocessor.
         * @param factory_function A pointer to a function that creates
         * such a postprocessor object and returns a pointer to it.
         **/
        static
        void
        register_visualization_postprocessor (const std::string &name,
                                              const std::string &description,
                                              void (*declare_parameters_function) (ParameterHandler &),
                                              VisualizationPostprocessors::Interface<dim> *(*factory_function) ());

        /**
         * Declare the parameters this class takes through input files.
         */
        static
        void
        declare_parameters (ParameterHandler &prm);

        /**
         * Read the parameters this class declares from the parameter
         * file.
         */
        virtual
        void
        parse_parameters (ParameterHandler &prm);

        /**
         * Save the state of this object.
         **/
        virtual
        void save (std::map<std::string, std::string> &status_strings) const;

        /**
         * Restore the state of the object.
         **/
        virtual
        void load (const std::map<std::string, std::string> &status_strings);

        /**
         * Serialize the contents of this class as far as they are not
         * read from input parameter files.
         */
        template <class Archive>
        void serialize (Archive &ar, const unsigned int version);

        /**
         * Exception.
         */
        DeclException1 (ExcPostprocessorNameNotFound,
                        std::string,
                        << "Could not find entry <"
                        << arg1
                        << "> among the names of registered postprocessors.");

      private:
        /**
         * Interval between the generation of graphical output. This
         * parameter is read from the input file and consequently is not part
         * of the state that needs to be saved and restored.
        *
        * For technical reasons, this value is stored as given in the
        * input file and upon use is either interpreted as seconds or
        * years, depending on how the global flag in the input parameter
        * file is set.
         */
        double output_interval;

        /**
         * A time (in years) after which the next time step should produce
         * graphical output again.
         */
        double next_output_time;

        /**
         * Consecutively counted number indicating the how-manyth time we
         * will create output the next time we get to it.
         */
        unsigned int output_file_number;

        /**
         * Graphical output format.
         */
        string output_format;

        /**
         * VTU file output supports grouping files from several CPUs
         * into one file using MPI I/O when writing on a parallel
         * filesystem. 0 means no grouping (and no parallel I/O).
         * 1 will generate one big file containing the whole solution.
         */
        unsigned int group_files;

        /**
         * Compute the next output time from the current one. In
         * the simplest case, this is simply the previous
         * next output time plus the interval, but in general
         * we'd like to ensure that it is larger than the current
         * time to avoid falling behind with next_output_time and
         * having to catch up once the time step becomes larger.
         */
        void set_next_output_time (const double current_time);

        /**
         * Handle to a thread that is used to write data in the
         * background. The background_writer() function runs
         * on this background thread.
         */
        Threads::Thread<void> background_thread;

        /**
         * A function that writes the text in the second argument
         * to a file with the name given in the first argument. The function
         * is run on a separate thread to allow computations to
         * continue even though writing data is still continuing.
         * The function takes over ownership of the arguments and deletes
        * them at the end of its work.
         */
        static
        void background_writer (const std::string *filename,
                                const std::string *file_contents);

        /**
         * A list of postprocessor objects that have been requested
         * in the parameter file.
         */
        std::list<std_cxx1x::shared_ptr<VisualizationPostprocessors::Interface<dim> > > postprocessors;

        /**
         * A list of pairs (time, pvtu_filename) that have so far been written
         * and that we will pass to DataOutInterface::write_pvd_record
         * to create a master file that can make the association
         * between simulation time and corresponding file name (this
         * is done because there is no way to store the simulation
         * time inside the .pvtu or .vtu files).
         */
        std::vector<std::pair<double,std::string> > times_and_pvtu_names;

        /**
         * A set of data related to XDMF file sections describing the HDF5 heavy data
         * files created. These contain things such as the dimensions and names of data
         * written at all steps during the simulation.
         */
        std::vector<XDMFEntry>  xdmf_entries;
    };
  }


  /**
   * Given a class name, a name, and a description for the parameter file for a postprocessor, register it with
   * the aspect::Postprocess::Manager class.
   *
   * @ingroup Postprocessing
   */
#define ASPECT_REGISTER_VISUALIZATION_POSTPROCESSOR(classname,name,description) \
  template class classname<2>; \
  template class classname<3>; \
  namespace ASPECT_REGISTER_VISUALIZATION_POSTPROCESSOR_ ## classname \
  { \
    aspect::internal::Plugins::RegisterHelper<VisualizationPostprocessors::Interface<2>,classname<2> > \
    dummy_ ## classname ## _2d (&aspect::Postprocess::Visualization<2>::register_visualization_postprocessor, \
                                name, description); \
    aspect::internal::Plugins::RegisterHelper<VisualizationPostprocessors::Interface<3>,classname<3> > \
    dummy_ ## classname ## _3d (&aspect::Postprocess::Visualization<3>::register_visualization_postprocessor, \
                                name, description); \
  }
}


#endif
